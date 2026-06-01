/*
 * MidiEditor AI - SID file parser implementation (Phase 42).
 */

#include "SidFile.h"

namespace sid {

namespace {

// PSID/RSID header words are big-endian.
uint16_t readBE16(const uint8_t *p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
uint32_t readBE32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
         | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// Fixed-width header strings are NUL-padded, max 32 bytes.
std::string readFixedString(const uint8_t *p, std::size_t maxLen) {
    std::size_t n = 0;
    while (n < maxLen && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char *>(p), n);
}

SidFile fail(const std::string &msg) {
    SidFile f;
    f.valid = false;
    f.error = msg;
    return f;
}

} // namespace

SidFile parseSid(const uint8_t *bytes, std::size_t len) {
    // Header is 0x76 bytes for v1, 0x7C for v2+. Require at least the v1
    // header before touching any field.
    static const std::size_t kHeaderV1 = 0x76;
    if (!bytes || len < kHeaderV1)
        return fail("File too small to be a SID (need at least 0x76 bytes).");

    const std::string magic(reinterpret_cast<const char *>(bytes), 4);
    if (magic != "PSID" && magic != "RSID")
        return fail("Not a SID file: magic is '" + magic + "', expected PSID or RSID.");

    SidFile f;
    f.format  = magic;
    f.version = readBE16(bytes + 0x04);
    if (f.version < 1 || f.version > 4)
        return fail("Unsupported SID version: " + std::to_string(f.version));

    const std::size_t dataOffset = readBE16(bytes + 0x06);
    if (dataOffset > len)
        return fail("Data offset 0x" + std::to_string(dataOffset) + " lies past end of file.");

    const uint16_t loadHdr = readBE16(bytes + 0x08);
    f.initAddress = readBE16(bytes + 0x0A);
    f.playAddress = readBE16(bytes + 0x0C);
    f.songs       = readBE16(bytes + 0x0E);
    f.startSong   = readBE16(bytes + 0x10);
    const uint32_t speed = readBE32(bytes + 0x12);

    f.title    = readFixedString(bytes + 0x16, 32);
    f.author   = readFixedString(bytes + 0x36, 32);
    f.released = readFixedString(bytes + 0x56, 32);

    // v2+ flags word carries the clock + SID-model hints.
    if (f.version >= 2 && len >= 0x78) {
        const uint16_t flags = readBE16(bytes + 0x76);
        switch ((flags >> 2) & 0x3) {
        case 1:  f.clock = Clock::PAL;  break;
        case 2:  f.clock = Clock::NTSC; break;
        case 3:  f.clock = Clock::Both; break;
        default: f.clock = Clock::Unknown; break;
        }
    } else {
        f.clock = Clock::PAL; // v1 has no flags; PAL is the safe default
    }

    if (f.songs < 1) f.songs = 1;
    if (f.startSong < 1) f.startSong = 1;
    if (f.startSong > f.songs) f.startSong = 1;

    // Speed bit per song (bits 0..31); the start song's bit selects vsync vs CIA.
    const int speedBit = (f.startSong - 1) & 31;
    f.playsCia = ((speed >> speedBit) & 0x1u) != 0;

    // Resolve the program image. When the header load address is 0, the real
    // load address is the first two bytes of the data (little-endian) and the
    // program follows; otherwise the whole data block loads at loadHdr.
    const uint8_t *data = bytes + dataOffset;
    const std::size_t dataLen = len - dataOffset;
    if (loadHdr == 0) {
        if (dataLen < 2)
            return fail("Embedded load address missing (data block shorter than 2 bytes).");
        f.loadAddress = static_cast<uint16_t>(data[0] | (data[1] << 8));
        f.programData.assign(data + 2, data + dataLen);
    } else {
        f.loadAddress = loadHdr;
        f.programData.assign(data, data + dataLen);
    }

    // An init address of 0 means "use the load address".
    if (f.initAddress == 0)
        f.initAddress = f.loadAddress;

    if (f.programData.empty())
        return fail("SID contains no program data.");

    f.valid = true;
    return f;
}

} // namespace sid
