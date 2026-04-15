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

#include "McpToggleWidget.h"
#include "Appearance.h"
#include "../ai/McpServer.h"

#include <QPainter>
#include <QMouseEvent>
#include <QSettings>
#include <QToolTip>

static const int ICON_SIZE = 32;
static const int WIDGET_PADDING = 4;

McpToggleWidget::McpToggleWidget(McpServer *server, QWidget *parent)
    : QWidget(parent), _server(server)
{
    _iconOn = Appearance::adjustIconForDarkMode(":/run_environment/graphics/tool/mcp_on.png");
    _iconOff = Appearance::adjustIconForDarkMode(":/run_environment/graphics/tool/mcp_off.png");

    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(sizeHint());
    setCursor(Qt::PointingHandCursor);

    if (_server) {
        connect(_server, &McpServer::started, this, &McpToggleWidget::onServerStarted);
        connect(_server, &McpServer::stopped, this, &McpToggleWidget::onServerStopped);
    }

    updateTooltip();
}

QSize McpToggleWidget::sizeHint() const {
    return QSize(ICON_SIZE + WIDGET_PADDING * 2, ICON_SIZE + WIDGET_PADDING * 2);
}

QSize McpToggleWidget::minimumSizeHint() const {
    return sizeHint();
}

void McpToggleWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    bool running = _server && _server->isRunning();
    const QIcon &icon = running ? _iconOn : _iconOff;

    QRect iconRect(WIDGET_PADDING, WIDGET_PADDING, ICON_SIZE, ICON_SIZE);

    // Draw hover highlight
    if (_hovered) {
        p.setPen(Qt::NoPen);
        QColor highlight = palette().highlight().color();
        highlight.setAlpha(40);
        p.setBrush(highlight);
        p.drawRoundedRect(rect(), 3, 3);
    }

    icon.paint(&p, iconRect);
}

void McpToggleWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && _server) {
        if (_server->isRunning()) {
            _server->stop();
            // Update settings to reflect disabled state
            QSettings settings;
            settings.setValue("MCP/enabled", false);
        } else {
            QSettings settings;
            quint16 port = static_cast<quint16>(settings.value("MCP/port", 9420).toInt());
            QString token = settings.value("MCP/auth_token").toString();
            if (!token.isEmpty()) {
                _server->setAuthToken(token);
            }
            _server->start(port);
            settings.setValue("MCP/enabled", true);
        }
    }
    QWidget::mousePressEvent(event);
}

void McpToggleWidget::enterEvent(QEnterEvent *event) {
    _hovered = true;
    update();
    QWidget::enterEvent(event);
}

void McpToggleWidget::leaveEvent(QEvent *event) {
    _hovered = false;
    update();
    QWidget::leaveEvent(event);
}

void McpToggleWidget::onServerStarted() {
    updateTooltip();
    update();
}

void McpToggleWidget::onServerStopped() {
    updateTooltip();
    update();
}

void McpToggleWidget::updateTooltip() {
    if (_server && _server->isRunning()) {
        setToolTip(tr("MCP Server running on port %1 - click to stop").arg(_server->port()));
    } else {
        setToolTip(tr("MCP Server stopped - click to start"));
    }
}
