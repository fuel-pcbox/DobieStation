#include <cstdio>
#include <cstdlib>
#include "iop_interpreter.hpp"
#include "../../util/errors.hpp"

namespace iop
{
    namespace interpreter
    {
        void interpret(IOP& cpu, uint32_t instruction)
        {
            if (!instruction)
                return;
            int op = instruction >> 26;
            switch (op)
            {
            case 0x00:
                special(cpu, instruction);
                break;
            case 0x01:
                regimm(cpu, instruction);
                break;
            case 0x02:
                j(cpu, instruction);
                break;
            case 0x03:
                jal(cpu, instruction);
                break;
            case 0x04:
                beq(cpu, instruction);
                break;
            case 0x05:
                bne(cpu, instruction);
                break;
            case 0x06:
                blez(cpu, instruction);
                break;
            case 0x07:
                bgtz(cpu, instruction);
                break;
            case 0x08:
                addi(cpu, instruction);
                break;
            case 0x09:
                addiu(cpu, instruction);
                break;
            case 0x0A:
                slti(cpu, instruction);
                break;
            case 0x0B:
                sltiu(cpu, instruction);
                break;
            case 0x0C:
                andi(cpu, instruction);
                break;
            case 0x0D:
                ori(cpu, instruction);
                break;
            case 0x0E:
                xori(cpu, instruction);
                break;
            case 0x0F:
                lui(cpu, instruction);
                break;
            case 0x10:
            case 0x11:
            case 0x12:
            case 0x13:
                cop(cpu, instruction);
                break;
            case 0x20:
                lb(cpu, instruction);
                break;
            case 0x21:
                lh(cpu, instruction);
                break;
            case 0x22:
                lwl(cpu, instruction);
                break;
            case 0x23:
                lw(cpu, instruction);
                break;
            case 0x24:
                lbu(cpu, instruction);
                break;
            case 0x25:
                lhu(cpu, instruction);
                break;
            case 0x26:
                lwr(cpu, instruction);
                break;
            case 0x28:
                sb(cpu, instruction);
                break;
            case 0x29:
                sh(cpu, instruction);
                break;
            case 0x2A:
                swl(cpu, instruction);
                break;
            case 0x2B:
                sw(cpu, instruction);
                break;
            case 0x2E:
                swr(cpu, instruction);
                break;
            default:
                unknown_op("regular", op, instruction);
            }
        }

        void j(IOP& cpu, uint32_t instruction)
        {
            uint32_t addr = (instruction & 0x3FFFFFF) << 2;
            uint32_t PC = cpu.get_PC();
            addr += (PC + 4) & 0xF0000000;

            //IOP module call detection
            /*uint32_t next_instr = cpu.read32(PC + 4);
            if ((next_instr & 0xFFFF0000) == 0x24000000)
            {
                //if (addr == 0x0000E9D0)
                    //cpu.set_disassembly(true);
                if (addr == 0x00003F78)
                {
                    uint32_t intr = cpu.get_gpr(4);
                    printf("[IOP] RegisterIntrHandler: $%02X\n", intr);
                }
                else if (addr == 0x1EC8 || addr == 0x00001F64)
                {
                    uint32_t struct_ptr = cpu.get_gpr(4);
                    uint16_t version = cpu.read32(struct_ptr + 8);
                    char name[9];
                    name[8] = 0;
                    for (int i = 0; i < 8; i++)
                        name[i] = cpu.read8(struct_ptr + 12 + i);
                    printf("[IOP] RegisterLibraryEntries: %s version %d.0%d\n", name, version >> 8, version & 0xFF);
                }
                else
                {
                    printf("[IOP] Call $%02X: $%08X ($%08X, args:", next_instr & 0xFF, addr, PC);
                    printf("%08X %08X %08X %08X)\n", cpu.get_gpr(4), cpu.get_gpr(5), cpu.get_gpr(6), cpu.get_gpr(7));
                }
            }*/
            if (addr == PC)
                cpu.halt();
            cpu.jp(addr);
        }

        void jal(IOP& cpu, uint32_t instruction)
        {
            uint32_t addr = (instruction & 0x3FFFFFF) << 2;
            uint32_t PC = cpu.get_PC();
            addr += (PC + 4) & 0xF0000000;
            cpu.jp(addr);
            cpu.set_gpr(31, PC + 8);
        }

