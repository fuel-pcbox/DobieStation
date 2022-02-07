#include <ee/dmac.hpp>
#include <emulator.hpp>
#include <util/errors.hpp>
#include <ee/emotion.hpp>
#include <ee/vu/vu.hpp>
#include <ee/vu/vif.hpp>
#include <gs/gif.hpp>
#include <ee/ipu/ipu.hpp>
#include <sif.hpp>
#include <algorithm>
#include <fmt/core.h>

namespace ee
{
    constexpr const char* CHANNEL[] = 
    { 
        "VIF0", "VIF1", "GIF", "IPU_FROM", "IPU_TO",
        "SIF0", "SIF1", "SIF2", "SPR_FROM", "SPR_TO" 
    };

    DMAC::DMAC(core::Emulator* e) : 
        e(e)
    {
        apply_dma_funcs();
    }

    void DMAC::reset()
    {
        /* SCPH-39001 requires this value to be set, possibly other BIOSes too */
        master_disable = 0x1201;
        control.master_enable = false;
        mfifo_empty_triggered = false;
        PCR = 0;
        STADR = 0;
        cycles_to_run = 0;

        active_channel = nullptr;
        queued_channels.clear();

        for (int i = 0; i < 15; i++)
        {
            channels[i].started = false;
            channels[i].control = 0;
            channels[i].dma_req = false;
            channels[i].index = i;
            interrupt_stat.channel_mask[i] = false;
            interrupt_stat.channel_stat[i] = false;
        }

        //SPR channels don't have a FIFO, so they can always run when started
        channels[SPR_FROM].dma_req = true;
        channels[SPR_TO].dma_req = true;

        channels[IPU_TO].dma_req = true;
        channels[EE_SIF1].dma_req = true;

        channels[VIF0].dma_req = true;
        channels[VIF1].dma_req = true;
    }

    uint128_t DMAC::fetch128(uint32_t addr)
    {
        if ((addr & (1 << 31)) || (addr & 0x70000000) == 0x70000000)
        {
            addr &= 0x3FF0;
            return *(uint128_t*)&e->cpu->scratchpad[addr];
        }
        else if (addr >= 0x11000000 && addr < 0x11010000)
        {
            if (addr < 0x11004000)
            {
                return e->vu0->read_instr<uint128_t>(addr);
            }
            if (addr < 0x11008000)
            {
                return e->vu0->read_mem<uint128_t>(addr);
            }
            if (addr < 0x1100C000)
            {
                return e->vu1->read_instr<uint128_t>(addr);
            }
            return e->vu1->read_mem<uint128_t>(addr);
        }
        else
        {
            addr &= 0x01FFFFF0;
            return *(uint128_t*)&e->cpu->rdram[addr];
        }
    }

    void DMAC::store128(uint32_t addr, uint128_t data)
    {
        if ((addr & (1 << 31)) || (addr & 0x70000000) == 0x70000000)
        {
            addr &= 0x3FF0;
            *(uint128_t*)&e->cpu->scratchpad[addr] = data;
        }
        else if (addr >= 0x11000000 && addr < 0x11010000)
        {
            if (addr < 0x11004000)
            {
                e->vu0->write_instr<uint128_t>(addr, data);
                return;
            }
            if (addr < 0x11008000)
            {
                e->vu0->write_mem<uint128_t>(addr, data);
                return;
            }
            if (addr < 0x1100C000)
            {
                e->vu1->write_instr<uint128_t>(addr, data);
                return;
            }
            
            e->vu1->write_mem<uint128_t>(addr, data);
            return;
        }
        else
        {
            addr &= 0x01FFFFF0;
            *(uint128_t*)&e->cpu->rdram[addr] = data;
        }
    }

    void DMAC::run(int cycles)
    {
        if (!control.master_enable || (master_disable & (1 << 16)))
            return;

        if (active_channel)
        {
            cycles_to_run += cycles;
            while (cycles_to_run > 0)
            {
                DMA_Channel* temp = active_channel;
                int qwc_transferred = (this->*active_channel->func)();
                cycles_to_run -= std::max(qwc_transferred, 1);
                if (!temp->is_spr)
                    cycles_to_run -= 12;

                if (!active_channel)
                {
                    cycles_to_run = 0;
                    break;
                }
            }
        }
    }

    //mfifo_handler will return false if the MFIFO is empty and the MFIFO is in use. Otherwise it returns true
    bool DMAC::mfifo_handler(int index)
    {
        if (control.mem_drain_channel - 1 == index)
        {
            uint8_t id = (channels[index].control >> 28) & 0x7;

            channels[index].tag_address = RBOR | (channels[index].tag_address & RBSR);

            uint32_t addr = channels[index].tag_address;

            //Don't mask the MADR on REFE/REF/REFS as they don't follow the tag, so likely outside the MFIFO
            if (channels[index].quadword_count)
            {
                if (id != 0 && id != 3 && id != 4)
                    channels[index].address = RBOR | (channels[index].address & RBSR);

                addr = channels[index].address;
            }

            if (addr == channels[SPR_FROM].address)
            {
                if (!mfifo_empty_triggered)
                {
                    interrupt_stat.channel_stat[MFIFO_EMPTY] = true;
                    int1_check();
                    mfifo_empty_triggered = true;
                    printf("[DMAC] MFIFO Empty\n");
                }
                //Continue transfer if using a reference and there's QWC left
                if (channels[index].quadword_count && (id == 0 || id == 3 || id == 4))
                    return true;
                else
                    return false;
            }
            mfifo_empty_triggered = false;
        }

        return true;
    }

    void DMAC::transfer_end(int index)
    {
        fmt::print("[DMAC] {} transfer ended\n", CHANNEL[index]);

        channels[index].control &= ~0x100;
        channels[index].started = false;
        interrupt_stat.channel_stat[index] = true;
        
        int1_check();
        deactivate_channel(index);
    }

    void DMAC::int1_check()
    {
        bool int1_signal = false;
        for (int i = 0; i < 15; i++)
        {
            if (interrupt_stat.channel_stat[i] & interrupt_stat.channel_mask[i])
            {
                int1_signal = true;
                break;
            }
        }

        e->cpu->set_int1_signal(int1_signal);
    }

