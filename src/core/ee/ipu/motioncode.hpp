#pragma once
#include "vlc_table.hpp"

namespace ipu
{
    class MotionCode : public VLC_Table
    {
    private:
        static VLC_Entry table[];
        static unsigned int index_table[];

        constexpr static int SIZE = 33;
    public:
        MotionCode();
    };
}
