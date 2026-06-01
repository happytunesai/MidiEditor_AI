/*
 * MidiEditor AI - SID (.sid) importer (Phase 42.1, C64 mode).
 *
 * Same contract as the other format importers: parse + transcribe a
 * Commodore 64 SID tune into an editable MidiFile. Internally runs the
 * Qt-free pipeline (parse -> 6502 emulate -> register capture -> note
 * reconstruction), encodes the notes as SMF bytes, and loads them through
 * MidiFile. Best-effort transcription - lossy and player-dependent.
 */

#ifndef SID_SIDIMPORTER_H
#define SID_SIDIMPORTER_H

#include <QString>

#include <functional>

class MidiFile;
class QWidget;

class SidImporter {
public:
    /// Load \a path (a PSID/RSID file) as a transcribed MidiFile. Sets
    /// *ok and returns the file on success, nullptr + *ok=false otherwise.
    ///
    /// SID tunes loop forever, so a generous window is captured and loop
    /// detection trims to the natural length. When no loop is found and
    /// \a parent is given, the user is asked how many seconds to import
    /// (so a long non-looping tune isn't silently truncated).
    /// \a onProgress (optional) is called with (framesDone, totalFrames) during
    /// the slow libsidplayfp render so the caller can drive a progress bar.
    static MidiFile *loadFile(QString path, bool *ok, QWidget *parent = nullptr,
                              const std::function<void(int, int)> &onProgress = {});

    /// True if \a path is an RSID tune with no play address (a self-installed
    /// interrupt player). These import via the cycle-accurate libsidplayfp
    /// engine, which takes ~1-2 s, so the caller can show a busy indicator.
    /// Cheap (parses only the header); false on any error.
    static bool isInterruptPlayer(QString path);
};

#endif // SID_SIDIMPORTER_H
