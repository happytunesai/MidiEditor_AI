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

#ifndef MATRIXWIDGET_H_
#define MATRIXWIDGET_H_

// Project includes
#include "PaintWidget.h"
#include "Appearance.h"
#include "../midi/MidiTrack.h"

// Qt includes
#include <QApplication>
#include <QEnterEvent>
#include <QHash>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>

// Forward declarations
class MidiFile;
class TempoChangeEvent;
class TimeSignatureEvent;
class MidiEvent;
class GraphicObject;
class NoteOnEvent;
class QSettings;

/**
 * \class MatrixWidget
 *
 * \brief The main visual MIDI editor widget for note editing and display.
 *
 * MatrixWidget is the central component of the MIDI Editor that provides a
 * piano-roll style interface for editing MIDI notes and events. It extends
 * PaintWidget to offer comprehensive MIDI editing capabilities:
 *
 * **Core Features:**
 * - **Visual Note Editing**: Piano-roll style note display and editing
 * - **Multi-track Support**: Display and edit multiple MIDI tracks simultaneously
 * - **Zoom and Navigation**: Horizontal and vertical zoom with scroll support
 * - **Time-based Display**: Accurate timing display with tempo and time signature support
 * - **Piano Keyboard**: Visual piano keyboard with optional sound playback
 * - **Color Coding**: Notes can be colored by channel or track for clarity
 * - **Event Management**: Handles all types of MIDI events (notes, controllers, etc.)
 *
 * **Display Areas:**
 * - **Matrix Area**: Main note editing area with piano-roll display
 * - **Piano Area**: Left-side piano keyboard for note reference and input
 * - **Timeline Area**: Top timeline showing measures, beats, and time signatures
 * - **Tool Area**: Areas for tool interaction and selection
 *
 * **Coordinate Systems:**
 * - **Time Axis (X)**: Measured in MIDI ticks and milliseconds
 * - **Pitch Axis (Y)**: MIDI note numbers (0-127) with piano key layout
 * - **Scaling**: Configurable zoom levels for both time and pitch dimensions
 *
 * The widget integrates with the broader MIDI Editor architecture through:
 * - MidiFile for data management
 * - Protocol system for undo/redo operations
 * - Tool system for different editing modes
 * - Settings system for user preferences
 */
class MatrixWidget : public PaintWidget {
    Q_OBJECT

public:
    // === Construction ===

    /**
     * \brief Creates a new MatrixWidget.
     * \param settings Application settings for configuration
     * \param parent Parent widget (optional)
     */
    MatrixWidget(QSettings *settings, QWidget *parent = nullptr);

    // === File Management ===

    /**
     * \brief Sets the MIDI file to display and edit.
     * \param file The MidiFile to load, or nullptr to clear
     */
    void setFile(MidiFile *file);

    /**
     * \brief Gets the currently loaded MIDI file.
     * \return The current MidiFile, or nullptr if none loaded
     */
    MidiFile *midiFile();

    // === Event Access ===

    /**
     * \brief Gets the list of currently active/visible events.
     * \return List of MidiEvent pointers for events in the current view
     */
    QList<MidiEvent *> *activeEvents();

    /**
     * \brief Gets the list of events for velocity editing.
     * \return List of MidiEvent pointers for velocity display
     */
    QList<MidiEvent *> *velocityEvents();

    /**
     * \brief Gets the list of graphic objects for rendering.
     * \return List of GraphicObject pointers (recast from events)
     */
    QList<GraphicObject *> *getObjects() { return reinterpret_cast<QList<GraphicObject *> *>(objects); }

    // === Coordinate Conversion ===

    /**
     * \brief Gets the height of each piano key line.
     * \return Height in pixels per MIDI note line
     */
    double lineHeight();

    /**
     * \brief Converts Y coordinate to MIDI note line number.
     * \param y Y coordinate in pixels
     * \return MIDI note line number (0-127)
     */
    int lineAtY(int y);

