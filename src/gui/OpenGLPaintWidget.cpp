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

#include "OpenGLPaintWidget.h"
#include "Appearance.h"
#include <QApplication>
#include <QDebug>
#include <QCursor>

OpenGLPaintWidget::OpenGLPaintWidget(QSettings *settings, QWidget *parent)
    : QOpenGLWidget(parent), _settings(settings), _paintDevice(nullptr) {
    // Initialize mouse tracking and state (same as PaintWidget)
    setMouseTracking(true);
    mouseOver = false;
    mousePressed = false;
    mouseReleased = false;
    repaintOnMouseMove = false;
    repaintOnMousePress = false;
    repaintOnMouseRelease = false;
    inDrag = false;
    mousePinned = false;
    mouseX = 0;
    mouseY = 0;
    mouseLastY = 0;
    mouseLastX = 0;
    enabled = true;

    // Configure OpenGL widget for optimal 2D rendering and high DPI support
    // Use NoPartialUpdate for immediate, responsive rendering needed by interactive tools
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

    // G10: the wrapper is the VISIBLE editor pane in hardware-acceleration mode
    // and the widget it hosts is a hidden child, which Qt refuses to focus. With
    // QWidget's default Qt::NoFocus the editor area could therefore never take
    // keyboard focus at all: once focus landed in a line edit or combo box,
    // nothing the user clicked in the piano roll or the velocity lane could get
    // it back, and piano emulation stayed dead for the rest of the session.
    // Qt::ClickFocus is exactly what the hosted widgets set for themselves
    // (MatrixWidget.cpp, MiscWidget.cpp), so this restores software parity and
    // makes this class's key handlers reachable.
    setFocusPolicy(Qt::ClickFocus);

    // Ensure proper high DPI handling
    setAttribute(Qt::WA_AcceptTouchEvents, false);
    setAttribute(Qt::WA_AlwaysShowToolTips, true);

    qDebug() << "OpenGLPaintWidget: Created with hardware acceleration support";
}

void OpenGLPaintWidget::initializeGL() {
    qDebug() << "OpenGLPaintWidget: Initializing OpenGL for hardware acceleration";

    // Get OpenGL context and functions
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if (!context) {
        qWarning() << "OpenGLPaintWidget: No OpenGL context available";
        return;
    }

    QOpenGLFunctions *f = context->functions();
    QSurfaceFormat format = context->format();

    qDebug() << "OpenGLPaintWidget: OpenGL Version:" << format.majorVersion() << "." << format.minorVersion();
    qDebug() << "OpenGLPaintWidget: OpenGL Profile:" << format.profile();

    // Set up OpenGL state for optimal 2D rendering performance
    f->glEnable(GL_BLEND);
    f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    f->glDisable(GL_DEPTH_TEST);
    f->glDisable(GL_CULL_FACE);

    // Enable multisampling if available for better visual quality
    if (format.samples() > 1) {
        f->glEnable(GL_MULTISAMPLE);
    }

    // Optimize for 2D rendering performance
    f->glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
    f->glHint(GL_POINT_SMOOTH_HINT, GL_FASTEST);
    f->glHint(GL_LINE_SMOOTH_HINT, GL_FASTEST);

    // initializeGL() runs again whenever Qt recreates the context (the widget
    // was moved to another top-level window). A device from the previous
    // context must not simply be overwritten (SP-02): its context is gone, so
    // plain delete is all that is left to do - normally aboutToBeDestroyed
    // released it already and this is a no-op.
    if (_paintDevice) {
        delete _paintDevice;
        _paintDevice = nullptr;
    }

    // Create OpenGL paint device for QPainter acceleration
    _paintDevice = new QOpenGLPaintDevice();
    if (!_paintDevice) {
        qWarning() << "OpenGLPaintWidget: Failed to create QOpenGLPaintDevice";
        return;
    }

    // Release the device while THIS context is still alive - the hook Qt
    // documents for QOpenGLWidget resources. Direct connection: the handler
    // must run before the native context goes away. (`context` is the local
    // above = the widget's own context, current during initializeGL().)
    connect(context, &QOpenGLContext::aboutToBeDestroyed,
            this, &OpenGLPaintWidget::releaseGlResources,
            Qt::DirectConnection);

    // Configure the paint device in DEVICE pixels plus the real device pixel
    // ratio (see glPaintDeviceSize()); QPainter's logical coordinates are then
    // identical to the hosted widget's own, at any scaling factor.
    applyPaintDeviceGeometry(size(), devicePixelRatioF());
}

