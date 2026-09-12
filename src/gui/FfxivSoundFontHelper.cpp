/*
 * MidiEditor
 * Copyright (C) 2010  Markus Schwenk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "FfxivSoundFontHelper.h"

#include "../AppPaths.h"

#include "DownloadSoundFontDialog.h"

#ifdef FLUIDSYNTH_SUPPORT
#include "../midi/FluidSynthEngine.h"
#endif
#include "../midi/MidiOutput.h"
#include "C64Mode.h" // shared "stop the transport before an engine handover" helper

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QWidget>

namespace {

const char *kSnapshotKey = "FFXIV/savedEnabledSoundFonts";

QString soundFontsDir() {
    return AppPaths::soundFontsDir();
}

// Every persist in this file goes through AppPaths::settings() - the scope the
// app READS this state from (MainWindow's out_port, FluidSynthEngine's
// "FluidSynth" group). A default-constructed QSettings has no organization name
// (nothing in src/ ever sets one), so on Windows its writes are dropped with an
// AccessError: the "crash-safe" persists here silently did nothing.
void persistValue(const QString &key, const QVariant &value) {
    AppPaths::settings()->setValue(key, value);
}

QString preferredMicrosoftSynthPort() {
    // Pick the first MIDI output whose name contains "Microsoft" and "Synth".
    QStringList ports = MidiOutput::outputPorts();
    for (const QString &name : ports) {
        if (name.contains("Microsoft", Qt::CaseInsensitive) &&
            name.contains("Synth", Qt::CaseInsensitive)) {
            return name;
        }
    }
    return QString();
}

} // namespace

namespace FfxivSoundFontHelper {

bool isFfxivSoundFont(const QString &path) {
    QString name = QFileInfo(path).fileName().toLower();
    return name.contains("ff14") || name.contains("ffxiv");
}

QString findLocalFfxivSoundFont() {
    QDir dir(soundFontsDir());
    const QStringList filters{"*.sf2", "*.sf3"};
    const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : entries) {
        if (isFfxivSoundFont(fi.filePath())) {
            return fi.absoluteFilePath();
        }
    }
    return QString();
}

#ifdef FLUIDSYNTH_SUPPORT

static QString findFfxivInStack(FluidSynthEngine *engine) {
    const QStringList all = engine->allSoundFontPaths();
    for (const QString &p : all) {
        if (isFfxivSoundFont(p)) return p;
    }
    return QString();
}

#endif // FLUIDSYNTH_SUPPORT

bool requestEnable(QWidget *parent) {
#ifdef FLUIDSYNTH_SUPPORT
    FluidSynthEngine *engine = FluidSynthEngine::instance();
    if (!engine) return false;

    // Stop the transport before switching the MIDI output / loading SoundFonts:
    // doing it under a live player thread frees the synth / closes the port mid
    // send (use-after-free, 0xc0000005 - same crash class as the C64 switch,
    // see BUG-C64-CRASH). FFXIV mode is arm-then-play so it's far less likely to
    // be toggled mid-playback, but the exposure is identical, so guard it too.
    C64Mode::stopPlaybackForEngineChange();

    // 1. Make FluidSynth the active MIDI output FIRST so the engine is
    //    initialized. Without this, every loadSoundFont() call below would
    //    silently return -1 because _initialized is false on a clean install
    //    where the user has never used FluidSynth before. See bug B-FIELD-002
    //    (1.6.1) - the second-attempt fix; the first attempt switched the
    //    output AFTER loadSoundFont, so the SF was never actually registered.
    bool outputSwitched = false;
    QString previousPort;
    if (!MidiOutput::isFluidSynthOutput()) {
        previousPort = MidiOutput::outputPort();
        if (MidiOutput::setOutputPort(MidiOutput::FLUIDSYNTH_PORT_NAME)) {
            persistValue(QStringLiteral("out_port"), MidiOutput::FLUIDSYNTH_PORT_NAME);
            outputSwitched = true;
        }
    }
    // Belt-and-suspenders: setOutputPort() initializes the engine on the
    // FluidSynth path, but if the user is already on FluidSynth or the
    // switch failed for some reason, force-init here so the rest of this
    // function can rely on engine->isInitialized().
    if (!engine->isInitialized()) {
        engine->initialize();
    }

    // If we bail out before actually enabling the mode (user declines the
    // download, or no font is found), undo the output switch so we don't
    // silently leave the user on a different MIDI output (BUG-CORE-003;
    // mirrors the C64 helper's abortRestoringOutput / BUG-C64-001).
    auto abortRestoringOutput = [&]() -> bool {
        // Only persist the port we actually managed to open: a failed
        // restore leaves us on FluidSynth, and out_port must say so.
        if (outputSwitched && MidiOutput::setOutputPort(previousPort)) {
            persistValue(QStringLiteral("out_port"), previousPort);
        }
        return false;
    };

    // 2. Snapshot the currently enabled non-FFXIV SoundFonts so we can
    //    restore them on disable.
    QStringList toSnapshot;
    const QStringList allPaths = engine->allSoundFontPaths();
    for (const QString &p : allPaths) {
        if (engine->isSoundFontEnabled(p) && !isFfxivSoundFont(p)) {
            toSnapshot << p;
        }
    }
    persistValue(QLatin1String(kSnapshotKey), toSnapshot);

    // 3. Find a FFXIV SoundFont. Look first in the existing stack, then on
    //    disk in <appDir>/soundfonts/, then offer to download.
    QString ffxivPath = findFfxivInStack(engine);
    if (ffxivPath.isEmpty()) {
        QString onDisk = findLocalFfxivSoundFont();
        if (!onDisk.isEmpty()) {
            // Engine is now initialized (step 1), so loadSoundFont takes effect.
            // Also add to pending list as a fallback in case the engine has to
            // re-initialize (e.g. driver change) before the user saves.
            engine->addPendingSoundFontPaths(QStringList{onDisk});
            engine->loadSoundFont(onDisk);
            ffxivPath = onDisk;
        }
    }
    if (ffxivPath.isEmpty()) {
        QMessageBox::StandardButton choice = QMessageBox::question(
            parent,
            QObject::tr("Download FFXIV SoundFont"),
            QObject::tr("FFXIV SoundFont Mode needs the FFXIV bard SoundFont "
                        "(FF14-c3c6-fixed.sf2), which is not installed yet.\n\n"
                        "Open the SoundFont download dialog now?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (choice != QMessageBox::Yes) {
            return abortRestoringOutput();
        }

        DownloadSoundFontDialog dialog(parent);
        QString downloaded;
        QObject::connect(&dialog, &DownloadSoundFontDialog::soundFontDownloaded,
                         [&downloaded, engine](const QString &path) {
            // Register EVERY downloaded SoundFont, not just the FFXIV one -
            // the user can download multiple SFs in the same dialog session
            // (e.g. FFXIV + GeneralUser GS) and we must not silently drop
            // the rest. See bug B-FIELD-002 follow-up (1.6.1).
            engine->addPendingSoundFontPaths(QStringList{path});
            engine->loadSoundFont(path);
            if (isFfxivSoundFont(path)) {
                downloaded = path;
            }
        });
        dialog.exec();

        if (downloaded.isEmpty()) {
            // User may have downloaded a different SF or cancelled. Re-scan.
            ffxivPath = findFfxivInStack(engine);
            if (ffxivPath.isEmpty()) {
                ffxivPath = findLocalFfxivSoundFont();
                if (!ffxivPath.isEmpty()) {
                    engine->addPendingSoundFontPaths(QStringList{ffxivPath});
                    engine->loadSoundFont(ffxivPath);
                }
            }
        } else {
            ffxivPath = downloaded;
        }

        if (ffxivPath.isEmpty()) {
            return abortRestoringOutput();
        }
    }

    // 4. Disable everything except the FFXIV SoundFont.
    const QStringList currentPaths = engine->allSoundFontPaths();
    for (const QString &p : currentPaths) {
        if (p == ffxivPath) {
            if (!engine->isSoundFontEnabled(p)) {
                engine->setSoundFontEnabled(p, true);
            }
        } else {
            if (engine->isSoundFontEnabled(p)) {
                engine->setSoundFontEnabled(p, false);
            }
        }
    }

    // 5. If we switched the MIDI output above, surface a one-line
    //    confirmation now (after the SoundFont is actually registered, so the
    //    user does not see the dialog before the SF list is populated).
    if (outputSwitched && parent) {
        QMessageBox::information(
            parent,
            QObject::tr("FFXIV SoundFont Mode"),
            QObject::tr("Switched MIDI output to \"%1\" so the FFXIV bard "
                        "SoundFont can be used.")
                .arg(MidiOutput::FLUIDSYNTH_PORT_NAME));
    }

    // 6. Flip the engine flag, THEN persist the FluidSynth state (SoundFont
    //    paths + disabled set + the mode flag) so a crash before normal app
    //    shutdown loses neither the freshly-installed SoundFont nor the mode.
    //    Order matters: saveSettings() writes FluidSynth/ffxivSoundFontMode,
    //    which is the key loadSettings() reads on the next start.
    engine->setFfxivSoundFontMode(true);
    {
        auto persistSettings = AppPaths::settings();
        engine->saveSettings(persistSettings.get());
    }
    return true;
#else
    Q_UNUSED(parent);
    return false;
#endif
}

void requestDisable(QWidget *parent) {
#ifdef FLUIDSYNTH_SUPPORT
    FluidSynthEngine *engine = FluidSynthEngine::instance();
    if (!engine) return;

    // Same race as requestEnable(): the GS-fallback path can switch output away
    // from FluidSynth (-> shutdown -> synth freed) while the player thread runs.
    // Join the player thread first (see BUG-C64-CRASH).
    C64Mode::stopPlaybackForEngineChange();

    // 1. Disable any FFXIV SoundFonts in the stack.
    const QStringList allPaths = engine->allSoundFontPaths();
    for (const QString &p : allPaths) {
        if (isFfxivSoundFont(p) && engine->isSoundFontEnabled(p)) {
            engine->setSoundFontEnabled(p, false);
        }
    }

    // 2. Restore snapshot if available.
    const QStringList snapshot =
        AppPaths::settings()->value(QLatin1String(kSnapshotKey)).toStringList();
    int restored = 0;
    for (const QString &p : snapshot) {
        if (!QFileInfo::exists(p)) continue;
        if (!engine->allSoundFontPaths().contains(p)) {
            engine->loadSoundFont(p);
        }
        if (!engine->isSoundFontEnabled(p)) {
            engine->setSoundFontEnabled(p, true);
        }
        ++restored;
    }

    // 3. If snapshot brought nothing back, enable the first non-FFXIV SF
    //    that is already in the stack as a sane fallback.
    if (restored == 0) {
        const QStringList paths = engine->allSoundFontPaths();
        for (const QString &p : paths) {
            if (!isFfxivSoundFont(p)) {
                if (!engine->isSoundFontEnabled(p)) {
                    engine->setSoundFontEnabled(p, true);
                }
                ++restored;
                break;
            }
        }
    }

    // 4. If the FluidSynth stack now has no enabled SoundFont AND the user
    //    is currently routed to FluidSynth, switch the MIDI output to the
    //    system "Microsoft GS Wavetable Synth" port so playback keeps working.
    bool anyEnabled = false;
    for (const QString &p : engine->allSoundFontPaths()) {
        if (engine->isSoundFontEnabled(p)) { anyEnabled = true; break; }
    }
    if (!anyEnabled) {
        const QString msPort = preferredMicrosoftSynthPort();
        const QString currentPort = MidiOutput::outputPort();
        if (!msPort.isEmpty() && currentPort != msPort) {
            // The port can be enumerated yet fail to open (held by another
            // application): persist and announce the switch only when it
            // really happened, otherwise the next start would also try a
            // port it cannot open while the message claimed success.
            if (MidiOutput::setOutputPort(msPort)) {
                persistValue(QStringLiteral("out_port"), msPort);
                if (parent) {
                    QMessageBox::information(
                        parent,
                        QObject::tr("FFXIV SoundFont Mode"),
                        QObject::tr("No SoundFonts are loaded. Switched MIDI output "
                                    "to \"%1\" so playback keeps working.").arg(msPort));
                }
            } else if (parent) {
                QMessageBox::warning(
                    parent,
                    QObject::tr("FFXIV SoundFont Mode"),
                    QObject::tr("No SoundFonts are loaded and the MIDI output "
                                "could not be switched to \"%1\". Playback will "
                                "be silent until you pick another MIDI output "
                                "or enable a SoundFont in the settings.").arg(msPort));
            }
        }
    }

    // 5. Flip the engine flag, then persist (same order and reason as
    //    requestEnable(); mirrors the C64 helper's disable path).
    engine->setFfxivSoundFontMode(false);
    {
        auto persist = AppPaths::settings();
        engine->saveSettings(persist.get());
    }
#else
    Q_UNUSED(parent);
#endif
}

} // namespace FfxivSoundFontHelper