        void beq(IOP& cpu, uint32_t instruction)
        {
            int offset = (int16_t)(instruction & 0xFFFF);
            offset <<= 2;
            uint32_t reg1 = cpu.get_gpr((instruction >> 21) & 0x1F);
            uint32_t reg2 = cpu.get_gpr((instruction >> 16) & 0x1F);
            cpu.branch(reg1 == reg2, offset);
        }

        void bne(IOP& cpu, uint32_t instruction)
        {
            int offset = (int16_t)(instruction & 0xFFFF);
            offset <<= 2;
            uint32_t reg1 = cpu.get_gpr((instruction >> 21) & 0x1F);
            uint32_t reg2 = cpu.get_gpr((instruction >> 16) & 0x1F);
            cpu.branch(reg1 != reg2, offset);
        }

        void blez(IOP& cpu, uint32_t instruction)
        {
            int offset = (int16_t)(instruction & 0xFFFF);
            offset <<= 2;
            int32_t reg = (int32_t)cpu.get_gpr((instruction >> 21) & 0x1F);
            cpu.branch(reg <= 0, offset);
        }

        void bgtz(IOP& cpu, uint32_t instruction)
        {
            int offset = (int16_t)(instruction & 0xFFFF);
            offset <<= 2;
            int32_t reg = (int32_t)cpu.get_gpr((instruction >> 21) & 0x1F);
            cpu.branch(reg > 0, offset);
        }

        void addi(IOP& cpu, uint32_t instruction)
        {
            int16_t imm = (int16_t)(instruction & 0xFFFF);
            uint32_t dest = (instruction >> 16) & 0x1F;
            uint32_t source = (instruction >> 21) & 0x1F;
            uint32_t result = cpu.get_gpr(source);
            result += imm;
            cpu.set_gpr(dest, result);
        }

        void addiu(IOP& cpu, uint32_t instruction)
        {
            int16_t imm = (int16_t)(instruction & 0xFFFF);
            uint32_t dest = (instruction >> 16) & 0x1F;
            uint32_t source = (instruction >> 21) & 0x1F;
            uint32_t result = cpu.get_gpr(source);
            result += imm;
            cpu.set_gpr(dest, result);
        }

        void slti(IOP& cpu, uint32_t instruction)
        {
            int32_t imm = (int32_t)(int16_t)(instruction & 0xFFFF);
            uint32_t dest = (instruction >> 16) & 0x1F;
            int32_t source = (instruction >> 21) & 0x1F;
            source = (int32_t)cpu.get_gpr(source);
            cpu.set_gpr(dest, source < imm);
        }

        void sltiu(IOP& cpu, uint32_t instruction)
        {
            uint32_t imm = (uint32_t)(int32_t)(int16_t)(instruction & 0xFFFF);
            uint32_t dest = (instruction >> 16) & 0x1F;
            uint32_t source = (instruction >> 21) & 0x1F;
            source = cpu.get_gpr(source);
            cpu.set_gpr(dest, source < imm);
        }

        void andi(IOP& cpu, uint32_t instruction)
        {
            uint32_t imm = instruction & 0xFFFF;
            uint32_t dest = (instruction >> 16) & 0x1F;
            uint32_t source = (instruction >> 21) & 0x1F;
            cpu.set_gpr(dest, cpu.get_gpr(source) & imm);
        }

        void ori(IOP& cpu, uint32_t instruction)
        {
            uint32_t imm = instruction & 0xFFFF;
            uint32_t dest = (instruction >> 16) & 0x1F;
            uint32_t source = (instruction >> 21) & 0x1F;
            cpu.set_gpr(dest, cpu.get_gpr(source) | imm);
        }

        void xori(IOP& cpu, uint32_t instruction)
        {
            uint32_t imm = instruction & 0xFFFF;
            uint32_t dest = (instruction >> 16) & 0x1F;
            uint32_t source = (instruction >> 21) & 0x1F;
            cpu.set_gpr(dest, cpu.get_gpr(source) ^ imm);
        }

        void lui(IOP& cpu, uint32_t instruction)
        {
            uint32_t imm = (instruction & 0xFFFF) << 16;
            uint32_t dest = (instruction >> 16) & 0x1F;
            cpu.set_gpr(dest, imm);
        }