void OpenGLPaintWidget::applyPaintDeviceGeometry(const QSize &logicalSize, qreal dpr) {
    if (!_paintDevice) {
        return;
    }

    _paintDevice->setSize(glPaintDeviceSize(logicalSize, dpr));
    _paintDevice->setDevicePixelRatio(dpr > 0.0 ? dpr : 1.0);

    // Keep Qt widget coordinate system (not flipped) for compatibility
    _paintDevice->setPaintFlipped(false);

    _lastPaintSize = logicalSize;
    _lastPaintDpr = dpr;
}

void OpenGLPaintWidget::paintGL() {
    // GLBLANK-001: paintContent() draws a HIDDEN child widget through
    // QWidget::render(). Qt reacts to a hidden widget in prepareToRender() by
    // walking the ancestor chain, clearing WA_WState_Hidden on every hidden
    // ancestor - and setting it back again afterwards. Two things follow, both
    // observed in the field with "Enable GPU acceleration for MIDI events" on:
    //
    //  1. That pass also invalidates the ancestors' layouts, so a full layout
    //     run can happen INSIDE our paint and resize us. Qt then paints us
    //     again, re-entering paintGL() while our QPainter still owns
    //     _paintDevice; the nested QPainter::begin() fails with "a paint
    //     device can only be painted by one painter at a time".
    //  2. During the window's show sequence Qt paints us once while we are not
    //     yet visible. At that moment our own ancestors are still flagged
    //     hidden, so prepareToRender re-hides them when it is done - for good.
    //     The editor group then keeps ZERO WIDTH for the whole session, which
    //     is why the piano roll AND the group's tab strip stayed invisible
    //     while the docks around them rendered normally.
    //
    // Both cases must DEFER the frame, never drop it: the dropped frame was
    // the only one those events ever scheduled.
    if (!canPaintNow(_inPaintGL, isVisible())) {
        requestDeferredRepaint();
        return;
    }

    if (!_paintDevice) {
        // Fallback: clear to background color
        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        QOpenGLFunctions *f = ctx ? ctx->functions() : nullptr;
        if (f) {
            f->glClearColor(0.9f, 0.9f, 0.9f, 1.0f);
            f->glClear(GL_COLOR_BUFFER_BIT);
        }
        return;
    }

    // PERFORMANCE: Minimize OpenGL state changes and allocations
    // Update paint device geometry only when needed to reduce GPU memory
    // allocations. The device pixel ratio has to take part in the test: moving
    // the window to a monitor with different scaling changes it while leaving
    // the logical size untouched, and a stale ratio makes us paint into a
    // fraction of the framebuffer for the rest of the session.
    const QSize currentSize = size();
    const qreal currentDpr = devicePixelRatioF();
    if (_lastPaintSize != currentSize || !qFuzzyCompare(_lastPaintDpr, currentDpr)) {
        applyPaintDeviceGeometry(currentSize, currentDpr);
    }

    // Create OpenGL-accelerated QPainter. The scope guard resets the flag on
    // EVERY exit path, so one failed frame can never latch the widget into a
    // permanent "already painting" state.
    struct PaintScope {
        bool &flag;
        explicit PaintScope(bool &f) : flag(f) { flag = true; }
        ~PaintScope() { flag = false; }
    } paintScope(_inPaintGL);

    QPainter painter(_paintDevice);
    if (!painter.isActive()) {
        // Busy for a reason the guard above does not cover. Same rule: ask for
        // another frame instead of leaving the widget blank until something
        // else happens to repaint it.
        qWarning() << "OpenGLPaintWidget: Failed to create active OpenGL painter";
        requestDeferredRepaint();
        return;
    }

    // Configure painter with hardware-specific settings. _settings is the
    // application-wide object, so these are plain lookups - never construct a
    // QSettings per frame here.
    bool hardwareSmoothTransforms = true;
    bool antialiasing = true;
    if (_settings) {
        hardwareSmoothTransforms = _settings->value("rendering/hardware_smooth_transforms", true).toBool();
        // G18: honour the user's anti-aliasing preference instead of forcing it
        // on. MSAA does not make this setting redundant - it only touches
        // primitives drawn into the multisampled framebuffer, never the
        // already-rasterised grid pixmap the hosted widget blits.
        antialiasing = _settings->value("rendering/antialiasing", true).toBool();
    }

    painter.setRenderHint(QPainter::Antialiasing, antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::VerticalSubpixelPositioning, true);

    // Use hardware smooth transforms setting (user preference for GPU texture filtering)
    painter.setRenderHint(QPainter::SmoothPixmapTransform, hardwareSmoothTransforms);

    // Enable high-quality rendering for OpenGL
    painter.setRenderHint(QPainter::LosslessImageRendering, true);

    // Call the subclass's paint implementation with OpenGL-accelerated painter
    paintContent(&painter);

    painter.end();
}

