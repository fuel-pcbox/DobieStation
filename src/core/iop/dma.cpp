#include "dma.hpp"
#include "cdvd/cdvd.hpp"
#include "intc.hpp"
#include "sio2/sio2.hpp"
#include "spu/spu.hpp"
#include "iop.hpp"
#include "../util/errors.hpp"
#include "../emulator.hpp"
#include "../sif.hpp"
#include <fmt/core.h>

namespace iop
{
    constexpr const char* CHANNEL[] =
    { 
        "MDECin", "MDECout", "SIF2", "CDVD", 
        "SPU1", "PIO", "OTC", "67", "SPU2", 
        "DEV9", "SIF0", "SIF1", "SIO2in", "SIO2out" 
    };

    DMA::DMA(core::Emulator* e) :
        e(e), intc(e->iop_intc.get())
    {
        apply_dma_functions();
    }

    void DMA::reset()
    {
        active_channel = nullptr;
        queued_channels.clear();
        for (int i = 0; i < 16; i++)
        {
            channels[i].addr = 0;
            channels[i].word_count = 0;
            channels[i].block_size = 0;
            channels[i].control.busy = false;
            channels[i].control.sync_mode = 0;
            channels[i].dma_req = false;
            channels[i].index = i;

            DPCR.enable[i] = false;
            DPCR.priorities[i] = 7;
        }
        DICR.STAT[0] = 0;
        DICR.STAT[1] = 0;
        DICR.MASK[0] = 0;
        DICR.MASK[1] = 0;
        DICR.master_int_enable[0] = false;
        DICR.master_int_enable[1] = false;

        set_DMA_request(IOP_SIO2in);
        set_DMA_request(IOP_SIO2out);
        set_DMA_request(IOP_SIF0);
    }

    void DMA::run(int cycles)
    {
        while (cycles--)
        {
            if (!active_channel)
                break;

            (this->*active_channel->func)();
        }
    }

    void DMA::process_CDVD()
    {
        auto& cdvd = e->cdvd;
        uint32_t count = channels[IOP_CDVD].word_count * channels[IOP_CDVD].block_size * 4;
        fmt::print("[IOP][DMA] CDVD bytes: {:#x}\n", count);
        
        uint32_t bytes_read = cdvd->read_to_RAM(e->iop->ram + channels[IOP_CDVD].addr, count);
        if (count <= bytes_read)
        {
            transfer_end(IOP_CDVD);
            set_chan_block(IOP_CDVD, 0);
            return;
        }
        channels[IOP_CDVD].addr += bytes_read;
        channels[IOP_CDVD].word_count -= bytes_read / (channels[IOP_CDVD].block_size * 4);
    }

    void DMA::process_SPU()
    {
        auto& spu = e->spu;
        bool write_to_spu = channels[IOP_SPU].control.direction_from;
        if (spu->running_ADMA())
        {
            if (!write_to_spu)
                Errors::die("[DMA] SPU doing ADMA read!");
            spu->write_ADMA(e->iop->ram + channels[IOP_SPU].addr);
            channels[IOP_SPU].size--;
            channels[IOP_SPU].addr += 4;
        }
        else
        {
            if (channels[IOP_SPU].delay <= 0)
            {
                //Normal DMA transfer
                if (write_to_spu)
                {
                    uint32_t value = *(uint32_t*)&e->iop->ram[channels[IOP_SPU].addr];
                    spu->write_DMA(value);
                }
                else
                {
                    uint32_t value = spu->read_DMA();
                    *(uint32_t*)&e->iop->ram[channels[IOP_SPU].addr] = value;
                }
                channels[IOP_SPU].size--;
                channels[IOP_SPU].addr += 4;
                channels[IOP_SPU].delay = 3;
            }
            else
                channels[IOP_SPU].delay--;
        }

        if (!channels[IOP_SPU].size)
        {
            channels[IOP_SPU].word_count = 0;
            transfer_end(IOP_SPU);
            spu->finish_DMA();
            return;
        }
    }

