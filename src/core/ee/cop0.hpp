#pragma once
#include <cstdint>
#include <fstream>

namespace core
{
    class Emulator;
}

namespace ee
{
    class EmotionEngine;
    namespace jit
    {
        class EE_JIT64;
    }
}

extern "C" uint8_t * exec_block_ee(ee::jit::EE_JIT64& jit, ee::EmotionEngine& ee);

namespace ee
{
    enum CACHE_MODE
    {
        UNCACHED = 2,
        CACHED = 3,
        UCAB = 7,
        SPR = 8
    };

    enum COP0_REG
    {
        STATUS = 12,
        CAUSE = 13
    };

    struct COP0_STATUS
    {
        bool int_enable;
        bool exception;
        bool error;
        uint8_t mode;
        bool bus_error;
        bool int0_mask;
        bool int1_mask;
        bool timer_int_mask;
        bool master_int_enable;
        bool edi;
        bool ch;
        bool bev;
        bool dev;
        uint8_t cu;
    };

    struct COP0_CAUSE
    {
        uint8_t code;
        bool int0_pending;
        bool int1_pending;
        bool timer_int_pending;
        uint8_t code2;
        uint8_t ce;
        bool bd2;
        bool bd;
    };

    struct TLB_Entry
    {
        bool valid[2];
        bool dirty[2];
        uint8_t cache_mode[2];
        uint32_t pfn[2];
        bool is_scratchpad;
        bool global;
        uint8_t asid;
        uint32_t vpn2;
        uint32_t page_size;
        uint32_t page_mask;

        uint8_t page_shift;
    };

    struct VTLB_Info
    {
        uint8_t cache_mode;
        bool modified = false;
    };

    class DMAC;

    class Cop0
    {
    private:
        core::Emulator* e;

        uint8_t** kernel_vtlb;
        uint8_t** sup_vtlb;
        uint8_t** user_vtlb;

        VTLB_Info* vtlb_info;

        void unmap_tlb(TLB_Entry* entry);
        void map_tlb(TLB_Entry* entry);

        uint8_t* get_mem_pointer(uint32_t paddr);
    public:
        TLB_Entry tlb[48];
        uint32_t gpr[32];
        COP0_STATUS status;
        COP0_CAUSE cause;
        uint32_t EPC, ErrorEPC;
        uint32_t PCCR, PCR0, PCR1;
        Cop0(core::Emulator* e);
        ~Cop0();

        uint8_t** get_vtlb_map();

        bool is_cached(uint32_t address);

        void reset();
        void init_tlb();

        uint32_t mfc(int index);
        void mtc(int index, uint32_t value);

        bool get_condition();
        bool int1_raised();
        bool int_enabled();
        bool int_pending();

        void count_up(int cycles);

        void read_tlb(int index);
        void set_tlb(int index);
        void clear_tlb_modified(size_t page);
        void set_tlb_modified(size_t page);
        bool get_tlb_modified(size_t page) const;

        void load_state(std::ifstream& state);
        void save_state(std::ofstream& state);

        //Friends needed for JIT convenience
        friend class EE_JIT64;
        friend class EE_JitTranslator;

        friend uint8_t* exec_block_ee(jit::EE_JIT64& jit, EmotionEngine& ee);
    };

    inline bool Cop0::is_cached(uint32_t address)
    {
        return vtlb_info[address / 4096].cache_mode == CACHE_MODE::CACHED;
    }

    inline bool Cop0::int_enabled()
    {
        return status.master_int_enable && status.int_enable && !status.exception && !status.error;
    }

    inline bool Cop0::get_tlb_modified(size_t page) const
    {
        return vtlb_info[page].modified;
    }
}