        void lb(IOP& cpu, uint32_t instruction)
        {
            int16_t offset = (int16_t)(instruction & 0xFFFF);
            uint32_t dest = (instruction >> 16) & 0x1F;
            uint32_t base = (instruction >> 21) & 0x1F;
            uint32_t addr = cpu.get_gpr(base);
            addr += offset;
            cpu.set_gpr(dest, (int32_t)(int8_t)cpu.read8(addr));
        }

        void lh(IOP& cpu, uint32_t instruction)
        {
            int16_t offset = (int16_t)(instruction & 0xFFFF);
            uint32_t dest = (instruction >> 16) & 0x1F;
            uint32_t base = (instruction >> 21) & 0x1F;
            uint32_t addr = cpu.get_gpr(base);
            addr += offset;
            cpu.set_gpr(dest, (int32_t)(int16_t)cpu.read16(addr));
        }

        void lwl(IOP& cpu, uint32_t instruction)
        {
            static const uint32_t LWL_MASK[4] = { 0xffffff, 0x0000ffff, 0x000000ff, 0x00000000 };
            static const uint8_t LWL_SHIFT[4] = { 24, 16, 8, 0 };
            int16_t offset = (int16_t)(instruction & 0xFFFF);
            uint32_t dest = (instruction >> 16) & 0x1F;
            uint32_t base = (instruction >> 21) & 0x1F;
            uint32_t addr = cpu.get_gpr(base) + offset;
            int shift = addr & 0x3;

            uint32_t mem = cpu.read32(addr & ~0x3);
            cpu.set_gpr(dest, (cpu.get_gpr(dest) & LWL_MASK[shift]) | (mem << LWL_SHIFT[shift]));
        }

        void lwr(IOP& cpu, uint32_t instruction)
        {
            static const uint32_t LWR_MASK[4] = { 0x000000, 0xff000000, 0xffff0000, 0xffffff00 };
            static const uint8_t LWR_SHIFT[4] = { 0, 8, 16, 24 };
            int16_t offset = (int16_t)(instruction & 0xFFFF);
            uint32_t dest = (instruction >> 16) & 0x1F;
            uint32_t base = (instruction >> 21) & 0x1F;
            uint32_t addr = cpu.get_gpr(base) + offset;
            int shift = addr & 0x3;

            uint32_t mem = cpu.read32(addr & ~0x3);
            mem = (cpu.get_gpr(dest) & LWR_MASK[shift]) | (mem >> LWR_SHIFT[shift]);
            cpu.set_gpr(dest, mem);
        }

        void lw(IOP& cpu, uint32_t instruction)
        {
            int16_t offset = (int16_t)(instruction & 0xFFFF);
            uint32_t dest = (instruction >> 16) & 0x1F;
            uint32_t base = (instruction >> 21) & 0x1F;
            uint32_t addr = cpu.get_gpr(base);
            addr += offset;
            cpu.set_gpr(dest, cpu.read32(addr));
        }

        void lbu(IOP& cpu, uint32_t instruction)
        {
            int16_t offset = (int16_t)(instruction & 0xFFFF);
            uint32_t dest = (instruction >> 16) & 0x1F;
            uint32_t base = (instruction >> 21) & 0x1F;
            uint32_t addr = cpu.get_gpr(base);
            addr += offset;
            cpu.set_gpr(dest, cpu.read8(addr));
        }

        void lhu(IOP& cpu, uint32_t instruction)
        {
            int16_t offset = (int16_t)(instruction & 0xFFFF);
            uint32_t dest = (instruction >> 16) & 0x1F;
            uint32_t base = (instruction >> 21) & 0x1F;
            uint32_t addr = cpu.get_gpr(base);
            addr += offset;
            cpu.set_gpr(dest, cpu.read16(addr));
        }

        void sb(IOP& cpu, uint32_t instruction)
        {
            int16_t offset = (int16_t)(instruction & 0xFFFF);
            uint32_t source = (instruction >> 16) & 0x1F;
            uint32_t base = (instruction >> 21) & 0x1F;
            uint32_t addr = cpu.get_gpr(base);
            addr += offset;
            cpu.write8(addr, cpu.get_gpr(source) & 0xFF);
        }

        void sh(IOP& cpu, uint32_t instruction)
        {
            int16_t offset = (int16_t)(instruction & 0xFFFF);
            uint32_t source = (instruction >> 16) & 0x1F;
            uint32_t base = (instruction >> 21) & 0x1F;
            uint32_t addr = cpu.get_gpr(base);
            addr += offset;
            cpu.write16(addr, cpu.get_gpr(source) & 0xFFFF);
        }

