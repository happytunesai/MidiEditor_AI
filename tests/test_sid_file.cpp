/*
 * MidiEditor AI - Unit test for the SID (PSID/RSID) parser (Phase 42).
 *
 * Builds synthetic PSID images in memory and checks the header fields +
 * program-image resolution (both the load-address-in-header and the
 * load-address-embedded-in-data cases), plus rejection of malformed input.
 *
 * The SID core is plain C++, so this compiles src/converter/Sid/SidFile.cpp
 * directly and links only Qt6::Core/Test.
 */

#include <QtTest/QtTest>

#include <cstdint>
#include <string>
#include <vector>

#include "../src/converter/Sid/SidFile.h"

using namespace sid;

namespace {

void putBE16(std::vector<uint8_t> &b, std::size_t off, uint16_t v) {
    b[off]     = static_cast<uint8_t>(v >> 8);
    b[off + 1] = static_cast<uint8_t>(v & 0xFF);
}
void putStr(std::vector<uint8_t> &b, std::size_t off, const std::string &s) {
    for (std::size_t i = 0; i < s.size() && i < 32; ++i)
        b[off + i] = static_cast<uint8_t>(s[i]);
}

// Build a v2 PSID header (0x7C bytes) + the given data block.
// loadHdr == 0 means the data block begins with a little-endian load address.
std::vector<uint8_t> buildPsid(uint16_t loadHdr, uint16_t initAddr, uint16_t playAddr,
                               int songs, int startSong,
                               const std::vector<uint8_t> &data,
                               const char *magic = "PSID") {
    std::vector<uint8_t> b(0x7C, 0);
    b[0] = magic[0]; b[1] = magic[1]; b[2] = magic[2]; b[3] = magic[3];
    putBE16(b, 0x04, 2);        // version
    putBE16(b, 0x06, 0x7C);     // data offset
    putBE16(b, 0x08, loadHdr);
    putBE16(b, 0x0A, initAddr);
    putBE16(b, 0x0C, playAddr);
    putBE16(b, 0x0E, static_cast<uint16_t>(songs));
    putBE16(b, 0x10, static_cast<uint16_t>(startSong));
    // speed dword at 0x12 left 0 (vsync)
    putStr(b, 0x16, "Test Tune");
    putStr(b, 0x36, "Author");
    putStr(b, 0x56, "2026 HappyTunes");
    putBE16(b, 0x76, 0x0004);   // flags: clock bits (2-3) = 01 -> PAL
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

} // namespace

class SidFileTest : public QObject {
    Q_OBJECT
private slots:

    void parsesLoadAddressInData() {
        // Program loads at $1000, encoded as the first two data bytes (LE).
        std::vector<uint8_t> prog = {0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0x60}; // LDA #$0F; STA $D418; RTS
        std::vector<uint8_t> data = {0x00, 0x10};                          // LE load addr $1000
        data.insert(data.end(), prog.begin(), prog.end());

        SidFile f = parseSid(buildPsid(0x0000, 0x1000, 0x1003, 1, 1, data));
        QVERIFY(f.valid);
        QCOMPARE(QString::fromStdString(f.format), QStringLiteral("PSID"));
        QCOMPARE(f.version, 2);
        QCOMPARE(int(f.loadAddress), 0x1000);
        QCOMPARE(int(f.initAddress), 0x1000);
        QCOMPARE(int(f.playAddress), 0x1003);
        QCOMPARE(f.songs, 1);
        QCOMPARE(f.startSong, 1);
        QCOMPARE(QString::fromStdString(f.title), QStringLiteral("Test Tune"));
        QCOMPARE(QString::fromStdString(f.author), QStringLiteral("Author"));
        QVERIFY(f.clock == Clock::PAL);
        QCOMPARE(int(f.programData.size()), int(prog.size()));
        QCOMPARE(f.programData, prog);
    }

    void parsesLoadAddressInHeader() {
        std::vector<uint8_t> prog = {0xEA, 0xEA, 0x60}; // NOP; NOP; RTS
        SidFile f = parseSid(buildPsid(0x2000, 0x2000, 0x2002, 1, 1, prog));
        QVERIFY(f.valid);
        QCOMPARE(int(f.loadAddress), 0x2000);
        QCOMPARE(f.programData, prog); // no embedded address to strip
    }

    void initAddressZeroDefaultsToLoad() {
        std::vector<uint8_t> prog = {0x60};
        SidFile f = parseSid(buildPsid(0x3000, 0x0000, 0x3001, 1, 1, prog));
        QVERIFY(f.valid);
        QCOMPARE(int(f.initAddress), 0x3000); // init 0 -> load address
    }

    void multiSongStartSong() {
        std::vector<uint8_t> prog = {0x60};
        SidFile f = parseSid(buildPsid(0x1000, 0x1000, 0x1001, 5, 3, prog));
        QVERIFY(f.valid);
        QCOMPARE(f.songs, 5);
        QCOMPARE(f.startSong, 3);
    }

    void acceptsRsidMagic() {
        std::vector<uint8_t> prog = {0x60};
        SidFile f = parseSid(buildPsid(0x1000, 0x1000, 0x1001, 1, 1, prog, "RSID"));
        QVERIFY(f.valid);
        QCOMPARE(QString::fromStdString(f.format), QStringLiteral("RSID"));
    }

    void rejectsBadMagic() {
        std::vector<uint8_t> prog = {0x60};
        SidFile f = parseSid(buildPsid(0x1000, 0x1000, 0x1001, 1, 1, prog, "MIDI"));
        QVERIFY(!f.valid);
        QVERIFY(!f.error.empty());
    }

    void rejectsTooSmall() {
        std::vector<uint8_t> tiny(0x10, 0);
        tiny[0] = 'P'; tiny[1] = 'S'; tiny[2] = 'I'; tiny[3] = 'D';
        SidFile f = parseSid(tiny);
        QVERIFY(!f.valid);
    }

    void rejectsEmptyProgram() {
        SidFile f = parseSid(buildPsid(0x1000, 0x1000, 0x1001, 1, 1, {}));
        QVERIFY(!f.valid); // no program bytes
    }
};

QTEST_MAIN(SidFileTest)
#include "test_sid_file.moc"
