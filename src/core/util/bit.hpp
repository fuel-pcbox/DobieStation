#pragma once
#ifdef _WIN32
#include <intrin.h>
#endif
#include <cstdint>

namespace util
{
    inline uint32_t count_leading_zeros_u32(uint32_t word) 
    {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_clz(word);
#else
        unsigned long result;
        _BitScanReverse(&result, word);
        return 31 - result;
#endif
    }
}