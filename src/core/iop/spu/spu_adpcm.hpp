#pragma once
#include <array>
#include <cstdint>
#include "spu_utils.hpp"

namespace spu
{
    class ADPCM_Decoder
    {
    public:
        std::array<int16_t, 28> decode_block(uint8_t* block);
        uint8_t flags;
    private:
        uint8_t block[14];
        uint8_t shift_factor, coef_index;
        int32_t hist1, hist2;
    };
}