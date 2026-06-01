/*
 * MidiEditor AI - retro SF2 <-> EMU engine switch implementation (see header).
 */
#include "C64ModeSwitchWidget.h"

#include "C64Mode.h"
#include "Appearance.h"
#include "../midi/SidAudioPlayer.h"
#ifdef FLUIDSYNTH_SUPPORT
#include "../midi/FluidSynthEngine.h"
#endif

#include <QAction>
#include <QFont>
#include <QLinearGradient>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QRadialGradient>
#include <QStatusBar>

namespace {
const int WIDGET_W = 80;
const int WIDGET_H = 30;
const int PAD      = 2;

// Mirrors C64ToggleWidget's "active" notion: in Emulation the mode is active
// when armed; in SoundFont when the engine's C64 mode is on. Uses the EFFECTIVE
// engine (isEmulationActive), so when "emulation" is chosen but no .sid is open
// it tracks the SoundFont state instead of a SID that can't play.
bool c64IsActive() {
    if (C64Mode::isEmulationActive())
        return SidAudioPlayer::instance()->isArmed();
#ifdef FLUIDSYNTH_SUPPORT
    FluidSynthEngine *e = FluidSynthEngine::instance();
    return e && e->c64SoundFontMode();
#else
    return false;
#endif
}
}

C64ModeSwitchWidget::C64ModeSwitchWidget(QWidget *parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(sizeHint());
    setCursor(Qt::PointingHandCursor);
    // Repaint + re-evaluate visibility whenever the engine choice changes
    // anywhere (settings / dialog / this switch).
    connect(C64Mode::Notifier::instance(), &C64Mode::Notifier::modeChanged,
            this, [this] { updateTooltip(); update(); updateVisibility(); });
    // Activation state drives visibility: armed (Emulation) / c64 mode on (SF).
    connect(SidAudioPlayer::instance(), &SidAudioPlayer::armedChanged,
            this, [this] { updateVisibility(); });
    // A file load changes whether a .sid is open -> Emulation may become (un)
    // available; re-paint (EMU half locks/unlocks) and re-gate visibility.
    connect(SidAudioPlayer::instance(), &SidAudioPlayer::sourceChanged,
            this, [this] { updateTooltip(); update(); updateVisibility(); });
#ifdef FLUIDSYNTH_SUPPORT
    if (FluidSynthEngine *e = FluidSynthEngine::instance())
        connect(e, &FluidSynthEngine::c64SoundFontModeChanged,
                this, [this] { updateVisibility(); });
#endif
    updateTooltip();
    updateVisibility(); // start hidden unless C64 is already active
}

void C64ModeSwitchWidget::setToolbarAction(QAction *action) {
    _action = action;
    updateVisibility();
}

void C64ModeSwitchWidget::updateVisibility() {
    const bool active = c64IsActive();
    if (_action)
        _action->setVisible(active); // authoritative in a QToolBar
    else
        setVisible(active);          // fallback before the action is wired
}

QSize C64ModeSwitchWidget::sizeHint() const { return QSize(WIDGET_W, WIDGET_H); }
QSize C64ModeSwitchWidget::minimumSizeHint() const { return sizeHint(); }

