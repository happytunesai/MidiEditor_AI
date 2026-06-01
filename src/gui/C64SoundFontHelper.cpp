/*
 * MidiEditor AI - C64 SoundFont Mode orchestration implementation (Phase 42.2).
 */

#include "C64SoundFontHelper.h"

#ifdef FLUIDSYNTH_SUPPORT
#include "../midi/FluidSynthEngine.h"
#include "DownloadSoundFontDialog.h"
#endif
#include "../midi/MidiOutput.h"
#include "C64Mode.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QObject>
#include <QSettings>
#include <QWidget>

namespace {

const char *kSnapshotKey = "C64/savedEnabledSoundFonts";

QString soundFontsDir() {
    QDir dir(QCoreApplication::applicationDirPath());
    if (!dir.exists("soundfonts"))
        dir.mkpath("soundfonts");
    dir.cd("soundfonts");
    return dir.absolutePath();
}

QString preferredMicrosoftSynthPort() {
    for (const QString &name : MidiOutput::outputPorts()) {
        if (name.contains("Microsoft", Qt::CaseInsensitive) &&
            name.contains("Synth", Qt::CaseInsensitive))
            return name;
    }
    return QString();
}

} // namespace

namespace C64SoundFontHelper {

bool isC64SoundFont(const QString &path) {
    QString name = QFileInfo(path).fileName().toLower();
    return name.contains("c64") || name.contains("commodore");
}

QString findLocalC64SoundFont() {
    QDir dir(soundFontsDir());
    const QStringList filters{"*.sf2", "*.sf3"};
    for (const QFileInfo &fi : dir.entryInfoList(filters, QDir::Files, QDir::Name)) {
        if (isC64SoundFont(fi.filePath()))
            return fi.absoluteFilePath();
    }
    return QString();
}

#ifdef FLUIDSYNTH_SUPPORT

bool requestEnable(QWidget *parent) {
    FluidSynthEngine *engine = FluidSynthEngine::instance();
    if (!engine)
        return false;

    // Stop the transport before touching the output / SoundFonts. Switching the
    // MIDI output (closePort / FluidSynth shutdown) or (un)loading SoundFonts
    // while the player thread is still calling sendMidiData() is a
    // use-after-free on the freed synth / closed port (crash 0xc0000005). The
    // Stop path joins the player thread (MidiPlayer::stop() waits), so after
    // this returns the backend is ours to reconfigure. Covers every caller:
    // the toolbar switch (via C64Mode::setMode) and the C64 logo button.
    C64Mode::stopPlaybackForEngineChange();

    // 0. Make FluidSynth the active, initialized output first - C64 SoundFont
    //    mode plays the converted MIDI through FluidSynth (the program remap
    //    lives in the engine), and loadSoundFont() is a no-op until the engine
    //    is initialized (mirrors the FFXIV helper / bug B-FIELD-002).
    QString previousPort;
    bool outputSwitched = false;
    if (!MidiOutput::isFluidSynthOutput()) {
        previousPort = MidiOutput::outputPort();
        if (MidiOutput::setOutputPort(MidiOutput::FLUIDSYNTH_PORT_NAME)) {
            QSettings().setValue("out_port", MidiOutput::FLUIDSYNTH_PORT_NAME);
            outputSwitched = true;
        }
    }
    if (!engine->isInitialized())
        engine->initialize();

    // If we bail out before actually enabling the mode (user cancels the
    // download, or no font is obtained), undo the output switch so we don't
    // silently leave the user on a different MIDI output (BUG-C64-001).
    auto abortRestoringOutput = [&]() -> bool {
        if (outputSwitched) {
            MidiOutput::setOutputPort(previousPort);
            QSettings().setValue("out_port", previousPort);
        }
        return false;
    };

    // 1. Snapshot the currently enabled non-C64 SoundFonts so we can restore
    //    them when the mode is turned off.
    QStringList snapshot;
    for (const QString &p : engine->allSoundFontPaths()) {
        if (engine->isSoundFontEnabled(p) && !isC64SoundFont(p))
            snapshot << p;
    }
    QSettings().setValue(kSnapshotKey, snapshot);

    // 2. Find a C64 SoundFont: already in the stack, else on disk.
    QString c64Path;
    for (const QString &p : engine->allSoundFontPaths()) {
        if (isC64SoundFont(p)) { c64Path = p; break; }
    }
    if (c64Path.isEmpty()) {
        const QString onDisk = findLocalC64SoundFont();
        if (!onDisk.isEmpty()) {
            engine->addPendingSoundFontPaths(QStringList{onDisk});
            engine->loadSoundFont(onDisk);
            c64Path = onDisk;
        }
    }
    // 3. Not installed yet -> offer to auto-download it (same flow as FFXIV
    //    mode): the Commodore_64.sf2 ships via the app's SoundFont download
    //    catalog, so the user can fetch it on demand instead of hunting for one.
    if (c64Path.isEmpty()) {
        const QMessageBox::StandardButton choice = QMessageBox::question(
            parent,
            QObject::tr("Download C64 SoundFont"),
            QObject::tr("C64 SoundFont Mode needs a Commodore 64 SoundFont "
                        "(Commodore_64.sf2), which is not installed yet.\n\n"
                        "Open the SoundFont download dialog now?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (choice != QMessageBox::Yes)
            return abortRestoringOutput();

        DownloadSoundFontDialog dialog(parent);
        QString downloaded;
        QObject::connect(&dialog, &DownloadSoundFontDialog::soundFontDownloaded,
                         [&downloaded, engine](const QString &path) {
            // Register every download (the user may grab more than one), and
            // remember the C64 one as our target.
            engine->addPendingSoundFontPaths(QStringList{path});
            engine->loadSoundFont(path);
            if (isC64SoundFont(path))
                downloaded = path;
        });
        dialog.exec();

        if (!downloaded.isEmpty()) {
            c64Path = downloaded;
        } else {
            // Cancelled or grabbed via another route - re-scan stack then disk.
            for (const QString &p : engine->allSoundFontPaths())
                if (isC64SoundFont(p)) { c64Path = p; break; }
            if (c64Path.isEmpty()) {
                const QString onDisk = findLocalC64SoundFont();
                if (!onDisk.isEmpty()) {
                    engine->addPendingSoundFontPaths(QStringList{onDisk});
                    engine->loadSoundFont(onDisk);
                    c64Path = onDisk;
                }
            }
        }
        if (c64Path.isEmpty())
            return abortRestoringOutput(); // still nothing -> don't enable the mode
    }

    // 3. Isolate the C64 SoundFont: enable it, disable everything else.
    for (const QString &p : engine->allSoundFontPaths()) {
        const bool isC64 = isC64SoundFont(p);
        if (isC64 && !engine->isSoundFontEnabled(p))
            engine->setSoundFontEnabled(p, true);
        else if (!isC64 && engine->isSoundFontEnabled(p))
            engine->setSoundFontEnabled(p, false);
    }

    // Commit the mode BEFORE any modal info dialog: that dialog runs its own
    // event loop, so the toolbar widgets repaint while it is up. If the flag
    // weren't set yet they'd read "neither armed nor SoundFont mode" and flash
    // grey for the duration of the popup (the toggle-flicker the user saw).
    QSettings persist;
    engine->saveSettings(&persist);
    engine->setC64SoundFontMode(true);
    QSettings().setValue("c64SoundFontMode", true);

    if (outputSwitched && parent) {
        QMessageBox::information(
            parent, QObject::tr("C64 SoundFont Mode"),
            QObject::tr("Switched MIDI output to \"%1\" so the C64 SoundFont can be used.")
                .arg(MidiOutput::FLUIDSYNTH_PORT_NAME));
    }
    return true;
}

void requestDisable(QWidget *parent) {
    FluidSynthEngine *engine = FluidSynthEngine::instance();
    if (!engine)
        return;

    // Same race as requestEnable(): the GS-fallback below switches the MIDI
    // output away from FluidSynth, which shuts the synth down. Doing that while
    // the player thread is mid-sendMidiData() frees _synth under it (this is
    // the exact crash in the log: "engine shut down" -> 0xc0000005). Join the
    // player thread first.
    C64Mode::stopPlaybackForEngineChange();

    // 1. Disable C64 SoundFonts in the stack.
    for (const QString &p : engine->allSoundFontPaths()) {
        if (isC64SoundFont(p) && engine->isSoundFontEnabled(p))
            engine->setSoundFontEnabled(p, false);
    }

    // 2. Restore the snapshot (the GM / previous selection).
    QSettings settings;
    const QStringList snapshot = settings.value(kSnapshotKey).toStringList();
    int restored = 0;
    for (const QString &p : snapshot) {
        if (!QFileInfo::exists(p)) continue;
        if (!engine->allSoundFontPaths().contains(p))
            engine->loadSoundFont(p);
        if (!engine->isSoundFontEnabled(p))
            engine->setSoundFontEnabled(p, true);
        ++restored;
    }

    // 3. Fallback: enable the first non-C64 SoundFont still in the stack.
    if (restored == 0) {
        for (const QString &p : engine->allSoundFontPaths()) {
            if (!isC64SoundFont(p)) {
                if (!engine->isSoundFontEnabled(p))
                    engine->setSoundFontEnabled(p, true);
                ++restored;
                break;
            }
        }
    }

    // 4. If nothing is enabled and we're on FluidSynth, fall back to the
    //    system Microsoft GS Wavetable Synth so playback keeps working.
    bool anyEnabled = false;
    for (const QString &p : engine->allSoundFontPaths()) {
        if (engine->isSoundFontEnabled(p)) { anyEnabled = true; break; }
    }
    if (!anyEnabled) {
        const QString msPort = preferredMicrosoftSynthPort();
        if (!msPort.isEmpty() && MidiOutput::outputPort() != msPort) {
            MidiOutput::setOutputPort(msPort);
            if (parent) {
                QMessageBox::information(
                    parent, QObject::tr("C64 SoundFont Mode"),
                    QObject::tr("No SoundFonts are loaded. Switched MIDI output to \"%1\".")
                        .arg(msPort));
            }
        }
    }

    QSettings persist;
    engine->saveSettings(&persist);
    engine->setC64SoundFontMode(false);
    QSettings().setValue("c64SoundFontMode", false);
}

#else // !FLUIDSYNTH_SUPPORT

bool requestEnable(QWidget *) { return false; }
void requestDisable(QWidget *) {}

#endif

} // namespace C64SoundFontHelper