    void DMA::process_SPU2()
    {
        auto& spu2 = e->spu2;
        bool write_to_spu = channels[IOP_SPU2].control.direction_from;
        if (spu2->running_ADMA())
        {
            if (!write_to_spu)
                Errors::die("[DMA] SPU2 doing ADMA read!");
            /* Transfer 512 bytes of data(128 words) at once */
            spu2->write_ADMA(e->iop->ram + channels[IOP_SPU2].addr);
            channels[IOP_SPU2].size--;
            channels[IOP_SPU2].addr += 4;
        }
        else
        {
            if (channels[IOP_SPU2].delay <= 0)
            {
                /* Normal DMA transfer */
                if (write_to_spu)
                {
                    uint32_t value = *(uint32_t*)&e->iop->ram[channels[IOP_SPU2].addr];
                    spu2->write_DMA(value);
                }
                else
                {
                    uint32_t value = spu2->read_DMA();
                    *(uint32_t*)&e->iop->ram[channels[IOP_SPU2].addr] = value;
                }
                channels[IOP_SPU2].size--;
                channels[IOP_SPU2].addr += 4;
                channels[IOP_SPU2].delay = 3;
            }
            else
                channels[IOP_SPU2].delay--;
        }

        if (!channels[IOP_SPU2].size)
        {
            channels[IOP_SPU2].word_count = 0;
            transfer_end(IOP_SPU2);
            spu2->finish_DMA();
            return;
        }
    }

    void DMA::process_SIF0()
    {
        auto& sif = e->sif;
        static int junk_words = 0;
        if (channels[IOP_SIF0].word_count)
        {
            uint32_t data = *(uint32_t*)&e->iop->ram[channels[IOP_SIF0].addr];
            sif->write_SIF0(data);

            channels[IOP_SIF0].addr += 4;
            channels[IOP_SIF0].word_count--;
            if (!channels[IOP_SIF0].word_count)
            {
                sif->send_SIF0_junk(junk_words);
                if (channels[IOP_SIF0].tag_end)
                    transfer_end(IOP_SIF0);
            }
        }
        //Read tag if there's enough room to transfer the EE's tag
        else if (sif->get_SIF0_size() <= core::SubsystemInterface::MAX_FIFO_SIZE - 2)
        {
            uint32_t data = *(uint32_t*)&e->iop->ram[channels[IOP_SIF0].tag_addr];
            uint32_t words = *(uint32_t*)&e->iop->ram[channels[IOP_SIF0].tag_addr + 4];
            //Transfer EEtag
            sif->write_SIF0(*(uint32_t*)&e->iop->ram[channels[IOP_SIF0].tag_addr + 8]);
            sif->write_SIF0(*(uint32_t*)&e->iop->ram[channels[IOP_SIF0].tag_addr + 12]);

            channels[IOP_SIF0].addr = data & 0xFFFFFF;
            channels[IOP_SIF0].word_count = words & 0xFFFFF;

            /* NOTE: UNCONFIRMED ON REAL HARDWARE!
             * The default behavior of PCSX2 is to round up "words" to the nearest 16 bytes.
             * This has the effect of reading additional data from the SIF0 buffer in IOP memory.
             * However, True Crime: Streets of L.A. stores important variables right next to a 4 byte receive buffer.
             * Thus, the variables get overwritten when certain transfers occur, and the game expects them to be nonzero.
             * PCSX2's behavior causes that memory to be zeroed out, which crashes the game in menus.
             *
             * I have surmised that the correct behavior on nonaligned transfers is to read the oldest values
             * from previous transfers. This indeed results in the game's memory being nonzero, allowing it to go in-game.
             */
            junk_words = (words & 0x3) ? (4 - (words & 0x3)) : 0;

            channels[IOP_SIF0].tag_addr += 16;

            fmt::print("[IOP][DMA] Read SIF0 DMAtag!\n");
            fmt::print("Data: {:#x}\n", data);
            fmt::print("Words: {:#x}\n", channels[IOP_SIF0].word_count);
            fmt::print("Junk: {:d}\n", junk_words);

            if ((data & (1 << 31)) || (data & (1 << 30)))
                channels[IOP_SIF0].tag_end = true;
        }
    }