    /**
     * \brief Converts X coordinate to time in milliseconds.
     * \param x X coordinate in pixels
     * \return Time in milliseconds
     */
    int msOfXPos(int x);

    /**
     * \brief Converts width to time duration in milliseconds.
     * \param w Width in pixels
     * \return Duration in milliseconds
     */
    int timeMsOfWidth(int w);

    /**
     * \brief Converts Y coordinate to line position.
     * \param line MIDI note line number
     * \return Y coordinate in pixels
     */
    int yPosOfLine(int line);

    /**
     * \brief Converts MIDI tick to milliseconds.
     * \param tick MIDI tick value
     * \return Time in milliseconds
     */
    int msOfTick(int tick);

    /**
     * \brief Converts milliseconds to X coordinate.
     * \param ms Time in milliseconds
     * \return X coordinate in pixels
     */
    int xPosOfMs(int ms);

    // === Event Testing ===

    /**
     * \brief Tests if an event is currently visible in the widget.
     * \param event The MidiEvent to test
     * \return True if the event is within the visible area
     */
    bool eventInWidget(MidiEvent *event);

    // === View Control ===

    /**
     * \brief Sets the viewport to display a specific time and pitch range.
     * \param startTick Starting MIDI tick
     * \param endTick Ending MIDI tick
     * \param startLine Starting MIDI note line
     * \param endLine Ending MIDI note line
     */
    void setViewport(int startTick, int endTick, int startLine, int endLine);

    /**
     * \brief Sets the width of the line name area (piano keys).
     * \param width Width in pixels for the piano key area
     */
    void setLineNameWidth(int width) {
        lineNameWidth = width;
        update();
    }

    /**
     * \brief Gets the current line name area width.
     * \return Width in pixels of the piano key area
     */
    int getLineNameWidth() const { return lineNameWidth; }

    /**
     * \brief Sets screen lock state to prevent automatic scrolling.
     * \param b True to lock the screen, false to allow auto-scroll
     */
    void setScreenLocked(bool b);

    /**
     * \brief Gets the current screen lock state.
     * \return True if screen is locked, false if auto-scroll is enabled
     */
    bool screenLocked();

    /**
     * \brief Gets the minimum visible MIDI time.
     * \return Earliest visible time in MIDI ticks
     */
    int minVisibleMidiTime();

    /**
     * \brief Gets the maximum visible MIDI time.
     * \return Latest visible time in MIDI ticks
     */
    int maxVisibleMidiTime();

    // === Display Preferences ===

    /**
     * \brief Sets note coloring mode to color by MIDI channel.
     */
    void setColorsByChannel();

    /**
     * \brief Sets note coloring mode to color by MIDI track.
     */
    void setColorsByTracks();

    /**
     * \brief Gets the current coloring mode.
     * \return True if coloring by channel, false if coloring by track
     */
    bool colorsByChannel();

    // === Piano Emulation ===

    /**
     * \brief Gets the piano emulation state.
     * \return True if piano emulation is enabled
     */
    bool getPianoEmulation();

    /**
     * \brief Sets the piano emulation state.
     * \param enabled True to enable piano sound on key presses
     */
    void setPianoEmulation(bool enabled);

    /**
     * \brief Plays a MIDI note through the piano emulation.
     * \param note MIDI note number to play (0-127)
     */
    void playNote(int note);

    // === Zoom accessors (Phase 9.9f §15.2 — follow-the-host) ===

    /** \brief Current horizontal (time-axis) zoom factor. 1.0 is the
     *  default; higher values zoom in (show less time per pixel). */
    double currentScaleX() const { return scaleX; }
    /** \brief Current vertical (pitch-axis) zoom factor. */
    double currentScaleY() const { return scaleY; }

    /** \brief Apply a presenter's zoom level on a viewer. Re-runs
     *  calcSizes() so the matrix's internal line / time book-keeping
     *  matches the presenter's view, which in turn lets the follow-up
     *  scrollYChanged land at the right line (otherwise the clamp in
     *  scrollYChanged would snap to 0 because the viewer's wider
     *  visible-line count would push endLineY past NUM_LINES). */
    void applyZoom(double newScaleX, double newScaleY);

