#pragma once
#include <cstdint>
#include <fstream>

namespace iop
{
    class IOP;
    class INTC
    {
    private:
        IOP* iop;

        uint32_t I_STAT, I_MASK, I_CTRL;

        void int_check();
    public:
        INTC(IOP* iop);

        void reset();
        void assert_irq(int id);

        uint32_t read_imask();
        uint32_t read_istat();
        uint32_t read_ictrl();

        void write_imask(uint32_t value);
        void write_istat(uint32_t value);
        void write_ictrl(uint32_t value);

        void load_state(std::ifstream& state);
        void save_state(std::ofstream& state);
    };
}