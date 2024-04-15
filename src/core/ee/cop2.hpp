#pragma once
#include "vu/vu.hpp"

namespace ee
{
    /* This is essentially a wrapper around VU0 with access to VU1 */
    class Cop2
    {
    private:
        vu::VectorUnit* vu0, * vu1;
    public:
        Cop2(vu::VectorUnit* vu0, vu::VectorUnit* vu1);

        uint32_t cfc2(int index);
        void ctc2(int index, uint32_t value);
    };
}