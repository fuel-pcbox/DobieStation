#pragma once
#include <cstdint>

class EmotionEngine;

namespace EE_JIT
{
    uint16_t run(EmotionEngine* ee);
    void reset(bool clear_cache);
};

