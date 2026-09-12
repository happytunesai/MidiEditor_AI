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

#ifndef OPENGLPAINTWIDGET_H_
#define OPENGLPAINTWIDGET_H_

// Qt includes
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <QSettings>

/**
 * \class OpenGLPaintWidget
 *
 * \brief OpenGL-accelerated version of PaintWidget for hardware-accelerated rendering.
 *
 * OpenGLPaintWidget provides the same interface as PaintWidget but with OpenGL
 * hardware acceleration. It's designed as a drop-in replacement that provides:
 *
 * - **Hardware Acceleration**: Uses QOpenGLWidget for GPU-accelerated rendering
 * - **Compatible Interface**: Same API as PaintWidget for easy migration
 * - **Enhanced Performance**: GPU-accelerated QPainter operations
 * - **Automatic Fallback**: Graceful fallback to software rendering if needed
 *
 * Key features:
 * - Identical mouse event handling to PaintWidget
 * - OpenGL-accelerated QPainter through QOpenGLPaintDevice
 * - Configurable render hints for optimal quality/performance balance
 * - Seamless integration with existing widget code
 */
class OpenGLPaintWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    /**
     * \brief Creates a new OpenGL-accelerated PaintWidget.
     * \param settings Application settings for configuration
     * \param parent The parent widget
     */
    OpenGLPaintWidget(QSettings *settings, QWidget *parent = nullptr);

    /**
     * \brief Destructor.
     */
    virtual ~OpenGLPaintWidget();

    /**
     * \brief Sets whether to repaint on mouse move events.
     * \param b True to enable repainting on mouse moves
     */
    void setRepaintOnMouseMove(bool b);

    /**
     * \brief Sets whether to repaint on mouse press events.
     * \param b True to enable repainting on mouse press
     */
    void setRepaintOnMousePress(bool b);

    /**
     * \brief Sets whether to repaint on mouse release events.
     * \param b True to enable repainting on mouse release
     */
    void setRepaintOnMouseRelease(bool b);

    // === Mouse Position and State ===

    /**
     * \brief Gets the current mouse X coordinate.
     * \return Mouse X position in widget coordinates
     */
    int getMouseX() const { return mouseX; }

    /**
     * \brief Gets the current mouse Y coordinate.
     * \return Mouse Y position in widget coordinates
     */
    int getMouseY() const { return mouseY; }

    /**
     * \brief Gets the previous mouse X coordinate.
     * \return Previous mouse X position
     */
    int getMouseLastX() const { return mouseLastX; }

    /**
     * \brief Gets the previous mouse Y coordinate.
     * \return Previous mouse Y position
     */
    int getMouseLastY() const { return mouseLastY; }

    /**
     * \brief Checks if mouse is currently over the widget.
     * \return True if mouse is over the widget
     */
    bool isMouseOver() const { return mouseOver; }

    /**
     * \brief Checks if mouse button is currently pressed.
     * \return True if mouse is pressed
     */
    bool isMousePressed() const { return mousePressed; }

    /**
     * \brief Checks if mouse was recently released.
     * \return True if mouse was released
     */
    bool isMouseReleased() const { return mouseReleased; }

    /**
     * \brief Checks if a drag operation is in progress.
     * \return True if dragging
     */
    bool isInDrag() const { return inDrag; }

    /**
     * \brief Gets the distance dragged in X direction since last call.
     * \return X drag distance in pixels
     */
    int draggedX();

    /**
     * \brief Gets the distance dragged in Y direction since last call.
     * \return Y drag distance in pixels
     */
    int draggedY();

    // === Geometric Testing ===

    /**
     * \brief Tests if mouse is within a rectangular area.
     * \param x Rectangle X coordinate
     * \param y Rectangle Y coordinate
     * \param width Rectangle width
     * \param height Rectangle height
     * \return True if mouse is within the rectangle
     */
    bool mouseInRect(int x, int y, int width, int height);

    /**
     * \brief Tests if mouse is within a rectangular area.
     * \param rect The rectangle to test
     * \return True if mouse is within the rectangle
     */
    bool mouseInRect(QRectF rect);

    /**
     * \brief Tests if mouse is between two points.
     * \param x1 First point X coordinate
     * \param y1 First point Y coordinate
     * \param x2 Second point X coordinate
     * \param y2 Second point Y coordinate
     * \return True if mouse is between the points
     */
    bool mouseBetween(int x1, int y1, int x2, int y2);

    /**
     * \brief Sets mouse pinning state for constrained operations.
     * \param b True to pin the mouse, false to unpin
     */
    void setMousePinned(bool b) { mousePinned = b; }

    /**
     * \brief Sets the widget enabled state.
     * \param enabled True to enable the widget
     */
    void setEnabled(bool enabled) { this->enabled = enabled; }

    /**
     * \brief Gets the widget enabled state.
     * \return True if widget is enabled
     */
    bool isEnabled() const { return enabled; }

    /**
     * \brief GLBLANK-001: the rule deciding whether a frame may be painted
     *  right now. Factored out of paintGL() so it can be unit-tested without
     *  an OpenGL context (tests/test_opengl_paint_guard.cpp).
     * \param alreadyPainting True while an outer paintGL() still owns the
     *  paint device - a layout pass inside paintContent() can re-enter.
     * \param widgetVisible The widget's QWidget::isVisible().
     * \return True only when painting is safe. When false the caller MUST
     *  defer the frame (requestDeferredRepaint), never drop it.
     */
    static bool canPaintNow(bool alreadyPainting, bool widgetVisible) {
        return !alreadyPainting && widgetVisible;
    }

    /**
     * \brief The QOpenGLPaintDevice size, in DEVICE pixels, for a widget of
     *  \a logicalSize on a screen whose device pixel ratio is \a dpr.
     *
     * QOpenGLWidget hands us a framebuffer of size() * devicePixelRatio, so the
     * paint device must be described in device pixels *plus* the real ratio.
     * Pinning it to the logical size with a ratio of 1.0 (as this class used to
     * do) confines the whole editor to a 1/dpr corner of the surface as soon as
     * the window lands on a scaled monitor. Passing the ratio separately keeps
     * QPainter's logical coordinate system unchanged, which is what the hosted
     * MatrixWidget / MiscWidget draw in.
     *
     * Static and pure so it can be unit-tested without an OpenGL context
     * (tests/test_opengl_paint_guard.cpp).
     */
    static QSize glPaintDeviceSize(const QSize &logicalSize, qreal dpr) {
        if (dpr <= 0.0) {
            return logicalSize;
        }
        return QSize(qRound(logicalSize.width() * dpr),
                     qRound(logicalSize.height() * dpr));
    }

