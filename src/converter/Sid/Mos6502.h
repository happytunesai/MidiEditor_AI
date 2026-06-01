/*
 * MidiEditor AI - MOS 6502 / 6510 CPU emulator (Phase 42, C64 mode).
 *
 * Just enough 6502 to run SID player routines: a flat 64 KB address space
 * (the SID registers at $D400+ are treated as plain RAM and snapshotted by
 * the capture layer - the classic "SIDDump" approach), the full official
 * instruction set, plus a few commonly-used illegal opcodes. Cycle counts
 * are tracked approximately; they don't gate vsync (50 Hz) capture, which
 * calls the play routine once per frame regardless.
 *
 * Plain C++ (no Qt) so it can be unit-tested standalone. sidtool delegates
 * CPU emulation to an external gem, so this is written from scratch against
 * the documented opcode matrix.
 */

#ifndef SID_MOS6502_H
#define SID_MOS6502_H

#include <cstdint>

namespace sid {

class Mos6502 {
public:
    // Status flag bits.
    enum Flag : uint8_t {
        FC = 0x01, FZ = 0x02, FI = 0x04, FD = 0x08,
        FB = 0x10, FU = 0x20, FV = 0x40, FN = 0x80
    };

    Mos6502();

    /// Clear registers to a known power-on-ish state (does not touch RAM).
    void reset();

    /// Copy \a len bytes into RAM starting at \a addr (wraps/clamps safely).
    void loadProgram(const uint8_t *data, int len, uint16_t addr);

    /// Execute one instruction; returns the (approximate) cycles consumed.
    int step();

    /// Run a subroutine like the SID init/play entry points: simulate a JSR
    /// to \a addr and execute until the matching RTS returns. Returns true on
    /// a clean return, false if \a maxInstructions tripped (runaway guard).
    bool callSubroutine(uint16_t addr, int maxInstructions = 2000000);

    // --- State (public for the capture layer + tests) ----------------
    uint8_t  a = 0, x = 0, y = 0;
    uint8_t  sp = 0xFD;
    uint8_t  p = FU | FI;
    uint16_t pc = 0;
    uint8_t  mem[65536];
    unsigned long long totalCycles = 0;

private:
    // PAL VIC timing: 63 cycles per raster line, 312 lines per frame.
    static const unsigned kCyclesPerLine = 63;
    static const unsigned kRasterLines   = 312;

    // Memory + stack helpers. The VIC raster register ($D012 + $D011 bit 7) is
    // modelled as a free-running, cycle-based counter so player wait-loops
    // ("LDA $D012 / CMP #n / BNE") and raster splits advance instead of spinning
    // forever against flat RAM. Every other address is plain RAM.
    inline uint8_t rd(uint16_t addr) const {
        if (addr == 0xD012)
            return uint8_t((totalCycles / kCyclesPerLine) % kRasterLines);
        if (addr == 0xD011) {
            unsigned raster = unsigned((totalCycles / kCyclesPerLine) % kRasterLines);
            return uint8_t((mem[0xD011] & 0x7F) | (((raster >> 8) & 1u) << 7));
        }
        return mem[addr];
    }
    inline void wr(uint16_t addr, uint8_t v) { mem[addr] = v; }
    inline uint8_t  fetch() { return mem[pc++]; }
    inline uint16_t fetch16() { uint16_t lo = fetch(); uint16_t hi = fetch(); return uint16_t(lo | (hi << 8)); }
    inline void     push8(uint8_t v) { mem[0x100 + sp] = v; sp = uint8_t(sp - 1); }
    inline uint8_t  pull8() { sp = uint8_t(sp + 1); return mem[0x100 + sp]; }
    inline void     push16(uint16_t v) { push8(uint8_t(v >> 8)); push8(uint8_t(v & 0xFF)); }
    inline uint16_t pull16() { uint8_t lo = pull8(); uint8_t hi = pull8(); return uint16_t(lo | (hi << 8)); }

    // Flag helpers.
    inline void setFlag(uint8_t mask, bool on) { if (on) p |= mask; else p = uint8_t(p & ~mask); }
    inline bool getFlag(uint8_t mask) const { return (p & mask) != 0; }
    inline void setZN(uint8_t v) { setFlag(FZ, v == 0); setFlag(FN, (v & 0x80) != 0); }

    // Addressing-mode operand-address resolvers (advance pc). \a crossed is
    // set when the index push crossed a page boundary (for cycle accounting).
    inline uint16_t aZP()  { return fetch(); }
    inline uint16_t aZPX() { return uint8_t(fetch() + x); }
    inline uint16_t aZPY() { return uint8_t(fetch() + y); }
    inline uint16_t aABS() { return fetch16(); }
    inline uint16_t aABSX(bool &crossed) { uint16_t b = fetch16(); uint16_t a_ = uint16_t(b + x); crossed = ((b ^ a_) & 0xFF00) != 0; return a_; }
    inline uint16_t aABSY(bool &crossed) { uint16_t b = fetch16(); uint16_t a_ = uint16_t(b + y); crossed = ((b ^ a_) & 0xFF00) != 0; return a_; }
    inline uint16_t aINDX() { uint8_t zp = uint8_t(fetch() + x); return uint16_t(mem[zp] | (mem[uint8_t(zp + 1)] << 8)); }
    inline uint16_t aINDY(bool &crossed) { uint8_t zp = fetch(); uint16_t b = uint16_t(mem[zp] | (mem[uint8_t(zp + 1)] << 8)); uint16_t a_ = uint16_t(b + y); crossed = ((b ^ a_) & 0xFF00) != 0; return a_; }

    // ALU helpers.
    void adc(uint8_t v);
    void sbc(uint8_t v);
    void cmpReg(uint8_t reg, uint8_t v);
    void branch(bool cond, int &cyc);
    inline uint8_t aslVal(uint8_t v) { setFlag(FC, (v & 0x80) != 0); v = uint8_t(v << 1); setZN(v); return v; }
    inline uint8_t lsrVal(uint8_t v) { setFlag(FC, (v & 0x01) != 0); v = uint8_t(v >> 1); setZN(v); return v; }
    inline uint8_t rolVal(uint8_t v) { uint8_t c = getFlag(FC) ? 1 : 0; setFlag(FC, (v & 0x80) != 0); v = uint8_t((v << 1) | c); setZN(v); return v; }
    inline uint8_t rorVal(uint8_t v) { uint8_t c = getFlag(FC) ? 0x80 : 0; setFlag(FC, (v & 0x01) != 0); v = uint8_t((v >> 1) | c); setZN(v); return v; }
};

} // namespace sid

#endif // SID_MOS6502_H
