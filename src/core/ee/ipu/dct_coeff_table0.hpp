#pragma once
#include "dct_coeff.hpp"

namespace ipu
{
    class DCT_Coeff_Table0 : public DCT_Coeff
    {
    private:
        static VLC_Entry table[];
        static RunLevelPair runlevel_table[];
        static unsigned int index_table[];

        constexpr static int SIZE = 112;
    public:
        DCT_Coeff_Table0();

        bool get_end_of_block(IPU_FIFO& FIFO, uint32_t& result);
        bool get_skip_block(IPU_FIFO& FIFO);
        bool get_runlevel_pair(IPU_FIFO& FIFO, RunLevelPair& pair, bool MPEG1);
        bool get_runlevel_pair_dc(IPU_FIFO& FIFO, RunLevelPair& pair, bool MPEG1);
    };
}