    /** \brief Phase 9.9f §15.2 (follow-the-host, fit-to-focus mode):
     *  resize + scroll the local matrix so the given focus rectangle
     *  (startMs..endMs × startLine..endLine) fits within the visible
     *  area with a ~10% padding on each side. Computes the required
     *  scaleX / scaleY locally based on this widget's pixel
     *  dimensions, then scrolls to centre the focus.
     *
     *  Replaces the 1:1 scroll-mirroring approach when the wire frame
     *  carries focus extents — that approach broke whenever host and
     *  viewer had different window sizes (user report 2026-05-21):
     *  the viewer's larger visible-line count would clamp startLine
     *  to 0 ("snap to top of piano roll"). Fitting based on the local
     *  pixel geometry sidesteps the clamp entirely. */
    void fitToFocus(int startMs, int endMs, int startLine, int endLine);

    /** \brief Currently visible region — accessors for the host-side
     *  broadcaster to populate the focus extent in the viewState
     *  wire frame. Updated by scrollXChanged / scrollYChanged /
     *  calcSizes; reads what was last computed. */
    int visibleEndMs() const { return endTimeX; }
    int visibleEndLine() const { return endLineY; }

    // === Editing lock (Phase 9.9c §15.2 — Show Mode viewer) ===

    /**
     * \brief Block tool-driven edits while still allowing scrolling,
     *        zooming, piano-key preview, and selection navigation.
     *        Used by Show Mode to gate non-presenters out of the
     *        editing pathway without freezing the whole viewport.
     *        Status-bar / title-bar indicators are wired in MainWindow.
     */
    void setEditingLocked(bool locked) { _editingLocked = locked; }
    bool isEditingLocked() const { return _editingLocked; }

    // === Grid and Division ===

    /**
     * \brief Gets the current time divisions for grid display.
     * \return List of time division pairs for grid lines
     */
    QList<QPair<int, int> > divs();

public slots:
    // === Scroll Control ===

    /**
     * \brief Handles horizontal scroll position changes.
     * \param scrollPositionX New horizontal scroll position
     */
    void scrollXChanged(int scrollPositionX);

    /**
     * \brief Handles vertical scroll position changes.
     * \param scrollPositionY New vertical scroll position
     */
    void scrollYChanged(int scrollPositionY);

    // === Zoom Control ===

    /**
     * \brief Zooms in horizontally (time axis).
     */
    void zoomHorIn();

    /**
     * \brief Zooms out horizontally (time axis).
     */
    void zoomHorOut();

    /**
     * \brief Zooms in vertically (pitch axis).
     */
    void zoomVerIn();

    /**
     * \brief Zooms out vertically (pitch axis).
     */
    void zoomVerOut();

    /**
     * \brief Resets zoom to standard/default levels.
     */
    void zoomStd();

    /**
     * \brief Resets the view to show the entire file.
     */
    void resetView();

    // === Time Control ===

    /**
     * \brief Updates the current playback time position.
     * \param ms Time in milliseconds
     * \param ignoreLocked If true, updates even when screen is locked
     */
    void timeMsChanged(int ms, bool ignoreLocked = false);

    // === Layout and Rendering ===

    /**
     * \brief Registers that a layout recalculation is needed.
     */
    void registerRelayout();

    /**
     * \brief Recalculates widget sizes and layout.
     */
    void calcSizes();

    /**
     * \brief Updates cached rendering settings from QSettings.
     * Call this when rendering settings have changed to refresh the cache.
     */
    void updateRenderingSettings();

    /**
     * \brief Updates cached appearance colors from Appearance system.
     * Call this when appearance/theme has changed to refresh the color cache.
     */
    void updateCachedAppearanceColors();

    // === Keyboard Input ===

