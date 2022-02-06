#pragma once
#include <cstdint>
#include <fstream>

namespace iop
{
    class INTC;
    class DMA; //Will be needed later for Firewire DMA's
}

namespace sio2
{
    class Firewire
    {
    private:
        iop::INTC* intc;
        iop::DMA* dma;

        uint32_t intr0, intr1, intr2;
        uint32_t intr0mask, intr1mask, intr2mask;
        uint32_t ctrl0, ctrl1, ctrl2;
        uint32_t dmaCtrlSR0, dmaCtrlSR1;
        uint32_t PHYAccess;
        uint8_t  PHYregisters[16];

        void readPHY();
        void writePHY();

    public:
        Firewire(iop::INTC* intc, iop::DMA* dma);

        void reset();
        void write32(uint32_t addr, uint32_t value);
        uint32_t read32(uint32_t addr);
        /*
        void load_state(std::ifstream& state);
        void save_state(std::ofstream& state);
        */
    };
}