    void DMAC::apply_dma_funcs()
    {
        channels[VIF0].func = &DMAC::process_VIF0;
        channels[VIF1].func = &DMAC::process_VIF1;
        channels[GIF].func = &DMAC::process_GIF;
        channels[IPU_TO].func = &DMAC::process_IPU_TO;
        channels[IPU_FROM].func = &DMAC::process_IPU_FROM;
        channels[EE_SIF0].func = &DMAC::process_SIF0;
        channels[EE_SIF1].func = &DMAC::process_SIF1;
        channels[SPR_FROM].func = &DMAC::process_SPR_FROM;
        channels[SPR_TO].func = &DMAC::process_SPR_TO;
    }

    int DMAC::process_VIF0()
    {
        int count = 0;
        auto& channel = channels[VIF0];
        auto& vif0 = e->vif0;

        if (channel.quadword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.quadword_count, max_qwc);
            while (count < quads_to_transfer)
            {
                uint128_t data = fetch128(channel.address);
                if (!vif0->feed_DMA(data))
                    break;
                
                advance_source_dma(VIF0);
                count++;
            }
        }
        if (!channel.quadword_count)
        {
            if (channel.tag_end)
            {
                transfer_end(VIF0);
                return count;
            }
            else
            {
                uint128_t DMAtag = fetch128(channel.tag_address);
                if (channel.control & (1 << 6))
                {
                    if (!vif0->transfer_DMAtag(DMAtag))
                    {
                        arbitrate();
                        return count;
                    }
                }
                handle_source_chain(VIF0);
            }
        }
        else
            arbitrate();

