/*
 * MidiEditor AI - C64 SoundFont Mode toolbar toggle implementation (Phase 42.2).
 */

#include "C64ToggleWidget.h"

#include "Appearance.h"
#include "C64Mode.h"
#include "C64SoundFontHelper.h"
#include "../midi/FluidSynthEngine.h"
#include "../midi/SidAudioPlayer.h"

#include <QMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QSettings>

static const int ICON_HEIGHT = 32;
static const int WIDGET_PADDING = 4;

namespace {
// The C64 button does whatever the MIDI-I/O "Commodore 64 / SID" setting picks:
// SoundFont (FluidSynth C64 timbres for the MIDI) or Emulation (play the
// original .sid through libsidplayfp). Emulation only counts when a .sid is
// actually open (isEmulationActive); otherwise the button behaves as SoundFont
// so the user can still chiptune-ify a plain MIDI.
bool c64EmulationSelected() {
    return C64Mode::isEmulationActive();
}
// Whether the button currently reads as "on" for its active mode. In emulation
// mode the button ARMS the mode (the transport then plays the .sid); in
// soundfont mode it toggles the FluidSynth C64 SoundFont mode.
bool c64Active() {
    if (c64EmulationSelected())
        return SidAudioPlayer::instance()->isArmed();
#ifdef FLUIDSYNTH_SUPPORT
    FluidSynthEngine *engine = FluidSynthEngine::instance();
    return engine && engine->c64SoundFontMode();
#else
    return false;
#endif
}
} // namespace

C64ToggleWidget::C64ToggleWidget(QWidget *parent)
    : QWidget(parent)
{
    // Active: the original colour icon (glows). Inactive: the dark-mode-adjusted
    // light/grey silhouette so it stays legible when off (in light mode
    // adjustIconForDarkMode returns the original unchanged).
    QPixmap raw(":/run_environment/graphics/tool/c64.png");
    _iconOn = raw;
    _iconOff = Appearance::adjustIconForDarkMode(raw, "c64");

    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(sizeHint());
    setCursor(Qt::PointingHandCursor);

#ifdef FLUIDSYNTH_SUPPORT
    FluidSynthEngine *engine = FluidSynthEngine::instance();
    if (engine) {
        connect(engine, &FluidSynthEngine::c64SoundFontModeChanged,
                this, &C64ToggleWidget::onModeChanged);
    }
#endif
    // Reflect Emulation arming (the button glows when armed).
    connect(SidAudioPlayer::instance(), &SidAudioPlayer::armedChanged,
            this, &C64ToggleWidget::onModeChanged);
    // A file load can flip whether Emulation applies (a .sid is open or not) ->
    // the button may switch between arming SID and toggling the SoundFont.
    connect(SidAudioPlayer::instance(), &SidAudioPlayer::sourceChanged,
            this, [this] { updateTooltip(); update(); });
    // Repaint when the engine choice changes from the toolbar switch / settings
    // (so the button reflects which mode it now activates).
    connect(C64Mode::Notifier::instance(), &C64Mode::Notifier::modeChanged,
            this, [this] { updateTooltip(); update(); });
    updateTooltip();
}

QSize C64ToggleWidget::sizeHint() const {
    return QSize(ICON_HEIGHT + WIDGET_PADDING * 2, ICON_HEIGHT + WIDGET_PADDING * 2);
}

QSize C64ToggleWidget::minimumSizeHint() const {
    return sizeHint();
}

