#pragma once
#include <util/int128.hpp>
#include <fstream>
#include <array>
#include <list>
#include <cassert>
#include <fmt/core.h>

namespace core
{
    class Emulator;
}

namespace ee
{
    enum DMAC_CHANNELS
    {
        VIF0,
        VIF1,
        GIF,
        IPU_FROM,
        IPU_TO,
        EE_SIF0,
        EE_SIF1,
        EE_SIF2,
        SPR_FROM,
        SPR_TO,
        DMA_STALL = 13,
        MFIFO_EMPTY = 14
    };

    class DMAC;

    union DMACAddress
    {
        uint32_t value;
        struct
        {
            uint32_t address : 30;
            uint32_t mem_select : 1;
        };
    };

    union DCHCR
    {
        uint32_t value;
        struct
        {
            uint32_t direction : 1; /* 0 = to memory, 1 = from memory */
            uint32_t : 1;
            uint32_t mode : 2; /* 0 = normal, 1 = chain, 2 = interleave */
            uint32_t stack_ptr : 2;
            uint32_t transfer_tag : 1;
            uint32_t enable_irq_bit : 1;
            uint32_t running : 1;
            uint32_t : 7;
            uint32_t tag : 16;
        };
    };

    using DMAHandler = int (DMAC::*)();
    struct DMAChannel
    {
        union
        {
            uint32_t registers[9];
            struct
            {
                DCHCR control;
                DMACAddress address;
                uint32_t qword_count;
                DMACAddress tag_address;
                DMACAddress saved_tag_address[2];
                uint32_t padding[2]; /* Used so we can read the struct properly */
                uint32_t scratchpad_address;
            };
        };

        /* For use by emulator */
        bool tag_end, paused;
        uint8_t interleaved_qwc, tag_id;

        /* Handler function for the channel */
        DMAHandler func;
        bool started, dma_req, is_spr;
        bool can_stall_drain, has_dma_stalled;
        int index;
    };

    /* Writing to this is a pain */
    union DSTAT
    {
        uint32_t value;
        struct
        {
            uint32_t channel_irq : 10; /* Clear with 1 */
            uint32_t : 3;
            uint32_t dma_stall : 1; /* Clear with 1 */
            uint32_t mfifo_empty : 1; /* Clear with 1 */
            uint32_t bus_error : 1; /* Clear with 1 */
            uint32_t channel_irq_mask : 10; /* Reverse with 1 */
            uint32_t : 3;
            uint32_t stall_irq_mask : 1; /* Reverse with 1 */
            uint32_t mfifo_irq_mask : 1; /* Reverse with 1 */
            uint32_t : 1;
        };
        /* If you notice above the lower 16bits are cleared when 1 is written to them
           while the upper 16bits are reversed. So I'm making this struct to better
           implement this behaviour */
        struct
        {
            uint32_t clear : 16;
            uint32_t reverse : 16;
        };
    };

    union DCTRL
    {
        uint32_t value;
        struct
        {
            uint32_t dma_enable : 1;
            uint32_t cycle_stealing : 1;
            uint32_t mfifo_drain_channel : 2;
            uint32_t stall_control_channel : 2;
            uint32_t stall_control_drain_channel : 2;
            uint32_t release_cycle_period : 3;
        };
    };

    union DSQWC
    {
        uint32_t value;
        struct
        {
            uint32_t qwords_to_skip : 8;
            uint32_t : 8;
            uint32_t qwords_to_transfer : 8;
            uint32_t : 8;
        };
    };

    /* Generate a small LUT at compile time that translates the
       the nibble of the channel address to its index. */
    constexpr auto CHANNEL_INDEX = []
    {
        std::array<int, 256> arr = {};
        arr[0x80] = 0; arr[0x90] = 1; arr[0xA0] = 2;
        arr[0xB0] = 3; arr[0xB4] = 4; arr[0xC0] = 5;
        arr[0xC4] = 6; arr[0xC8] = 7; arr[0xD0] = 8;
        arr[0xD4] = 9;

        return arr;
    }();

    /* String LUTs used for logging. */
    constexpr const char* CHANNEL[] =
    {
        "VIF0", "VIF1", "GIF", "IPU_FROM", "IPU_TO",
        "SIF0", "SIF1", "SIF2", "SPR_FROM", "SPR_TO"
    };

    constexpr const char* GLOBALS[] =
    {
        "D_CTRL", "D_STAT", "D_PCR", "D_SQWC",
        "D_RBSR", "D_RBOR", "D_STADT",
    };

    constexpr const char* CHANNEL_REGS[] =
    {
        "Dn_CHCR", "Dn_MADR", "Dn_QWC", "Dn_TADR",
        "Dn_ASR0", "Dn_ASR1", "", "", "Dn_SADR"
    };

    class EmotionEngine;

    class DMAC
    {
    private:
        core::Emulator* e;
        