        return count;
    }

    int DMAC::process_VIF1()
    {
        int count = 0;
        auto& channel = channels[VIF1];
        auto& vif1 = e->vif1;

        if (channel.quadword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.quadword_count, max_qwc);

            if ((channel.control & 0x1) && control.stall_dest_channel == 1 && channel.can_stall_drain)
            {
                if (channel.address + (quads_to_transfer * 16) > STADR)
                {
                    if (channel.has_dma_stalled == false)
                    {
                        fmt::print("[DMAC] VIF1 DMA Stall at {:#x} STADR = {:#x}\n", channel.address, STADR);
                        interrupt_stat.channel_stat[DMA_STALL] = true;
                        int1_check();
                        channel.has_dma_stalled = true;
                    }

                    clear_DMA_request(VIF1);
                    return count;
                }
                
                channel.has_dma_stalled = false;
            }

            while (count < quads_to_transfer)
            {
                if (!mfifo_handler(VIF1))
                {
                    arbitrate();
                    return count;
                }
                
                if (channel.control & 0x1)
                {
                    uint128_t data = fetch128(channel.address);
                    if (!vif1->feed_DMA(data))
                        break;
                }
                else
                {
                    auto quad_data = vif1->readFIFO();
                    if (std::get<1>(quad_data))
                        store128(channel.address, std::get<0>(quad_data));
                    else
                    {
                        arbitrate();
                        return count;
                    }
                }
                
                advance_source_dma(VIF1);
                count++;
            }
        }
        
        if (!channel.quadword_count)
        {
            if (channel.tag_end)
            {
                transfer_end(VIF1);
                return count;
            }
            else
            {
                if (!mfifo_handler(VIF1))
                {
                    arbitrate();
                    return count;
                }
                
                uint128_t DMAtag = fetch128(channels[VIF1].tag_address);
                if (channels[VIF1].control & (1 << 6))
                {
                    if (!vif1->transfer_DMAtag(DMAtag))
                    {
                        arbitrate();
                        return count;
                    }
                }

                handle_source_chain(VIF1);
            }
        }
        else
            arbitrate();

        return count;
    }

    int DMAC::process_GIF()
    {
        int count = 0;
        auto& channel = channels[GIF];
        auto& gif = e->gif;

        gif->dma_running(true);
        if (channel.quadword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.quadword_count, max_qwc);

            if (control.stall_dest_channel == 2 && channel.can_stall_drain)
            {
                if (channel.address + (quads_to_transfer * 16) > STADR)
                {
                    if (channel.has_dma_stalled == false)
                    {
                        fmt::print("[DMAC] GIF DMA Stall at {:#x} STADR = {:#x}\n", channel.address, STADR);
                        interrupt_stat.channel_stat[DMA_STALL] = true;
                        
                        int1_check();
                        gif->deactivate_PATH(3);
                        channel.has_dma_stalled = true;
                    }

                    clear_DMA_request(GIF);
                    return count;
                }
                
                channel.has_dma_stalled = false;
            }

            while (count < quads_to_transfer)
            {
                if (!mfifo_handler(GIF))
                {
                    arbitrate();
                    gif->deactivate_PATH(3);
                    return count;
                }
            
                if (!gif->fifo_full() && !gif->fifo_draining())
                {
                    uint128_t data = fetch128(channels[GIF].address);
                    gif->send_PATH3(data);
                    advance_source_dma(GIF);
                    count++;
                }
                else
                {
                    break;
                }
            }
        }
        
        //gif->intermittent_check();
        if (!channel.quadword_count)
        {
            if (channel.tag_end)
            {
                gif->dma_running(false);
                transfer_end(GIF);
            }
            else
            {
                if (!mfifo_handler(GIF))
                {
                    arbitrate();
                    gif->deactivate_PATH(3);
                    return count;
                }

                handle_source_chain(GIF);
            }
        }
        else
            arbitrate();

        return count;
    }

    int DMAC::process_IPU_FROM()
    {
        int count = 0;
        auto& channel = channels[IPU_FROM];
        auto& ipu = e->ipu;

        if (channel.quadword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.quadword_count, max_qwc);
            while (count < quads_to_transfer)
            {
                if (!ipu->can_read_FIFO())
                    break;
                
                uint128_t data = ipu->read_FIFO();
                store128(channel.address, data);

                advance_dest_dma(IPU_FROM);
                count++;
            }
        }
        
        if (control.stall_source_channel == 3)
            update_stadr(channel.address);
        
        if (!channel.quadword_count)
        {
            if (channel.tag_end)
                transfer_end(IPU_FROM);
            else
                Errors::die("[DMAC] IPU_FROM uses dest chain!\n");
        }
        else
            arbitrate();

        return count;
    }

    int DMAC::process_IPU_TO()
    {
        int count = 0;
        auto& channel = channels[IPU_TO];
        auto& ipu = e->ipu;

        if (channel.quadword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.quadword_count, max_qwc);
            
            while (count < quads_to_transfer)
            {
                if (!ipu->can_write_FIFO())
                    break;
                
                ipu->write_FIFO(fetch128(channel.address));
                advance_source_dma(IPU_TO);
                count++;
            }
        }

        if (!channel.quadword_count)
        {
            if (channel.tag_end)
                transfer_end(IPU_TO);
            else
                handle_source_chain(IPU_TO);
        }
        else
            arbitrate();

        return count;
    }

    int DMAC::process_SIF0()
    {
        int count = 0;
        auto& sif = e->sif;
        auto& channel = channels[EE_SIF0];

        uint32_t max_qwc = 8 - ((channel.address >> 4) & 0x7);
        int quads_to_transfer = std::min({channel.quadword_count, max_qwc, sif->get_SIF0_size() / 4U});
        while (count < quads_to_transfer)
        {
            uint128_t quad;
            for (int i = 0; i < 4; i++)
                quad._u32[i] = sif->read_SIF0();
            
            store128(channel.address, quad);
            advance_dest_dma(EE_SIF0);
            count++;
        }

        if (!channel.quadword_count)
        {
            if (channel.tag_end)
            {
                transfer_end(EE_SIF0);
                return count;
            }
            else if (sif->get_SIF0_size() >= 2)
            {
                uint64_t DMAtag = sif->read_SIF0();
                DMAtag |= (uint64_t)sif->read_SIF0() << 32;
                channel.quadword_count = DMAtag & 0xFFFF;
                channel.address = DMAtag >> 32;

                fmt::print("[DMAC] SIF0 tag: {:#x}\n", DMAtag);
                
                uint32_t address = channel.address;
                channel.is_spr = (address & (1 << 31)) || (address & 0x70000000) == 0x70000000;
                channel.tag_id = (DMAtag >> 28) & 0x7;

                bool IRQ = (DMAtag & (1UL << 31));
                bool TIE = channel.control & (1 << 7);
                
                if (channel.tag_id == 7 || (IRQ && TIE))
                    channel.tag_end = true;

                channel.control &= 0xFFFF;
                channel.control |= DMAtag & 0xFFFF0000;
                arbitrate();
            }
        }
        else
            arbitrate();

        return count;
    }

    int DMAC::process_SIF1()
    {
        int count = 0;
        auto& channel = channels[EE_SIF1];
        auto& sif = e->sif;

        if (channel.quadword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.quadword_count, max_qwc);

            if (control.stall_dest_channel == 3 && channel.can_stall_drain)
            {
                if (channel.address + (quads_to_transfer * 16) > STADR)
                {
                    if (channel.has_dma_stalled == false)
                    {
                        fmt::print("[DMAC] SIF1 DMA Stall at {} STADR = {:#x}\n", channel.address, STADR);
                        
                        interrupt_stat.channel_stat[DMA_STALL] = true;
                        int1_check();
                        channel.has_dma_stalled = true;
                    }
                    
                    clear_DMA_request(EE_SIF1);
                    return count;
                }
                
                channel.has_dma_stalled = false;
            }

            while (count < quads_to_transfer)
            {
                uint128_t data = fetch128(channel.address);
                sif->write_SIF1(data);
                
                advance_source_dma(EE_SIF1);
                count++;
            }
        }

        if (!channel.quadword_count)
        {
            if (channel.tag_end)
                transfer_end(EE_SIF1);
            else
                handle_source_chain(EE_SIF1);
        }

        return count;
    }

    int DMAC::process_SPR_FROM()
    {
        int count = 0;
        auto& channel = channels[SPR_FROM];

        if (channel.quadword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.quadword_count, max_qwc);
            
            while (count < quads_to_transfer)
            {
                if (control.mem_drain_channel != 0)
                {
                    channel.address = RBOR | (channel.address & RBSR);
                }
                uint128_t DMAData = fetch128(channel.scratchpad_address | (1 << 31));
                store128(channel.address & 0x7FFFFFFF, DMAData);

                channel.scratchpad_address += 16;
                advance_dest_dma(SPR_FROM);
                count++;

                if (((channel.control >> 2) & 0x3) == 0x2)
                {
                    channel.interleaved_qwc--;
                    if (!channel.interleaved_qwc)
                    {
                        channel.interleaved_qwc = SQWC.transfer_qwc;
                        channel.address += SQWC.skip_qwc * 16;
                        
                        arbitrate();
                        break;
                    }
                }

                if (control.mem_drain_channel != 0)
                    channel.address = RBOR | (channel.address & RBSR);
            }
        }

        if (!channel.quadword_count)
        {
            if (channel.tag_end)
            {
                transfer_end(SPR_FROM);
                return count;
            }
            else
            {
                uint128_t DMAtag = fetch128(channel.scratchpad_address | (1 << 31));
                fmt::print("[DMAC] SPR_FROM tag: {:#x}\n", DMAtag._u64[0]);

                channel.quadword_count = DMAtag._u32[0] & 0xFFFF;
                channel.address = DMAtag._u32[1];
                channel.scratchpad_address += 16;
                channel.scratchpad_address &= 0x3FFF;
                channel.tag_id = (DMAtag._u32[0] >> 28) & 0x7;

                bool IRQ = (DMAtag._u32[0] & (1UL << 31));
                bool TIE = channel.control & (1 << 7);
                
                if (channel.tag_id == 7 || (IRQ && TIE))
                    channel.tag_end = true;

                channel.control &= 0xFFFF;
                channel.control |= DMAtag._u32[0] & 0xFFFF0000;
                arbitrate();
            }
        }

        return count;
    }

    int DMAC::process_SPR_TO()
    {
        int count = 0;
        auto& channel = channels[SPR_TO];
        
        if (channel.quadword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.quadword_count, max_qwc);
            
            while (count < quads_to_transfer)
            {
                uint128_t DMAData = fetch128(channel.address & 0x7FFFFFFF);
                store128(channel.scratchpad_address | (1 << 31), DMAData);
                channel.scratchpad_address += 16;

                advance_source_dma(SPR_TO);
                count++;

                //Interleave mode
                if (((channel.control >> 2) & 0x3) == 0x2)
                {
                    channel.interleaved_qwc--;
                    if (!channel.interleaved_qwc)
                    {
                        channel.interleaved_qwc = SQWC.transfer_qwc;
                        channel.address += SQWC.skip_qwc * 16;

                        //On interleave boundaries, always arbitrate
                        arbitrate();
                        break;
                    }
                }
            }
        }

        if (!channel.quadword_count)
        {
            if (channel.tag_end)
                transfer_end(SPR_TO);
            else
            {
                uint128_t DMAtag = fetch128(channel.tag_address);
                if (channel.control & (1 << 6))
                {
                    store128(channel.scratchpad_address | (1 << 31), DMAtag);
                    channel.scratchpad_address += 16;
                }

                handle_source_chain(SPR_TO);
            }
        }

        return count;
    }

    void DMAC::advance_source_dma(int index)
    {
        int mode = (channels[index].control >> 2) & 0x3;
        channels[index].address += 16;

        /* PS2 checks MFIFO MADR as it transfers but it needs to check also at the end of a packet
           and send an empty signal. This needs to be done on the MADR as TADR doesn't incrmenet on END tags.
           For the code to work, we need to check this before the QWC decrements. HW Test confirmed. */
        if (channels[index].quadword_count == 1)
            mfifo_handler(index);

        channels[index].quadword_count--;
        if (mode == 1) //Chain
        {
            switch (channels[index].tag_id)
            {
                case 1: //CNT
                    channels[index].tag_address = channels[index].address;
                    break;
                default: 
                    break;
            }
        }
    }

    void DMAC::advance_dest_dma(int index)
    {
        int mode = (channels[index].control >> 2) & 0x3;

        channels[index].address += 16;
        channels[index].quadword_count--;

        //Update stall address if we're not in chain mode or the tag id is cnts
        if (mode != 1 || channels[index].tag_id == 0)
        {
            //SIF0 source stall drain
            if (index == 5 && control.stall_source_channel == 1)
                update_stadr(channels[index].address);
            //SPR_FROM source stall drain
            if (index == 8 && control.stall_source_channel == 2)
                update_stadr(channels[index].address);
            //IPU_FROM source stall drain
            if (index == 3 && control.stall_source_channel == 3)
                update_stadr(channels[index].address);
        }
    }

    void DMAC::handle_source_chain(int index)
    {
        auto& channel = channels[index];
        uint128_t quad = fetch128(channel.tag_address);
        uint64_t DMAtag = quad._u64[0];
        //printf("[DMAC] Ch.%d Source DMAtag read $%08X: $%08X_%08X\n", index, channels[index].tag_address, DMAtag >> 32, DMAtag & 0xFFFFFFFF);

        //Change CTRL to have the upper 16 bits equal to bits 16-31 of the most recently read DMAtag
        channel.control &= 0xFFFF;
        channel.control |= DMAtag & 0xFFFF0000;

        uint16_t quadword_count = DMAtag & 0xFFFF;
        uint32_t addr = (DMAtag >> 32) & 0xFFFFFFF0;
        channel.is_spr = (addr & (1 << 31)) || (addr & 0x70000000) == 0x70000000;
        bool IRQ_after_transfer = DMAtag & (1UL << 31);
        bool TIE = channels[index].control & (1 << 7);
        int PCR_toggle = (DMAtag >> 26) & 0x3;
        channel.tag_id = (DMAtag >> 28) & 0x7;
        channel.quadword_count = quadword_count;
        channel.can_stall_drain = false;
        switch (channel.tag_id)
        {
            case 0:
            {
                //refe
                channel.address = addr;
                channel.tag_address += 16;
                channel.tag_end = true;
                break;
            }
            case 1:
            {
                //cnt
                channel.address = channels[index].tag_address + 16;
                channel.tag_address = channels[index].address;
                break;
            }
            case 2:
            {
                //next
                channel.address = channels[index].tag_address + 16;
                channel.tag_address = addr;
                break;
            }
            case 3:
            {
                //ref
                channel.address = addr;
                channel.tag_address += 16;
                break;
            }
            case 4:
            {
                //refs
                channel.address = addr;
                channel.tag_address += 16;
                channel.can_stall_drain = true;
                break;
            }
            case 5:
            {
                //call
                channel.address = channel.tag_address + 16;
                int asp = (channel.control >> 4) & 0x3;
                uint32_t saved_addr = channel.address + (channel.quadword_count << 4);
                
                switch (asp)
                {
                    case 0:
                        channel.tag_save0 = saved_addr;
                        break;
                    case 1:
                        channel.tag_save1 = saved_addr;
                        break;
                    case 2:
                        Errors::die("[DMAC] DMAtag 'call' sent when ASP == 2!\n");
                }
                
                asp++;
                channel.control &= ~(0x3 << 4);
                channel.control |= asp << 4;

                channel.tag_address = addr;
                break;
            }
            case 6:
            {
                //ret
                channel.address = channel.tag_address + 16;
                int asp = (channel.control >> 4) & 0x3;
                
                switch (asp)
                {
                    case 0:
                        channel.tag_end = true;
                        break;
                    case 1:
                        channel.tag_address = channel.tag_save0;
                        asp--;
                        break;
                    case 2:
                        channel.tag_address = channel.tag_save1;
                        asp--;
                        break;
                }

                channel.control &= ~(0x3 << 4);
                channel.control |= asp << 4;
                break;
            }
            case 7:
            {
                //end
                channel.address = channel.tag_address + 16;
                channel.tag_end = true;
                break;
            }
            default:
                Errors::die("[DMAC] Unrecognized source chain DMAtag id %d", channel.tag_id);
        }
        
        if (IRQ_after_transfer && TIE)
            channel.tag_end = true;
        //printf("New address: $%08X\n", channels[index].address);
        //printf("New tag addr: $%08X\n", channels[index].tag_address);

        switch (PCR_toggle)
        {
            case 1:
                Errors::die("[DMAC] PCR info set to 1!");
            case 2:
                //Disable priority control
                PCR &= ~(1 << 31);
                break;
            case 3:
                //Enable priority control
                PCR |= (1 << 31);
                break;
        }

        //If another channel is queued, always switch to it on a tag boundary
        arbitrate();
    }

    void DMAC::start_DMA(int index)
    {
        auto& channel = channels[index];
        fmt::print("[DMAC] {} DMA started: {:#x}\n", CHANNEL[index], channel.control);
        
        int mode = (channel.control >> 2) & 0x3;
        if (mode == 3)
        {
            //Strange invalid mode... FFXII sets VIF1 DMA to this mode. Having it mean chain is what works best.
            channel.control &= ~(1 << 3);
            mode = 1;
        }

        channel.tag_end = !(mode & 0x1); //always end transfers in normal and interleave mode

        //Stall drain happens on either normal transfers or refs tags
        int tag = (channel.control >> 28) & 0x7;
        bool tag_irq = (channel.control >> 31) & 0x1;
        bool TIE = channel.control & (1 << 7);
        channel.can_stall_drain = !(mode & 0x1) || tag == 4;
        
        switch (mode)
        {
            case 1: //Chain
            {
                //If QWC > 0 and the current tag in CHCR is a terminal tag, end the transfer
                if (channels[index].quadword_count > 0)
                {
                    channels[index].tag_end = (tag == 0 || tag == 7) || (TIE && tag_irq);
                    channels[index].tag_id = tag;
                }
                break;
            }
            case 2: //Interleave
            {
                channels[index].interleaved_qwc = SQWC.transfer_qwc;
                break;
            }
        }

        uint32_t addr = channel.address;
        channel.is_spr = (addr & (1 << 31)) || (addr & 0x70000000) == 0x70000000;
        channel.started = true;

        if (!active_channel)
            cycles_to_run = 0;

        check_for_activation(index);
    }

    uint32_t DMAC::read_master_disable()
    {
        return master_disable;
    }

    void DMAC::write_master_disable(uint32_t value)
    {
        master_disable = value;
    }

    uint8_t DMAC::read8(uint32_t address)
    {
        int shift = (address & 0x3) * 8;
        return (read32(address & ~0x3) >> shift) & 0xFF;
    }

    uint16_t DMAC::read16(uint32_t address)
    {
        int shift = (address & 0x2) * 8;
        return (read32(address & ~0x2) >> shift) & 0xFFFF;
    }

    uint32_t DMAC::read32(uint32_t address)
    {
        uint32_t reg = 0;
        switch (address)
        {
            case 0x10008000:
                reg = channels[VIF0].control;
                break;
            case 0x10008010:
                reg = channels[VIF0].address;
                break;
            case 0x10008020:
                reg = channels[VIF0].quadword_count;
                break;
            case 0x10008030:
                reg = channels[VIF0].tag_address;
                break;
            case 0x10008040:
                reg = channels[VIF0].tag_save0;
                break;
            case 0x10008050:
                reg = channels[VIF0].tag_save1;
                break;
            case 0x10009000:
                reg = channels[VIF1].control;
                break;
            case 0x10009010:
                reg = channels[VIF1].address;
                break;
            case 0x10009020:
                reg = channels[VIF1].quadword_count;
                break;
            case 0x10009030:
                reg = channels[VIF1].tag_address;
                break;
            case 0x10009040:
                reg = channels[VIF1].tag_save0;
                break;
            case 0x10009050:
                reg = channels[VIF1].tag_save1;
                break;
            case 0x1000A000:
                reg = channels[GIF].control;
                break;
            case 0x1000A010:
                reg = channels[GIF].address;
                break;
            case 0x1000A020:
                reg = channels[GIF].quadword_count;
                break;
            case 0x1000A030:
                reg = channels[GIF].tag_address;
                break;
            case 0x1000A040:
                reg = channels[GIF].tag_save0;
                break;
            case 0x1000A050:
                reg = channels[GIF].tag_save1;
                break;
            case 0x1000B000:
                reg = channels[IPU_FROM].control;
                break;
            case 0x1000B010:
                reg = channels[IPU_FROM].address;
                break;
            case 0x1000B020:
                reg = channels[IPU_FROM].quadword_count;
                break;
            case 0x1000B030:
                reg = channels[IPU_FROM].tag_address;
                break;
            case 0x1000B400:
                reg = channels[IPU_TO].control;
                break;
            case 0x1000B410:
                reg = channels[IPU_TO].address;
                break;
            case 0x1000B420:
                reg = channels[IPU_TO].quadword_count;
                break;
            case 0x1000B430:
                reg = channels[IPU_TO].tag_address;
                break;
            case 0x1000C000:
                reg = channels[EE_SIF0].control;
                break;
            case 0x1000C010:
                reg = channels[EE_SIF0].address;
                break;
            case 0x1000C020:
                reg = channels[EE_SIF0].quadword_count;
                break;
            case 0x1000C400:
                reg = channels[EE_SIF1].control;
                break;
            case 0x1000C410:
                reg = channels[EE_SIF1].address;
                break;
            case 0x1000C420:
                reg = channels[EE_SIF1].quadword_count;
                break;
            case 0x1000C430:
                reg = channels[EE_SIF1].tag_address;
                break;
            case 0x1000D000:
                reg = channels[SPR_FROM].control;
                break;
            case 0x1000D010:
                reg = channels[SPR_FROM].address;
                break;
            case 0x1000D020:
                reg = channels[SPR_FROM].quadword_count;
                break;
            case 0x1000D080:
                reg = channels[SPR_FROM].scratchpad_address;
                break;
            case 0x1000D400:
                reg = channels[SPR_TO].control;
                break;
            case 0x1000D410:
                reg = channels[SPR_TO].address;
                break;
            case 0x1000D420:
                reg = channels[SPR_TO].quadword_count;
                break;
            case 0x1000D430:
                reg = channels[SPR_TO].tag_address;
                break;
            case 0x1000D480:
                reg = channels[SPR_TO].scratchpad_address;
                break;
            case 0x1000E000:
                reg |= control.master_enable;
                reg |= control.cycle_stealing << 1;
                reg |= control.mem_drain_channel << 2;
                reg |= control.stall_source_channel << 4;
                reg |= control.stall_dest_channel << 6;
                reg |= control.release_cycle << 8;
                break;
            case 0x1000E010:
                for (int i = 0; i < 15; i++)
                {
                    reg |= interrupt_stat.channel_stat[i] << i;
                    reg |= interrupt_stat.channel_mask[i] << (i + 16);
                }
                break;
            case 0x1000E020:
                reg = PCR;
                break;
            case 0x1000E040:
                reg = RBSR;
                break;
            case 0x1000E050:
                reg = RBOR;
                break;
            default:
                fmt::print("[DMAC] Unrecognized read32 from {:#x}\n", address);
                break;
        }

        return reg;
    }

    void DMAC::write8(uint32_t address, uint8_t value)
    {
        switch (address)
        {
            case 0x10008000:
                channels[VIF0].control &= ~0xFF;
                channels[VIF0].control |= value;
                break;
            case 0x10008001:
                write32(0x10008000, (channels[VIF0].control & 0xFFFF00FF) | (value << 8));
                break;
            case 0x10009000:
                channels[VIF1].control &= ~0xFF;
                channels[VIF1].control |= value;
                break;
            case 0x10009001:
                write32(0x10009000, (channels[VIF1].control & 0xFFFF00FF) | (value << 8));
                break;
            case 0x1000A001:
                write32(0x1000A000, (channels[GIF].control & 0xFFFF00FF) | (value << 8));
                break;
            case 0x1000D001:
                write32(0x1000D000, (channels[SPR_FROM].control & 0xFFFF00FF) | (value << 8));
                break;
            case 0x1000D401:
                write32(0x1000D400, (channels[SPR_TO].control & 0xFFFF00FF) | (value << 8));
                break;
            case 0x1000E000:
                control.master_enable = value & 0x1;
                control.cycle_stealing = value & 0x2;
                control.mem_drain_channel = (value >> 2) & 0x3;
                control.stall_source_channel = (value >> 4) & 0x3;
                control.stall_dest_channel = (value >> 6) & 0x3;
                break;
            default:
                fmt::print("[DMAC] Unrecognized write8 to {:#x} of {:#x}\n", address, value);
                break;
        }
    }

    void DMAC::write16(uint32_t address, uint16_t value)
    {
        switch (address)
        {
            case 0x10008000:
                write32(address, (channels[VIF0].control & 0xFFFF0000) | value);
                break;
            case 0x10009000:
                write32(address, (channels[VIF1].control & 0xFFFF0000) | value);
                break;
            case 0x1000A000:
                write32(address, (channels[GIF].control & 0xFFFF0000) | value);
                break;
            case 0x1000B000:
                write32(address, (channels[IPU_FROM].control & 0xFFFF0000) | value);
                break;
            case 0x1000B400:
                write32(address, (channels[IPU_TO].control & 0xFFFF0000) | value);
                break;
            case 0x1000D000:
                write32(address, (channels[SPR_FROM].control & 0xFFFF0000) | value);
                break;
            case 0x1000D400:
                write32(address, (channels[SPR_TO].control & 0xFFFF0000) | value);
                break;
            default:
                fmt::print("[DMAC] Unrecognized write16 to {:#x} of {:#x}\n", address, value);
                break;
        }
    }

    void DMAC::write32(uint32_t address, uint32_t value)
    {
        switch (address)
        {
            case 0x10008000:
                fmt::print("[DMAC] VIF0 CTRL: {:#x}\n", value);
                if (!(channels[VIF0].control & 0x100))
                {
                    channels[VIF0].control = value;
                    if (value & 0x100)
                        start_DMA(VIF0);
                    else
                        channels[VIF0].started = false;
                }
                else
                {
                    channels[VIF0].control &= (value & 0x100) | 0xFFFFFEFF;
                    channels[VIF0].started = (channels[VIF0].control & 0x100);
                    if (!channels[VIF0].started)
                        deactivate_channel(VIF0);
                }
                break;
            case 0x10008010:
                fmt::print("[DMAC] VIF0 M_ADR: {:#x}\n", value);
                channels[VIF0].address = value & ~0xF;
                break;
            case 0x10008020:
                fmt::print("[DMAC] VIF0 QWC: {:#x}\n", value);
                channels[VIF0].quadword_count = value & 0xFFFF;
                break;
            case 0x10008030:
                fmt::print("[DMAC] VIF0 T_ADR: {:#x}\n", value);
                channels[VIF0].tag_address = value & ~0xF;
                break;
            case 0x10008040:
                fmt::print("[DMAC] VIF0 ASR0: {:#x}\n", value);
                channels[VIF0].tag_save0 = value & ~0xF;
                break;
            case 0x10008050:
                fmt::print("[DMAC] VIF0 ASR1: {:#x}\n", value);
                channels[VIF0].tag_save1 = value & ~0xF;
                break;
            case 0x10009000:
                fmt::print("[DMAC] VIF1 CTRL: {:#x}\n", value);
                if (!(channels[VIF1].control & 0x100))
                {
                    channels[VIF1].control = value;
                    if (value & 0x100)
                        start_DMA(VIF1);
                    else
                        channels[VIF1].started = false;
                }
                else
                {
                    channels[VIF1].control &= (value & 0x100) | 0xFFFFFEFF;
                    channels[VIF1].started = (channels[VIF1].control & 0x100);
                    if (!channels[VIF1].started)
                        deactivate_channel(VIF1);
                }
                break;
            case 0x10009010:
                fmt::print("[DMAC] VIF1 M_ADR: {:#x}\n", value);
                channels[VIF1].address = value & ~0xF;
                break;
            case 0x10009020:
                fmt::print("[DMAC] VIF1 QWC: {:#x}\n", value);
                channels[VIF1].quadword_count = value & 0xFFFF;
                break;
            case 0x10009030:
                fmt::print("[DMAC] VIF1 T_ADR: {:#x}\n", value);
                channels[VIF1].tag_address = value & ~0xF;
                break;
            case 0x10009040:
                fmt::print("[DMAC] VIF1 ASR0: {:#x}\n", value);
                channels[VIF1].tag_save0 = value & ~0xF;
                break;
            case 0x10009050:
                fmt::print("[DMAC] VIF1 ASR1: {:#x}\n", value);
                channels[VIF1].tag_save1 = value & ~0xF;
                break;
            case 0x1000A000:
                fmt::print("[DMAC] GIF CTRL: {:#x}\n", value);
                if (!(channels[GIF].control & 0x100))
                {
                    channels[GIF].control = value;
                    if (value & 0x100)
                    {
                        start_DMA(GIF);
                    }
                    else
                    {
                        e->gif->dma_running(false);
                        channels[GIF].started = false;
                    }
                }
                else
                {
                    channels[GIF].control &= (value & 0x100) | 0xFFFFFEFF;
                    channels[GIF].started = (channels[GIF].control & 0x100);
                    if (!channels[GIF].started)
                    {
                        e->gif->dma_running(false);
                        deactivate_channel(GIF);
                    }
                }
                break;
            case 0x1000A010:
                fmt::print("[DMAC] GIF M_ADR: {:#x}\n", value);
                channels[GIF].address = value & ~0xF;
                break;
            case 0x1000A020:
                fmt::print("[DMAC] GIF QWC: {:#x}\n", value & 0xFFFF);
                channels[GIF].quadword_count = value & 0xFFFF;
                break;
            case 0x1000A030:
                fmt::print("[DMAC] GIF T_ADR: {:#x}\n", value);
                channels[GIF].tag_address = value & ~0xF;
                break;
            case 0x1000A040:
                fmt::print("[DMAC] GIF ASR0: {:#x}\n", value);
                channels[GIF].tag_save0 = value & ~0xF;
                break;
            case 0x1000A050:
                fmt::print("[DMAC] GIF ASR1: {:#x}\n", value);
                channels[GIF].tag_save1 = value & ~0xF;
                break;
            case 0x1000B000:
                fmt::print("[DMAC] IPU_FROM CTRL: {:#x}\n", value);
                if (!(channels[IPU_FROM].control & 0x100))
                {
                    channels[IPU_FROM].control = value;
                    if (value & 0x100)
                        start_DMA(IPU_FROM);
                    else
                        channels[IPU_FROM].started = false;
                }
                else
                {
                    channels[IPU_FROM].control &= (value & 0x100) | 0xFFFFFEFF;
                    channels[IPU_FROM].started = (channels[IPU_FROM].control & 0x100);
                    if (!channels[IPU_FROM].started)
                        deactivate_channel(IPU_FROM);
                }
                break;
            case 0x1000B010:
                fmt::print("[DMAC] IPU_FROM M_ADR: {:#x}\n", value);
                channels[IPU_FROM].address = value & ~0xF;
                break;
            case 0x1000B020:
                fmt::print("[DMAC] IPU_FROM QWC: {:#x}\n", value);
                channels[IPU_FROM].quadword_count = value & 0xFFFF;
                break;
            case 0x1000B400:
                fmt::print("[DMAC] IPU_TO CTRL: {:#x}\n", value);
                if (!(channels[IPU_TO].control & 0x100))
                {
                    channels[IPU_TO].control = value;
                    if (value & 0x100)
                        start_DMA(IPU_TO);
                    else
                        channels[IPU_TO].started = false;
                }
                else
                {
                    channels[IPU_TO].control &= (value & 0x100) | 0xFFFFFEFF;
                    channels[IPU_TO].started = (channels[IPU_TO].control & 0x100);
                    if (!channels[IPU_TO].started)
                        deactivate_channel(IPU_TO);
                }
                break;
            case 0x1000B410:
                fmt::print("[DMAC] IPU_TO M_ADR: {:#x}\n", value);
                channels[IPU_TO].address = value & ~0xF;
                break;
            case 0x1000B420:
                fmt::print("[DMAC] IPU_TO QWC: {:#x}\n", value);
                channels[IPU_TO].quadword_count = value & 0xFFFF;
                break;
            case 0x1000B430:
                fmt::print("[DMAC] IPU_TO T_ADR: {:#x}\n", value);
                channels[IPU_TO].tag_address = value & ~0xF;
                break;
            case 0x1000C000:
                fmt::print("[DMAC] SIF0 CTRL: {:#x}\n", value);
                if (!(channels[EE_SIF0].control & 0x100))
                {
                    channels[EE_SIF0].control = value;
                    if (value & 0x100)
                        start_DMA(EE_SIF0);
                    else
                        channels[EE_SIF0].started = false;
                }
                else
                {
                    channels[EE_SIF0].control &= (value & 0x100) | 0xFFFFFEFF;
                    channels[EE_SIF0].started = (channels[EE_SIF0].control & 0x100);
                    if (!channels[EE_SIF0].started)
                        deactivate_channel(EE_SIF0);
                }
                break;
            case 0x1000C010:
                fmt::print("[DMAC] SIF0 M_ADR: {:#x}\n", value);
                channels[EE_SIF0].address = value & ~0xF;
                break;
            case 0x1000C020:
                fmt::print("[DMAC] SIF0 QWC: {:#x}\n", value);
                channels[EE_SIF0].quadword_count = value & 0xFFFF;
                break;
            case 0x1000C400:
                fmt::print("[DMAC] SIF1 CTRL: {:#x}\n", value);
                if (!(channels[EE_SIF1].control & 0x100))
                {
                    channels[EE_SIF1].control = value;
                    if (value & 0x100)
                        start_DMA(EE_SIF1);
                    else
                        channels[EE_SIF1].started = false;
                }
                else
                {
                    channels[EE_SIF1].control &= (value & 0x100) | 0xFFFFFEFF;
                    channels[EE_SIF1].started = (channels[EE_SIF1].control & 0x100);
                    if (!channels[EE_SIF1].started)
                        deactivate_channel(EE_SIF1);
                }
                break;
            case 0x1000C410:
                fmt::print("[DMAC] SIF1 M_ADR: {:#x}\n", value);
                channels[EE_SIF1].address = value & ~0xF;
                break;
            case 0x1000C420:
                fmt::print("[DMAC] SIF1 QWC: {:#x}\n", value);
                channels[EE_SIF1].quadword_count = value & 0xFFFF;
                break;
            case 0x1000C430:
                fmt::print("[DMAC] SIF1 T_ADR: {:#x}\n", value);
                channels[EE_SIF1].tag_address = value & ~0xF;
                break;
            case 0x1000D000:
                fmt::print("[DMAC] SPR_FROM CTRL: {:#x}\n", value);
                if (!(channels[SPR_FROM].control & 0x100))
                {
                    channels[SPR_FROM].control = value;
                    if (value & 0x100)
                        start_DMA(SPR_FROM);
                    else
                        channels[SPR_FROM].started = false;
                }
                else
                {
                    channels[SPR_FROM].control &= (value & 0x100) | 0xFFFFFEFF;
                    channels[SPR_FROM].started = (channels[SPR_FROM].control & 0x100);
                    if (!channels[SPR_FROM].started)
                        deactivate_channel(SPR_FROM);
                }
                break;
            case 0x1000D010:
                fmt::print("[DMAC] SPR_FROM M_ADR: {:#x}\n", value);
                channels[SPR_FROM].address = value & ~0xF;
                break;
            case 0x1000D020:
                fmt::print("[DMAC] SPR_FROM QWC: {:#x}\n", value);
                channels[SPR_FROM].quadword_count = value & 0xFFFF;
                break;
            case 0x1000D080:
                fmt::print("[DMAC] SPR_FROM SADR: {:#x}\n", value);
                channels[SPR_FROM].scratchpad_address = value & 0x3FFC;
                break;
            case 0x1000D400:
                fmt::print("[DMAC] SPR_TO CTRL: {:#x}\n", value);
                if (!(channels[SPR_TO].control & 0x100))
                {
                    channels[SPR_TO].control = value;
                    if (value & 0x100)
                        start_DMA(SPR_TO);
                    else
                        channels[SPR_TO].started = false;
                }
                else
                {
                    channels[SPR_TO].control &= (value & 0x100) | 0xFFFFFEFF;
                    channels[SPR_TO].started = (channels[SPR_TO].control & 0x100);
                    if (!channels[SPR_TO].started)
                        deactivate_channel(SPR_TO);
                }
                break;
            case 0x1000D410:
                fmt::print("[DMAC] SPR_TO M_ADR: {:#x}\n", value);
                channels[SPR_TO].address = value & ~0xF;
                break;
            case 0x1000D420:
                fmt::print("[DMAC] SPR_TO QWC: {:#x}\n", value);
                channels[SPR_TO].quadword_count = value & 0xFFFF;
                break;
            case 0x1000D430:
                fmt::print("[DMAC] SPR_TO T_ADR: {:#x}\n", value);
                channels[SPR_TO].tag_address = value & ~0xF;
                break;
            case 0x1000D480:
                fmt::print("[DMAC] SPR_TO SADR: {:#x}\n", value);
                channels[SPR_TO].scratchpad_address = value & 0x3FFC;
                break;
            case 0x1000E000:
                fmt::print("[DMAC] Write32 D_CTRL: {:#x}\n", value);
                control.master_enable = value & 0x1;
                control.cycle_stealing = value & 0x2;
                control.mem_drain_channel = (value >> 2) & 0x3;
                control.stall_source_channel = (value >> 4) & 0x3;
                control.stall_dest_channel = (value >> 6) & 0x3;
                control.release_cycle = (value >> 8) & 0x7;
                break;
            case 0x1000E010:
            case 0x1000E100:
                fmt::print("[DMAC] Write32 D_STAT: {:#x}\n", value);
                for (int i = 0; i < 15; i++)
                {
                    if (value & (1 << i))
                        interrupt_stat.channel_stat[i] = false;

                    //Reverse mask
                    if (value & (1 << (i + 16)))
                        interrupt_stat.channel_mask[i] ^= 1;
                }
                int1_check();
                break;
            case 0x1000E020:
                fmt::print("[DMAC] Write to PCR: {:#x}\n", value);
                PCR = value;

                //Global priority control
                if (PCR & (1 << 31))
                {
                    //TODO
                }
                break;
            case 0x1000E030:
                fmt::print("[DMAC] Write to SQWC: {:#x}\n", value);
                SQWC.skip_qwc = value & 0xFF;
                SQWC.transfer_qwc = (value >> 16) & 0xFF;
                break;
            case 0x1000E040:
                fmt::print("[DMAC] Write to RBSR: {:#x}\n", value);
                RBSR = value;
                break;
            case 0x1000E050:
                fmt::print("[DMAC] Write to RBOR: {:#x}\n", value);
                RBOR = value;
                break;
            case 0x1000E060:
                fmt::print("[DMAC] Write to STADR: {:#x}\n", value);
                update_stadr(value);
                break;
            default:
                fmt::print("[DMAC] Unrecognized write32 of {:#x} to {:#x}\n", value, address);
                break;
        }
    }

    void DMAC::set_DMA_request(int index)
    {
        bool old_req = channels[index].dma_req;
        channels[index].dma_req = true;
        if (!old_req)
            check_for_activation(index);
    }

    void DMAC::clear_DMA_request(int index)
    {
        bool old_req = channels[index].dma_req;
        channels[index].dma_req = false;

        if (old_req)
            deactivate_channel(index);
    }

    void DMAC::update_stadr(uint32_t addr)
    {
        uint32_t old_stadr = STADR;
        STADR = addr;

        //Reactivate a stalled channel if necessary
        int c = 0;
        switch (control.stall_dest_channel)
        {
            case 0:
                return;
            case 1:
                c = VIF1;
                break;
            case 2:
                c = GIF;
                break;
            case 3:
                c = EE_SIF1;
                break;
            default:
                Errors::die("DMAC::update_stadr: control.stall_dest_channel >= 4");
        }

        if(channels[c].has_dma_stalled)
            set_DMA_request(c);
    }

    void DMAC::check_for_activation(int index)
    {
        auto& channel = channels[index];
        if (channel.dma_req && channel.started)
        {
            //Keep a channel deactivated if it has been stalled
            bool do_stall_check = false;
            switch (control.stall_dest_channel)
            {
                case 1:
                    do_stall_check = index == VIF1;
                    break;
                case 2:
                    do_stall_check = index == GIF;
                    break;
                case 3:
                    do_stall_check = index == EE_SIF1;
                    break;
            }

            if (do_stall_check && channel.can_stall_drain && channel.address == STADR)
            {
                if (channel.has_dma_stalled == false)
                {
                    fmt::print("[DMAC] Stall Drain channel: {:d} address: {:#x} STADR: {:#x}\n", index, channel.address, STADR);
                    interrupt_stat.channel_stat[DMA_STALL] = true;
                    
                    int1_check();
                    channel.has_dma_stalled = true;
                }
                
                queued_channels.push_back(&channel);
                return;
            }

            if (!active_channel)
                active_channel = &channel;
            else
            {
                queued_channels.push_back(&channel);
            }
        }
    }

    void DMAC::deactivate_channel(int index)
    {
        //printf("[DMAC] Deactivating %s\n", CHAN(index));
        if (active_channel == &channels[index])
        {
            active_channel = nullptr;
            arbitrate();
        }
        else
        {
            for (auto it = queued_channels.begin(); it != queued_channels.end(); it++)
            {
                DMA_Channel* channel = *it;
                if (channel == &channels[index])
                {
                    queued_channels.erase(it);
                    break;
                }
            }
        }
    }

    void DMAC::arbitrate()
    {
        //Only switch to a new channel if something is queued
        if (queued_channels.size())
        {
            if (active_channel)
            {
                queued_channels.push_back(active_channel);
                active_channel = nullptr;
            }

            active_channel = queued_channels.front();
            queued_channels.pop_front();
        }
    }
}