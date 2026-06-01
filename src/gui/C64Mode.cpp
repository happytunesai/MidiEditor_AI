/*
 * MidiEditor AI - C64 playback-engine mode helper implementation (see C64Mode.h).
 */
#include "C64Mode.h"

#include "C64SoundFontHelper.h"
#include "../midi/SidAudioPlayer.h"
#ifdef FLUIDSYNTH_SUPPORT
#include "../midi/FluidSynthEngine.h"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QUrl>

#include <functional>
#include <utility>

namespace C64Mode {

namespace {
const char *kModeKey   = "C64/playbackMode";
const char *kChosenKey = "C64/modeChosen";
const QString kSf2Url  =
    QStringLiteral("https://github.com/happytunesai/MidiEditor_AI/releases/download/soundfonts/Commodore_64.sf2");

// Set by MainWindow; runs the full Stop path before an engine handover so the
// player thread is joined before FluidSynth/the MIDI port are reconfigured.
std::function<void()> g_stopHook;
} // namespace

void setStopPlaybackHook(std::function<void()> hook) {
    g_stopHook = std::move(hook);
}

void stopPlaybackForEngineChange() {
    if (g_stopHook)
        g_stopHook();
}

Notifier *Notifier::instance() {
    static Notifier inst;
    return &inst;
}

QString current() {
    const QString m = QSettings("MidiEditor", "NONE")
                          .value(kModeKey, "soundfont").toString();
    return (m == QStringLiteral("emulation")) ? m : QStringLiteral("soundfont");
}

bool emulationAvailable() {
    return SidAudioPlayer::instance()->hasSource();
}

bool isChosen() {
    return QSettings("MidiEditor", "NONE").value(kChosenKey, false).toBool();
}

void setMode(const QString &mode, QWidget *parent) {
    const QString m = (mode == QStringLiteral("emulation"))
                          ? QStringLiteral("emulation") : QStringLiteral("soundfont");
    QSettings("MidiEditor", "NONE").setValue(kModeKey, m);

    // Hand the active engine over to the newly chosen one (same logic the
    // Settings radios used; only acts when C64 was actually active).
    SidAudioPlayer *sid = SidAudioPlayer::instance();
    if (m == QStringLiteral("soundfont")) {
        if (sid->isArmed()) {            // was active via Emulation -> SoundFont
            // requestEnable() stops the transport first (its guard runs while the
            // SID may still be playing, so the full Stop path resets cursor/
            // panels). Enable SoundFont mode BEFORE disarming the SID so the
            // toolbar switch - which reads c64SoundFontMode() while in soundfont
            // mode - stays continuously "active" across the handover and doesn't
            // hide/relayout (the "button neu läuft" flicker).
            C64SoundFontHelper::requestEnable(parent);
            sid->setArmed(false);
        }
    } else {                             // emulation
#ifdef FLUIDSYNTH_SUPPORT
        FluidSynthEngine *engine = FluidSynthEngine::instance();
        if (engine && engine->c64SoundFontMode()) { // was active via SoundFont
            // Arm Emulation first (the switch reads isArmed() in emulation mode),
            // then disable SoundFont mode - keeps the switch visible throughout.
            // requestDisable() stops the transport before the FluidSynth teardown.
            sid->setArmed(true);
            C64SoundFontHelper::requestDisable(parent);
        }
#endif
    }

    Notifier::instance()->notifyChanged();
}

QString ensureChosen(QWidget *parent) {
    if (isChosen())
        return current();

    // No .sid open -> Emulation isn't a real option yet. Default to SoundFont
    // WITHOUT marking the choice as made, so the first-use SoundFont/Emulation
    // picker still appears the first time the user engages C64 on an actual
    // .sid (where both engines genuinely apply).
    if (!emulationAvailable()) {
        setMode(QStringLiteral("soundfont"), parent); // persist + notify, no handover
        prefetchSoundFont();
        return current();
    }

    QMessageBox box(parent);
    box.setWindowTitle(QObject::tr("Commodore 64 Sound"));
    box.setIcon(QMessageBox::Question);
    box.setText(QObject::tr("How should Commodore 64 tunes play?"));
    box.setInformativeText(QObject::tr(
        "SoundFont — play the converted MIDI with authentic C64 timbres.\n"
        "Emulation — play the original .sid through the cycle-accurate engine.\n\n"
        "You can switch anytime. The C64 SoundFont (~11 MB) is downloaded in "
        "the background so either choice works instantly later."));
    QPushButton *sfBtn  = box.addButton(QObject::tr("C64 SoundFont"), QMessageBox::AcceptRole);
    QPushButton *emuBtn = box.addButton(QObject::tr("Emulation"), QMessageBox::AcceptRole);
    box.setDefaultButton(sfBtn);
    box.exec();

    const QString chosen = (box.clickedButton() == emuBtn)
                               ? QStringLiteral("emulation") : QStringLiteral("soundfont");
    QSettings("MidiEditor", "NONE").setValue(kChosenKey, true);
    setMode(chosen, parent);   // persist + (no-op) handover + notify
    // Only prefetch for the Emulation choice: if they picked SoundFont, the
    // activation path (C64SoundFontHelper::requestEnable) downloads the font
    // itself - starting a second background download to the same file would
    // race/corrupt it. Emulation never downloads, so prefetch makes a later
    // switch to SoundFont instant.
    if (chosen == QStringLiteral("emulation"))
        prefetchSoundFont();
    return chosen;
}

void prefetchSoundFont() {
    static bool started = false;
    if (started)
        return;
    started = true;

    // Already installed? findLocalC64SoundFont() works without FluidSynth.
    if (!C64SoundFontHelper::findLocalC64SoundFont().isEmpty())
        return;

    QDir dir(QCoreApplication::applicationDirPath());
    if (!dir.exists("soundfonts"))
        dir.mkpath("soundfonts");
    dir.cd("soundfonts");
    const QString dest = dir.absoluteFilePath("Commodore_64.sf2");
    if (QFileInfo::exists(dest))
        return;

    static QNetworkAccessManager *nam = new QNetworkAccessManager(); // app lifetime
    QNetworkRequest req((QUrl(kSf2Url)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, dest]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return; // quiet: the normal download dialog is still a fallback
        const QByteArray data = reply->readAll();
        if (data.size() < 4096)
            return; // not a real .sf2 (404 page etc.)
        QFile f(dest);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(data);
            f.close();
        }
    });
}

} // namespace C64Mode
