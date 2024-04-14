#include "mac_b_pic.hpp"

namespace ipu
{
    VLC_Entry Macroblock_BPic::table[] =
    {
        {0x2, 0x2000C, 2},
        {0x3, 0x2000E, 2},
        {0x2, 0x30004, 3},
        {0x3, 0x30006, 3},

        {0x2, 0x40008, 4},
        {0x3, 0x4000A, 4},
        {0x3, 0x50001, 5},
        {0x2, 0x5001E, 5},

        {0x3, 0x6001A, 6},
        {0x2, 0x60016, 6},
        {0x1, 0x60011, 6}
    };

    unsigned int Macroblock_BPic::index_table[6] =
    {
        0,
        0,
        2,
        4,
        6,
        8,
    };

    Macroblock_BPic::Macroblock_BPic() : VLC_Table(table, SIZE, 6, index_table)
    {

    }
}