    void DMA::process_SIF1()
    {
        auto& sif = e->sif;
        if (channels[IOP_SIF1].word_count)
        {
            uint32_t data = sif->read_SIF1();

            *(uint32_t*)&e->iop->ram[channels[IOP_SIF1].addr] = data;
            channels[IOP_SIF1].addr += 4;
            channels[IOP_SIF1].word_count--;
            if (!channels[IOP_SIF1].word_count && channels[IOP_SIF1].tag_end)
                transfer_end(IOP_SIF1);
        }
        else if (sif->get_SIF1_size() >= 4)
        {
            //Read IOP DMAtag
            uint32_t data = sif->read_SIF1();
            channels[IOP_SIF1].addr = data & 0xFFFFFF;
            channels[IOP_SIF1].word_count = sif->read_SIF1() & 0xFFFFC;

            //EEtag
            sif->read_SIF1();
            sif->read_SIF1();
            
            fmt::print("[IOP][DMA] Read SIF1 DMAtag!\n");
            fmt::print("Addr: {:#x}\n", channels[IOP_SIF1].addr);
            fmt::print("Words: {:#x}\n", channels[IOP_SIF1].word_count);
            
            if ((data & (1 << 31)) || (data & (1 << 30)))
                channels[IOP_SIF1].tag_end = true;
        }
    }

    void DMA::process_SIO2in()
    {
        auto& sio2 = e->sio2;
        sio2->dma_reset();
        int size = channels[IOP_SIO2in].word_count * channels[IOP_SIO2in].block_size * 4;
        while (size)
        {
            sio2->write_dma(e->iop->ram[channels[IOP_SIO2in].addr]);
            channels[IOP_SIO2in].addr++;
            size--;
        }
        channels[IOP_SIO2in].word_count = 0;
        if (channels[IOP_SIO2in].word_count == 0)
            transfer_end(IOP_SIO2in);
    }

    void DMA::process_SIO2out()
    {
        auto& sio2 = e->sio2;
        int size = channels[IOP_SIO2out].word_count * channels[IOP_SIO2out].block_size * 4;
        while (size)
        {
            e->iop->ram[channels[IOP_SIO2out].addr] = sio2->read_serial();
            channels[IOP_SIO2out].addr++;
            size--;
        }
        channels[IOP_SIO2out].word_count = 0;
        if (channels[IOP_SIO2out].word_count == 0)
            transfer_end(IOP_SIO2out);
    }

    void DMA::transfer_end(int index)
    {
        fmt::print("[IOP][DMA] {} transfer ended\n", CHANNEL[index]);
        channels[index].control.busy = false;
        channels[index].tag_end = false;

        if (active_channel && active_channel->index == index)
            find_new_active_channel();

        bool dicr2 = index > 7;
        if (dicr2)
            index -= 8;
        if (DICR.MASK[dicr2] & (1 << index))
        {
            fmt::print("[IOP][DMA] IRQ requested: {:#x} {:#x}\n", DICR.STAT[dicr2], DICR.MASK[dicr2]);
            DICR.STAT[dicr2] |= 1 << index;
            intc->assert_irq(3);
        }
    }

    uint32_t DMA::get_DPCR()
    {
        uint32_t reg = 0;
        for (int i = 0; i < 8; i++)
        {
            reg |= DPCR.priorities[i] << (i << 2);
            reg |= DPCR.enable[i] << ((i << 2) + 3);
        }
        return reg;
    }

    uint32_t DMA::get_DPCR2()
    {
        uint32_t reg = 0;
        for (int i = 8; i < 16; i++)
        {
            int bit = i - 8;
            reg |= DPCR.priorities[i] << (bit << 2);
            reg |= DPCR.enable[i] << ((bit << 2) + 3);
        }
        return reg;
    }

    uint32_t DMA::get_DICR()
    {
        uint32_t reg = 0;
        reg |= DICR.force_IRQ[0] << 15;
        reg |= DICR.MASK[0] << 16;
        reg |= DICR.master_int_enable[0] << 23;
        reg |= DICR.STAT[0] << 24;

        bool IRQ;
        if (DICR.master_int_enable[0] && (DICR.MASK[0] & DICR.STAT[0]))
            IRQ = true;
        else
            IRQ = false;
        reg |= IRQ << 31;
        //printf("[IOP DMA] Get DICR: $%08X\n", reg);
        return reg;
    }