        DMAChannel channels[15];
        DMAChannel* active_channel;
        DMAChannel* queued_VIF0;
        std::list<DMAChannel*> queued_channels;

        bool mfifo_empty_triggered;
        int cycles_to_run;
        uint32_t master_disable;

        /* Usage of struct here is to guarantee item
           linearity in memory. */
        union
        {
            uint32_t globals[7];
            struct
            {
                DCTRL control;
                DSTAT status;
                uint32_t priority_control;
                DSQWC skip_qword;
                uint32_t ringbuffer_size;
                uint32_t ringbuffer_offset;
                uint32_t stall_address;
            };
        };

        void apply_dma_funcs();

        int process_VIF0();
        int process_VIF1();
        int process_GIF();
        int process_IPU_FROM();
        int process_IPU_TO();
        int process_SIF0();
        int process_SIF1();
        int process_SPR_FROM();
        int process_SPR_TO();

        void handle_source_chain(int index);
        void advance_source_dma(int index);
        void advance_dest_dma(int index);
        bool mfifo_handler(int index);
        void transfer_end(int index);
        void int1_check();

        uint128_t fetch128(uint32_t addr);
        void store128(uint32_t addr, uint128_t data);

        void update_stadr(uint32_t addr);
        void check_for_activation(int index);
        void deactivate_channel(int index);
        void arbitrate();
    
    public:
        DMAC(core::Emulator* e);
        void reset();
        void run(int cycles);
        void start_DMA(int index);

        template <typename T>
        T read(uint32_t address);

        template <typename T>
        void write(uint32_t address, T value);

        uint32_t read_master_disable();
        void write_master_disable(uint32_t value);

        void set_DMA_request(int index);
        void clear_DMA_request(int index);

        void load_state(std::ifstream& state);
        void save_state(std::ofstream& state);
    };
    
    template<typename T>
    inline T DMAC::read(uint32_t address)
    {
        /* Write to global registers */
        if (address >= 0x1000E000)
        {
            int reg = (address >> 4) & 0xf, offset = (address & 0xf) / sizeof(T);
            auto ptr = (T*)&globals[reg] + offset;

            fmt::print("[DMAC] Reading {:#x} from {}\n", *ptr, GLOBALS[reg]);
            return *ptr;
        }
        else
        {
            int id = (address >> 8) & 0xff;
            int channel = CHANNEL_INDEX[id];
            int reg = (address >> 4) & 0xf, offset = (address & 0xf) / sizeof(T);
            auto ptr = (T*)&channels[channel].registers[reg] + offset;

            fmt::print("[DMAC] Reading {:#x} from {} of channel {:d}\n", *ptr, CHANNEL_REGS[offset], channel);
            return *ptr;
        }
    }
    
    template<typename T>
    inline void DMAC::write(uint32_t address, T value)
    {
        /* Write to global registers */
        if (address >= 0x1000E000)
        {
            int reg = (address >> 4) & 0xf, offset = (address & 0xf) / sizeof(T);
            auto ptr = (T*)&globals[reg] + offset;

            fmt::print("[DMAC] Writing {:#x} to {}\n", value, GLOBALS[offset]);

            switch (reg)
            {
            case 1:
                /* D_STAT is only written with word alignment, but just in case */
                assert(std::is_same<T, uint32_t>::value);

                /* The lower bits are cleared while the upper ones are reversed */
                status.clear &= ~(value & 0xffff);
                status.reverse ^= (value >> 16);

                int1_check();
                return;
            case 6:
                update_stadr(value);
                return;
            }

            *ptr = value;
        }
        else
        {
            int id = (address >> 8) & 0xff;
            int channel_id = CHANNEL_INDEX[id];
            int offset = (address >> 4) & 0xf;
            auto ptr = (uint32_t*)&channels[channel_id] + offset;

            fmt::print("[DMAC] Writing {:#x} to {} of channel {:d}\n", value, CHANNEL_REGS[offset], channel_id);

            auto& channel = channels[channel_id];
            switch (offset)
            {
            case 0:
                /* Writing to control when DMA is running requires special handling. */
                if (!channel.control.running)
                {
                    channel.control.value = value;

                    if (value & 0x100)
                        start_DMA(channel_id);
                    else
                        channel.started = false;
                }
                else
                {
                    channel.control.running = value & 0x100;
                    channel.started = channel.control.running;

                    if (!channel.started)
                        deactivate_channel(channel_id);
                }

                return;
            case 2:
                value &= 0xFFFF;
                break;
            case 1: [[fallthrough]];
            case 3:
            case 4:
            case 5:
            case 8:
                /* The lower bits of DMA addresses must be zero: */
                /* NOTE: This is actually required since the BIOS can write unaligned
                   addresses and expect the DMAC to read them correctly. */
                value &= ~0xF;
                break;
            }

            *ptr = value;
        }
    }
}