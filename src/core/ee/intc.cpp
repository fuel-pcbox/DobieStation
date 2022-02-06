#include <ee/intc.hpp>
#include <ee/emotion.hpp>
#include <scheduler.hpp>
#include <fmt/core.h>

namespace ee
{
    INTC::INTC(EmotionEngine* cpu, core::Scheduler* scheduler) : 
        ee(cpu), scheduler(scheduler)
    {
    }

    void INTC::reset()
    {
        INTC_MASK = 0;
        INTC_STAT = 0;
        read_stat_count = 0;
        stat_speedhack_active = false;
        int_check_event_id = scheduler->register_function([this](uint64_t param) { int0_check(); });
    }

    uint32_t INTC::read_mask()
    {
        return INTC_MASK;
    }

    uint32_t INTC::read_stat()
    {
        read_stat_count++;
        if (read_stat_count >= 1000)
        {
            ee->halt();
            stat_speedhack_active = true;
        }

        return INTC_STAT;
    }

    void INTC::write_mask(uint32_t value)
    {
        INTC_MASK ^= (value & 0x7FFF);
        fmt::print("[INTC] New INTC_MASK: {:#x}\n", INTC_MASK);
        int0_check();
    }

    void INTC::write_stat(uint32_t value)
    {
        INTC_STAT &= ~value;
        int0_check();
    }

    void INTC::assert_IRQ(int id)
    {
        INTC_STAT |= (1 << id);
        if (stat_speedhack_active)
        {
            ee->unhalt();
            stat_speedhack_active = false;
            read_stat_count = 0;
        }

        /* Some games will enter a wait - for - VBLANK loop where they poll INTC_STAT while a VBLANK interrupt handler
           is registered.
           If we fire the interrupt immediately, those games will never exit the loop.
           I don't know the exact number of cycles we need to wait, but 8 seems to work. */
        scheduler->add_event(int_check_event_id, 8);
    }

    void INTC::deassert_IRQ(int id)
    {
        INTC_STAT &= ~(1 << id);
        int0_check();
    }

    void INTC::int0_check()
    {
        if (stat_speedhack_active)
        {
            ee->unhalt();
            stat_speedhack_active = false;
            read_stat_count = 0;
        }

        read_stat_count = 0;
        ee->set_int0_signal(INTC_STAT & INTC_MASK);
    }
}