        void swl(IOP& cpu, uint32_t instruction)
        {
            static const uint32_t SWL_MASK[4] = { 0xffffff00, 0xffff0000, 0xff000000, 0x00000000 };
            static const uint8_t SWL_SHIFT[4] = { 24, 16, 8, 0 };
            int16_t offset = (int16_t)(instruction & 0xFFFF);
            uint32_t source = (instruction >> 16) & 0x1F;
            uint32_t base = (instruction >> 21) & 0x1F;
            uint32_t addr = cpu.get_gpr(base) + offset;
            int shift = addr & 3;
            uint32_t mem = cpu.read32(addr & ~3);

            cpu.write32(addr & ~0x3, (cpu.get_gpr(source) >> SWL_SHIFT[shift]) | (mem & SWL_MASK[shift]));
        }

        void sw(IOP& cpu, uint32_t instruction)
        {
            int16_t offset = (int16_t)(instruction & 0xFFFF);
            uint32_t source = (instruction >> 16) & 0x1F;
            uint32_t base = (instruction >> 21) & 0x1F;
            uint32_t addr = cpu.get_gpr(base);
            addr += offset;
            cpu.write32(addr, cpu.get_gpr(source));
        }

        void swr(IOP& cpu, uint32_t instruction)
        {
            static const uint32_t SWR_MASK[4] = { 0x00000000, 0x000000ff, 0x0000ffff, 0x00ffffff };
            static const uint8_t SWR_SHIFT[4] = { 0, 8, 16, 24 };
            int16_t offset = (int16_t)(instruction & 0xFFFF);
            uint32_t source = (instruction >> 16) & 0x1F;
            uint32_t base = (instruction >> 21) & 0x1F;
            uint32_t addr = cpu.get_gpr(base) + offset;
            int shift = addr & 3;
            uint32_t mem = cpu.read32(addr & ~3);

            cpu.write32(addr & ~0x3, (cpu.get_gpr(source) << SWR_SHIFT[shift]) | (mem & SWR_MASK[shift]));
        }

        void special(IOP& cpu, uint32_t instruction)
        {
            int op = instruction & 0x3F;
            switch (op)
            {
            case 0x00:
                sll(cpu, instruction);
                break;
            case 0x02:
                srl(cpu, instruction);
                break;
            case 0x03:
                sra(cpu, instruction);
                break;
            case 0x04:
                sllv(cpu, instruction);
                break;
            case 0x06:
                srlv(cpu, instruction);
                break;
            case 0x07:
                srav(cpu, instruction);
                break;
            case 0x08:
                jr(cpu, instruction);
                break;
            case 0x09:
                jalr(cpu, instruction);
                break;
            case 0x0C:
                syscall(cpu, instruction);
                break;
            case 0x10:
                mfhi(cpu, instruction);
                break;
            case 0x11:
                mthi(cpu, instruction);
                break;
            case 0x12:
                mflo(cpu, instruction);
                break;
            case 0x13:
                mtlo(cpu, instruction);
                break;
            case 0x18:
                mult(cpu, instruction);
                break;
            case 0x19:
                multu(cpu, instruction);
                break;
            case 0x1A:
                div(cpu, instruction);
                break;
            case 0x1B:
                divu(cpu, instruction);
                break;
            case 0x20:
                add(cpu, instruction);
                break;
            case 0x21:
                addu(cpu, instruction);
                break;
            case 0x22:
                sub(cpu, instruction);
                break;
            case 0x23:
                subu(cpu, instruction);
                break;
            case 0x24:
                and_cpu(cpu, instruction);
                break;
            case 0x25:
                or_cpu(cpu, instruction);
                break;
            case 0x26:
                xor_cpu(cpu, instruction);
                break;
            case 0x27:
                nor(cpu, instruction);
                break;
            case 0x2A:
                slt(cpu, instruction);
                break;
            case 0x2B:
                sltu(cpu, instruction);
                break;
            default:
                unknown_op("special", op, instruction);
            }
        }

        void sll(IOP& cpu, uint32_t instruction)
        {
            uint32_t source = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            uint32_t shift = (instruction >> 6) & 0x1F;
            source = cpu.get_gpr(source);
            source <<= shift;
            cpu.set_gpr(dest, source);
        }

