#include <iop/intc.hpp>
#include <iop/iop.hpp>

namespace iop
{
    INTC::INTC(IOP* iop) : 
        iop(iop)
    {
    }

    void INTC::int_check()
    {
        iop->interrupt_check(I_CTRL && (I_STAT & I_MASK));
    }

    void INTC::reset()
    {
        I_MASK = 0;
        I_STAT = 0;
        I_CTRL = 0;
    }

    void INTC::assert_irq(int id)
    {
        I_STAT |= 1 << id;
        int_check();
    }

    uint32_t INTC::read_imask()
    {
        return I_MASK;
    }

    uint32_t INTC::read_istat()
    {
        return I_STAT;
    }

    uint32_t INTC::read_ictrl()
    {
        /* I_CTRL disables interrupts when read */
        uint32_t value = I_CTRL;
        I_CTRL = 0;
        int_check();
        return value;
    }

    void INTC::write_imask(uint32_t value)
    {
        I_MASK = value;
        int_check();
    }

    void INTC::write_istat(uint32_t value)
    {
        I_STAT &= value;
        int_check();
    }

    void INTC::write_ictrl(uint32_t value)
    {
        I_CTRL = value & 0x1;
        int_check();
    }
}