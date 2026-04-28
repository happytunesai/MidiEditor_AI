/*
 * MidiEditor
 * Copyright (C) 2010  Markus Schwenk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "FfxivToggleWidget.h"

#include "Appearance.h"
#include "FfxivSoundFontHelper.h"
#include "../midi/FluidSynthEngine.h"

#include <QMouseEvent>
#include <QPainter>
#include <QSettings>

static const int ICON_HEIGHT = 32;
static const int WIDGET_PADDING = 4;

FfxivToggleWidget::FfxivToggleWidget(QWidget *parent)
    : QWidget(parent)
{
    _iconOn = Appearance::adjustIconForDarkMode(
        QPixmap(":/run_environment/graphics/tool/XIV_on.png"), "XIV_on");
    _iconOff = Appearance::adjustIconForDarkMode(
        QPixmap(":/run_environment/graphics/tool/XIV_off.png"), "XIV_off");

    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(sizeHint());
    setCursor(Qt::PointingHandCursor);

#ifdef FLUIDSYNTH_SUPPORT
    FluidSynthEngine *engine = FluidSynthEngine::instance();
    if (engine) {
        connect(engine, &FluidSynthEngine::ffxivSoundFontModeChanged,
                this, &FfxivToggleWidget::onModeChanged);
        updateTooltip(engine->ffxivSoundFontMode());
    } else {
        updateTooltip(false);
    }
#else
    updateTooltip(false);
#endif
}

QSize FfxivToggleWidget::sizeHint() const {
    // Match MCP toggle dimensions exactly so toolbar buttons line up.
    return QSize(ICON_HEIGHT + WIDGET_PADDING * 2, ICON_HEIGHT + WIDGET_PADDING * 2);
}

QSize FfxivToggleWidget::minimumSizeHint() const {
    return sizeHint();
}

void FfxivToggleWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

#ifdef FLUIDSYNTH_SUPPORT
    FluidSynthEngine *engine = FluidSynthEngine::instance();
    bool enabled = engine && engine->ffxivSoundFontMode();
#else
    bool enabled = false;
#endif

    if (_hovered) {
        p.setPen(Qt::NoPen);
        QColor highlight = palette().highlight().color();
        highlight.setAlpha(40);
        p.setBrush(highlight);
        p.drawRoundedRect(rect(), 3, 3);
    }

    const QPixmap &icon = enabled ? _iconOn : _iconOff;
    if (!icon.isNull()) {
        QRect inner = rect().adjusted(WIDGET_PADDING, WIDGET_PADDING,
                                      -WIDGET_PADDING, -WIDGET_PADDING);
        QPixmap scaled = icon.scaled(inner.size(), Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        QPoint topLeft(inner.x() + (inner.width()  - scaled.width())  / 2,
                       inner.y() + (inner.height() - scaled.height()) / 2);
        p.drawPixmap(topLeft, scaled);
    }
}

void FfxivToggleWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
#ifdef FLUIDSYNTH_SUPPORT
        FluidSynthEngine *engine = FluidSynthEngine::instance();
        if (engine) {
            if (engine->ffxivSoundFontMode()) {
                FfxivSoundFontHelper::requestDisable(window());
            } else {
                FfxivSoundFontHelper::requestEnable(window());
            }
        }
#endif
    }
    QWidget::mousePressEvent(event);
}

void FfxivToggleWidget::enterEvent(QEnterEvent *event) {
    _hovered = true;
    update();
    QWidget::enterEvent(event);
}

void FfxivToggleWidget::leaveEvent(QEvent *event) {
    _hovered = false;
    update();
    QWidget::leaveEvent(event);
}

void FfxivToggleWidget::onModeChanged(bool enabled) {
    updateTooltip(enabled);
    update();
}

void FfxivToggleWidget::updateTooltip(bool enabled) {
    if (enabled) {
        setToolTip(tr("FFXIV SoundFont Mode: ON — click to disable"));
    } else {
        setToolTip(tr("FFXIV SoundFont Mode: OFF — click to enable"));
    }
}