        void srl(IOP& cpu, uint32_t instruction)
        {
            uint32_t source = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            uint32_t shift = (instruction >> 6) & 0x1F;
            source = cpu.get_gpr(source);
            source >>= shift;
            cpu.set_gpr(dest, source);
        }

        void sra(IOP& cpu, uint32_t instruction)
        {
            int32_t source = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            uint32_t shift = (instruction >> 6) & 0x1F;
            source = (int32_t)cpu.get_gpr(source);
            source >>= shift;
            cpu.set_gpr(dest, (uint32_t)source);
        }

        void sllv(IOP& cpu, uint32_t instruction)
        {
            uint32_t source = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            uint32_t shift = (instruction >> 21) & 0x1F;
            source = cpu.get_gpr(source);
            source <<= cpu.get_gpr(shift) & 0x1F;
            cpu.set_gpr(dest, source);
        }

        void srlv(IOP& cpu, uint32_t instruction)
        {
            uint32_t source = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            uint32_t shift = (instruction >> 21) & 0x1F;
            source = cpu.get_gpr(source);
            source >>= cpu.get_gpr(shift) & 0x1F;
            cpu.set_gpr(dest, source);
        }

        void srav(IOP& cpu, uint32_t instruction)
        {
            int32_t source = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            uint32_t shift = (instruction >> 21) & 0x1F;
            source = (int32_t)cpu.get_gpr(source);
            source >>= cpu.get_gpr(shift) & 0x1F;
            cpu.set_gpr(dest, (uint32_t)source);
        }

        void jr(IOP& cpu, uint32_t instruction)
        {
            uint32_t address = (instruction >> 21) & 0x1F;
            cpu.jp(cpu.get_gpr(address));
        }

        void jalr(IOP& cpu, uint32_t instruction)
        {
            uint32_t new_addr = (instruction >> 21) & 0x1F;
            uint32_t return_reg = (instruction >> 11) & 0x1F;
            uint32_t return_addr = cpu.get_PC() + 8;
            cpu.jp(cpu.get_gpr(new_addr));
            cpu.set_gpr(return_reg, return_addr);
        }

        void syscall(IOP& cpu, uint32_t instruction)
        {
            cpu.syscall_exception();
        }

        void mfhi(IOP& cpu, uint32_t instruction)
        {
            uint32_t dest = (instruction >> 11) & 0x1F;
            cpu.set_gpr(dest, cpu.get_HI());
        }

        void mthi(IOP& cpu, uint32_t instruction)
        {
            uint32_t source = (instruction >> 21) & 0x1F;
            cpu.set_HI(cpu.get_gpr(source));
        }

        void mflo(IOP& cpu, uint32_t instruction)
        {
            uint32_t dest = (instruction >> 11) & 0x1F;
            cpu.set_gpr(dest, cpu.get_LO());
        }

        void mtlo(IOP& cpu, uint32_t instruction)
        {
            uint32_t source = (instruction >> 21) & 0x1F;
            cpu.set_LO(cpu.get_gpr(source));
        }

        void mult(IOP& cpu, uint32_t instruction)
        {
            int64_t op1 = (instruction >> 21) & 0x1F;
            int64_t op2 = (instruction >> 16) & 0x1F;
            op1 = (int32_t)cpu.get_gpr(op1);
            op2 = (int32_t)cpu.get_gpr(op2);
            int64_t temp = op1 * op2;
            cpu.set_LO(temp & 0xFFFFFFFF);
            cpu.set_HI(temp >> 32);
            if (op1 >= 0)
            {
                if (op1 < 0x800)
                    cpu.set_muldiv_delay(6);
                else if (op1 < 0x100000)
                    cpu.set_muldiv_delay(9);
                else
                    cpu.set_muldiv_delay(13);
            }
            else
            {
                if (op1 >= 0xFFFFF800)
                    cpu.set_muldiv_delay(6);
                else if (op1 > 0xFF000000)
                    cpu.set_muldiv_delay(9);
                else
                    cpu.set_muldiv_delay(13);
            }
        }

