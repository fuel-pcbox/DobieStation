#include "mac_i_pic.hpp"

VLC_Entry Macroblock_IPic::table[] =
{
    {0x1, 0x10001, 1},
    {0x1, 0x20011, 2}
};

Macroblock_IPic::Macroblock_IPic() : VLC_Table(table, SIZE, 2)
{

}