void OpenGLPaintWidget::requestDeferredRepaint() {
    // update() called from inside a paint is swallowed by Qt, so the request
    // has to land after the current paint returned. One pending frame is
    // enough - without the flag a widget that defers on every frame would
    // queue an unbounded repaint storm.
    if (_repaintPending) {
        return;
    }
    _repaintPending = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            _repaintPending = false;
            update();
        },
        Qt::QueuedConnection);
}

void OpenGLPaintWidget::resizeGL(int w, int h) {
    // Call base class to ensure proper OpenGL setup
    QOpenGLWidget::resizeGL(w, h);

    // Derive both the paint device and the viewport from size()/devicePixelRatioF()
    // rather than from w/h, so the code is correct whichever unit Qt hands in.
    const QSize logicalSize = size();
    const qreal dpr = devicePixelRatioF();
    applyPaintDeviceGeometry(logicalSize, dpr);

    // glViewport takes DEVICE pixels. QOpenGLWidget's framebuffer is
    // size() * devicePixelRatio, so a logical-size viewport would squeeze the
    // whole editor into a corner of the surface on any scaled display.
    const QSize deviceSize = glPaintDeviceSize(logicalSize, dpr);
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    QOpenGLFunctions *f = ctx ? ctx->functions() : nullptr;
    if (f) {
        f->glViewport(0, 0, deviceSize.width(), deviceSize.height());
    }
}

// === Event Forwarding Infrastructure ===

bool OpenGLPaintWidget::deliverToHosted(QEvent *event) {
    if (!event) {
        return false;
    }

    // Re-entry: Qt propagated an event the hidden child left un-accepted back
    // up the parent chain to us. Forwarding it again is what blew the stack in
    // 1.8.1.1 (right-click) and still did for every wheel notch over the
    // velocity lane. Refuse instead.
    if (_forwardingEvent) {
        return false;
    }

    QWidget *hosted = hostedWidget();
    if (!hosted) {
        return false;
    }

    struct ForwardScope {
        bool &flag;
        explicit ForwardScope(bool &f) : flag(f) { flag = true; }
        ~ForwardScope() { flag = false; }
    } scope(_forwardingEvent);

    return QApplication::sendEvent(hosted, event);
}

bool OpenGLPaintWidget::forwardToHosted(QEvent *event) {
    const bool delivered = deliverToHosted(event);
    if (event) {
        event->accept();
    }
    return delivered;
}

bool OpenGLPaintWidget::event(QEvent *e) {
    // G11: Qt hit-tests only visible widgets, so the QHelpEvent for a hover
    // over this wrapper is delivered here and never reaches the hidden widget
    // whose content the user is actually pointing at (which is where the
    // tooltip logic lives). Forward it, but only claim the event when the
    // hosted widget really showed something - otherwise let Qt keep looking up
    // the parent chain the way it would without this wrapper.
    if (e && e->type() == QEvent::ToolTip && hostedWidget()) {
        if (deliverToHosted(e) && e->isAccepted()) {
            return true;
        }
        // Not handled. The ignored QHelpEvent Qt now propagates from the hidden
        // child arrives back here, but deliverToHosted()'s latch refuses the
        // second forward, so this cannot loop.
    }

    return QOpenGLWidget::event(e);
}

// === Mouse Event Handlers (identical to PaintWidget) ===

