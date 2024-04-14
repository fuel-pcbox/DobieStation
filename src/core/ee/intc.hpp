#pragma once
#include <cstdint>
#include <fstream>

namespace core
{
    class Scheduler;
}

namespace ee
{
    class EmotionEngine;

    enum class Interrupt
    {
        GS,
        SBUS,
        VBLANK_START,
        VBLANK_END,
        VIF0,
        VIF1,
        VU0,
        VU1,
        IPU,
        TIMER0,
        TIMER1,
        TIMER2,
        TIMER3,
        SFIFO,
        VU0_WATCHDOG //bark
    };

    class INTC
    {
    private:
        EmotionEngine* ee;
        core::Scheduler* scheduler;
        
        uint32_t INTC_MASK = 0, INTC_STAT = 0;
        int read_stat_count = 0;
        bool stat_speedhack_active = 0;
        int int_check_event_id;
    
    public:
        INTC(EmotionEngine* cpu, core::Scheduler* scheduler);
        ~INTC() = default;

        void reset();
        void int0_check();

        uint32_t read_mask();
        uint32_t read_stat();
        void write_mask(uint32_t value);
        void write_stat(uint32_t value);

        void assert_IRQ(int id);
        void deassert_IRQ(int id);

        void load_state(std::ifstream& state);
        void save_state(std::ofstream& state);
    };
}