    /**
     * \brief Handles key press events from external sources.
     * \param event The key press event to process
     */
    void takeKeyPressEvent(QKeyEvent *event);

    /**
     * \brief Handles key release events from external sources.
     * \param event The key release event to process
     */
    void takeKeyReleaseEvent(QKeyEvent *event);

    // === Grid Division ===

    /**
     * \brief Sets the current time division for grid display.
     * \param div The time division value
     */
    void setDiv(int div);

    /**
     * \brief Gets the current time division.
     * \return The current time division value
     */
    int div();

signals:
    // === Widget State Signals ===

    /**
     * \brief Phase 28: emitted when this view receives focus (e.g. the user
     * clicks it). The host (MainWindow) uses this to make this view's document
     * the active one when several views are shown side by side.
     */
    void focusReceived(MatrixWidget *view);

    /**
     * \brief Emitted when the widget size or scroll limits change.
     * \param maxScrollTime Maximum scroll time in milliseconds
     * \param maxScrollLine Maximum scroll line number
     * \param valueX Current horizontal scroll value
     * \param valueY Current vertical scroll value
     */
    void sizeChanged(int maxScrollTime, int maxScrollLine, int valueX, int valueY);

    /**
     * \brief Emitted when the list of objects/events changes.
     */
    void objectListChanged();

    /**
     * \brief Emitted when the scroll position or visible range changes.
     * \param startMs Starting time in milliseconds
     * \param maxMs Maximum time in milliseconds
     * \param startLine Starting line number
     * \param maxLine Maximum line number
     */
    void scrollChanged(int startMs, int maxMs, int startLine, int maxLine);

protected:
    // === Event Handlers ===

    /**
     * \brief Handles paint events to render the widget.
     * \param event The paint event
     */
    void paintEvent(QPaintEvent *event);

    /**
     * \brief Paints timeline markers (CC/PC/Text) as dashed vertical lines.
     */
    void paintTimelineMarkers(QPainter *painter);

    /**
     * \brief Paints the dedicated marker bar above the timeline with label badges.
     */
    void paintMarkerBar(QPainter *painter);

    /**
     * \brief Phase 32.2: paints the FFXIV voice-load overlay (yellow/red
     * tints + per-channel rate hatch) on top of the note area when enabled.
     */
    void paintVoiceLoadOverlay(QPainter *painter);

public:
    /// Phase 32.2: toggle the voice-load overlay on/off (persisted by caller).
    void setShowVoiceLoadOverlay(bool on);
    bool showVoiceLoadOverlay() const { return _cachedShowVoiceLoad; }

protected:

    /**
     * \brief Handles mouse move events.
     * \param event The mouse move event
     */
    void mouseMoveEvent(QMouseEvent *event);

    /**
     * \brief Generic event filter — handles QEvent::ToolTip so that hovering
     *        over a PR-merged event shows "From: <author>" (Phase 9.1e+).
     *        Falls through to the default handler for all other events.
     */
    bool event(QEvent *event) override;

    /**
     * \brief Handles widget resize events.
     * \param event The resize event
     */
    void resizeEvent(QResizeEvent *event);

    /**
     * \brief Handles mouse enter events.
     * \param event The enter event
     */
    void enterEvent(QEnterEvent *event);

    /**
     * \brief Handles mouse leave events.
     * \param event The leave event
     */
    void leaveEvent(QEvent *event);

    /**
     * \brief Handles mouse press events.
     * \param event The mouse press event
     */
    void mousePressEvent(QMouseEvent *event);

    /**
     * \brief Phase 28: claim the active tool target and announce focus, so the
     * view the user clicks drives the tools and becomes the active document.
     */
    void focusInEvent(QFocusEvent *event);

    /**
     * \brief Handles mouse double-click events.
     * \param event The mouse double-click event
     */
    void mouseDoubleClickEvent(QMouseEvent *event);

    /**
     * \brief Handles mouse release events.
     * \param event The mouse release event
     */
    void mouseReleaseEvent(QMouseEvent *event);