void OpenGLPaintWidget::mouseMoveEvent(QMouseEvent *event) {
    mouseOver = true;

    if (mousePinned) {
        // do not change mousePosition but lastMousePosition to get the
        // correct move distance
        QCursor::setPos(mapToGlobal(QPoint(mouseX, mouseY)));
        mouseLastX = 2 * mouseX - qRound(event->position().x());
        mouseLastY = 2 * mouseY - qRound(event->position().y());
    } else {
        mouseLastX = mouseX;
        mouseLastY = mouseY;
        mouseX = qRound(event->position().x());
        mouseY = qRound(event->position().y());
    }
    if (mousePressed) {
        inDrag = true;
    }

    if (!enabled) {
        return;
    }

    if (repaintOnMouseMove) {
        update();
    }
}

void OpenGLPaintWidget::enterEvent(QEnterEvent *event) {
    mouseOver = true;

    if (!enabled) {
        return;
    }

    update();
}

void OpenGLPaintWidget::leaveEvent(QEvent *event) {
    mouseOver = false;

    if (!enabled) {
        return;
    }

    update();
}

void OpenGLPaintWidget::mousePressEvent(QMouseEvent *event) {
    mousePressed = true;
    mouseReleased = false;

    if (!enabled) {
        return;
    }

    if (repaintOnMousePress) {
        update();
    }
}

void OpenGLPaintWidget::mouseReleaseEvent(QMouseEvent *event) {
    inDrag = false;
    mouseReleased = true;
    mousePressed = false;

    if (!enabled) {
        return;
    }

    if (repaintOnMouseRelease) {
        update();
    }
}

// === Geometric Testing Methods (identical to PaintWidget) ===

bool OpenGLPaintWidget::mouseInRect(int x, int y, int width, int height) {
    return mouseBetween(x, y, x + width, y + height);
}

bool OpenGLPaintWidget::mouseInRect(QRectF rect) {
    return mouseInRect(rect.x(), rect.y(), rect.width(), rect.height());
}

bool OpenGLPaintWidget::mouseBetween(int x1, int y1, int x2, int y2) {
    int temp;
    if (x1 > x2) {
        temp = x1;
        x1 = x2;
        x2 = temp;
    }
    if (y1 > y2) {
        temp = y1;
        y1 = y2;
        y2 = temp;
    }
    return mouseOver && mouseX >= x1 && mouseX <= x2 && mouseY >= y1 && mouseY <= y2;
}

int OpenGLPaintWidget::draggedX() {
    if (!inDrag) {
        return 0;
    }
    int i = mouseX - mouseLastX;
    mouseLastX = mouseX;
    return i;
}

int OpenGLPaintWidget::draggedY() {
    if (!inDrag) {
        return 0;
    }
    int i = mouseY - mouseLastY;
    mouseLastY = mouseY;
    return i;
}

void OpenGLPaintWidget::setRepaintOnMouseMove(bool b) {
    repaintOnMouseMove = b;
}

void OpenGLPaintWidget::setRepaintOnMousePress(bool b) {
    repaintOnMousePress = b;
}

void OpenGLPaintWidget::setRepaintOnMouseRelease(bool b) {
    repaintOnMouseRelease = b;
}

void OpenGLPaintWidget::releaseGlResources() {
    if (!_paintDevice) {
        return;
    }
    // Only OUR context, made current through QOpenGLWidget::makeCurrent()
    // (which pairs the context with the widget's own offscreen surface).
    // The previous cleanup took QOpenGLContext::currentContext() - whatever
    // context another widget had left current - deleted the device against
    // it and even called doneCurrent() on that foreign context.
    QOpenGLContext *own = context();
    const bool ownContextAlive = own && own->isValid();
    if (ownContextAlive) {
        makeCurrent();
    }
    delete _paintDevice;
    _paintDevice = nullptr;
    if (ownContextAlive) {
        doneCurrent();
    }
}

OpenGLPaintWidget::~OpenGLPaintWidget() {
    // QOpenGLWidget's destructor (which runs AFTER this body) destroys the
    // context and emits aboutToBeDestroyed - by then the derived part of this
    // object is gone, so the slot must not fire any more.
    if (QOpenGLContext *own = context()) {
        disconnect(own, &QOpenGLContext::aboutToBeDestroyed,
                   this, &OpenGLPaintWidget::releaseGlResources);
    }
    // The context is still alive here (Qt tears it down in the base class
    // destructor), so the device is released on its own context. When
    // aboutToBeDestroyed already ran (context recreated earlier), this is a
    // no-op.
    releaseGlResources();
}
