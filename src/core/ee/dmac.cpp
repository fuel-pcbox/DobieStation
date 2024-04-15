#include "dmac.hpp"
#include "../emulator.hpp"
#include "../util/errors.hpp"
#include "emotion.hpp"
#include "vu/vu.hpp"
#include "vu/vif.hpp"
#include "../gs/gif.hpp"
#include "ipu/ipu.hpp"
#include "../sif.hpp"
#include <algorithm>
#include <fmt/core.h>

namespace ee
{
    DMAC::DMAC(core::Emulator* e) : 
        e(e)
    {
        apply_dma_funcs();
    }

    void DMAC::reset()
    {
        /* SCPH-39001 requires this value to be set, possibly other BIOSes too */
        master_disable = 0x1201;
        control.dma_enable = false;
        mfifo_empty_triggered = false;
        status.value = 0;
        priority_control = 0;
        stall_address = 0;
        cycles_to_run = 0;

        active_channel = nullptr;
        queued_channels.clear();

        for (int i = 0; i < 15; i++)
        {
            channels[i].started = false;
            channels[i].control.value = 0;
            channels[i].dma_req = false;
            channels[i].index = i;
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
        if (!control.dma_enable || (master_disable & (1 << 16)))
            return;

        if (active_channel)
        {
            cycles_to_run += cycles;
            while (cycles_to_run > 0)
            {
                DMAChannel* temp = active_channel;
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
        if (control.mfifo_drain_channel - 1 == index)
        {
            auto& channel = channels[index];
            uint8_t id = (channel.control.tag >> 12) & 0x7;
            channel.tag_address.value = ringbuffer_offset | (channel.tag_address.value & ringbuffer_size);

            /* Don't mask the MADR on REFE/REF/REFS as they don't follow the tag, so likely outside the MFIFO */
            uint32_t addr = channel.tag_address.value;
            if (channel.qword_count)
            {
                if (id != 0 && id != 3 && id != 4)
                    channel.address.value = ringbuffer_offset | (channel.address.value & ringbuffer_size);

                addr = channel.address.value;
            }

            if (addr == channels[SPR_FROM].address.value)
            {
                if (!mfifo_empty_triggered)
                {
                    status.mfifo_empty = true;
                    int1_check();
                    mfifo_empty_triggered = true;
                    fmt::print("[DMAC] MFIFO Empty\n");
                }
                
                /* Continue transfer if using a referenceand there's QWC left */
                if (channel.qword_count && (id == 0 || id == 3 || id == 4))
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

        auto& channel = channels[index];
        channel.control.running = false;
        channel.started = false;
        status.channel_irq |= (1 << index);
        
        int1_check();
        deactivate_channel(index);
    }

    void DMAC::int1_check()
    {
        bool int1_signal = (status.channel_irq & status.channel_irq_mask) ||
                           (status.dma_stall && status.stall_irq_mask) ||
                           (status.mfifo_empty && status.mfifo_irq_mask);
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

        if (channel.qword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address.value >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.qword_count, max_qwc);
            while (count < quads_to_transfer)
            {
                uint128_t data = fetch128(channel.address.value);
                if (!vif0->feed_DMA(data))
                    break;
                
                advance_source_dma(VIF0);
                count++;
            }
        }
        if (!channel.qword_count)
        {
            if (channel.tag_end)
            {
                transfer_end(VIF0);
                return count;
            }
            else
            {
                uint128_t DMAtag = fetch128(channel.tag_address.value);
                if (channel.control.transfer_tag)
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

        if (channel.qword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address.value >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.qword_count, max_qwc);

            if (channel.control.direction && control.stall_control_drain_channel == 1 && channel.can_stall_drain)
            {
                if (channel.address.value + (quads_to_transfer * 16) > stall_address)
                {
                    if (channel.has_dma_stalled == false)
                    {
                        fmt::print("[DMAC] VIF1 DMA Stall at {:#x} stall_address = {:#x}\n", channel.address.value, stall_address);
                        status.dma_stall = true;
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
                
                if (channel.control.direction)
                {
                    uint128_t data = fetch128(channel.address.value);
                    if (!vif1->feed_DMA(data))
                        break;
                }
                else
                {
                    auto quad_data = vif1->readFIFO();
                    if (std::get<1>(quad_data))
                        store128(channel.address.value, std::get<0>(quad_data));
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
        
        if (!channel.qword_count)
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
                
                uint128_t DMAtag = fetch128(channel.tag_address.value);
                if (channel.control.transfer_tag)
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
        if (channel.qword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address.value >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.qword_count, max_qwc);

            if (control.stall_control_drain_channel == 2 && channel.can_stall_drain)
            {
                if (channel.address.value + (quads_to_transfer * 16) > stall_address)
                {
                    if (channel.has_dma_stalled == false)
                    {
                        fmt::print("[DMAC] GIF DMA Stall at {:#x} stall_address = {:#x}\n", channel.address.value, stall_address);
                        status.dma_stall = true;
                        
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
                    uint128_t data = fetch128(channel.address.value);
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
        
        if (!channel.qword_count)
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

        if (channel.qword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address.value >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.qword_count, max_qwc);
            while (count < quads_to_transfer)
            {
                if (!ipu->can_read_FIFO())
                    break;
                
                uint128_t data = ipu->read_FIFO();
                store128(channel.address.value, data);

                advance_dest_dma(IPU_FROM);
                count++;
            }
        }
        
        if (control.stall_control_channel == 3)
            update_stadr(channel.address.value);
        
        if (!channel.qword_count)
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

        if (channel.qword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address.value >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.qword_count, max_qwc);
            
            while (count < quads_to_transfer)
            {
                if (!ipu->can_write_FIFO())
                    break;
                
                ipu->write_FIFO(fetch128(channel.address.value));
                advance_source_dma(IPU_TO);
                count++;
            }
        }

        if (!channel.qword_count)
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

        uint32_t max_qwc = 8 - ((channel.address.value >> 4) & 0x7);
        int quads_to_transfer = std::min({channel.qword_count, max_qwc, sif->get_SIF0_size() / 4U});
        while (count < quads_to_transfer)
        {
            uint128_t quad;
            for (int i = 0; i < 4; i++)
                quad._u32[i] = sif->read_SIF0();
            
            store128(channel.address.value, quad);
            advance_dest_dma(EE_SIF0);
            count++;
        }

        if (!channel.qword_count)
        {
            if (channel.tag_end)
            {
                transfer_end(EE_SIF0);
                return count;
            }
            else if (sif->get_SIF0_size() >= 2)
            {
                uint128_t data;
                data._u32[0] = sif->read_SIF0();
                data._u32[1] = sif->read_SIF0();
                fmt::print("[DMAC] SIF0 tag: {:#x}\n", data._u64[0]);

                /* Read data from DMA tag */
                DMACTag tag = { .value = data };
                channel.control.tag = tag.value._u16[1];
                channel.qword_count = tag.qwords;
                channel.address.value = tag.address;            
                channel.tag_id = tag.id;
                channel.is_spr = channel.address.is_scratchpad();

                if (channel.tag_id == 7 || (tag.irq && channel.control.enable_irq_bit))
                    channel.tag_end = true;

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

        if (channel.qword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address.value >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.qword_count, max_qwc);

            if (control.stall_control_drain_channel == 3 && channel.can_stall_drain)
            {
                if (channel.address.value + (quads_to_transfer * 16) > stall_address)
                {
                    if (channel.has_dma_stalled == false)
                    {
                        fmt::print("[DMAC] SIF1 DMA Stall at {} stall_address = {:#x}\n", channel.address.value, stall_address);
                        
                        status.dma_stall = true;
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
                uint128_t data = fetch128(channel.address.value);
                sif->write_SIF1(data);
                
                advance_source_dma(EE_SIF1);
                count++;
            }
        }

        if (!channel.qword_count)
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

        if (channel.qword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address.value >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.qword_count, max_qwc);
            
            while (count < quads_to_transfer)
            {
                if (control.mfifo_drain_channel != 0)
                {
                    channel.address.value = ringbuffer_offset | (channel.address.value & ringbuffer_size);
                }

                uint128_t DMAData = fetch128(channel.scratchpad_address | (1 << 31));
                store128(channel.address.value & 0x7FFFFFFF, DMAData);

                channel.scratchpad_address += 16;
                advance_dest_dma(SPR_FROM);
                count++;

                if (channel.control.mode == 0x2)
                {
                    channel.interleaved_qwc--;
                    if (!channel.interleaved_qwc)
                    {
                        channel.interleaved_qwc = skip_qword.qwords_to_transfer;
                        channel.address.value += skip_qword.qwords_to_skip * 16;
                        
                        arbitrate();
                        break;
                    }
                }

                if (control.mfifo_drain_channel != 0)
                    channel.address.value = ringbuffer_offset | (channel.address.value & ringbuffer_size);
            }
        }

        if (!channel.qword_count)
        {
            if (channel.tag_end)
            {
                transfer_end(SPR_FROM);
                return count;
            }
            else
            {
                /* Read data from tag */
                DMACTag tag = { .value = fetch128(channel.scratchpad_address | (1 << 31)) };
                channel.qword_count = tag.qwords;
                channel.address.value = tag.address;
                channel.scratchpad_address += 16;
                channel.scratchpad_address &= 0x3FFF;
                channel.control.tag = tag.value._u16[1];
                channel.tag_id = tag.id;

                fmt::print("[DMAC] SPR_FROM tag: {:#x}\n", tag.value._u64[0]);

                if (channel.tag_id == 7 || (tag.irq && channel.control.enable_irq_bit))
                    channel.tag_end = true;

                arbitrate();
            }
        }

        return count;
    }

    int DMAC::process_SPR_TO()
    {
        int count = 0;
        auto& channel = channels[SPR_TO];
        
        if (channel.qword_count)
        {
            uint32_t max_qwc = 8 - ((channel.address.value >> 4) & 0x7);
            int quads_to_transfer = std::min(channel.qword_count, max_qwc);
            
            while (count < quads_to_transfer)
            {
                uint128_t DMAData = fetch128(channel.address.value & 0x7FFFFFFF);
                store128(channel.scratchpad_address | (1 << 31), DMAData);
                channel.scratchpad_address += 16;

                advance_source_dma(SPR_TO);
                count++;

                //Interleave mode
                if (channel.control.mode == 0x2)
                {
                    channel.interleaved_qwc--;
                    if (!channel.interleaved_qwc)
                    {
                        channel.interleaved_qwc = skip_qword.qwords_to_transfer;
                        channel.address.value += skip_qword.qwords_to_skip * 16;

                        //On interleave boundaries, always arbitrate
                        arbitrate();
                        break;
                    }
                }
            }
        }

        if (!channel.qword_count)
        {
            if (channel.tag_end)
                transfer_end(SPR_TO);
            else
            {
                uint128_t DMAtag = fetch128(channel.tag_address.value);
                if (channel.control.transfer_tag)
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
        auto& channel = channels[index];
        int mode = channel.control.mode;
        channel.address.value += 16;

        /* PS2 checks MFIFO MADR as it transfers but it needs to check also at the end of a packet
           and send an empty signal. This needs to be done on the MADR as TADR doesn't incrmenet on END tags.
           For the code to work, we need to check this before the QWC decrements. HW Test confirmed. */
        if (channel.qword_count == 1)
            mfifo_handler(index);

        channel.qword_count--;
        if (mode == 1) //Chain
        {
            switch (channel.tag_id)
            {
                case 1: //CNT
                    channel.tag_address.value = channel.address.value;
                    break;
                default: 
                    break;
            }
        }
    }

    void DMAC::advance_dest_dma(int index)
    {
        auto& channel = channels[index];
        int mode = channel.control.mode;

        channel.address.value += 16;
        channel.qword_count--;

        //Update stall address if we're not in chain mode or the tag id is cnts
        if (mode != 1 || channel.tag_id == 0)
        {
            //SIF0 source stall drain
            if (index == 5 && control.stall_control_channel == 1)
                update_stadr(channel.address.value);
            //SPR_FROM source stall drain
            if (index == 8 && control.stall_control_channel == 2)
                update_stadr(channel.address.value);
            //IPU_FROM source stall drain
            if (index == 3 && control.stall_control_channel == 3)
                update_stadr(channel.address.value);
        }
    }

    void DMAC::handle_source_chain(int index)
    {
        auto& channel = channels[index];
        DMACTag tag = { .value = fetch128(channel.tag_address.value) };
        DMACAddress address = { .value = tag.address & ~0xF };

        /* Update channel according to the data in the tag */
        channel.control.tag = tag.value._u16[1];
        channel.is_spr = address.is_scratchpad();
        channel.tag_id = tag.id;
        channel.qword_count = tag.qwords;
        channel.can_stall_drain = false;

        switch (channel.tag_id)
        {
            case 0:
            {
                //refe
                channel.address = address;
                channel.tag_address.value += 16;
                channel.tag_end = true;
                break;
            }
            case 1:
            {
                //cnt
                channel.address = channel.tag_address + 16;
                channel.tag_address = channel.address;
                break;
            }
            case 2:
            {
                //next
                channel.address = channel.tag_address + 16;
                channel.tag_address = address;
                break;
            }
            case 3:
            {
                //ref
                channel.address = address;
                channel.tag_address.value += 16;
                break;
            }
            case 4:
            {
                //refs
                channel.address = address;
                channel.tag_address.value += 16;
                channel.can_stall_drain = true;
                break;
            }
            case 5:
            {
                //call
                int asp = channel.control.stack_ptr;
                channel.address = channel.tag_address + 16;
                channel.tag_address = address;

                channel.saved_tag_address[asp] = channel.address + (channel.qword_count << 4);
                channel.control.stack_ptr = ++asp;
                break;
            }
            case 6:
            {
                //ret
                channel.address = channel.tag_address + 16;
                int asp = channel.control.stack_ptr;
                
                switch (asp)
                {
                    case 0:
                        channel.tag_end = true;
                        break;
                    case 1:
                        channel.tag_address = channel.saved_tag_address[0];
                        asp--;
                        break;
                    case 2:
                        channel.tag_address = channel.saved_tag_address[1];
                        asp--;
                        break;
                }

                channel.control.stack_ptr = asp;
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
        
        if (tag.irq && channel.control.enable_irq_bit)
            channel.tag_end = true;
        
        switch (tag.priority)
        {
            case 1:
                Errors::die("[DMAC] PCR info set to 1!");
            case 2:
                //Disable priority control
                priority_control &= ~(1 << 31);
                break;
            case 3:
                //Enable priority control
                priority_control |= (1 << 31);
                break;
        }

        //If another channel is queued, always switch to it on a tag boundary
        arbitrate();
    }

    void DMAC::start_DMA(int index)
    {
        auto& channel = channels[index];
        fmt::print("[DMAC] {} DMA started: {:#x}\n", CHANNEL[index], channel.control.value);
        
        int mode = channel.control.mode;
        if (mode == 3)
        {
            /* Strange invalid mode... FFXII sets VIF1 DMA to this mode.
               Having it mean chain is what works best. */
            channel.control.mode &= ~0x2;
            mode = 1;
        }

        /* Always end transfers in normaland interleave mode */
        channel.tag_end = !(mode & 0x1); 
        
        /* Stall drain happens on either normal transfers or refs tags */
        int tag_id = channel.control.tag_info.id;
        channel.can_stall_drain = channel.tag_end || tag_id == 4;
        
        switch (mode)
        {
            case 1: //Chain
            {
                /* If QWC > 0 and the current tag in CHCR is a terminal tag, end the transfer. */
                if (channel.qword_count > 0)
                {
                    channel.tag_id = tag_id;
                    channel.tag_end = (channel.control.enable_irq_bit && channel.control.tag_info.irq) || 
                                      (tag_id == 0 || tag_id == 7);
                }
                break;
            }
            case 2: //Interleave
            {
                channel.interleaved_qwc = skip_qword.qwords_to_transfer;
                break;
            }
        }

        channel.is_spr = channel.address.is_scratchpad();
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
        uint32_t old_stall_address = stall_address;
        stall_address = addr;

        //Reactivate a stalled channel if necessary
        int c = 0;
        switch (control.stall_control_drain_channel)
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
                Errors::die("DMAC::update_stall_address: control.stall_dest_channel >= 4");
        }

        if (channels[c].has_dma_stalled)
            set_DMA_request(c);
    }

    void DMAC::check_for_activation(int index)
    {
        auto& channel = channels[index];
        if (channel.dma_req && channel.started)
        {
            //Keep a channel deactivated if it has been stalled
            bool do_stall_check = false;
            switch (control.stall_control_drain_channel)
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

            if (do_stall_check && channel.can_stall_drain && channel.address.value == stall_address)
            {
                if (channel.has_dma_stalled == false)
                {
                    fmt::print("[DMAC] Stall Drain channel: {:d} address: {:#x} stall_address: {:#x}\n", index, channel.address.value, stall_address);
                    status.dma_stall = true;
                    
                    int1_check();
                    channel.has_dma_stalled = true;
                }
                
                queued_channels.push_back(&channel);
                return;
            }

            if (!active_channel)
                active_channel = &channel;
            else
                queued_channels.push_back(&channel);
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
                DMAChannel* channel = *it;
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