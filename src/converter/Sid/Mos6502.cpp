/*
 * MidiEditor AI - MOS 6502 / 6510 CPU emulator implementation (Phase 42).
 *
 * Official instruction set, written against the documented opcode matrix.
 * Unknown / illegal opcodes fall through to a 2-cycle NOP (SID players
 * almost never use illegals; documented limitation).
 */

#include "Mos6502.h"

namespace sid {

Mos6502::Mos6502() {
    for (int i = 0; i < 65536; ++i) mem[i] = 0;
    reset();
}

void Mos6502::reset() {
    a = x = y = 0;
    sp = 0xFD;
    p = FU | FI;
    pc = 0;
    totalCycles = 0;
}

void Mos6502::loadProgram(const uint8_t *data, int len, uint16_t addr) {
    for (int i = 0; i < len; ++i)
        mem[uint16_t(addr + i)] = data[i];
}

void Mos6502::adc(uint8_t v) {
    if (getFlag(FD)) {
        uint16_t lo = uint16_t((a & 0x0F) + (v & 0x0F) + (getFlag(FC) ? 1 : 0));
        uint16_t hi = uint16_t((a >> 4) + (v >> 4));
        if (lo > 9) { lo += 6; hi++; }
        setFlag(FZ, uint8_t(a + v + (getFlag(FC) ? 1 : 0)) == 0);
        setFlag(FN, (hi & 0x08) != 0);
        setFlag(FV, (~(uint16_t(a) ^ v) & (uint16_t(a) ^ (hi << 4)) & 0x80) != 0);
        if (hi > 9) hi += 6;
        setFlag(FC, hi > 0x0F);
        a = uint8_t((hi << 4) | (lo & 0x0F));
    } else {
        uint16_t sum = uint16_t(a + v + (getFlag(FC) ? 1 : 0));
        setFlag(FC, sum > 0xFF);
        setFlag(FV, (~(uint16_t(a) ^ v) & (uint16_t(a) ^ sum) & 0x80) != 0);
        a = uint8_t(sum);
        setZN(a);
    }
}

void Mos6502::sbc(uint8_t v) {
    uint16_t diff = uint16_t(a - v - (getFlag(FC) ? 0 : 1));
    if (getFlag(FD)) {
        int lo = (a & 0x0F) - (v & 0x0F) - (getFlag(FC) ? 0 : 1);
        int hi = (a >> 4) - (v >> 4);
        if (lo < 0) { lo -= 6; hi--; }
        if (hi < 0) hi -= 6;
        setFlag(FC, diff < 0x100);
        setFlag(FV, ((uint16_t(a) ^ v) & (uint16_t(a) ^ diff) & 0x80) != 0);
        setZN(uint8_t(diff));
        a = uint8_t((hi << 4) | (lo & 0x0F));
    } else {
        setFlag(FC, diff < 0x100);
        setFlag(FV, ((uint16_t(a) ^ v) & (uint16_t(a) ^ diff) & 0x80) != 0);
        a = uint8_t(diff);
        setZN(a);
    }
}

void Mos6502::cmpReg(uint8_t reg, uint8_t v) {
    uint16_t diff = uint16_t(reg - v);
    setFlag(FC, reg >= v);
    setZN(uint8_t(diff));
}

void Mos6502::branch(bool cond, int &cyc) {
    int8_t off = int8_t(fetch());
    cyc = 2;
    if (cond) {
        uint16_t old = pc;
        pc = uint16_t(pc + off);
        cyc += 1;
        if ((old & 0xFF00) != (pc & 0xFF00)) cyc += 1;
    }
}

bool Mos6502::callSubroutine(uint16_t addr, int maxInstructions) {
    const uint16_t kReturn = 0x0001; // RTS lands here; players never execute at $0001
    push16(uint16_t(kReturn - 1));
    pc = addr;
    for (int i = 0; i < maxInstructions; ++i) {
        if (pc == kReturn) return true;
        step();
    }
    return false;
}

int Mos6502::step() {
    uint8_t op = fetch();
    int cyc = 2;
    bool crossed = false;
    uint16_t addr;
    uint8_t v;

    switch (op) {
    // ---- LDA ----
    case 0xA9: a = fetch();               setZN(a); cyc = 2; break;
    case 0xA5: a = rd(aZP());             setZN(a); cyc = 3; break;
    case 0xB5: a = rd(aZPX());            setZN(a); cyc = 4; break;
    case 0xAD: a = rd(aABS());            setZN(a); cyc = 4; break;
    case 0xBD: a = rd(aABSX(crossed));    setZN(a); cyc = 4 + (crossed ? 1 : 0); break;
    case 0xB9: a = rd(aABSY(crossed));    setZN(a); cyc = 4 + (crossed ? 1 : 0); break;
    case 0xA1: a = rd(aINDX());           setZN(a); cyc = 6; break;
    case 0xB1: a = rd(aINDY(crossed));    setZN(a); cyc = 5 + (crossed ? 1 : 0); break;
    // ---- LDX ----
    case 0xA2: x = fetch();               setZN(x); cyc = 2; break;
    case 0xA6: x = rd(aZP());             setZN(x); cyc = 3; break;
    case 0xB6: x = rd(aZPY());            setZN(x); cyc = 4; break;
    case 0xAE: x = rd(aABS());            setZN(x); cyc = 4; break;
    case 0xBE: x = rd(aABSY(crossed));    setZN(x); cyc = 4 + (crossed ? 1 : 0); break;
    // ---- LDY ----
    case 0xA0: y = fetch();               setZN(y); cyc = 2; break;
    case 0xA4: y = rd(aZP());             setZN(y); cyc = 3; break;
    case 0xB4: y = rd(aZPX());            setZN(y); cyc = 4; break;
    case 0xAC: y = rd(aABS());            setZN(y); cyc = 4; break;
    case 0xBC: y = rd(aABSX(crossed));    setZN(y); cyc = 4 + (crossed ? 1 : 0); break;
    // ---- STA ----
    case 0x85: wr(aZP(), a);  cyc = 3; break;
    case 0x95: wr(aZPX(), a); cyc = 4; break;
    case 0x8D: wr(aABS(), a); cyc = 4; break;
    case 0x9D: wr(aABSX(crossed), a); cyc = 5; break;
    case 0x99: wr(aABSY(crossed), a); cyc = 5; break;
    case 0x81: wr(aINDX(), a); cyc = 6; break;
    case 0x91: wr(aINDY(crossed), a); cyc = 6; break;
    // ---- STX / STY ----
    case 0x86: wr(aZP(), x);  cyc = 3; break;
    case 0x96: wr(aZPY(), x); cyc = 4; break;
    case 0x8E: wr(aABS(), x); cyc = 4; break;
    case 0x84: wr(aZP(), y);  cyc = 3; break;
    case 0x94: wr(aZPX(), y); cyc = 4; break;
    case 0x8C: wr(aABS(), y); cyc = 4; break;
    // ---- transfers ----
    case 0xAA: x = a; setZN(x); break;  // TAX
    case 0xA8: y = a; setZN(y); break;  // TAY
    case 0x8A: a = x; setZN(a); break;  // TXA
    case 0x98: a = y; setZN(a); break;  // TYA
    case 0xBA: x = sp; setZN(x); break; // TSX
    case 0x9A: sp = x; break;           // TXS (no flags)
    // ---- stack ----
    case 0x48: push8(a); cyc = 3; break;                       // PHA
    case 0x68: a = pull8(); setZN(a); cyc = 4; break;          // PLA
    case 0x08: push8(uint8_t(p | FB | FU)); cyc = 3; break;    // PHP
    case 0x28: p = uint8_t((pull8() & ~FB) | FU); cyc = 4; break; // PLP
    // ---- AND ----
    case 0x29: a &= fetch();            setZN(a); break;
    case 0x25: a &= rd(aZP());          setZN(a); cyc = 3; break;
    case 0x35: a &= rd(aZPX());         setZN(a); cyc = 4; break;
    case 0x2D: a &= rd(aABS());         setZN(a); cyc = 4; break;
    case 0x3D: a &= rd(aABSX(crossed)); setZN(a); cyc = 4 + (crossed ? 1 : 0); break;
    case 0x39: a &= rd(aABSY(crossed)); setZN(a); cyc = 4 + (crossed ? 1 : 0); break;
    case 0x21: a &= rd(aINDX());        setZN(a); cyc = 6; break;
    case 0x31: a &= rd(aINDY(crossed)); setZN(a); cyc = 5 + (crossed ? 1 : 0); break;
    // ---- ORA ----
    case 0x09: a |= fetch();            setZN(a); break;
    case 0x05: a |= rd(aZP());          setZN(a); cyc = 3; break;
    case 0x15: a |= rd(aZPX());         setZN(a); cyc = 4; break;
    case 0x0D: a |= rd(aABS());         setZN(a); cyc = 4; break;
    case 0x1D: a |= rd(aABSX(crossed)); setZN(a); cyc = 4 + (crossed ? 1 : 0); break;
    case 0x19: a |= rd(aABSY(crossed)); setZN(a); cyc = 4 + (crossed ? 1 : 0); break;
    case 0x01: a |= rd(aINDX());        setZN(a); cyc = 6; break;
    case 0x11: a |= rd(aINDY(crossed)); setZN(a); cyc = 5 + (crossed ? 1 : 0); break;
    // ---- EOR ----
    case 0x49: a ^= fetch();            setZN(a); break;
    case 0x45: a ^= rd(aZP());          setZN(a); cyc = 3; break;
    case 0x55: a ^= rd(aZPX());         setZN(a); cyc = 4; break;
    case 0x4D: a ^= rd(aABS());         setZN(a); cyc = 4; break;
    case 0x5D: a ^= rd(aABSX(crossed)); setZN(a); cyc = 4 + (crossed ? 1 : 0); break;
    case 0x59: a ^= rd(aABSY(crossed)); setZN(a); cyc = 4 + (crossed ? 1 : 0); break;
    case 0x41: a ^= rd(aINDX());        setZN(a); cyc = 6; break;
    case 0x51: a ^= rd(aINDY(crossed)); setZN(a); cyc = 5 + (crossed ? 1 : 0); break;
    // ---- BIT ----
    case 0x24: v = rd(aZP());  setFlag(FZ, (a & v) == 0); setFlag(FN, (v & 0x80) != 0); setFlag(FV, (v & 0x40) != 0); cyc = 3; break;
    case 0x2C: v = rd(aABS()); setFlag(FZ, (a & v) == 0); setFlag(FN, (v & 0x80) != 0); setFlag(FV, (v & 0x40) != 0); cyc = 4; break;
    // ---- ADC ----
    case 0x69: adc(fetch()); break;
    case 0x65: adc(rd(aZP())); cyc = 3; break;
    case 0x75: adc(rd(aZPX())); cyc = 4; break;
    case 0x6D: adc(rd(aABS())); cyc = 4; break;
    case 0x7D: adc(rd(aABSX(crossed))); cyc = 4 + (crossed ? 1 : 0); break;
    case 0x79: adc(rd(aABSY(crossed))); cyc = 4 + (crossed ? 1 : 0); break;
    case 0x61: adc(rd(aINDX())); cyc = 6; break;
    case 0x71: adc(rd(aINDY(crossed))); cyc = 5 + (crossed ? 1 : 0); break;
    // ---- SBC ----
    case 0xE9: sbc(fetch()); break;
    case 0xE5: sbc(rd(aZP())); cyc = 3; break;
    case 0xF5: sbc(rd(aZPX())); cyc = 4; break;
    case 0xED: sbc(rd(aABS())); cyc = 4; break;
    case 0xFD: sbc(rd(aABSX(crossed))); cyc = 4 + (crossed ? 1 : 0); break;
    case 0xF9: sbc(rd(aABSY(crossed))); cyc = 4 + (crossed ? 1 : 0); break;
    case 0xE1: sbc(rd(aINDX())); cyc = 6; break;
    case 0xF1: sbc(rd(aINDY(crossed))); cyc = 5 + (crossed ? 1 : 0); break;
    // ---- CMP / CPX / CPY ----
    case 0xC9: cmpReg(a, fetch()); break;
    case 0xC5: cmpReg(a, rd(aZP())); cyc = 3; break;
    case 0xD5: cmpReg(a, rd(aZPX())); cyc = 4; break;
    case 0xCD: cmpReg(a, rd(aABS())); cyc = 4; break;
    case 0xDD: cmpReg(a, rd(aABSX(crossed))); cyc = 4 + (crossed ? 1 : 0); break;
    case 0xD9: cmpReg(a, rd(aABSY(crossed))); cyc = 4 + (crossed ? 1 : 0); break;
    case 0xC1: cmpReg(a, rd(aINDX())); cyc = 6; break;
    case 0xD1: cmpReg(a, rd(aINDY(crossed))); cyc = 5 + (crossed ? 1 : 0); break;
    case 0xE0: cmpReg(x, fetch()); break;
    case 0xE4: cmpReg(x, rd(aZP())); cyc = 3; break;
    case 0xEC: cmpReg(x, rd(aABS())); cyc = 4; break;
    case 0xC0: cmpReg(y, fetch()); break;
    case 0xC4: cmpReg(y, rd(aZP())); cyc = 3; break;
    case 0xCC: cmpReg(y, rd(aABS())); cyc = 4; break;
    // ---- INC / DEC (memory) ----
    case 0xE6: addr = aZP();  v = uint8_t(rd(addr) + 1); wr(addr, v); setZN(v); cyc = 5; break;
    case 0xF6: addr = aZPX(); v = uint8_t(rd(addr) + 1); wr(addr, v); setZN(v); cyc = 6; break;
    case 0xEE: addr = aABS(); v = uint8_t(rd(addr) + 1); wr(addr, v); setZN(v); cyc = 6; break;
    case 0xFE: addr = aABSX(crossed); v = uint8_t(rd(addr) + 1); wr(addr, v); setZN(v); cyc = 7; break;
    case 0xC6: addr = aZP();  v = uint8_t(rd(addr) - 1); wr(addr, v); setZN(v); cyc = 5; break;
    case 0xD6: addr = aZPX(); v = uint8_t(rd(addr) - 1); wr(addr, v); setZN(v); cyc = 6; break;
    case 0xCE: addr = aABS(); v = uint8_t(rd(addr) - 1); wr(addr, v); setZN(v); cyc = 6; break;
    case 0xDE: addr = aABSX(crossed); v = uint8_t(rd(addr) - 1); wr(addr, v); setZN(v); cyc = 7; break;
    // ---- INX/DEX/INY/DEY ----
    case 0xE8: x = uint8_t(x + 1); setZN(x); break;
    case 0xCA: x = uint8_t(x - 1); setZN(x); break;
    case 0xC8: y = uint8_t(y + 1); setZN(y); break;
    case 0x88: y = uint8_t(y - 1); setZN(y); break;
    // ---- shifts (accumulator) ----
    case 0x0A: a = aslVal(a); break;
    case 0x4A: a = lsrVal(a); break;
    case 0x2A: a = rolVal(a); break;
    case 0x6A: a = rorVal(a); break;
    // ---- ASL (memory) ----
    case 0x06: addr = aZP();  wr(addr, aslVal(rd(addr))); cyc = 5; break;
    case 0x16: addr = aZPX(); wr(addr, aslVal(rd(addr))); cyc = 6; break;
    case 0x0E: addr = aABS(); wr(addr, aslVal(rd(addr))); cyc = 6; break;
    case 0x1E: addr = aABSX(crossed); wr(addr, aslVal(rd(addr))); cyc = 7; break;
    // ---- LSR (memory) ----
    case 0x46: addr = aZP();  wr(addr, lsrVal(rd(addr))); cyc = 5; break;
    case 0x56: addr = aZPX(); wr(addr, lsrVal(rd(addr))); cyc = 6; break;
    case 0x4E: addr = aABS(); wr(addr, lsrVal(rd(addr))); cyc = 6; break;
    case 0x5E: addr = aABSX(crossed); wr(addr, lsrVal(rd(addr))); cyc = 7; break;
    // ---- ROL (memory) ----
    case 0x26: addr = aZP();  wr(addr, rolVal(rd(addr))); cyc = 5; break;
    case 0x36: addr = aZPX(); wr(addr, rolVal(rd(addr))); cyc = 6; break;
    case 0x2E: addr = aABS(); wr(addr, rolVal(rd(addr))); cyc = 6; break;
    case 0x3E: addr = aABSX(crossed); wr(addr, rolVal(rd(addr))); cyc = 7; break;
    // ---- ROR (memory) ----
    case 0x66: addr = aZP();  wr(addr, rorVal(rd(addr))); cyc = 5; break;
    case 0x76: addr = aZPX(); wr(addr, rorVal(rd(addr))); cyc = 6; break;
    case 0x6E: addr = aABS(); wr(addr, rorVal(rd(addr))); cyc = 6; break;
    case 0x7E: addr = aABSX(crossed); wr(addr, rorVal(rd(addr))); cyc = 7; break;
    // ---- jumps / calls ----
    case 0x4C: pc = aABS(); cyc = 3; break;                     // JMP abs
    case 0x6C: {                                                // JMP (ind) with page-boundary bug
        uint16_t ptr = fetch16();
        uint8_t lo = rd(ptr);
        uint8_t hi = rd(uint16_t((ptr & 0xFF00) | uint8_t(ptr + 1)));
        pc = uint16_t(lo | (hi << 8));
        cyc = 5;
    } break;
    case 0x20: { uint16_t target = fetch16(); push16(uint16_t(pc - 1)); pc = target; cyc = 6; } break; // JSR
    case 0x60: pc = uint16_t(pull16() + 1); cyc = 6; break;     // RTS
    case 0x40: p = uint8_t((pull8() & ~FB) | FU); pc = pull16(); cyc = 6; break; // RTI
    // ---- branches ----
    case 0x10: branch(!getFlag(FN), cyc); break; // BPL
    case 0x30: branch(getFlag(FN),  cyc); break; // BMI
    case 0x50: branch(!getFlag(FV), cyc); break; // BVC
    case 0x70: branch(getFlag(FV),  cyc); break; // BVS
    case 0x90: branch(!getFlag(FC), cyc); break; // BCC
    case 0xB0: branch(getFlag(FC),  cyc); break; // BCS
    case 0xD0: branch(!getFlag(FZ), cyc); break; // BNE
    case 0xF0: branch(getFlag(FZ),  cyc); break; // BEQ
    // ---- flag ops ----
    case 0x18: setFlag(FC, false); break; // CLC
    case 0x38: setFlag(FC, true);  break; // SEC
    case 0x58: setFlag(FI, false); break; // CLI
    case 0x78: setFlag(FI, true);  break; // SEI
    case 0xB8: setFlag(FV, false); break; // CLV
    case 0xD8: setFlag(FD, false); break; // CLD
    case 0xF8: setFlag(FD, true);  break; // SED
    // ---- NOP / BRK ----
    case 0xEA: break;                      // NOP
    case 0x00:                             // BRK
        pc++;
        push16(pc);
        push8(uint8_t(p | FB | FU));
        setFlag(FI, true);
        pc = uint16_t(rd(0xFFFE) | (rd(0xFFFF) << 8));
        cyc = 7;
        break;
    default:
        // Unknown / illegal opcode -> treated as a 2-cycle NOP.
        break;
    }

    totalCycles += cyc;
    return cyc;
}

} // namespace sid