    /**
     * \brief Handles key press events.
     * \param e The key press event
     */
    void keyPressEvent(QKeyEvent *e);

    /**
     * \brief Handles key release events.
     * \param event The key release event
     */
    void keyReleaseEvent(QKeyEvent *event);

    /**
     * \brief Handles mouse wheel events for zooming.
     * \param event The wheel event
     */
    void wheelEvent(QWheelEvent *event);

    /**
     * \brief Shows a context menu with common operations on right-click.
     * \param event The context menu event
     */
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    // === Helper Methods ===

    /**
     * \brief Handles piano emulation for keyboard input.
     * \param event The key event to process for piano emulation
     */
    void pianoEmulator(QKeyEvent *event);

    /**
     * \brief Paints all events for a specific MIDI channel.
     * \param painter The QPainter to draw with
     * \param channel The MIDI channel to paint (0-15)
     */
    void paintChannel(QPainter *painter, int channel);

    /**
     * \brief Paints a single piano key in the piano area.
     * \param painter The QPainter to draw with
     * \param number MIDI note number (0-127)
     * \param x X coordinate for the key
     * \param y Y coordinate for the key
     * \param width Width of the key in pixels
     * \param height Height of the key in pixels
     */
    void paintPianoKey(QPainter *painter, int number, int x, int y, int width, int height);

    // === Configuration ===

    /** \brief Application settings for configuration persistence */
    QSettings *_settings;

    /** \brief Piano emulation enabled state */
    bool _isPianoEmulationEnabled = false;

    /** \brief Note coloring mode (true = by channels, false = by tracks) */
    bool _colorsByChannels;

    /** \brief Current time division for grid display */
    int _div;

    // === Cached Rendering Settings ===

    /** \brief Cached setting for antialiasing rendering */
    bool _antialiasing;

    /** \brief Cached setting for smooth pixmap transform rendering */
    bool _smoothPixmapTransform;

    /** \brief Flag to prevent cascading repaints during scroll operations */
    bool _suppressScrollRepaints;

    /** \brief Tracks whether a Ctrl+Right-click is in progress to suppress context menu */
    bool _ctrlRightClickInProgress = false;

    /** \brief Cached background color to avoid expensive theme checks */
    QColor _cachedBackgroundColor;

    // === Cached Appearance Colors ===

    /** \brief Cached appearance colors to avoid expensive theme checks during paint */
    bool _cachedShowRangeLines;
    Appearance::stripStyle _cachedStripStyle;
    QColor _cachedRangeLineColor;
    QColor _cachedStripHighlightColor;
    QColor _cachedStripNormalColor;
    QColor _cachedProgramEventHighlightColor;
    QColor _cachedProgramEventNormalColor;
    QColor _cachedSystemWindowColor;
    QColor _cachedDarkGrayColor;
    QColor _cachedPianoWhiteKeyColor;
    QColor _cachedForegroundColor;

    // Piano key colors (most expensive in partial paint)
    QColor _cachedPianoBlackKeyColor;
    QColor _cachedPianoBlackKeyHoverColor;
    QColor _cachedPianoBlackKeySelectedColor;
    QColor _cachedPianoWhiteKeyHoverColor;
    QColor _cachedPianoWhiteKeySelectedColor;
    QColor _cachedPianoKeyLineHighlightColor;
    QColor _cachedPlaybackCursorColor;
    QColor _cachedBorderColor;
    QColor _cachedRecordingIndicatorColor;
    QColor _cachedCursorTriangleColor;

    // Additional appearance colors used in paint events
    QColor _cachedErrorColor;
    QColor _cachedMeasureBarColor;
    QColor _cachedMeasureLineColor;
    QColor _cachedMeasureTextColor;
    QColor _cachedTimelineGridColor;
    QColor _cachedGrayColor;

    // Cached theme state to avoid expensive shouldUseDarkMode() calls
    bool _cachedShouldUseDarkMode;