    uint32_t DMA::get_DICR2()
    {
        uint32_t reg = 0;
        reg |= DICR.force_IRQ[1] << 15;
        reg |= DICR.MASK[1] << 16;
        reg |= DICR.master_int_enable[1] << 23;
        reg |= DICR.STAT[1] << 24;

        bool IRQ;
        if (DICR.master_int_enable[1] && (DICR.MASK[1] & DICR.STAT[1]))
            IRQ = true;
        else
            IRQ = false;
        reg |= IRQ << 31;
        //printf("[IOP DMA] Get DICR2: $%08X\n", reg);
        return reg;
    }

    uint32_t DMA::get_chan_addr(int index)
    {
        //printf("[IOP DMA] Read %s addr: $%08X\n", CHAN(index), channels[index].addr);
        return channels[index].addr;
    }

    uint32_t DMA::get_chan_block(int index)
    {
        //printf("[IOP DMA] Read %s block: $%08X\n", CHAN(index), channels[index].word_count | (channels[index].block_size << 16));
        return channels[index].word_count | (channels[index].block_size << 16);
    }

    uint32_t DMA::get_chan_control(int index)
    {
        uint32_t reg = 0;
        reg |= channels[index].control.direction_from;
        reg |= channels[index].control.unk8 << 8;
        reg |= channels[index].control.sync_mode << 9;
        reg |= channels[index].control.busy << 24;
        reg |= channels[index].control.unk30 << 30;
        //printf("[IOP DMA] Read %s control: $%08X\n", CHAN(index), reg);
        return reg;
    }

    void DMA::set_DPCR(uint32_t value)
    {
        fmt::print("[IOP][DMA] Set DPCR: {:#x}\n", value);
        for (int i = 0; i < 8; i++)
        {
            bool old_enable = DPCR.enable[i];
            DPCR.priorities[i] = (value >> (i << 2)) & 0x7;
            DPCR.enable[i] = value & (1 << ((i << 2) + 3));
            if (!old_enable && DPCR.enable[i])
                channels[i].tag_end = false;
        }
    }

    void DMA::set_DPCR2(uint32_t value)
    {
        fmt::print("[IOP][DMA] Set DPCR2: {:#x}\n", value);
        for (int i = 8; i < 16; i++)
        {
            int bit = i - 8;
            bool old_enable = DPCR.enable[i];
            DPCR.priorities[i] = (value >> (bit << 2)) & 0x7;
            DPCR.enable[i] = value & (1 << ((bit << 2) + 3));
            if (!old_enable && DPCR.enable[i])
                channels[i].tag_end = false;
        }
    }

    void DMA::set_DICR(uint32_t value)
    {
        //printf("[IOP DMA] Set DICR: $%08X\n", value);
        DICR.force_IRQ[0] = value & (1 << 15);
        DICR.MASK[0] = (value >> 16) & 0x7F;
        DICR.master_int_enable[0] = value & (1 << 23);
        DICR.STAT[0] &= ~((value >> 24) & 0x7F);
        if (DICR.force_IRQ[0])
            intc->assert_irq(3);
    }

    void DMA::set_DICR2(uint32_t value)
    {
        //printf("[IOP DMA] Set DICR2: $%08X\n", value);
        DICR.force_IRQ[1] = value & (1 << 15);
        DICR.MASK[1] = (value >> 16) & 0x7F;
        DICR.master_int_enable[1] = value & (1 << 23);
        DICR.STAT[1] &= ~((value >> 24) & 0x7F);
        if (DICR.force_IRQ[1])
            intc->assert_irq(3);
    }

    void DMA::set_DMA_request(int index)
    {
        //printf("[DMA] DMA req: %s\n", CHAN(index));
        bool old_req = channels[index].dma_req;
        channels[index].dma_req = true;

        if (!old_req)
            active_dma_check(index);
    }

    void DMA::clear_DMA_request(int index)
    {
        //printf("[DMA] DMA clear: %s\n", CHAN(index));
        bool old_req = channels[index].dma_req;
        channels[index].dma_req = false;

        if (old_req)
            deactivate_dma(index);
    }

    void DMA::set_chan_addr(int index, uint32_t value)
    {
        fmt::print("[IOP][DMA] {} address: {:#x}\n", CHANNEL[index], value);
        channels[index].addr = value;
    }

