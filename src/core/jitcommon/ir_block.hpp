#pragma once
#include <deque>
#include "ir_instr.hpp"

namespace IR
{

class Block
{
    private:
        std::deque<Instruction> instructions;
        int cycle_count;
    public:
        Block();

        void add_instr(Instruction& instr);

        unsigned int get_instruction_count() const;
        int get_cycle_count() const;
        Instruction get_next_instr();

        void set_cycle_count(int cycles);
};

};