        void multu(IOP& cpu, uint32_t instruction)
        {
            uint64_t op1 = (instruction >> 21) & 0x1F;
            uint64_t op2 = (instruction >> 16) & 0x1F;
            op1 = cpu.get_gpr(op1);
            op2 = cpu.get_gpr(op2);

            if (op1 < 0x800)
                cpu.set_muldiv_delay(6);
            else if (op1 < 0x100000)
                cpu.set_muldiv_delay(9);
            else
                cpu.set_muldiv_delay(13);
            uint64_t temp = op1 * op2;
            cpu.set_LO(temp & 0xFFFFFFFF);
            cpu.set_HI(temp >> 32);
        }

        void div(IOP& cpu, uint32_t instruction)
        {
            int32_t op1 = (instruction >> 21) & 0x1F;
            int32_t op2 = (instruction >> 16) & 0x1F;
            op1 = (int32_t)cpu.get_gpr(op1);
            op2 = (int32_t)cpu.get_gpr(op2);
            if (!op2)
            {
                cpu.set_HI(op1);
                if (op1 > 0x80000000)
                    cpu.set_LO(1);
                else
                    cpu.set_LO(0xFFFFFFFF);
            }
            else if (op1 == 0x80000000 && op2 == 0xFFFFFFFF)
            {
                cpu.set_LO(0x80000000);
                cpu.set_HI(0);
            }
            else
            {
                cpu.set_LO(op1 / op2);
                cpu.set_HI(op1 % op2);
            }
            cpu.set_muldiv_delay(36);
        }

        void divu(IOP& cpu, uint32_t instruction)
        {
            uint32_t op1 = (instruction >> 21) & 0x1F;
            uint32_t op2 = (instruction >> 16) & 0x1F;
            op1 = cpu.get_gpr(op1);
            op2 = cpu.get_gpr(op2);
            if (!op2)
            {
                cpu.set_LO(0xFFFFFFFF);
                cpu.set_HI(op1);
            }
            else
            {
                cpu.set_LO(op1 / op2);
                cpu.set_HI(op1 % op2);
            }
            cpu.set_muldiv_delay(36);
        }

        void add(IOP& cpu, uint32_t instruction)
        {
            uint32_t op1 = (instruction >> 21) & 0x1F;
            uint32_t op2 = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            op1 = cpu.get_gpr(op1);
            op2 = cpu.get_gpr(op2);
            cpu.set_gpr(dest, op1 + op2);
        }

        void addu(IOP& cpu, uint32_t instruction)
        {
            uint32_t op1 = (instruction >> 21) & 0x1F;
            uint32_t op2 = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            op1 = cpu.get_gpr(op1);
            op2 = cpu.get_gpr(op2);
            cpu.set_gpr(dest, op1 + op2);
        }

        void sub(IOP& cpu, uint32_t instruction)
        {
            uint32_t op1 = (instruction >> 21) & 0x1F;
            uint32_t op2 = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            op1 = cpu.get_gpr(op1);
            op2 = cpu.get_gpr(op2);
            cpu.set_gpr(dest, op1 - op2);
        }

        void subu(IOP& cpu, uint32_t instruction)
        {
            uint32_t op1 = (instruction >> 21) & 0x1F;
            uint32_t op2 = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            op1 = cpu.get_gpr(op1);
            op2 = cpu.get_gpr(op2);
            cpu.set_gpr(dest, op1 - op2);
        }

        void and_cpu(IOP& cpu, uint32_t instruction)
        {
            uint32_t op1 = (instruction >> 21) & 0x1F;
            uint32_t op2 = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            op1 = cpu.get_gpr(op1);
            op2 = cpu.get_gpr(op2);
            cpu.set_gpr(dest, op1 & op2);
        }

        void or_cpu(IOP& cpu, uint32_t instruction)
        {
            uint32_t op1 = (instruction >> 21) & 0x1F;
            uint32_t op2 = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            op1 = cpu.get_gpr(op1);
            op2 = cpu.get_gpr(op2);
            cpu.set_gpr(dest, op1 | op2);
        }

        void xor_cpu(IOP& cpu, uint32_t instruction)
        {
            uint32_t op1 = (instruction >> 21) & 0x1F;
            uint32_t op2 = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            op1 = cpu.get_gpr(op1);
            op2 = cpu.get_gpr(op2);
            cpu.set_gpr(dest, op1 ^ op2);
        }

