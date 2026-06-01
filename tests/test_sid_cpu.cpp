/*
 * MidiEditor AI - Unit test for the MOS 6502 emulator (Phase 42).
 *
 * Runs small hand-assembled programs and checks the resulting memory /
 * flags, exercising the paths SID players lean on: immediate/indexed
 * loads + stores, a branch-driven loop, binary arithmetic with carry/zero
 * flags, and JSR/RTS nesting via callSubroutine().
 *
 * Plain-C++ core, so this compiles Mos6502.cpp directly and links only
 * Qt6::Core/Test.
 */

#include <QtTest/QtTest>

#include <cstdint>
#include <vector>

#include "../src/converter/Sid/Mos6502.h"

using namespace sid;

class Mos6502Test : public QObject {
    Q_OBJECT
private slots:

    // LDX/TXA/STA $D400,X loop counting X from 4 down to 0.
    void indexedStoreLoop() {
        std::vector<uint8_t> prog = {
            0xA2, 0x04,        // LDX #$04
            0x8A,              // TXA
            0x9D, 0x00, 0xD4,  // STA $D400,X
            0xCA,              // DEX
            0x10, 0xF9,        // BPL $1002
            0x60               // RTS
        };
        Mos6502 cpu;
        cpu.loadProgram(prog.data(), int(prog.size()), 0x1000);
        QVERIFY(cpu.callSubroutine(0x1000));
        QCOMPARE(int(cpu.mem[0xD400]), 0);
        QCOMPARE(int(cpu.mem[0xD401]), 1);
        QCOMPARE(int(cpu.mem[0xD402]), 2);
        QCOMPARE(int(cpu.mem[0xD403]), 3);
        QCOMPARE(int(cpu.mem[0xD404]), 4);
    }

    // ADC wrap to zero sets carry + zero; result stored to a SID register.
    void adcCarryAndZeroFlags() {
        std::vector<uint8_t> prog = {
            0xA9, 0xFE,        // LDA #$FE
            0x18,              // CLC
            0x69, 0x01,        // ADC #$01  -> $FF
            0x69, 0x01,        // ADC #$01  -> $00, C=1, Z=1
            0x8D, 0x10, 0xD4,  // STA $D410
            0x60               // RTS
        };
        Mos6502 cpu;
        cpu.loadProgram(prog.data(), int(prog.size()), 0x1100);
        QVERIFY(cpu.callSubroutine(0x1100));
        QCOMPARE(int(cpu.mem[0xD410]), 0);
        QVERIFY((cpu.p & Mos6502::FC) != 0); // carry out
        QVERIFY((cpu.p & Mos6502::FZ) != 0); // zero result
    }

    // JSR into a subroutine that sets A, then arithmetic on return.
    void jsrRtsNesting() {
        std::vector<uint8_t> prog = {
            0x20, 0x0B, 0x12,  // JSR $120B
            0x18,              // CLC
            0x69, 0x10,        // ADC #$10
            0x8D, 0x20, 0xD4,  // STA $D420
            0x60,              // RTS  (main returns)
            0x00,              // $120A filler
            0xA9, 0x05,        // $120B LDA #$05
            0x60               // RTS  (subroutine returns)
        };
        Mos6502 cpu;
        cpu.loadProgram(prog.data(), int(prog.size()), 0x1200);
        QVERIFY(cpu.callSubroutine(0x1200));
        QCOMPARE(int(cpu.mem[0xD420]), 0x15); // 5 + 0x10
    }

    // A runaway routine (infinite JMP-to-self) must trip the guard, not hang.
    void runawayGuardTrips() {
        std::vector<uint8_t> prog = {
            0x4C, 0x00, 0x13   // JMP $1300 (itself)
        };
        Mos6502 cpu;
        cpu.loadProgram(prog.data(), int(prog.size()), 0x1300);
        QVERIFY(!cpu.callSubroutine(0x1300, 10000)); // guard returns false
    }
};

QTEST_MAIN(Mos6502Test)
#include "test_sid_cpu.moc"