    // Cached timeline marker settings
    bool _cachedShowPCMarkers;
    bool _cachedShowCCMarkers;
    bool _cachedShowTextMarkers;
    Appearance::MarkerColorMode _cachedMarkerColorMode;

    // Phase 32.2: voice-load overlay (FFXIV awareness)
    bool _cachedShowVoiceLoad = false;

    // === View State ===

    /** \brief Viewport boundaries in MIDI ticks */
    int startTick, endTick;

    /** \brief Viewport boundaries in screen coordinates */
    int startTimeX, endTimeX, startLineY, endLineY;

    /** \brief Width of the piano key area in pixels */
    int lineNameWidth;

    /** \brief Height of the timeline area in pixels */
    int timeHeight;

    /** \brief Height of the marker bar above the timeline (0 if no markers enabled) */
    int markerBarHeight;

    /** \brief Time of first event in current event list */
    int msOfFirstEventInList;

    /** \brief Scaling factors for time and pitch axes */
    double scaleX, scaleY;

    /** \brief Screen lock state to prevent auto-scrolling */
    bool screen_locked;

    /** \brief Whether playback was active on last timeMsChanged call */
    bool _wasPlaying = false;

    /** \brief Offset in ms from cursor to viewport left edge at playback start */
    int _dynamicOffsetMs = 0;

    // === Data References ===

    /** \brief Currently loaded MIDI file */
    MidiFile *file;

    // === Display Areas ===

    /** \brief Tool interaction area */
    QRectF ToolArea;

    /** \brief Piano keyboard display area */
    QRectF PianoArea;

    /** \brief Timeline display area */
    QRectF TimeLineArea;

    /** \brief Marker display area (below ruler, above matrix) */
    QRectF MarkerArea;

    // === Rendering Cache ===

    /**
     * \brief Cached pixmap of the painted widget (without tools and cursor lines).
     * Will be null when a repaint is needed.
     */
    QPixmap *pixmap;

    // === Event Collections ===

    /**
     * \brief Tempo events from one before the first shown tick to the last in window.
     * Used for accurate time calculations during display.
     */
    QList<MidiEvent *> *currentTempoEvents;

    /**
     * \brief Time signature events currently visible in the window.
     * Used for timeline and grid display.
     */
    QList<TimeSignatureEvent *> *currentTimeSignatureEvents;

    /**
     * \brief All events currently visible in the main matrix area.
     */
    QList<MidiEvent *> *objects;

    /**
     * \brief Events to show in the velocity widget/area.
     */
    QList<MidiEvent *> *velocityObjects;

    /**
     * \brief Current time divisions for grid line display.
     */
    QList<QPair<int, int> > currentDivs;

    // === Piano Emulation ===

    /**
     * \brief NoteOnEvent used for piano key playback.
     * Reused for all piano emulation note playback.
     */
    NoteOnEvent *pianoEvent;

    /**
     * \brief Map of MIDI note numbers to their display rectangles.
     * Used for piano key hit testing and display.
     */
    QMap<int, QRect> pianoKeys;

    // === Constants ===

    /**
     * \brief Bitmask for sharp/black keys in piano layout.
     * Bit positions correspond to notes within an octave (C=0, C#=1, D=2, etc.).
     * Set bits indicate sharp/black keys: C#, D#, F#, G#, A#
     */
    static const unsigned sharp_strip_mask = (1 << 4) | (1 << 6) | (1 << 9) | (1 << 11) | (1 << 1);

    /**
     * \brief Editing lock (Phase 9.9c §15.2 — Show Mode viewer). When
     * true, tool-driven mouse interaction (press/move/release) is
     * suppressed, but scrolling, zooming, piano-key preview, and
     * selection navigation stay live. Toggled by MainWindow via
     * \ref setEditingLocked when the local peer's presenter status
     * changes in a Show-mode session.
     */
    bool _editingLocked = false;
};

#endif // MATRIXWIDGET_H_