        void nor(IOP& cpu, uint32_t instruction)
        {
            uint32_t op1 = (instruction >> 21) & 0x1F;
            uint32_t op2 = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            op1 = cpu.get_gpr(op1);
            op2 = cpu.get_gpr(op2);
            cpu.set_gpr(dest, ~(op1 | op2));
        }

        void slt(IOP& cpu, uint32_t instruction)
        {
            int32_t op1 = (instruction >> 21) & 0x1F;
            int32_t op2 = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            op1 = (int32_t)cpu.get_gpr(op1);
            op2 = (int32_t)cpu.get_gpr(op2);
            cpu.set_gpr(dest, op1 < op2);
        }

        void sltu(IOP& cpu, uint32_t instruction)
        {
            uint32_t op1 = (instruction >> 21) & 0x1F;
            uint32_t op2 = (instruction >> 16) & 0x1F;
            uint32_t dest = (instruction >> 11) & 0x1F;
            op1 = cpu.get_gpr(op1);
            op2 = cpu.get_gpr(op2);
            cpu.set_gpr(dest, op1 < op2);
        }

        void regimm(IOP& cpu, uint32_t instruction)
        {
            int op = (instruction >> 16) & 0x1F;
            switch (op)
            {
            case 0x00:
                bltz(cpu, instruction);
                break;
            case 0x01:
                bgez(cpu, instruction);
                break;
            case 0x10:
                bltzal(cpu, instruction);
                break;
            case 0x11:
                bgezal(cpu, instruction);
                break;
            default:
                unknown_op("regimm", op, instruction);
            }
        }

        void bltz(IOP& cpu, uint32_t instruction)
        {
            int offset = (int16_t)(instruction & 0xFFFF);
            offset <<= 2;
            int32_t reg = (int32_t)cpu.get_gpr((instruction >> 21) & 0x1F);
            cpu.branch(reg < 0, offset);
        }

        void bgez(IOP& cpu, uint32_t instruction)
        {
            int offset = (int16_t)(instruction & 0xFFFF);
            offset <<= 2;
            int32_t reg = (int32_t)cpu.get_gpr((instruction >> 21) & 0x1F);
            cpu.branch(reg >= 0, offset);
        }

        void bltzal(IOP& cpu, uint32_t instruction)
        {
            int32_t offset = (int16_t)(instruction & 0xFFFF);
            offset <<= 2;
            int32_t reg = (instruction >> 21) & 0x1F;
            reg = (int32_t)cpu.get_gpr(reg);
            cpu.set_gpr(31, cpu.get_PC() + 8);
            cpu.branch(reg < 0, offset);
        }

        void bgezal(IOP& cpu, uint32_t instruction)
        {
            int32_t offset = (int16_t)(instruction & 0xFFFF);
            offset <<= 2;
            int32_t reg = (instruction >> 21) & 0x1F;
            reg = (int32_t)cpu.get_gpr(reg);
            cpu.set_gpr(31, cpu.get_PC() + 8);
            cpu.branch(reg >= 0, offset);
        }

        void cop(IOP& cpu, uint32_t instruction)
        {
            int op = (instruction >> 21) & 0x1F;
            uint8_t cop_id = ((instruction >> 26) & 0x3);
            op |= cop_id << 8;
            switch (op)
            {
            case 0x000:
                mfc(cpu, instruction);
                break;
            case 0x004:
                mtc(cpu, instruction);
                break;
            case 0x010:
                cpu.rfe();
                break;
            default:
                unknown_op("cop", op, instruction);
            }
        }

        void mfc(IOP& cpu, uint32_t instruction)
        {
            uint8_t cop_id = (instruction >> 26) & 0x3;
            uint8_t reg = (instruction >> 16) & 0x1F;
            uint8_t cop_reg = (instruction >> 11) & 0x1F;
            cpu.mfc(cop_id, cop_reg, reg);
        }

        void mtc(IOP& cpu, uint32_t instruction)
        {
            uint8_t cop_id = (instruction >> 26) & 0x3;
            uint8_t reg = (instruction >> 16) & 0x1F;
            uint8_t cop_reg = (instruction >> 11) & 0x1F;
            cpu.mtc(cop_id, cop_reg, reg);
        }

        void unknown_op(const char* type, uint16_t op, uint32_t instruction)
        {
            Errors::die("[IOP_Interpreter] Unrecognized %s op $%02X (instr: $%08X)", type, op, instruction);
        }
    }
}