protected:
    // === OpenGL Methods ===

    /**
     * \brief Initializes OpenGL context and resources.
     */
    void initializeGL() override;

    /**
     * \brief Frees the paint device with THIS widget's context current.
     *        Connected to the context's aboutToBeDestroyed (Qt recreates the
     *        context when the widget moves to another top-level window) and
     *        called from the destructor - never with whatever context happens
     *        to be current at the time (SP-02, external review 2026-09-06).
     */
    void releaseGlResources();

    /**
     * \brief Handles OpenGL rendering.
     */
    void paintGL() override;

    /**
     * \brief Handles OpenGL viewport resizing.
     * \param w New width
     * \param h New height
     */
    void resizeGL(int w, int h) override;

    // === Event Forwarding Infrastructure ===

    /**
     * \brief The hidden widget this wrapper renders and forwards input to.
     * \return The hosted widget, or nullptr for a wrapper that hosts nothing.
     *
     * Subclasses that composite a hidden child (OpenGLMatrixWidget,
     * OpenGLMiscWidget) return it here so the base class can implement the
     * forwarding rules once - see forwardToHosted() and event().
     */
    virtual QWidget *hostedWidget() const { return nullptr; }

    /**
     * \brief Sends \a event to hostedWidget() exactly once, without touching
     *  its accepted state.
     * \return True when the event was delivered.
     *
     * GLCTX-001 / G2: the hosted widget is a HIDDEN CHILD of this wrapper. When
     * its handler leaves an event un-accepted - which every QWidget base
     * handler does, because their whole body is event->ignore() - Qt propagates
     * the event UP the parent chain, i.e. straight back into this wrapper,
     * which forwards it to the child again: an unbounded loop that overflows the
     * stack (0xc00000fd). It cost a hotfix once already (1.8.1.1, right-click)
     * and it was still live for the mouse wheel over the velocity lane.
     *
     * The `_forwardingEvent` latch closes that class of bug structurally: a
     * re-entered forward is refused instead of recursing, whatever event type
     * Qt decides to propagate.
     */
    bool deliverToHosted(QEvent *event);

    /**
     * \brief deliverToHosted() plus accept(): the contract for every input
     *  event this wrapper forwards.
     * \return True when the event was delivered to the hosted widget.
     *
     * Accepting is the second half of the GLCTX-001 remedy and is required even
     * where the hosted widget happens to accept the event today: we have handled
     * the event by delegating it, so it must not bubble back to us (nor be
     * handled a second time by MainWindow's key fallback).
     */
    bool forwardToHosted(QEvent *event);

    /**
     * \brief Routes QEvent::ToolTip into the hosted widget.
     *
     * Qt hit-tests only visible widgets, so a QHelpEvent for a hover over this
     * wrapper never reaches the hidden widget whose content the user is looking
     * at. Everything else is delegated to QOpenGLWidget.
     */
    bool event(QEvent *e) override;

    // === Mouse Event Handlers ===

    /**
     * \brief Handles mouse move events with OpenGL acceleration.
     * \param event The mouse move event
     */
    void mouseMoveEvent(QMouseEvent *event) override;

    /**
     * \brief Handles mouse enter events.
     * \param event The enter event
     */
    void enterEvent(QEnterEvent *event) override;

    /**
     * \brief Handles mouse leave events.
     * \param event The leave event
     */
    void leaveEvent(QEvent *event) override;

    /**
     * \brief Handles mouse press events.
     * \param event The mouse press event
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * \brief Handles mouse release events.
     * \param event The mouse release event
     */
    void mouseReleaseEvent(QMouseEvent *event) override;

    // === Virtual Methods for Subclasses ===

    /**
     * \brief Virtual method for subclasses to implement custom OpenGL painting.
     * \param painter OpenGL-accelerated QPainter
     * 
     * Subclasses should override this method to implement their custom rendering
     * using the provided OpenGL-accelerated QPainter.
     */
    virtual void paintContent(QPainter *painter) = 0;

    // === State Variables ===

    /** \brief Application settings */
    QSettings *_settings;

    /** \brief OpenGL paint device for hardware acceleration */
    QOpenGLPaintDevice *_paintDevice;

    /** \brief Mouse and widget state flags */
    bool mouseOver, mousePressed, mouseReleased, repaintOnMouseMove,
            repaintOnMousePress, repaintOnMouseRelease, inDrag, mousePinned,
            enabled;

    /** \brief Mouse position tracking */
    int mouseX, mouseY, mouseLastX, mouseLastY;

    /** \brief Cached LOGICAL paint size to avoid unnecessary GPU reallocations */
    QSize _lastPaintSize;

    /** \brief Cached device pixel ratio the paint device was configured for.
     *  A monitor / scaling change alters this without changing the logical
     *  size, so it has to take part in the "needs re-sync" test. */
    qreal _lastPaintDpr = 0.0;

    /** \brief True while deliverToHosted() has an event in flight. Blocks the
     *  parent-chain re-entry described in deliverToHosted(). */
    bool _forwardingEvent = false;

    /** \brief GLBLANK-001: true while paintGL() holds a QPainter on
     *  _paintDevice. See canPaintNow(). */
    bool _inPaintGL = false;

    /** \brief GLBLANK-001: a deferred frame is already queued. */
    bool _repaintPending = false;

    /**
     * \brief Queues exactly one repaint to run after the current paint has
     *  finished. Without it a deferred frame would never come back and the
     *  widget would keep its previous (usually empty) framebuffer forever.
     */
    void requestDeferredRepaint();

    /**
     * \brief Points the paint device at \a logicalSize / \a dpr and caches both.
     */
    void applyPaintDeviceGeometry(const QSize &logicalSize, qreal dpr);
};

#endif // OPENGLPAINTWIDGET_H_
