#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include "instructions.hpp"

namespace {
using U = std::uint64_t;
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
struct Region { U base; std::vector<unsigned char> data; unsigned flags; };
class Machine {
public:
    std::vector<Region> regions;
    std::array<U, 31> x{};
    U sp = 0, pc = 0;
    bool negative = false, zero = false, carry = false, overflow = false;
    unsigned char* access(U address, U size, unsigned permission) {
        for (auto& r : regions)
            if (address >= r.base && address - r.base <= r.data.size() &&
                size <= r.data.size() - (address - r.base) && (r.flags & permission) == permission)
                return r.data.data() + (address - r.base);
        fail("invalid guest memory access at " + std::to_string(address));
    }
    U read(U address, unsigned size, unsigned permission = 4) {
        auto* p = access(address, size, permission);
        U value = 0;
        for (unsigned i = 0; i < size; ++i) value |= U(p[i]) << (8 * i);
        return value;
    }
    void store(U address, U value, unsigned size = 8) {
        auto* p = access(address, size, 2);
        for (unsigned i = 0; i < size; ++i) p[i] = static_cast<unsigned char>(value >> (8 * i));
    }
    U reg(unsigned n, bool stack = false) const { return n == 31 ? (stack ? sp : 0) : x[n]; }
    void put(unsigned n, U value, bool wide, bool stack = false) {
        if (!wide) value &= 0xffffffffu;
        if (n != 31) x[n] = value;
        else if (stack) sp = value;
    }
    static U sext(U value, unsigned bits) { U sign = U(1) << (bits - 1); return (value ^ sign) - sign; }
    bool condition(unsigned code) const {
        bool result;
        switch (code >> 1) {
            case 0: result = zero; break;
            case 1: result = carry; break;
            case 2: result = negative; break;
            case 3: result = overflow; break;
            case 4: result = carry && !zero; break;
            case 5: result = negative == overflow; break;
            case 6: result = !zero && negative == overflow; break;
            default: return true;
        }
        return (code & 1) ? !result : result;
    }
public:
    void load(const std::string& path, const std::vector<std::string>& args) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) fail("cannot open program: " + path);
        auto length = input.tellg();
        if (length < 64 || length > 64 * 1024 * 1024) fail("invalid or oversized ELF file");
        std::vector<unsigned char> file(static_cast<std::size_t>(length));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(file.data()), length)) fail("cannot read program");
        loadBytes(file, args);
    }
    void loadBytes(const std::vector<unsigned char>& file, const std::vector<std::string>& args) {
        if (file.size() < 64) fail("truncated ELF header");
        auto field = [&](U offset, unsigned size) -> U {
            if (offset > file.size() || size > file.size() - offset) fail("truncated ELF header");
            U value = 0;
            for (unsigned i = 0; i < size; ++i) value |= U(file[offset+i]) << (8*i);
            return value;
        };
        if (std::memcmp(file.data(), "\177ELF", 4) || file[4] != 2 || file[5] != 1 || file[6] != 1)
            fail("expected a little-endian ELF64 program");
        U elfType = field(16, 2);
        if ((elfType != 2 && elfType != 3) || field(18, 2) != 183 || field(20, 4) != 1)
            fail("expected an AArch64 ELF executable");
        U loadBias = elfType == 3 ? 0x40000000 : 0;
        if (field(24, 8) > std::numeric_limits<U>::max()-loadBias) fail("invalid ELF entry address");
        pc = field(24, 8)+loadBias;
        U dynamicAddress = 0, dynamicSize = 0, headerAddress = 0;
        U phoff = field(32, 8), count = field(56, 2), stride = field(54, 2), allocated = 0;
        if (stride != 56 || count > 1024 || phoff > file.size() || count * stride > file.size() - phoff)
            fail("invalid ELF program header table");
        for (U i = 0; i < count; ++i) {
            U h = phoff + i * stride, type = field(h, 4);
            if (type == 3) fail("required Linux dynamic runtime is not supported yet");
            if (type == 2) {
                if (field(h+16, 8) > std::numeric_limits<U>::max()-loadBias) fail("invalid dynamic table address");
                dynamicAddress = field(h+16, 8)+loadBias;
                dynamicSize = field(h+32, 8);
            }
            if (type != 1) continue;
            U flags = field(h+4, 4), offset = field(h+8, 8), base = field(h+16, 8);
            if (base > std::numeric_limits<U>::max()-loadBias) fail("invalid ELF segment address");
            base += loadBias;
            U filesz = field(h+32, 8), memsz = field(h+40, 8);
            if (filesz > memsz || offset > file.size() || filesz > file.size()-offset ||
                memsz > 64*1024*1024 - allocated || base > std::numeric_limits<U>::max()-memsz)
                fail("invalid ELF load segment");
            if (!memsz) continue;
            for (const auto& r : regions)
                if (base < r.base+r.data.size() && r.base < base+memsz) fail("overlapping ELF segments");
            Region r{base, std::vector<unsigned char>(memsz), static_cast<unsigned>(flags & 7)};
            std::memcpy(r.data.data(), file.data()+offset, filesz);
            if (phoff >= offset && phoff-offset <= filesz && count*stride <= filesz-(phoff-offset))
                headerAddress = base+(phoff-offset);
            regions.push_back(std::move(r));
            allocated += memsz;
        }
        if (dynamicSize) {
            if (dynamicSize > 64*1024*1024 || dynamicSize % 16) fail("invalid ELF dynamic table");
            access(dynamicAddress, dynamicSize, 4);
            U rela = 0, relaSize = 0, relaEntry = 24;
            bool terminated = false;
            for (U offset = 0; offset < dynamicSize; offset += 16) {
                U tag = read(dynamicAddress+offset, 8), value = read(dynamicAddress+offset+8, 8);
                if (!tag) { terminated = true; break; }
                if (tag == 1) fail("required shared library runtime is not supported yet");
                if (tag == 7) rela = value;
                if (tag == 8) relaSize = value;
                if (tag == 9) relaEntry = value;
                if ((tag == 17 || tag == 18 || tag == 23 || tag == 35 || tag == 36) && value)
                    fail("unsupported ELF relocation table format");
            }
            if (!terminated) fail("unterminated ELF dynamic table");
            if (relaSize) {
                if (relaEntry != 24 || relaSize % 24 || rela > std::numeric_limits<U>::max()-loadBias)
                    fail("invalid ELF relocation table");
                rela += loadBias;
                access(rela, relaSize, 4);
                for (U offset = 0; offset < relaSize; offset += 24) {
                    U target = read(rela+offset, 8), info = read(rela+offset+8, 8), addend = read(rela+offset+16, 8);
                    if (!info) continue;
                    if (info != 1027) fail("unsupported ELF relocation: " + std::to_string(info));
                    if (target > std::numeric_limits<U>::max()-loadBias) fail("invalid relocation target");
                    store(target+loadBias, addend+loadBias);
                }
            }
        }
        access(pc, 4, 1);
        constexpr U stackBase = 0x7fff00000000ULL, stackSize = 1024*1024;
        for (const auto& r : regions)
            if (r.base < stackBase+stackSize && stackBase < r.base+r.data.size()) fail("ELF overlaps stack");
        regions.push_back({stackBase, std::vector<unsigned char>(stackSize), 6});
        sp = stackBase+stackSize;
        std::vector<U> pointers;
        for (const auto& arg : args) {
            if (arg.size()+1 > sp-stackBase) fail("arguments exceed guest stack");
            sp -= arg.size()+1;
            std::memcpy(access(sp, arg.size()+1, 2), arg.c_str(), arg.size()+1);
            pointers.push_back(sp);
        }
        std::vector<U> auxiliary{6, 4096, 9, pc, 4, stride, 5, count};
        if (headerAddress) { auxiliary.push_back(3); auxiliary.push_back(headerAddress); }
        auxiliary.push_back(0); auxiliary.push_back(0);
        U tableSize = (pointers.size()+3+auxiliary.size())*8;
        if (tableSize+16 > sp-stackBase) fail("arguments exceed guest stack");
        sp = (sp-tableSize) & ~U(15);
        store(sp, pointers.size());
        U at = sp+8;
        for (U pointer : pointers) { store(at, pointer); at += 8; }
        // argv terminator, empty environment, and the Linux auxiliary vector.
        for (int i = 0; i < 2; ++i) { store(at, 0); at += 8; }
        for (U value : auxiliary) { store(at, value); at += 8; }
    }
    int run() {
        for (U steps = 0; steps < 100000000; ++steps) {
            if (pc & 3) fail("unaligned guest instruction address");
            U here = pc;
            auto op = static_cast<std::uint32_t>(read(pc, 4, 1)); pc += 4;
            unsigned rd = op & 31, rn = (op >> 5) & 31;
            bool wide = op >> 31;
            if (aeroInstructions::supports(op)) {
                aeroInstructions::execute(*this, op);
            } else if ((op & 0xfffffc1f) == 0xd65f0000 ||
                       (op & 0xfffffc1f) == 0xd61f0000 || (op & 0xfffffc1f) == 0xd63f0000) {
                U target = reg(rn);
                if ((op & 0xfffffc1f) == 0xd63f0000) x[30] = pc;
                pc = target;
            } else if ((op & 0xff000010) == 0x54000000) {
                if (condition(op & 15)) pc = here + (sext((op >> 5) & 0x7ffff, 19) << 2);
            } else if ((op & 0x7e000000) == 0x36000000) {
                unsigned bit = ((op >> 19) & 31) + ((op >> 31) * 32);
                if (bool((reg(rd) >> bit) & 1) == bool(op & 0x01000000))
                    pc = here + (sext((op >> 5) & 0x3fff, 14) << 2);
            } else if ((op & 0xffe0001f) == 0xd4000001) {
                if (x[8] == 93 || x[8] == 94) return static_cast<int>(x[0] & 255);
                if (x[8] != 64) fail("unsupported Linux syscall: " + std::to_string(x[8]));
                if (x[0] != 1 && x[0] != 2) { x[0] = U(-9); continue; }
                if (!x[2]) { x[0] = 0; continue; }
                unsigned char* data;
                try { data = access(x[1], x[2], 4); }
                catch (const std::runtime_error&) { x[0] = U(-14); continue; }
                auto& out = x[0] == 1 ? std::cout : std::cerr;
                out.write(reinterpret_cast<char*>(data), static_cast<std::streamsize>(x[2]));
                out.flush(); x[0] = out ? x[2] : U(-5);
            } else if ((op & 0x7f800000) == 0x52800000) {
                unsigned shift = ((op >> 21) & 3)*16;
                if (!wide && shift >= 32) fail("invalid MOVZ encoding");
                put(rd, U((op >> 5) & 65535) << shift, wide);
            } else if ((op & 0x1f000000) == 0x10000000) {
                U imm = sext(((op >> 5) & 0x7ffff)*4 + ((op >> 29) & 3), 21);
                put(rd, (wide ? (here & ~U(4095)) + (imm << 12) : here + imm), true);
            } else if ((op & 0x7c000000) == 0x14000000) {
                if (wide) x[30] = pc;
                pc = here + (sext(op & 0x3ffffff, 26) << 2);
            } else if ((op & 0x7e000000) == 0x34000000) {
                U value = reg(rd); if (!wide) value &= 0xffffffffu;
                if ((value != 0) == bool(op & 0x01000000)) pc = here + (sext((op >> 5) & 0x7ffff, 19) << 2);
            } else if ((op & 0x3f800000) == 0x11000000) {
                U imm = U((op >> 10) & 4095) << ((op & (1u << 22)) ? 12 : 0);
                put(rd, (op & (1u << 30)) ? reg(rn, true)-imm : reg(rn, true)+imm, wide, true);
            } else if ((op & 0x7fe0fc00) == 0x0b000000) {
                put(rd, reg(rn) + reg((op >> 16) & 31), wide);
            } else if ((op & 0x3fc00000) == 0x39400000) {
                unsigned size = 1u << (op >> 30);
                U address = reg(rn, true) + U((op >> 10) & 4095)*size;
                put(rd, read(address, size), size == 8);
            } else {
                fail("unsupported AArch64 instruction " + std::to_string(op) + " at " + std::to_string(here));
            }
        }
        fail("interpreter instruction limit reached");
    }
};
}
#ifndef aeroRuntimeOnly
#include "system.hpp"
void exportNative(const std::string& input, const std::string& output);
int main(int argc, char** argv) {
    try {
        if (argc >= 2 && std::string(argv[1]) == "--system") return systemCommand(argc, argv);
        if (argc == 2 && std::string(argv[1]) == "--version") { std::cout << "aero 0.1.0\n"; return 0; }
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) {
            std::cout << "Usage: aero --program ./program [-o ./native-program] [--] [arguments...]\n"
                         "       aero --system --kernel Image [options] (see --system --help)\n"; return 0;
        }
        if (argc < 3 || std::string(argv[1]) != "--program") fail("use aero --program ./program [arguments...]");
        std::vector<std::string> args{argv[2]};
        std::string output;
        bool options = true;
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (options && arg == "--") { options = false; continue; }
            if (options && arg == "-o") {
                if (++i == argc || !output.empty()) fail("-o requires one output path");
                output = argv[i];
                if (output.empty()) fail("-o requires an output path");
            } else args.push_back(arg);
        }
        if (!output.empty()) {
            if (args.size() != 1) fail("pass program arguments to the resulting native binary");
            exportNative(argv[2], output);
            return 0;
        }
        std::cerr << "aero: warning: using the initial interpreter; execution will be slower than native.\n";
        Machine machine;
        machine.load(argv[2], args);
        return machine.run();
    } catch (const std::exception& e) { std::cerr << "aero: " << e.what() << '\n'; return 1; }
}
#endif
