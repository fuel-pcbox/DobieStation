#pragma once
#include <cstdint>
#include <fstream>

namespace core
{
    class Scheduler;
}

namespace iop
{
    struct Timer_Control
    {
        bool use_gate;
        uint8_t gate_mode;
        bool zero_return;
        bool compare_interrupt_enabled;
        bool overflow_interrupt_enabled;
        bool repeat_int;
        bool toggle_int;
        bool int_enable;
        bool extern_signal;
        uint8_t prescale;
        bool compare_interrupt;
        bool overflow_interrupt;
        bool started;
    };

    struct Timer
    {
        uint64_t counter;
        Timer_Control control;
        uint64_t target;
    };

    class INTC;

    class IOPTiming
    {
    private:
        iop::INTC* intc;
        core::Scheduler* scheduler;

        Timer timers[6];

        int timer_interrupt_event_id;
        int events[6];

        void timer_interrupt(int index);
        void IRQ_test(int index, bool overflow);
    
    public:
        IOPTiming(iop::INTC* intc, core::Scheduler* scheduler);

        void reset();
        uint32_t read_counter(int index);
        uint16_t read_control(int index);
        uint32_t read_target(int index);

        void write_counter(int index, uint32_t value);
        void write_control(int index, uint16_t value);
        void write_target(int index, uint32_t value);

        void load_state(std::ifstream& state);
        void save_state(std::ofstream& state);
    };
}