void C64ToggleWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);

    const bool enabled = c64Active();

    const QColor accent = palette().highlight().color();

    if (enabled) {
        // The Commodore logo is dark blue on transparent - designed for light
        // backgrounds, so it sinks into a dark toolbar. When active we therefore
        // give it a soft glow (the "leuchten") plus a light rounded chip so the
        // colour logo pops and reads clearly.
        QRadialGradient glow(QRectF(rect()).center(), width() * 0.62);
        QColor g = accent;
        g.setAlpha(150); glow.setColorAt(0.0, g);
        g.setAlpha(70);  glow.setColorAt(0.5, g);
        g.setAlpha(0);   glow.setColorAt(1.0, g);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(rect());

        QRect plate = rect().adjusted(2, 2, -2, -2);
        p.setBrush(QColor(238, 240, 245, 235)); // near-white chip
        p.setPen(QPen(accent, 1.4));
        p.drawRoundedRect(plate, 5, 5);
    } else if (_hovered) {
        QColor highlight = accent;
        highlight.setAlpha(40);
        p.setPen(Qt::NoPen);
        p.setBrush(highlight);
        p.drawRoundedRect(rect(), 3, 3);
    }

    // Active: the original colour logo at full opacity (over the light chip).
    // Inactive: the dark-mode-adjusted light/grey silhouette, lightly muted so
    // it stays legible on a dark toolbar yet clearly reads as off.
    const QPixmap &icon = enabled ? _iconOn : _iconOff;
    if (!icon.isNull()) {
        p.setOpacity(enabled ? 1.0 : 0.70);
        QRect inner = rect().adjusted(WIDGET_PADDING, WIDGET_PADDING,
                                      -WIDGET_PADDING, -WIDGET_PADDING);
        QPixmap scaled = icon.scaled(inner.size(), Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        QPoint topLeft(inner.x() + (inner.width()  - scaled.width())  / 2,
                       inner.y() + (inner.height() - scaled.height()) / 2);
        p.drawPixmap(topLeft, scaled);
        p.setOpacity(1.0);
    }
}

void C64ToggleWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // First C64 interaction ever -> one-time SoundFont/Emulation picker.
        C64Mode::ensureChosen(window());
        if (c64EmulationSelected()) {
            // Emulation: arm/disarm the mode. Actual play/stop is driven by the
            // normal transport (Play/Stop buttons), starting from the cursor.
            SidAudioPlayer *sp = SidAudioPlayer::instance();
            sp->setArmed(!sp->isArmed());
        } else {
#ifdef FLUIDSYNTH_SUPPORT
            FluidSynthEngine *engine = FluidSynthEngine::instance();
            if (engine) {
                // SoundFont: manage the stack like FFXIV mode - enabling
                // isolates a C64 SoundFont, disabling restores the previous
                // (GM) selection so playback falls back to General MIDI.
                if (engine->c64SoundFontMode())
                    C64SoundFontHelper::requestDisable(window());
                else
                    C64SoundFontHelper::requestEnable(window());
            }
#endif
        }
        updateTooltip();
    }
    QWidget::mousePressEvent(event);
}

void C64ToggleWidget::enterEvent(QEnterEvent *event) {
    _hovered = true;
    update();
    QWidget::enterEvent(event);
}

void C64ToggleWidget::leaveEvent(QEvent *event) {
    _hovered = false;
    update();
    QWidget::leaveEvent(event);
}

void C64ToggleWidget::onModeChanged(bool /*enabled*/) {
    updateTooltip();
    update();
}

void C64ToggleWidget::updateTooltip() {
    const bool active = c64Active();
    if (c64EmulationSelected()) {
        setToolTip(active
            ? tr("Authentic SID mode: ARMED — click to disarm.\n"
                 "Use Play/Stop to play the original .sid (libsidplayfp) from\n"
                 "the cursor, instead of the converted MIDI.")
            : tr("Authentic SID mode: OFF — click to arm.\n"
                 "Then Play plays the original .sid via libsidplayfp.\n"
                 "(Mode set in Settings → MIDI I/O → Commodore 64 / SID.)"));
    } else if (active) {
        setToolTip(tr("C64 SoundFont Mode: ON — click to disable.\n"
                      "Remaps to the C64 SoundFont's waveform presets. "
                      "Load a C64 .sf2 in the SoundFont manager first."));
    } else {
        setToolTip(tr("C64 SoundFont Mode: OFF — click to enable.\n"
                      "Plays imported SID tunes with real C64 timbres "
                      "(needs a C64 .sf2 loaded)."));
    }
}