void C64ModeSwitchWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    const bool dark = Appearance::shouldUseDarkMode();
    // Emulation needs a .sid; without one the switch is locked to SoundFont and
    // the EMU side is drawn disabled. The lit side follows the *effective*
    // engine, so a persisted "emulation" with no .sid still shows SF2 lit.
    const bool emuAvail = C64Mode::emulationAvailable();
    const bool emu = C64Mode::isEmulation() && emuAvail;
    QColor accent = palette().highlight().color();

    // Track (pill).
    QRectF track = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    const double tr = track.height() / 2.0;
    QColor bg     = dark ? QColor(0x14, 0x18, 0x20) : QColor(0xe9, 0xec, 0xf1);
    QColor border = dark ? QColor(0x3a, 0x40, 0x4a) : QColor(0xc2, 0xc8, 0xd0);
    p.setPen(QPen(border, 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(track, tr, tr);

    // Active half geometry.
    QRectF inner = track.adjusted(PAD, PAD, -PAD, -PAD);
    const double hw = inner.width() / 2.0;
    QRectF activeRect(emu ? inner.left() + hw : inner.left(), inner.top(), hw, inner.height());
    const double ar = activeRect.height() / 2.0;

    // Soft glow behind the lit side ("leuchten").
    QRadialGradient glow(activeRect.center(), activeRect.width() * 0.85);
    QColor g = accent;
    g.setAlpha(150); glow.setColorAt(0.0, g);
    g.setAlpha(45);  glow.setColorAt(0.6, g);
    g.setAlpha(0);   glow.setColorAt(1.0, g);
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawRoundedRect(track.adjusted(-1, -1, 1, 1), tr, tr);

    // Lit knob over the active side.
    QLinearGradient lit(activeRect.topLeft(), activeRect.bottomLeft());
    lit.setColorAt(0.0, accent.lighter(118));
    lit.setColorAt(1.0, accent.darker(115));
    p.setPen(QPen(accent.darker(140), 0.8));
    p.setBrush(lit);
    p.drawRoundedRect(activeRect, ar, ar);

    // Labels: SF2 (left), EMU (right) - the active side bright.
    QFont f = font();
    f.setPointSizeF(7.5);
    f.setBold(true);
    p.setFont(f);
    const QColor onText  = QColor(0xff, 0xff, 0xff);
    const QColor offText = dark ? QColor(0x8c, 0x94, 0x9e) : QColor(0x6c, 0x74, 0x7e);
    // EMU is unusable without a .sid -> draw it clearly disabled (extra faint).
    const QColor lockedText = dark ? QColor(0x4a, 0x50, 0x5a) : QColor(0xb0, 0xb6, 0xbe);
    QRectF leftZone(track.left(), track.top(), track.width() / 2.0, track.height());
    QRectF rightZone(track.left() + track.width() / 2.0, track.top(),
                     track.width() / 2.0, track.height());
    p.setPen(emu ? offText : onText);
    p.drawText(leftZone, Qt::AlignCenter, QStringLiteral("SF2"));
    p.setPen(!emuAvail ? lockedText : (emu ? onText : offText));
    p.drawText(rightZone, Qt::AlignCenter, QStringLiteral("EMU"));

    if (_hovered) {
        QColor h = accent;
        h.setAlpha(36);
        p.setPen(Qt::NoPen);
        p.setBrush(h);
        p.drawRoundedRect(track, tr, tr);
    }
}

void C64ModeSwitchWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // Emulation needs an open .sid; without one the switch is locked to
        // SoundFont. Don't flip - just explain (the persisted preference is
        // kept, so it returns when a .sid is opened again).
        if (!C64Mode::emulationAvailable()) {
            if (QMainWindow *mw = qobject_cast<QMainWindow *>(window()))
                mw->statusBar()->showMessage(
                    tr("Emulation needs an imported .sid file — C64 stays on SoundFont (chiptune)."),
                    4000);
            QWidget::mousePressEvent(event);
            return;
        }
        const bool wasChosen = C64Mode::isChosen();
        // First ever C64 interaction: the picker sets the mode (don't also flip).
        C64Mode::ensureChosen(window());
        if (wasChosen) {
            C64Mode::setMode(C64Mode::isEmulation() ? QStringLiteral("soundfont")
                                                    : QStringLiteral("emulation"),
                             window());
        }
        updateTooltip();
        update();
    }
    QWidget::mousePressEvent(event);
}

void C64ModeSwitchWidget::enterEvent(QEnterEvent *event) {
    _hovered = true;
    update();
    QWidget::enterEvent(event);
}

void C64ModeSwitchWidget::leaveEvent(QEvent *event) {
    _hovered = false;
    update();
    QWidget::leaveEvent(event);
}

void C64ModeSwitchWidget::updateTooltip() {
    if (!C64Mode::emulationAvailable()) {
        setToolTip(tr("C64 engine: SoundFont (converted MIDI with C64 timbres).\n"
                      "Emulation is locked — it needs an imported .sid file.\n"
                      "Open a .sid to switch to authentic libsidplayfp playback."));
        return;
    }
    setToolTip(C64Mode::isEmulation()
        ? tr("C64 engine: Emulation (plays the original .sid).\n"
             "Click to switch to SoundFont (converted MIDI with C64 timbres).\n"
             "The C64 toolbar button still activates playback.")
        : tr("C64 engine: SoundFont (converted MIDI with C64 timbres).\n"
             "Click to switch to Emulation (plays the original .sid).\n"
             "The C64 toolbar button still activates playback."));
}
