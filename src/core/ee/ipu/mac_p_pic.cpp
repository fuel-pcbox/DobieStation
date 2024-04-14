#include "mac_p_pic.hpp"

namespace ipu
{
    VLC_Entry Macroblock_PPic::table[] =
    {
        {0x1, 0x1000A, 1},
        {0x1, 0x20002, 2},
        {0x1, 0x30008, 3},
        {0x3, 0x50001, 5},
        {0x2, 0x5001A, 5},
        {0x1, 0x50012, 5},
        {0x1, 0x60011, 6}
    };

    unsigned int Macroblock_PPic::index_table[6] =
    {
        0,
        1,
        2,
        2,
        3,
        6,
    };

    Macroblock_PPic::Macroblock_PPic() :
        VLC_Table(table, SIZE, 6, index_table)
    {

    }
}