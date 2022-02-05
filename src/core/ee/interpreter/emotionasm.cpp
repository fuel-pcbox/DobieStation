#include "emotionasm.hpp"

namespace ee
{
    namespace assembler
    {
        uint32_t jr(uint8_t addr)
        {
            uint32_t output = 0;
            output |= 0x8;
            output |= addr << 21;
            printf("\nJR: $%08X", output);
            return output;
        }

        uint32_t jalr(uint8_t return_addr, uint8_t addr)
        {
            uint32_t output = 0;
            output |= 0x9;
            output |= return_addr << 11;
            output |= addr << 21;
            printf("\nJALR: $%08X", output);
            return output;
        }

        uint32_t add(uint8_t dest, uint8_t reg1, uint8_t reg2)
        {
            uint32_t output = 0;
            output |= 0x20;
            output |= dest << 11;
            output |= reg2 << 16;
            output |= reg1 << 21;
            printf("\nADD: $%08X", output);
            return output;
        }

        uint32_t and_ee(uint8_t dest, uint8_t reg1, uint8_t reg2)
        {
            uint32_t output = 0;
            output |= 0x24;
            output |= dest << 11;
            output |= reg2 << 16;
            output |= reg1 << 21;
            printf("\nAND: $%08X", output);
            return output;
        }

        uint32_t addiu(uint8_t dest, uint8_t source, int16_t offset)
        {
            uint32_t output = 0;
            output |= (uint16_t)offset;
            output |= dest << 16;
            output |= source << 21;
            output |= 0x09 << 26;
            printf("\nADDIU: $%08X", output);
            return output;
        }

        uint32_t ori(uint8_t dest, uint8_t source, uint16_t offset)
        {
            uint32_t output = 0;
            output |= offset;
            output |= dest << 16;
            output |= source << 21;
            output |= 0xD << 26;
            printf("\nORI: $%08X", output);
            return output;
        }

        uint32_t lui(uint8_t dest, int32_t offset)
        {
            uint32_t output = 0;
            output |= (uint16_t)(offset >> 16);
            output |= dest << 16;
            output |= 0x0F << 26;
            printf("\nLUI: $%08X", output);
            return output;
        }

        uint32_t mfc0(uint8_t dest, uint8_t source)
        {
            uint32_t output = 0;
            output |= source << 11;
            output |= dest << 16;
            output |= 0x10 << 26;
            printf("\nMFC0: $%08X", output);
            return output;
        }

        uint32_t eret()
        {
            uint32_t output = 0;
            output |= 0x18;
            output |= 0x10 << 21;
            output |= 0x10 << 26;
            printf("\nERET: $%08X", output);
            return output;
        }

        uint32_t lq(uint8_t dest, uint8_t base, int16_t offset)
        {
            uint32_t output = 0;
            output |= (uint16_t)offset;
            output |= dest << 16;
            output |= base << 21;
            output |= 0x1E << 26;
            printf("\nLQ: $%08X", output);
            return output;
        }

        uint32_t sq(uint8_t source, uint8_t base, int16_t offset)
        {
            uint32_t output = 0;
            output |= (uint16_t)offset;
            output |= source << 16;
            output |= base << 21;
            output |= 0x1F << 26;
            printf("\nSQ: $%08X", output);
            return output;
        }

        uint32_t lw(uint8_t dest, uint8_t base, int16_t offset)
        {
            uint32_t output = 0;
            output |= (uint16_t)offset;
            output |= dest << 16;
            output |= base << 21;
            output |= 0x23 << 26;
            printf("\nLW: $%08X", output);
            return output;
        }

        uint32_t sw(uint8_t source, uint8_t base, int16_t offset)
        {
            uint32_t output = 0;
            output |= (uint16_t)offset;
            output |= source << 16;
            output |= base << 21;
            output |= 0x2B << 26;
            printf("\nSW: $%08X", output);
            return output;
        }
    }
}