    void DMA::set_chan_block(int index, uint32_t value)
    {
        fmt::print("[IOP][DMA] {} block: {:#x}\n", CHANNEL[index], value);
        channels[index].block_size = value & 0xFFFF;
        channels[index].word_count = value >> 16;
        channels[index].size = channels[index].block_size * channels[index].word_count;
    }

    void DMA::set_chan_size(int index, uint16_t value)
    {
        fmt::print("[IOP][DMA] {} size: {:#x}\n", CHANNEL[index], value);
        channels[index].block_size = value;
        channels[index].size = channels[index].block_size * channels[index].word_count;
    }

    void DMA::set_chan_count(int index, uint16_t value)
    {
        fmt::print("[IOP][DMA] {} count: {:#x}\n", CHANNEL[index], value);
        channels[index].word_count = value;
        channels[index].size = channels[index].block_size * channels[index].word_count;
    }

    void DMA::set_chan_control(int index, uint32_t value)
    {
        auto& spu = e->spu;
        auto& spu2 = e->spu2;

        fmt::print("[IOP][DMA] {} control: {:#x}\n", CHANNEL[index], value);
        bool old_busy = channels[index].control.busy;
        channels[index].control.direction_from = value & 1;
        channels[index].control.unk8 = value & (1 << 8);
        channels[index].control.sync_mode = (value >> 9) & 0x3;
        channels[index].control.busy = value & (1 << 24);
        if (!old_busy && channels[index].control.busy)
        {
            if (index == IOP_SPU)
            {
                spu->start_DMA(channels[index].size * 2);
                channels[index].delay = 3;
            }
            if (index == IOP_SPU2)
            {
                spu2->start_DMA(channels[index].size * 2);
                channels[index].delay = 3;
            }
            active_dma_check(index);
        }
        else if (old_busy && !channels[index].control.busy)
        {
            deactivate_dma(index);
            if (index == IOP_SPU)
                spu->pause_DMA();
            else if (index == IOP_SPU2)
                spu2->pause_DMA();
        }
        channels[index].control.unk30 = value & (1 << 30);
    }

    void DMA::set_chan_tag_addr(int index, uint32_t value)
    {
        fmt::print("[IOP][DMA] {} tag address: {:#x}\n", CHANNEL[index], value);
        channels[index].tag_addr = value;
    }

    void DMA::active_dma_check(int index)
    {
        if (channels[index].dma_req && channels[index].control.busy)
        {
            if (!active_channel)
                active_channel = &channels[index];
            else if (active_channel->index < index)
            {
                queued_channels.push_back(active_channel);
                active_channel = &channels[index];
            }
            else
                queued_channels.push_back(&channels[index]);
        }
    }

    void DMA::deactivate_dma(int index)
    {
        if (active_channel == &channels[index])
            find_new_active_channel();
        else
        {
            for (auto it = queued_channels.begin(); it != queued_channels.end(); it++)
            {
                if (*it == &channels[index])
                {
                    queued_channels.erase(it);
                    return;
                }
            }
        }
    }

    void DMA::find_new_active_channel()
    {
        if (!queued_channels.size())
        {
            active_channel = nullptr;
            return;
        }
        int index = -1;
        auto it = queued_channels.begin();
        auto channel_to_erase = it;

        for (it = queued_channels.begin(); it != queued_channels.end(); it++)
        {
            DMA_Channel* test = *it;
            if (test->index > index)
            {
                channel_to_erase = it;
                index = test->index;
                active_channel = &channels[index];
            }
        }

        queued_channels.erase(channel_to_erase);
        fmt::print("[IOP][DMA] New active channel: {}\n", CHANNEL[active_channel->index]);
    }

    void DMA::apply_dma_functions()
    {
        for (int i = 0; i < 16; i++)
            channels[i].func = nullptr;

        channels[IOP_CDVD].func = &DMA::process_CDVD;
        channels[IOP_SIF0].func = &DMA::process_SIF0;
        channels[IOP_SIF1].func = &DMA::process_SIF1;
        channels[IOP_SPU].func = &DMA::process_SPU;
        channels[IOP_SPU2].func = &DMA::process_SPU2;
        channels[IOP_SIO2in].func = &DMA::process_SIO2in;
        channels[IOP_SIO2out].func = &DMA::process_SIO2out;
    }
}