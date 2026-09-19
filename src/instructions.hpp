#pragma once
#include <cstdint>
#include <stdexcept>

namespace aeroInstructions {
using Word = std::uint64_t;
inline Word mask(unsigned bits) { return bits == 64 ? ~Word(0) : (Word(1) << bits)-1; }
inline Word rotate(Word value, unsigned amount, unsigned bits) {
    amount %= bits; value &= mask(bits);
    return amount ? ((value >> amount) | (value << (bits-amount))) & mask(bits) : value;
}
inline Word shift(Word value, unsigned kind, unsigned amount, unsigned bits) {
    value &= mask(bits);
    if (amount >= bits) throw std::runtime_error("invalid shift encoding");
    if (!amount) return value;
    switch (kind) {
        case 0: return (value << amount) & mask(bits);
        case 1: return value >> amount;
        case 2: return (value >> amount) | ((value >> (bits-1)) ? (mask(bits) ^ mask(bits-amount)) : 0);
        default: return rotate(value, amount, bits);
    }
}
struct Masks { Word writeMask, topMask; };
inline Masks decodeMasks(unsigned n, unsigned imms, unsigned immr, unsigned bits, bool immediate) {
    unsigned encoded = (n << 6) | ((~imms) & 63), length = 0;
    while (encoded >>= 1) ++length;
    if (length < 1 || (1u << length) > bits) throw std::runtime_error("invalid bitmask encoding");
    unsigned size = 1u << length, levels = size-1, s = imms & levels, r = immr & levels;
    if (immediate && s == levels) throw std::runtime_error("invalid logical immediate");
    Word element = rotate(mask(s+1), r, size), top = mask(((s-r) & levels)+1);
    Masks result{};
    for (unsigned at = 0; at < bits; at += size) {
        result.writeMask |= element << at;
        result.topMask |= top << at;
    }
    return result;
}
inline Word multiplyHigh(Word a, Word b) {
    Word low = (a & 0xffffffff) * (b & 0xffffffff);
    Word middle = (a >> 32) * (b & 0xffffffff) + (low >> 32);
    Word carry = middle >> 32;
    middle = (middle & 0xffffffff) + (a & 0xffffffff) * (b >> 32);
    return (a >> 32)*(b >> 32) + carry + (middle >> 32);
}
inline bool supports(std::uint32_t op) {
    return (op & 0x1f800000) == 0x12800000 || // Wide moves.
           (op & 0x1f800000) == 0x11000000 || // Immediate arithmetic.
           (op & 0x1f200000) == 0x0b000000 || // Shifted arithmetic.
           (op & 0x1f000000) == 0x0a000000 || // Logical register.
           (op & 0x1f800000) == 0x12000000 || // Logical immediate.
           (op & 0x1f800000) == 0x13000000 || // Bitfields.
           (op & 0x3f000000) == 0x39000000 || // Unsigned-offset memory.
           (op & 0x3f200000) == 0x38000000 || // Unscaled/pre/post memory.
           (op & 0x3f200c00) == 0x38200800 || // Register-offset memory.
           (op & 0x3e000000) == 0x28000000 || // Integer load/store pair.
           (op & 0x1fe00800) == 0x1a800000 || // Conditional select.
           (op & 0x7fe00000) == 0x1b000000 || // Multiply add/subtract.
           (op & 0xffe0fc00) == 0x9bc07c00 || // Unsigned multiply high.
           (op & 0xffe0fc00) == 0x9b407c00 || // Signed multiply high.
           (op & 0x7fe0f800) == 0x1ac00800 || // Divide.
           op == 0xd503201f; // NOP.
}
template<class Machine>
#if defined(__clang__) || defined(__GNUC__)
__attribute__((always_inline))
#endif
inline void execute(Machine& m, std::uint32_t op) {
    unsigned rd = op & 31, rn = (op >> 5) & 31, rm = (op >> 16) & 31;
    bool wide = op >> 31;
    unsigned bits = wide ? 64 : 32;
    auto reg = [&](unsigned n) { return m.reg(n) & mask(bits); };
    auto put = [&](Word value, bool stack = false) { m.put(rd, value, wide, stack); };
    auto flags = [&](Word result) { m.negative = bool((result >> (bits-1)) & 1); m.zero = (result & mask(bits)) == 0; };
    if (op == 0xd503201f) return;
    if ((op & 0x1f800000) == 0x12800000) {
        unsigned kind = (op >> 29) & 3, amount = ((op >> 21) & 3)*16;
        if (kind == 1 || amount >= bits) throw std::runtime_error("invalid wide move encoding");
        Word value = Word((op >> 5) & 65535) << amount;
        if (kind == 0) value = ~value;
        if (kind == 3) value |= reg(rd) & ~(Word(65535) << amount);
        put(value);
    } else if ((op & 0x1f800000) == 0x11000000 || (op & 0x1f200000) == 0x0b000000) {
        bool immediate = (op & 0x1f800000) == 0x11000000;
        bool subtract = op & (1u << 30), setFlags = op & (1u << 29);
        Word a = m.reg(rn, immediate) & mask(bits), b;
        if (immediate) b = Word((op >> 10) & 4095) << ((op & (1u << 22)) ? 12 : 0);
        else {
            unsigned kind = (op >> 22) & 3;
            if (kind == 3) throw std::runtime_error("invalid arithmetic shift");
            b = shift(reg(rm), kind, (op >> 10) & 63, bits);
        }
        Word result = (subtract ? a-b : a+b) & mask(bits);
        if (setFlags) {
            flags(result);
            m.carry = subtract ? a >= b : result < a;
            m.overflow = bool(((subtract ? (a ^ b) : ~(a ^ b)) & (a ^ result)) >> (bits-1) & 1);
        }
        put(result, immediate && !setFlags);
    } else if ((op & 0x1f000000) == 0x0a000000 || (op & 0x1f800000) == 0x12000000) {
        bool immediate = (op & 0x1f800000) == 0x12000000;
        unsigned kind = (op >> 29) & 3;
        Word a = reg(rn), b;
        if (immediate) b = decodeMasks((op >> 22) & 1, (op >> 10) & 63, (op >> 16) & 63, bits, true).writeMask;
        else {
            b = shift(reg(rm), (op >> 22) & 3, (op >> 10) & 63, bits);
            if (op & (1u << 21)) b = ~b;
        }
        Word result = kind == 1 ? a | b : kind == 2 ? a ^ b : a & b;
        if (kind == 3) { flags(result); m.carry = m.overflow = false; }
        put(result, immediate && kind != 3);
    } else if ((op & 0x1f800000) == 0x13000000) {
        unsigned kind = (op >> 29) & 3, r = (op >> 16) & 63, s = (op >> 10) & 63;
        if (kind == 3 || bool((op >> 22) & 1) != wide || r >= bits || s >= bits)
            throw std::runtime_error("invalid bitfield encoding");
        auto masks = decodeMasks(wide, s, r, bits, false);
        Word value = rotate(reg(rn), r, bits) & masks.writeMask;
        if (kind == 1) value |= reg(rd) & ~masks.writeMask;
        Word top = kind == 1 ? reg(rd) : kind == 0 && ((reg(rn) >> s) & 1) ? ~Word(0) : 0;
        put((top & ~masks.topMask) | (value & masks.topMask));
    } else if ((op & 0x3e000000) == 0x28000000) {
        unsigned kind = op >> 30, mode = (op >> 23) & 3, rt2 = (op >> 10) & 31;
        bool load = op & (1u << 22);
        if (kind != 0 && kind != 2) throw std::runtime_error("unsupported load/store pair encoding");
        unsigned size = kind == 2 ? 8 : 4;
        Word base = m.reg(rn, true), offset = Machine::sext((op >> 15) & 127, 7)*size;
        Word address = mode == 1 ? base : base+offset;
        if (load) {
            Word a = m.read(address, size), b = m.read(address+size, size);
            m.put(rd, a, size == 8); m.put(rt2, b, size == 8);
        } else {
            m.access(address, size*2, 2);
            m.store(address, m.reg(rd), size); m.store(address+size, m.reg(rt2), size);
        }
        if (mode == 1 || mode == 3) m.put(rn, base+offset, true, true);
    } else if ((op & 0x3f000000) == 0x39000000 || (op & 0x3f200000) == 0x38000000 ||
               (op & 0x3f200c00) == 0x38200800) {
        unsigned scale = op >> 30, size = 1u << scale, kind = (op >> 22) & 3;
        bool unsignedOffset = (op & 0x3f000000) == 0x39000000;
        bool registerOffset = (op & 0x3f200c00) == 0x38200800;
        Word base = m.reg(rn, true), offset;
        unsigned mode = (op >> 10) & 3;
        if (unsignedOffset) offset = Word((op >> 10) & 4095)*size;
        else if (registerOffset) {
            unsigned extend = (op >> 13) & 7;
            if (extend != 2 && extend != 3 && extend != 6 && extend != 7)
                throw std::runtime_error("invalid memory register extension");
            offset = m.reg(rm);
            if (extend == 2) offset &= 0xffffffff;
            if (extend == 6) offset = Machine::sext(offset & 0xffffffff, 32);
            if (op & (1u << 12)) offset <<= scale;
        } else {
            if (mode == 2) throw std::runtime_error("unprivileged memory access is not supported");
            offset = Machine::sext((op >> 12) & 511, 9);
        }
        Word address = !unsignedOffset && !registerOffset && mode == 1 ? base : base+offset;
        if (kind == 0) m.store(address, m.reg(rd), size);
        else {
            if ((kind == 2 && size == 8) || (kind == 3 && size >= 4))
                throw std::runtime_error("unsupported signed load encoding");
            Word value = m.read(address, size);
            if (kind >= 2) value = Machine::sext(value, size*8);
            m.put(rd, value, kind == 2 || size == 8);
        }
        if (!unsignedOffset && !registerOffset && (mode == 1 || mode == 3)) m.put(rn, base+offset, true, true);
    } else if ((op & 0x1fe00800) == 0x1a800000) {
        Word value = reg(rn);
        if (!m.condition((op >> 12) & 15)) {
            value = reg(rm);
            if (op & (1u << 30)) value = ~value;
            if (op & (1u << 10)) ++value;
        }
        put(value);
    } else if ((op & 0x7fe00000) == 0x1b000000) {
        Word product = reg(rn)*reg(rm), a = reg((op >> 10) & 31);
        put((op & (1u << 15)) ? a-product : a+product);
    } else if ((op & 0xffe0fc00) == 0x9bc07c00 || (op & 0xffe0fc00) == 0x9b407c00) {
        Word a = reg(rn), b = reg(rm), result = multiplyHigh(a,b);
        if (!(op & (1u << 23))) { if (a >> 63) result -= b; if (b >> 63) result -= a; }
        put(result);
    } else if ((op & 0x7fe0f800) == 0x1ac00800) {
        Word a = reg(rn), b = reg(rm), result = 0;
        if (op & (1u << 10)) {
            bool aNegative = a >> (bits-1), bNegative = b >> (bits-1);
            if (aNegative) a = (0-a) & mask(bits);
            if (bNegative) b = (0-b) & mask(bits);
            if (b) result = a/b;
            if (aNegative != bNegative) result = 0-result;
        } else if (b) result = a/b;
        put(result);
    } else throw std::runtime_error("unsupported data instruction");
}
}
