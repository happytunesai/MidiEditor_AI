/*
 * MidiEditor AI - C64 SoundFont Mode toolbar toggle (Phase 42.2).
 *
 * Compact toolbar button that shows C64 SoundFont Mode state and toggles it
 * on click - mirrors FfxivToggleWidget. Active = the original colour c64.png
 * with a glow; inactive = the dark-mode-adjusted (light/grey) silhouette,
 * dimmed. Stays in sync with FluidSynthEngine via c64SoundFontModeChanged.
 */

#ifndef C64TOGGLEWIDGET_H
#define C64TOGGLEWIDGET_H

#include <QPixmap>
#include <QWidget>

class C64ToggleWidget : public QWidget {
    Q_OBJECT

public:
    explicit C64ToggleWidget(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private slots:
    void onModeChanged(bool enabled);

private:
    void updateTooltip();

    QPixmap _iconOn;   // original colour icon, shown when active
    QPixmap _iconOff;  // dark-mode-adjusted (light/grey) silhouette, shown when off
    bool _hovered = false;
};

#endif // C64TOGGLEWIDGET_H
