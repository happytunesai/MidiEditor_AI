/*
 * MidiEditor AI - SID file parser (Phase 42, C64 mode).
 *
 * Parses the PSID/RSID container around a Commodore 64 SID tune: header
 * fields + the embedded 6502 program image. This is plain C++ (no Qt) so
 * the deterministic core (parser + 6502 emulator + register capture) can be
 * unit-tested on its own, the same split rationale as the converter's other
 * pure pieces.
 *
 * Reference: olefriis/sidtool (MIT) lib/sidtool/file_reader.rb for the
 * header layout, generalised here to also cover non-zero load addresses,
 * RSID, and the v2+ clock flags.
 */

#ifndef SID_SIDFILE_H
#define SID_SIDFILE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sid {

enum class Clock { Unknown, PAL, NTSC, Both };

struct SidFile {
    bool        valid = false;
    std::string error;

    std::string format;          ///< "PSID" or "RSID"
    int         version = 0;     ///< 1..4

    uint16_t    loadAddress = 0; ///< resolved actual load address in C64 RAM
    uint16_t    initAddress = 0; ///< JSR target to initialise a song
    uint16_t    playAddress = 0; ///< JSR target called once per frame (0 = player installs its own IRQ)

    int         songs = 1;       ///< number of sub-tunes
    int         startSong = 1;   ///< default sub-tune (1-based, as stored)

    std::string title;
    std::string author;
    std::string released;

    Clock       clock = Clock::PAL;
    bool        playsCia = false; ///< start song's speed bit: true = CIA-timer, false = vsync/50Hz

    /// Program bytes to place into C64 RAM starting at \ref loadAddress.
    std::vector<uint8_t> programData;
};

/// Parse a PSID/RSID image. On failure returns a SidFile with valid==false
/// and a human-readable \ref SidFile::error. Never throws.
SidFile parseSid(const uint8_t *bytes, std::size_t len);

inline SidFile parseSid(const std::vector<uint8_t> &v) {
    return parseSid(v.data(), v.size());
}

} // namespace sid

#endif // SID_SIDFILE_H
