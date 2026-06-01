/*
 * MidiEditor AI - retro SF2 <-> EMU engine switch (Phase 42.3, 1.8.0).
 *
 * A small code-drawn toolbar rocker that selects the C64 playback engine
 * (SoundFont vs Emulation) without opening Settings. The active side glows;
 * clicking flips the engine via C64Mode::setMode (with the same handover the
 * Settings radios do). Stays in sync with the Settings radios / first-use
 * picker via C64Mode::Notifier::modeChanged. Does NOT activate playback - the
 * C64 logo button (C64ToggleWidget) still arms/toggles the chosen engine.
 */
#ifndef C64MODESWITCHWIDGET_H
#define C64MODESWITCHWIDGET_H

#include <QPointer>
#include <QWidget>

class QAction;

class C64ModeSwitchWidget : public QWidget {
    Q_OBJECT
public:
    explicit C64ModeSwitchWidget(QWidget *parent = nullptr);

    /// Give the widget the QToolBar action returned by addWidget(), so
    /// updateVisibility() can hide it via the *action* (a QToolBar overrides a
    /// plain widget->setVisible(), so toggling the action is what reclaims the
    /// toolbar space).
    void setToolbarAction(QAction *action);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void updateTooltip();
    /// Show the switch only while C64 mode is actually active (SoundFont mode on
    /// or Emulation armed); hidden otherwise so it doesn't clutter the toolbar.
    void updateVisibility();
    QPointer<QAction> _action;  // the QToolBar widget-action wrapping this widget
    bool _hovered = false;
};

#endif // C64MODESWITCHWIDGET_H
