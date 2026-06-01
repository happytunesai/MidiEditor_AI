/*
 * MidiEditor AI - SID importer implementation (Phase 42.1).
 */

#include "SidImporter.h"

#include "SidFile.h"
#include "SidCapture.h"
#include "SidReconstruct.h"
#include "SidMidiWriter.h"
#include "SidFpCapture.h" // cycle-accurate libsidplayfp capture (RSID import)

#include "../../midi/MidiFile.h"

#include <array>
#include <cstdint>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QInputDialog>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QTemporaryFile>
#include <QWidget>

namespace {
// Generous window captured for loop detection (and the buffer the no-loop
// dialog draws from). SID tunes loop forever; loop detection trims this to
// the natural length when a loop is found.
constexpr double kDetectMaxSeconds = 600.0;
// Default / fallback length used when no loop is found and the user doesn't
// pick one (e.g. non-interactive import).
constexpr int kDefaultSeconds = 240;
} // namespace

bool SidImporter::isInterruptPlayer(QString path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = file.readAll(); // SID files are small (< 64 KB)
    file.close();
    std::vector<uint8_t> bytes(raw.begin(), raw.end());
    sid::SidFile sf = sid::parseSid(bytes);
    return sf.valid && sf.playAddress == 0;
}

MidiFile *SidImporter::loadFile(QString path, bool *ok, QWidget *parent,
                                const std::function<void(int, int)> &onProgress) {
    if (ok) *ok = false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return nullptr;
    QByteArray raw = file.readAll();
    file.close();
    if (raw.isEmpty())
        return nullptr;

    std::vector<uint8_t> bytes(raw.begin(), raw.end());
    sid::SidFile sf = sid::parseSid(bytes);
    if (!sf.valid)
        return nullptr;

    const double fps = (sf.clock == sid::Clock::NTSC) ? sid::kNtscFramesPerSecond
                                                      : sid::kPalFramesPerSecond;
    const double safeFps = fps > 0 ? fps : 50.0;

    // User-configurable default length (MIDI I/O settings -> Commodore 64 / SID).
    const int defaultSeconds =
        QSettings("MidiEditor", "NONE").value("C64/defaultImportSeconds", kDefaultSeconds).toInt();

    sid::CaptureResult cap;
    const bool viaLib = (sf.playAddress == 0);
    if (viaLib) {
        // RSID with a self-installed interrupt player: the from-scratch 6502
        // can't run these accurately (it imports as noise), so use the
        // cycle-accurate libsidplayfp engine and feed its per-frame SID
        // register stream into the same reconstruction pipeline. That engine is
        // ~real-time, so we capture just the configured default length (loop-
        // trimmed if a loop is found within it) rather than the full 600 s
        // detection window the fast from-scratch path can afford.
        const int rsidFrames = int(fps * defaultSeconds);
        sidfp::CaptureResult fp = sidfp::captureSidRegisters(
            bytes.data(), bytes.size(), sf.startSong, rsidFrames, onProgress);
        if (!fp.ok)
            return nullptr;
        cap.ok = true;
        cap.song = fp.song;
        cap.clockHz = fp.clockHz;
        cap.framesPerSecond = fp.framesPerSecond;
        cap.frames.reserve(fp.frames.size());
        for (const std::array<uint8_t, 32> &a : fp.frames) {
            sid::SidFrame sfr;
            for (int i = 0; i < sid::kSidRegisterCount; ++i)
                sfr.regs[i] = a[i];
            cap.frames.push_back(sfr);
        }
        // Same loop trimming the from-scratch capture applies.
        const int keep = sid::detectLoopEnd(cap.frames);
        if (keep > 0 && keep < int(cap.frames.size())) {
            cap.frames.resize(static_cast<std::size_t>(keep));
            cap.loopDetected = true;
        }
    } else {
        // PSID: the fast from-scratch path can capture a generous window.
        cap = sid::captureSid(sf, sf.startSong, int(fps * kDetectMaxSeconds));
    }
    if (!cap.ok)
        return nullptr;

    // No loop detected: the tune would play forever, so decide how much to
    // keep. Only the (fast) from-scratch path asks - the libsidplayfp path
    // already captured exactly the configured default length.
    if (!cap.loopDetected && !viaLib) {
        int seconds = defaultSeconds;
        const int capturedSeconds = int(cap.frames.size() / safeFps);
        if (parent) {
            int maxSeconds = capturedSeconds > defaultSeconds ? capturedSeconds : defaultSeconds;
            bool pressedOk = false;
            int chosen = QInputDialog::getInt(
                parent,
                QObject::tr("SID Import - No Loop Detected"),
                QObject::tr("No loop was detected in this tune, so it has no natural end "
                            "(a SID plays forever).\n\nHow many seconds should be imported?\n"
                            "Tip: you can read the length from your SID player."),
                defaultSeconds, 5, maxSeconds, 5, &pressedOk);
            if (pressedOk)
                seconds = chosen;
        }
        const int keep = int(seconds * safeFps);
        if (keep > 0 && keep < int(cap.frames.size()))
            cap.frames.resize(static_cast<std::size_t>(keep));
    }

    std::vector<sid::SidNote> notes = sid::reconstructNotes(cap);
    if (notes.empty())
        return nullptr;

    QByteArray smf = sid::writeSidNotesToSmf(notes, cap, QString::fromStdString(sf.title));
    if (smf.isEmpty())
        return nullptr;

    // Round-trip through a temp file, like the other importers, so the SMF
    // goes through MidiFile's normal loader.
    QTemporaryFile temp;
    temp.setAutoRemove(false);
    if (!temp.open())
        return nullptr;
    temp.write(smf);
    const QString tempPath = temp.fileName();
    temp.close();

    bool midiOk = false;
    MidiFile *midiFile = new MidiFile(tempPath, &midiOk);
    QFile::remove(tempPath);

    if (!midiOk || !midiFile) {
        delete midiFile;
        return nullptr;
    }

    midiFile->setPath(path);
    if (ok) *ok = true;
    return midiFile;
}
