/*
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using i32 = int32_t;

class Memory {
    static constexpr u32 PAGE_BITS = 12;
    static constexpr u32 PAGE_SIZE = 1u << PAGE_BITS;
    std::unordered_map<u32, std::array<u8, PAGE_SIZE>> pages;

public:
    u8 read8(u32 addr) const {
        auto it = pages.find(addr >> PAGE_BITS);
        return it == pages.end() ? 0 : it->second[addr & (PAGE_SIZE - 1)];
    }

    void write8(u32 addr, u8 v) {
        pages[addr >> PAGE_BITS][addr & (PAGE_SIZE - 1)] = v;  // paginas nuevas = 0
    }

    u16 read16(u32 addr) const { return read8(addr) | (u16)(read8(addr + 1) << 8); }

    u32 read32(u32 addr) const {
        return (u32)read8(addr) | ((u32)read8(addr + 1) << 8) | ((u32)read8(addr + 2) << 16) |
               ((u32)read8(addr + 3) << 24);
    }

    void write16(u32 addr, u16 v) {
        write8(addr, v & 0xFF);
        write8(addr + 1, v >> 8);
    }

    void write32(u32 addr, u32 v) {
        for (int i = 0; i < 4; i++) write8(addr + i, (v >> (8 * i)) & 0xFF);
    }
};


enum class StepResult { Ok, Halt, Error };

struct Cpu {
    u32 pc = 0;
    u32 x[32] = {};
    Memory mem;
    std::string message;  // detalle cuando Step devuelve Halt o Error

    StepResult step();
};

static i32 sext(u32 value, int bits) {
    u32 m = 1u << (bits - 1);
    return (i32)((value ^ m) - m);
}

StepResult Cpu::step() {
    const u32 inst = mem.read32(pc);
    const u32 opcode = inst & 0x7F;
    const u32 rd = (inst >> 7) & 0x1F;
    const u32 funct3 = (inst >> 12) & 0x7;
    const u32 rs1 = (inst >> 15) & 0x1F;
    const u32 rs2 = (inst >> 20) & 0x1F;
    const u32 funct7 = inst >> 25;

    const i32 imm_i = (i32)inst >> 20;
    const i32 imm_s = (((i32)inst >> 25) << 5) | (i32)((inst >> 7) & 0x1F);
    const i32 imm_b = sext(((inst >> 31) << 12) | (((inst >> 7) & 1) << 11) |
                               (((inst >> 25) & 0x3F) << 5) | (((inst >> 8) & 0xF) << 1),
                           13);
    const u32 imm_u = inst & 0xFFFFF000;
    const i32 imm_j = sext(((inst >> 31) << 20) | (((inst >> 12) & 0xFF) << 12) |
                               (((inst >> 20) & 1) << 11) | (((inst >> 21) & 0x3FF) << 1),
                           21);

    auto invalid = [&]() {
        char buf[96];
        std::snprintf(buf, sizeof buf, "instruccion invalida 0x%08X en pc = 0x%08X", inst, pc);
        message = buf;
        return StepResult::Error;
    };
    auto misaligned = [&](u32 target) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "salto a direccion no alineada 0x%08X desde pc = 0x%08X",
                      target, pc);
        message = buf;
        return StepResult::Error;
    };

    u32 next_pc = pc + 4;
    const u32 a = x[rs1];
    const u32 b = x[rs2];
    u32 result = 0;
    bool write_rd = false;

    switch (opcode) {
        case 0x37:  // lui
            result = imm_u;
            write_rd = true;
            break;
        case 0x17:  // auipc
            result = pc + imm_u;
            write_rd = true;
            break;
        case 0x6F:  // jal
            result = pc + 4;
            write_rd = true;
            next_pc = pc + imm_j;
            break;
        case 0x67:  // jalr
            if (funct3 != 0) return invalid();
            result = pc + 4;
            write_rd = true;
            next_pc = (a + imm_i) & ~1u;
            break;
        case 0x63: {  // branches
            bool taken;
            switch (funct3) {
                case 0: taken = a == b; break;                 // beq
                case 1: taken = a != b; break;                 // bne
                case 4: taken = (i32)a < (i32)b; break;        // blt
                case 5: taken = (i32)a >= (i32)b; break;       // bge
                case 6: taken = a < b; break;                  // bltu
                case 7: taken = a >= b; break;                 // bgeu
                default: return invalid();
            }
            if (taken) next_pc = pc + imm_b;
            break;
        }
        case 0x03: {  // loads
            u32 addr = a + imm_i;
            switch (funct3) {
                case 0: result = (u32)(i32)(int8_t)mem.read8(addr); break;    // lb
                case 1: result = (u32)(i32)(int16_t)mem.read16(addr); break;  // lh
                case 2: result = mem.read32(addr); break;                     // lw
                case 4: result = mem.read8(addr); break;                      // lbu
                case 5: result = mem.read16(addr); break;                     // lhu
                default: return invalid();
            }
            write_rd = true;
            break;
        }
        case 0x23: {  // stores
            u32 addr = a + imm_s;
            switch (funct3) {
                case 0: mem.write8(addr, b & 0xFF); break;    // sb
                case 1: mem.write16(addr, b & 0xFFFF); break; // sh
                case 2: mem.write32(addr, b); break;          // sw
                default: return invalid();
            }
            break;
        }
        case 0x13: {  // op-imm
            const u32 shamt = rs2;  // bits 24:20
            switch (funct3) {
                case 0: result = a + imm_i; break;                           // addi
                case 1:                                                      // slli
                    if (funct7 != 0) return invalid();
                    result = a << shamt;
                    break;
                case 2: result = (i32)a < imm_i; break;                      // slti
                case 3: result = a < (u32)imm_i; break;                      // sltiu
                case 4: result = a ^ imm_i; break;                           // xori
                case 5:                                                      // srli / srai
                    if (funct7 == 0x00) result = a >> shamt;
                    else if (funct7 == 0x20) result = (u32)((i32)a >> shamt);
                    else return invalid();
                    break;
                case 6: result = a | imm_i; break;                           // ori
                case 7: result = a & imm_i; break;                           // andi
            }
            write_rd = true;
            break;
        }
        case 0x33: {  // op
            if (funct7 != 0x00 && funct7 != 0x20) return invalid();
            if (funct7 == 0x20 && funct3 != 0 && funct3 != 5) return invalid();
            switch (funct3) {
                case 0: result = funct7 ? a - b : a + b; break;                       // sub / add
                case 1: result = a << (b & 0x1F); break;                              // sll
                case 2: result = (i32)a < (i32)b; break;                              // slt
                case 3: result = a < b; break;                                        // sltu
                case 4: result = a ^ b; break;                                        // xor
                case 5: result = funct7 ? (u32)((i32)a >> (b & 0x1F)) : a >> (b & 0x1F); break;
                case 6: result = a | b; break;                                        // or
                case 7: result = a & b; break;                                        // and
            }
            write_rd = true;
            break;
        }
        case 0x0F:  // fence: nop
            break;
        case 0x73: {  // ecall / ebreak: no implementados, se detiene la ejecucion
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s en pc = 0x%08X",
                          (inst >> 20) == 0 ? "ecall" : "ebreak", pc);
            message = buf;
            return StepResult::Halt;
        }
        default:
            return invalid();
    }

    if (next_pc & 3) return misaligned(next_pc);

    if (write_rd && rd != 0) x[rd] = result;
    pc = next_pc;
    return StepResult::Ok;
}

static const char* ABI_NAMES[32] = {"zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2",
                                    "s0",   "s1", "a0", "a1", "a2",  "a3",  "a4", "a5",
                                    "a6",   "a7", "s2", "s3", "s4",  "s5",  "s6", "s7",
                                    "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

static int parse_reg(const std::string& s) {
    if (s.size() >= 2 && s[0] == 'x') {
        char* end;
        long n = std::strtol(s.c_str() + 1, &end, 10);
        if (*end == '\0' && n >= 0 && n < 32) return (int)n;
    }
    if (s == "fp") return 8;
    for (int i = 0; i < 32; i++)
        if (s == ABI_NAMES[i]) return i;
    return -1;
}

static bool parse_u32(const std::string& s, u32& out) {
    char* end;
    errno = 0;
    unsigned long long v = std::strtoull(s.c_str(), &end, 0);
    if (s.empty() || *end != '\0' || errno != 0 || v > 0xFFFFFFFFull) return false;
    out = (u32)v;
    return true;
}

static void print_step_problem(const Cpu& cpu, StepResult r) {
    if (r == StepResult::Halt) std::cout << "Ejecucion detenida: " << cpu.message << "\n";
    else if (r == StepResult::Error) std::cout << "Error de ejecucion: " << cpu.message << "\n";
}

static void cmd_step(Cpu& cpu, const std::vector<std::string>& args) {
    u32 n = 1;
    if (args.size() > 1 && (!parse_u32(args[1], n) || n == 0)) {
        std::cout << "Uso: step [n]\n";
        return;
    }
    for (u32 i = 0; i < n; i++) {
        StepResult r = cpu.step();
        if (r != StepResult::Ok) {
            print_step_problem(cpu, r);
            return;
        }
        std::cout << "Ejecutando instruccion.\n";
    }
}

static void cmd_run(Cpu& cpu, const std::vector<std::string>& args) {
    std::string bits;
    for (size_t i = 1; i < args.size(); i++) bits += args[i];
    bool ok = bits.size() == 32;
    for (char c : bits) ok = ok && (c == '0' || c == '1');
    if (!ok) {
        std::cout << "Uso: run <instruccion de 32 bits en binario>\n";
        return;
    }
    u32 inst = (u32)std::stoull(bits, nullptr, 2);
    cpu.mem.write32(cpu.pc, inst);
    StepResult r = cpu.step();
    if (r != StepResult::Ok) print_step_problem(cpu, r);
    else std::cout << "Ejecutando instruccion.\n";
}

static void cmd_regs(const Cpu& cpu, const std::vector<std::string>& args) {
    if (args.size() == 1) {
        for (int i = 0; i < 32; i++) std::printf("x%-2d = 0x%08X\n", i, cpu.x[i]);
        return;
    }
    for (size_t i = 1; i < args.size(); i++) {
        int r = parse_reg(args[i]);
        if (r < 0) std::cout << "Registro desconocido: " << args[i] << "\n";
        else std::printf("x%d = 0x%08X\n", r, cpu.x[r]);
    }
}

static void cmd_mem(const Cpu& cpu, const std::vector<std::string>& args) {
    u32 start, end;
    if (args.size() != 3 || !parse_u32(args[1], start) || !parse_u32(args[2], end) || end < start) {
        std::cout << "Uso: mem <inicio> <fin>   (ambos inclusivos)\n";
        return;
    }
    std::printf("Memoria (0x%X-0x%X):", start, end);
    u32 addr = start;
    while (true) {
        std::printf(" %02X", cpu.mem.read8(addr));
        if (addr == end) break;
        addr++;
    }
    std::printf("\n");
}

int main() {
    Cpu cpu;
    std::cout << "Simulador iniciado con programa vacio.\n";

    std::string line;
    while (std::cout << "> " << std::flush, std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::vector<std::string> args;
        for (std::string tok; iss >> tok;) args.push_back(tok);
        if (args.empty()) continue;

        const std::string& cmd = args[0];
        if (cmd == "pc") std::printf("pc = 0x%08X\n", cpu.pc);
        else if (cmd == "step") cmd_step(cpu, args);
        else if (cmd == "run") cmd_run(cpu, args);
        else if (cmd == "regs") cmd_regs(cpu, args);
        else if (cmd == "mem") cmd_mem(cpu, args);
        else if (cmd == "exit" || cmd == "quit") break;
        else std::cout << "Comandos: pc, step [n], run <binario>, regs [x1 x2 ...], mem <inicio> <fin>, exit\n";
    }

    std::cout << "See you next time...\nProgram exited with code 0.\n";
    return 0;
}
*/