#pragma once
#include <cstdint>

namespace ee
{
    class EmotionEngine;
    namespace jit
    {
        uint16_t run(EmotionEngine* ee);
        void reset(bool clear_cache);
    }
}