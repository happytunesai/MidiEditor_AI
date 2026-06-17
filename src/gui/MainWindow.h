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

#ifndef MAINWINDOW_H_
#define MAINWINDOW_H_

// Qt includes
#include <QCloseEvent>
#include <QMainWindow>
#include <QScrollBar>
#include <QSettings>
#include <QKeySequence>
#include <QVector>
#include <QSet>

#include <atomic>

// Project includes
#include "ToolbarActionInfo.h"
#ifdef MIDIEDITOR_COLLAB_ENABLED
#include "../collab/SessionMode.h"
#endif

// Forward declarations
class QProgressDialog;
class QTabBar;
class QToolButton;
class DocumentTabBar;
class MatrixWidget;
class OpenGLMatrixWidget;
class OpenGLMiscWidget;
class MidiEvent;
class MidiFile;
class DocumentManager;
class Document;

#ifdef FLUIDSYNTH_SUPPORT
struct ExportOptions;
#endif
class ChannelListWidget;
class ProtocolWidget;
class EventWidget;
class ClickButton;
class QTabWidget;
class QSplitter;
class QMenu;
class TrackListWidget;
class QComboBox;
class MiscWidget;
class LyricTimelineWidget;
class FfxivVoiceLaneWidget;
class QGridLayout;
class MidiTrack;
class QShowEvent;
class Update;
class SelectionNavigator;
class TweakTarget;
class UpdateChecker;
class AutoUpdater;
class UpdateAvailableDialog;
class PostUpdateDialog;
class MidiPilotWidget;
class MidiVisualizerWidget;
class LyricVisualizerWidget;
class McpToggleWidget;
class McpServer;
class QDockWidget;
class QLabel;

/**
 * \class MainWindow
 *
 * \brief The main application window for the MIDI Editor.
 *
 * MainWindow is the central hub of the MIDI Editor application. It provides:
 *
 * - The main user interface with menus, toolbars, and panels
 * - File management (open, save, new, recent files)
 * - Integration of all major components (matrix widget, event widget, etc.)
 * - MIDI playback controls and transport functions
 * - Tool selection and editing operations
 * - Settings and preferences management
 * - Drag-and-drop file support
 *
 * The window is organized into several main areas:
 * - Matrix widget for visual note editing
 * - Event widget for detailed event editing
 * - Channel and track management panels
 * - Protocol widget for undo/redo history
 * - Various control panels and toolbars
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * \brief Creates a new MainWindow.
     * \param initFile Optional initial file to load on startup
     */
    MainWindow(QString initFile = "");

    /**
     * \brief Destroys the MainWindow and cleans up resources.
     */
    ~MainWindow();

    /**
     * \brief Performs early cleanup of OpenGL resources to prevent shutdown issues.
     */
    void performEarlyCleanup();

    /**
     * \brief Sets the current MIDI file.
     * \param f The MidiFile to set as current
     */
    void setFile(MidiFile *f);

    /**
     * \brief Gets the current MIDI file.
     * \return The currently loaded MidiFile, or nullptr if none
     */
    MidiFile *getFile();

    /**
     * \brief Gets the matrix widget for note editing.
     * \return Pointer to the MatrixWidget
     */
    MatrixWidget *matrixWidget();

    /**
     * \brief Gets the matrix widget container (MatrixWidget or OpenGLMatrixWidget).
     */
    QWidget *matrixWidgetContainer() { return _matrixWidgetContainer; }

    /**
     * \brief Gets the event widget for detailed event editing.
     * \return Pointer to the EventWidget
     */
    EventWidget *eventWidget();

    /**
     * \brief Sets the starting directory for file dialogs.
     * \param dir The directory path to use as default
     */
    void setStartDir(QString dir);

    /**
     * \brief Sets the initial file to load on startup.
     * \param file Path to the file to load initially
     */
    void setInitFile(const char *file);

    /**
     * \brief Prompts user to save before closing if needed.
     * \return True if it's safe to close, false if user cancelled
     */
    bool saveBeforeClose();

    /**
     * \brief Phase 28 (editor groups): on app close, prompt to save EVERY dirty
     * open document across both editor groups (in tab order), not just the
     * active one. \return true if it is safe to close (all saved/discarded),
     * false if the user cancelled any prompt (abort the quit).
     */
    bool promptSaveAllDirtyTabs();

    /**
     * \brief Returns a copy of the action map (id -> QAction*)
     */
    QMap<QString, QAction*> getActionMap() const { return _actionMap; }

    /**
     * \brief Returns stored default shortcuts per action id.
     */
    QMap<QString, QList<QKeySequence>> getDefaultShortcuts() const { return _defaultShortcuts; }

    /**
     * \brief Apply custom shortcuts from settings.
     */
    void applyStoredShortcuts();

    /**
     * \brief Set shortcuts for an action id.
     */
    void setActionShortcuts(const QString &actionId, const QList<QKeySequence> &seqs);

protected:
    /**
     * \brief Handles file drop events for drag-and-drop file loading.
     * \param ev The drop event containing file information
     */
    void dropEvent(QDropEvent *ev);

    /**
     * \brief Handles drag enter events to accept file drops.
     * \param ev The drag enter event
     */
    void dragEnterEvent(QDragEnterEvent *ev);

    /**
     * \brief Phase 28 (editor groups): while a file is dragged over a split
     * editor, highlight the group under the cursor so the user sees where the
     * file will open. dragLeave clears the highlight.
     */
    void dragMoveEvent(QDragMoveEvent *ev);
    void dragLeaveEvent(QDragLeaveEvent *ev);

public slots:
    // === General Update Methods ===

    /**
     * \brief Updates all widgets and components.
     */
    void updateAll();

    /**
     * \brief Updates rendering mode when settings change.
     */
    void updateRenderingMode();

    /**
     * \brief Loads the initial file specified at startup.
     */
    void loadInitFile();

    /**
     * \brief Initializes the shared clipboard system.
     */
    void initializeSharedClipboard();

    /**
     * \brief Updates the state of paste-related actions.
     */
    void updatePasteActionState();

    /**
     * \brief Called when the matrix widget size changes.
     * \param maxScrollTime Maximum scroll time in milliseconds
     * \param maxScrollLine Maximum scroll line
     * \param vX Viewport X position
     * \param vY Viewport Y position
     */
    void matrixSizeChanged(int maxScrollTime, int maxScrollLine, int vX, int vY);

    // === Playback Control Methods ===

    /**
     * \brief Starts MIDI playback.
     */
    void play();

    /**
     * \brief Toggles between play and stop.
     */
    void playStop();

    /**
     * \brief Stops MIDI playback.
     * \param autoConfirmRecord Auto-confirm recording if active
     * \param addEvents Add recorded events to the file
     * \param resetPause Reset pause state
     */
    void stop(bool autoConfirmRecord = false, bool addEvents = true, bool resetPause = true);

    /**
     * \brief Pauses MIDI playback.
     */
    void pause();

    /**
     * \brief Moves playback position forward.
     */
    void forward();

    /**
     * \brief Moves playback position backward.
     */
    void back();

    /**
     * \brief Moves playback position to the beginning.
     */
    void backToBegin();

    /**
     * \brief Moves playback position to the next marker.
     */
    void forwardMarker();

    /**
     * \brief Moves playback position to the previous marker.
     */
    void backMarker();

    // === File Operations ===

    /**
     * \brief Opens a file dialog to load a MIDI file.
     */
    void load();

    /**
     * \brief Loads a specific MIDI file.
     * \param file Path to the MIDI file to load
     */
    void loadFile(QString file);

    /**
     * \brief Opens a MIDI file at the specified path.
     * \param filePath Path to the MIDI file to open
     */
    void openFile(QString filePath);

    /**
     * \brief Saves the current MIDI file.
     */
    void save();

    /**
     * \brief Opens a save-as dialog to save the file with a new name.
     * \return true if the file was actually written; false if the dialog was
     * cancelled or the write failed (so close paths can abort safely).
     */
    bool saveas();

    /**
     * \brief Undoes the last action.
     */
    void undo();

    /**
     * \brief Redoes the last undone action.
     */
    void redo();

    // === Channel Management ===

    /**
     * \brief Mutes all MIDI channels.
     */
    void muteAllChannels();

    /**
     * \brief Unmutes all MIDI channels.
     */
    void unmuteAllChannels();

    /**
     * \brief Makes all channels visible in the editor.
     */
    void allChannelsVisible();

    /**
     * \brief Hides all channels in the editor.
     */
    void allChannelsInvisible();

    // === Track Management ===

    /**
     * \brief Mutes all MIDI tracks.
     */
    void muteAllTracks();

    /**
     * \brief Unmutes all MIDI tracks.
     */
    void unmuteAllTracks();

    /**
     * \brief Makes all tracks visible in the editor.
     */
    void allTracksVisible();

    /**
     * \brief Hides all tracks in the editor.
     */
    void allTracksInvisible();

    // === Dialogs and Settings ===

    /**
     * \brief Shows the about dialog.
     */
    void about();

    /**
     * \brief Opens dialog to set the file length in milliseconds.
     */
    void setFileLengthMs();

    /**
     * \brief Called when scroll positions change in the editor.
     * \param startMs Start time in milliseconds
     * \param maxMs Maximum time in milliseconds
     * \param startLine Start line number
     * \param maxLine Maximum line number
     */
    void scrollPositionsChanged(int startMs, int maxMs, int startLine, int maxLine);

#ifdef MIDIEDITOR_COLLAB_ENABLED
    /** \brief Phase 9.9f §15.2: gather the local viewport + per-track
     *  + per-channel visibility, hand it to LanLiveSession's throttled
     *  broadcaster. Internally guarded by broadcastViewState's
     *  role/mode/isPresenter check; safe to call from anywhere. */
    void broadcastLocalViewState();

    /** \brief Phase 9.9f §15.2: apply a viewState received from the
     *  presenter. Sets track/channel visibility via setHiddenSilent /
     *  setVisibleSilent (no Protocol step), scrolls the matrix to the
     *  presenter's viewport, then forces a repaint. */
    void applyRemoteViewState(const LiveSession::ViewportState &viewport,
                              const QVector<bool> &trackVisibility,
                              const QVector<bool> &channelVisibility);
#endif

    /**
     * \brief Starts MIDI recording.
     */
    void record();

    /**
     * \brief Creates a new MIDI file.
     */
    void newFile();

    /**
     * \brief Sends panic (all notes off) to all MIDI channels.
     */
    void panic();

    /**
     * \brief Handles screen lock button press.
     * \param enable True to enable screen lock, false to disable
     */
    void screenLockPressed(bool enable);

    /**
     * \brief Opens the scale selection dialog.
     */
    void scaleSelection();

    /**
     * \brief Aligns selected events to the left.
     */
    void alignLeft();

    /**
     * \brief Aligns selected events to the right.
     */
    void alignRight();

    /**
     * \brief Equalizes selected events.
     */
    void equalize();

    /**
     * \brief Glues selected events together.
     */
    void glueSelection();

    /**
     * \brief Glues selected events together across all channels.
     */
    void glueSelectionAllChannels();

    /**
     * \brief Deletes overlapping notes.
     */
    void deleteOverlaps();

    /**
     * \brief Resets the view to default settings.
     */
    void resetView();

    /**
     * \brief Deletes all selected events.
     */
    void deleteSelectedEvents();

    /**
     * \brief Deletes all events from a specific channel.
     * \param action The action containing channel information
     */
    void deleteChannel(QAction *action);

    /**
     * \brief Moves selected events to a specific channel.
     * \param action The action containing channel information
     */
    void moveSelectedEventsToChannel(QAction *action);

    /**
     * \brief Moves selected events to a specific track.
     * \param action The action containing track information
     */
    void moveSelectedEventsToTrack(QAction *action);

    /**
     * \brief Phase 36 -- duplicates selected events onto a specific
     * channel (0..15). Originals stay; the copies become the new
     * selection.
     */
    void copySelectedEventsToChannel(QAction *action);

    /**
     * \brief Phase 36 -- duplicates selected events onto a specific
     * track. Originals stay; the copies become the new selection.
     */
    void copySelectedEventsToTrack(QAction *action);

    /**
     * \brief Updates the recent files list.
     */
    void updateRecentPathsList();

    /**
     * \brief Opens a recent file.
     * \param action The action containing file path information
     */
    void openRecent(QAction *action);

    /**
     * \brief Updates the channel menu with current channels.
     */
    void updateChannelMenu();

    /**
     * \brief Updates the track menu with current tracks.
     */
    void updateTrackMenu();

    /**
     * \brief Mutes or unmutes a channel.
     * \param action The action containing channel information
     */
    void muteChannel(QAction *action);

    /**
     * \brief Solos or unsolos a channel.
     * \param action The action containing channel information
     */
    void soloChannel(QAction *action);

    /**
     * \brief Shows or hides a channel.
     * \param action The action containing channel information
     */
    void viewChannel(QAction *action);

    /**
     * \brief Opens instrument selection for a channel.
     * \param action The action containing channel information
     */
    void instrumentChannel(QAction *action);

    void renameTrackMenuClicked(QAction *action);

    void removeTrackMenuClicked(QAction *action);

    void showEventWidget(bool show);

    void showTrackMenuClicked(QAction *action);

    void muteTrackMenuClicked(QAction *action);

    void renameTrack(int tracknumber);

    void removeTrack(int tracknumber);

    void setInstrumentForChannel(int i);

    void spreadSelection();

    void copy();

    void paste();

    void pasteSpecial();

    void addTrack();

    void selectAll();

    void transposeNSemitones();

    /**
     * \brief Converts pitch bend data affecting selected notes into discrete notes.
     *
     * Splits each selected note at pitch bend change points and replaces the
     * bent portions with separate notes whose pitches approximate the bend.
     */
    void convertPitchBendToNotes();

    /**
     * \brief Explodes chords into separate tracks based on chosen strategy.
     */
    void explodeChordsToTracks();

    /**
     * \brief Splits a multi-channel track into one track per channel.
     */
    void splitChannelsToTracks();

    /**
     * \brief Opens the Strummer dialog to stagger notes.
     */
    void strumNotes();

    /**
     * \brief Fixes FFXIV channel assignments and program_change events.
     */
    void fixFFXIVChannels();

    /**
     * \brief Opens the FFXIV SoundFont per-instrument volume mixer
     *        (Phase 39). Modal dialog; only enabled while FFXIV
     *        SoundFont Mode is on. No-op without FluidSynth support.
     */
    void openFfxivEqualizer();

    /**
     * \brief Imports lyrics from an SRT subtitle file.
     */
    void importLyricsSrt();

    /**
     * \brief Imports lyrics from plain text via a dialog.
     */
    void importLyricsText();

    /**
     * \brief Opens the Tap-to-Sync dialog for lyric timing.
     */
    void syncLyrics();

    /**
     * \brief Exports lyrics to an SRT subtitle file.
     */
    void exportLyricsSrt();

    /**
     * \brief Embeds lyric blocks into the MIDI file as meta events.
     */
    void embedLyricsInMidi();

    /**
     * \brief Imports lyrics from an LRC file.
     */
    void importLyricsLrc();

    /**
     * \brief Exports lyrics to an LRC file (MidiBard2 compatible).
     */
    void exportLyricsLrc();

    /**
     * \brief Exports the current file as MusicXML (notation), openable in
     *  MuseScore / Finale / Sibelius / Dorico. Reconstructs notation (measures,
     *  note values, rests, ties, chords, key spelling) from the MIDI.
     */
    void exportMusicXml();

    /**
     * \brief Clears all lyric blocks and their MIDI events.
     */
    void clearAllLyrics();

    /**
     * \brief Marks the file as edited (unsaved changes).
     */
    void markEdited();

    /**
     * \brief Sets coloring mode to color by MIDI channels.
     */
    void colorsByChannel();

    /**
     * \brief Sets coloring mode to color by MIDI tracks.
     */
    void colorsByTrack();

    /**
     * \brief Edits a specific MIDI channel.
     * \param i The channel number to edit
     * \param assign Whether to assign the channel
     */
    void editChannel(int i, bool assign = true);

    /**
     * \brief Edits a specific MIDI track.
     * \param i The track number to edit
     * \param assign Whether to assign the track
     */
    void editTrack(int i, bool assign = true);

    /**
     * \brief Edits both track and channel for a MIDI track.
     * \param track The MidiTrack to edit
     */
    void editTrackAndChannel(MidiTrack *track);

    /**
     * \brief Opens the manual/help documentation.
     */
    void manual();

    /**
     * \brief Changes the miscellaneous editor mode.
     * \param mode The new editor mode
     */
    void changeMiscMode(int mode);

    /**
     * \brief Handles selection mode changes.
     * \param action The action that triggered the change
     */
    void selectModeChanged(QAction *action);

    /**
     * \brief Pastes clipboard content to a specific channel.
     * \param action The action containing channel information
     */
    void pasteToChannel(QAction *action);

    /**
     * \brief Pastes clipboard content to a specific track.
     * \param action The action containing track information
     */
    void pasteToTrack(QAction *action);

    /**
     * \brief Selects all events from a specific channel.
     * \param action The action containing channel information
     */
    void selectAllFromChannel(QAction *action);

    /**
     * \brief Selects all events from a specific track.
     * \param action The action containing track information
     */
    void selectAllFromTrack(QAction *action);

    /**
     * \brief Handles division setting changes.
     * \param action The action containing division information
     */
    void divChanged(QAction *action);

    /**
     * \brief Handles quantization setting changes.
     * \param action The action containing quantization information
     */
    void quantizationChanged(QAction *action);

    /**
     * \brief Enables or disables the magnet (snap to grid) feature.
     * \param enable True to enable magnet, false to disable
     */
    void enableMagnet(bool enable);

    /**
     * \brief Opens the configuration/settings dialog.
     */
    void openConfig();

    /**
     * \brief Enables or disables the metronome.
     * \param enable True to enable metronome, false to disable
     */
    void enableMetronome(bool enable);

    /**
     * \brief Enables or disables MIDI thru functionality.
     * \param enable True to enable thru, false to disable
     */
    void enableThru(bool enable);

    /**
     * \brief Toggles piano emulation mode.
     * \param enable True to enable piano emulation, false to disable
     */
    void togglePianoEmulation(bool enable);

    /**
     * \brief Rebuilds the toolbar from current settings.
     */
    void rebuildToolbarFromSettings();

    /**
     * \brief Quantizes the selected events to the current grid.
     */
    void quantizeSelection();

    /**
     * \brief Opens the time-preserving tempo conversion dialog (Phase 33).
     *        Whole-project scope; called from the Tools menu.
     */
    void convertTempoPreserveDuration();

    /**
     * \brief Opens the tempo conversion dialog pre-filled with the current
     *        selection (Phase 33). Called from the matrix right-click menu.
     */
    void convertTempoForSelection();

    /**
     * \brief Opens the tempo conversion dialog pre-filled with one track id.
     */
    void convertTempoForTrack(int trackNumber);

    /**
     * \brief Opens the tempo conversion dialog pre-filled with one channel.
     */
    void convertTempoForChannel(int channel);

    /**
     * \brief Opens the N-tole quantization dialog.
     */
    void quantizeNtoleDialog();

    /**
     * \brief Applies N-tole quantization to selected events.
     */
    void quantizeNtole();

    /**
     * \brief Sets the playback speed from an action.
     * \param action The action containing speed information
     */
    void setSpeed(QAction *action);

    /**
     * \brief Checks and enables/disables actions based on current selection.
     */
    void checkEnableActionsForSelection();

    /**
     * \brief Handles tool change events.
     */
    void toolChanged();

    /**
     * \brief Handles note duration preset selection.
     */
    void noteDurationSelected(QAction *action);

    /**
     * \brief Handles changes to copied events.
     */
    void copiedEventsChanged();

    /**
     * \brief Opens the time tweaking dialog.
     */
    void tweakTime();

    /**
     * \brief Opens the start time tweaking dialog.
     */
    void tweakStartTime();

    /**
     * \brief Opens the end time tweaking dialog.
     */
    void tweakEndTime();

    /**
     * \brief Opens the note tweaking dialog.
     */
    void tweakNote();

    /**
     * \brief Opens the value tweaking dialog.
     */
    void tweakValue();

    /**
     * \brief Applies small decrease tweaking to selected events.
     */
    void tweakSmallDecrease();

    /**
     * \brief Applies small increase tweaking to selected events.
     */
    void tweakSmallIncrease();

    /**
     * \brief Applies medium decrease tweaking to selected events.
     */
    void tweakMediumDecrease();

    /**
     * \brief Applies medium increase tweaking to selected events.
     */
    void tweakMediumIncrease();

    /**
     * \brief Applies large decrease tweaking to selected events.
     */
    void tweakLargeDecrease();

    /**
     * \brief Applies large increase tweaking to selected events.
     */
    void tweakLargeIncrease();

    /**
     * \brief Navigates selection up (higher pitch).
     */
    void navigateSelectionUp();

    /**
     * \brief Navigates selection down (lower pitch).
     */
    void navigateSelectionDown();

    /**
     * \brief Navigates selection left (earlier time).
     */
    void navigateSelectionLeft();

    /**
     * \brief Navigates selection right (later time).
     */
    void navigateSelectionRight();

    /**
     * \brief Transposes selected notes up by one octave.
     */
    void transposeSelectedNotesOctaveUp();

    /**
     * \brief Transposes selected notes down by one octave.
     */
    void transposeSelectedNotesOctaveDown();

    /**
     * \brief Check for application updates.
     * \param silent If true, only show UI if an update is available.
     */
    void checkForUpdates(bool silent = false);

    /**
     * \brief Show "Update Successful" dialog after a self-update restart.
     * \param updatedFromVersion The version we updated from (passed via --updated-from).
     */
    void showPostUpdateDialog(const QString &updatedFromVersion);

    /**
     * \brief Exports the current MIDI file as audio.
     */
    void exportAudio();

    /**
     * \brief Exports the current selection as audio.
     */
    void exportAudioSelection();

    /**
     * \brief Refreshes toolbar icons when theme changes.
     */
    void refreshToolbarIcons();

    /**
     * \brief Restarts the application to apply a theme change cleanly.
     *
     * Saves the current file (prompting if needed), persists all settings,
     * then relaunches the executable with --open and --open-settings flags.
     */
    void restartForThemeChange();

    /**
     * \brief Opens the settings dialog and navigates to the Appearance tab.
     *
     * Called after a theme-change restart to return the user to where they were.
     */
    void openConfigOnAppearanceTab();

    /**
     * \\brief Updates the status bar with cursor position, selection, and chord info.
     */
    void updateStatusBar();

    /**
     * \brief Applies widget size constraints at startup based on settings.
     */
    void applyWidgetSizeConstraints();

protected:
    /**
     * \brief Handles close events to save work before closing.
     * \param event The close event
     */
    void closeEvent(QCloseEvent *event);

    /**
     * \brief Handles key press events for shortcuts and piano emulation.
     * \param e The key press event
     */
    void keyPressEvent(QKeyEvent *e);

    /**
     * \brief Handles key release events for piano emulation.
     * \param event The key release event
     */
    void keyReleaseEvent(QKeyEvent *event);

private:
    /**
     * \brief Removes trailing separators from a toolbar.
     * \param toolbar The toolbar to clean up
     */
    void removeTrailingSeparators(QToolBar *toolbar);

    /**
     * \brief Feeds the MIDI Visualizer during authentic SID (Emulation)
     *  playback. The SID renders audio directly (no MIDI is sent to the
     *  output), so the visualizer's per-channel activity would stay empty;
     *  this mirrors the notes active at \a ms into MidiOutput::channelActivity
     *  so the bars animate just like they do for normal MIDI playback.
     */
    void feedSidVisualizer(int ms);

    /**
     * \brief Null every on-demand toolbar widget pointer (BUG-CORE-001).
     *  These widgets are children of the toolbars; a toolbar rebuild deletes
     *  them, so their member pointers must be cleared or later guarded derefs
     *  (setFile / stop) would touch freed memory. Called from both rebuild
     *  paths; widgets still enabled are recreated immediately afterwards.
     */
    void nullOnDemandToolbarWidgets();

    // === Core Widgets ===

    /** \brief Main matrix widget for MIDI editing (internal widget for data access) */
    MatrixWidget *mw_matrixWidget;

    /** \brief Container for the displayed matrix widget (OpenGL or software) */
    QWidget *_matrixWidgetContainer;

    /**
     * \brief Phase 28: horizontal splitter that holds the editor view(s). With
     * a single document it contains just the primary matrix container (visually
     * identical to before); the side-by-side compare view is added here.
     */
    QSplitter *_viewSplitter = nullptr;

    /**
     * \brief Phase 28: read-only side-by-side compare view (nullptr when off).
     * Shows another open document next to the primary editor for comparison;
     * it never claims the tools/selection. _compareFile is the document it is
     * pinned to, tracked so closing that document can tear the view down.
     */
    MatrixWidget *_compareMatrixWidget = nullptr;
    MidiFile *_compareFile = nullptr;

    /**
     * \brief Phase 28 (editor groups): the secondary editor group ("group 1").
     * Created on split, torn down on un-split. Like group 0 (the primary) it has
     * its OWN tab bar + document list + view, so each tab belongs to a group and
     * can be moved between groups (VS Code-style editor groups). The pieces:
     * _compareMatrixWidget is the group's view; _group1Docs its open documents;
     * _group1TabBar its tab strip; _group1Container the vertical [tab bar | view]
     * wrapper that lives in _viewSplitter beside the primary group.
     */
    QWidget *_group1Container = nullptr;
    DocumentManager *_group1Docs = nullptr;
    QTabBar *_group1TabBar = nullptr;
    bool _suppressGroup1TabSignals = false;

    /**
     * \brief Phase 28 (editor groups): the secondary group can be COLLAPSED -
     * its pane is hidden but its tabs/documents are kept alive - to reclaim
     * space without closing anything. _viewSplitterSizes remembers the split so
     * restore returns to the same widths.
     */
    bool _group1Collapsed = false;
    QList<int> _viewSplitterSizes;

    /**
     * \brief Phase 28 (editor groups): a colour-highlighted chip at the right of
     * the primary tab strip, shown ONLY while the secondary group is collapsed.
     * It signals "a second editor group is hidden here" and restores it on click.
     */
    QToolButton *_group1RestoreButton = nullptr;

    /**
     * \brief Phase 28 (editor groups): an X next to the restore chip (shown only
     * while the secondary group is collapsed) that closes the collapsed group
     * outright (with save prompts), without restoring it first.
     */
    QToolButton *_group1RestoreCloseButton = nullptr;

    /** \brief Phase 28 (editor groups): collapse / restore / close the secondary
     *  group. Collapse hides it (tabs kept); close prompts to save each dirty
     *  tab then tears the group down. */
    void collapseGroup1();
    void restoreGroup1();
    void closeGroup1();

    /**
     * \brief Phase 28 (editor groups): the primary group's container (the
     * [ tab strip | body ] widget in the splitter). Kept so a file dragged over
     * it can be highlighted / targeted, symmetrically with _group1Container.
     */
    QWidget *_group0Container = nullptr;

    /**
     * \brief Phase 28 (editor groups): when split, return the editor VIEW of the
     * group whose container contains \a windowPos (the primary view or the
     * secondary view); nullptr when not split. Used to route a dropped file to
     * the pane under the cursor.
     */
    MatrixWidget *viewAtWindowPos(const QPoint &windowPos) const;

    /** \brief Phase 28 (editor groups): highlight \a target group's container as
     *  the drop target (transparent border otherwise), or clear when nullptr.
     *  No-op when the target is unchanged (dragMove fires continuously). */
    void highlightDropGroup(QWidget *target);

    /** \brief Phase 28 (editor groups): the container currently drop-highlighted. */
    QWidget *_dropHighlightTarget = nullptr;

    /**
     * \brief Phase 28 (B): the currently focused editor pane. A tab click loads
     * its document into THIS pane, so the user can set each pane's document
     * independently. Defaults to / falls back to the primary view.
     */
    MatrixWidget *_activeView = nullptr;

    /** \brief Phase 28: show/hide the side-by-side compare/edit view. */
    void toggleCompareView();

    /**
     * \brief Phase 28 (B): a side-by-side view received focus (click/keyboard).
     * Makes that view's document the active one (sidebars/selection/tools/
     * transport follow) without rebinding the other view. No-op when the
     * focused view already shows the active document.
     */
    void onViewFocused(MatrixWidget *view);

    /** \brief Container for the displayed misc widget (OpenGL or software) */
    QWidget *_miscWidgetContainer;

    /** \brief Vertical and horizontal scroll bars */
    QScrollBar *vert, *hori;

    /** \brief Widget for managing MIDI channels */
    ChannelListWidget *channelWidget;

    /** \brief Widget for undo/redo protocol management */
    ProtocolWidget *protocolWidget;

    /** \brief Widget for managing MIDI tracks */
    TrackListWidget *_trackWidget;

    /** \brief Current MIDI file (the active document's file) */
    MidiFile *file;

    /**
     * \brief Phase 28: bind the active document's file to all panels, globals
     * and the editor view. The per-file signal wiring runs only the first time
     * a given file is activated (tracked in _connectedFiles), so this is safe
     * to call repeatedly for tab switching without duplicating connections.
     * Does NOT delete any previously-active file.
     */
    void activateDocument(MidiFile *newFile);

    /**
     * \brief Phase 28 (B): rebind the sidebars / globals / selection / tools /
     * transport / MidiPilot / MCP to \a newFile WITHOUT touching any editor
     * view's file binding. The "active document" is the focused view's
     * document; this is what makes the panels follow the focused pane in a
     * side-by-side layout. activateDocument() = setActiveDocument() + rebinding
     * the primary view (the single-view / tab-switch path).
     */
    void setActiveDocument(MidiFile *newFile);

    /**
     * \brief Phase 28: tear down a document's file when it is closed - drop its
     * retained selection + analyzer state, forget its connection bookkeeping,
     * and delete it (QObject destruction auto-disconnects its signals).
     */
    void closeDocumentFile(MidiFile *oldFile);

    /**
     * \brief Phase 28: open `f` as a NEW document/tab (non-destructive - the
     * current document stays open in its own tab) and make it active. \a title
     * overrides the tab label when non-empty (e.g. "song.mid (copy)" for Clone);
     * otherwise the label is derived from the file path.
     */
    void openInNewTab(MidiFile *f, const QString &title = QString());

    /** \brief Phase 28: tab label for a file (basename, or "Untitled"). */
    QString documentTabTitle(MidiFile *f) const;

    /** \brief Phase 28: tab-bar slots (regular members, connected via PMF). */
    void onDocumentTabChanged(int index);
    void onDocumentTabCloseRequested(int index);

    /** \brief Phase 28 (editor groups): group-1 (secondary) tab-bar slots. */
    void onGroup1TabChanged(int index);
    void onGroup1TabCloseRequested(int index);

    /**
     * \brief Phase 28 (editor groups): a tab was dragged+dropped. source ==
     * target reorders within a group; otherwise the document moves between
     * groups (the dropped tab becomes active + focused in its new group; the
     * secondary group collapses if it loses its last tab). The DocumentManagers
     * are updated and both tab bars rebuilt from them.
     */
    void onTabMoveRequested(DocumentTabBar *source, int sourceIndex,
                            DocumentTabBar *target, int targetIndex);

    /** \brief Phase 28 (editor groups): the DocumentManager owning \a bar's tabs. */
    DocumentManager *managerForTabBar(QTabBar *bar) const;

    /**
     * \brief Phase 28 (editor groups): the tab-tools toolbar (New Tab / Split /
     * Clone) placed under the essential toolbar in two-row mode. \a iconSize
     * matches the essential toolbar so the row lines up.
     */
    QToolBar *buildTabToolsBar(QWidget *parent, int iconSize);

    /**
     * \brief Phase 28 (editor groups): a hand-drawn "split editor" glyph (a
     * rounded frame split into two panes) for the Split tab-tool, painted in the
     * theme foreground colour so it matches both light and dark toolbars.
     */
    QIcon makeSplitViewIcon(int size) const;

    /**
     * \brief Phase 28 (editor groups): duplicate the active document into a new
     * tab (an untitled, unsaved copy). Round-trips through a temp .mid; the
     * original's saved-state is preserved (save() would otherwise clear it).
     */
    void cloneCurrentDocument();

    /**
     * \brief Phase 28 (editor groups): rebuild \a bar's tabs from \a mgr (order
     * + active tab), guarded against re-entrant tab signals. Called after a
     * move/reorder so the bar exactly matches the manager.
     */
    void rebuildTabBar(QTabBar *bar, DocumentManager *mgr);

    /**
     * \brief Phase 28 (editor groups): bind ONLY the primary editor view to \a f
     * (OpenGL-wrapper-aware) without touching the active document/sidebars. Used
     * to keep the primary pane showing group 0's document while another group is
     * the focused/active one. activateDocument() = setActiveDocument + this.
     */
    void bindPrimaryView(MidiFile *f);

    /**
     * \brief Phase 28 (editor groups): configure a document tab bar (movable,
     * closable, eliding) - shared by both groups so they look/behave alike.
     */
    void configureDocumentTabBar(QTabBar *bar);

    /**
     * \brief Phase 28 (editor groups): build a group's tab strip widget
     * ( [ + | tab bar | stretch ] ), used identically for both groups so their
     * strips have the same height and sit aligned beside each other.
     */
    QWidget *buildGroupTabStrip(QToolButton *plusButton, QTabBar *bar);

    /**
     * \brief Phase 28 (editor groups): tear down the secondary group (group 1) -
     * delete its view/tab-bar/manager (the Document handles, NOT the MidiFiles;
     * the caller disposes of those) and restore the primary view as focused.
     */
    void tearDownGroup1();

    /**
     * \brief Phase 28 (editor groups): build an EMPTY secondary group (container
     * + tab bar + manager + view, added to the splitter), without moving any
     * document into it. No-op if group 1 already exists. Used by both Split
     * (which then moves a doc over) and session restore (which opens docs into it).
     */
    void ensureGroup1();

    /**
     * \brief Phase 28 (editor groups): persist the open documents of both groups
     * (paths + active tab + split/collapse state) to QSettings, so the session
     * can be restored after a restart (e.g. a theme change). Only documents with
     * a real file path are saved (untitled docs cannot be reopened by path).
     */
    void saveSession();

    /**
     * \brief Phase 28 (editor groups): reopen the documents/groups persisted by
     * saveSession(). \return true if at least one document was reopened (so the
     * caller skips the default new/initial document); false if there was no
     * usable session.
     */
    bool restoreSession();

    /** \brief Phase 28: files whose one-time signal wiring has been done. */
    QSet<MidiFile *> _connectedFiles;

    /** \brief Phase 28: open documents + active-tab tracking. */
    DocumentManager *_documentManager = nullptr;

    /** \brief Phase 28: the tab strip above the editor (one tab per document). */
    QTabBar *_documentTabBar = nullptr;

    /** \brief Phase 28: guards programmatic tab-bar edits from re-entrant slots. */
    bool _suppressTabSignals = false;

    /** \brief Start directory and initialization file */
    QString startDirectory, _initFile;

    /** \brief Widget for editing event properties */
    EventWidget *_eventWidget;

    /** \brief Application settings */
    QSettings *_settings;

    /** \brief List of recent file paths */
    QStringList _recentFilePaths;

    // === Menus ===

    /** \brief Various context and action menus */
    QMenu *_recentPathsMenu, *_deleteChannelMenu, *_moveSelectedEventsToTrackMenu, *_moveSelectedEventsToChannelMenu,
    *_pasteToTrackMenu, *_pasteToChannelMenu, *_selectAllFromTrackMenu, *_selectAllFromChannelMenu, *_pasteOptionsMenu,
    *_copySelectedEventsToTrackMenu, *_copySelectedEventsToChannelMenu;

    /** \brief Lower tab widget for additional panels */
    QTabWidget *lowerTabWidget;

    /** \brief Upper tab widget for tracks and channels */
    QTabWidget *upperTabWidget;

    /** \brief Chooser widget for track/channel selection */
    QWidget *chooserWidget;

    /** \brief Tracks container widget */
    QWidget *tracksWidget;

    /** \brief Channels container widget */
    QWidget *channelsWidget;

    /** \brief Right splitter containing the tab widgets */
    QSplitter *rightSplitter;

    /** \brief Main horizontal splitter */
    QSplitter *mainSplitter;

    /** \brief Actions for color mode selection */
    QAction *_colorsByChannel, *_colorsByTracks;

    /** \brief Combo boxes for track and channel selection */
    QComboBox *_chooseEditTrack, *_chooseEditChannel;

    // === Misc Widget Controls ===

    /** \brief Control widget for misc editor */
    QWidget *_miscWidgetControl;

    /** \brief Layout for misc control widgets */
    QGridLayout *_miscControlLayout;

    /** \brief Combo boxes for misc editor configuration */
    QComboBox *_miscMode, *_miscController, *_miscChannel;

    // === Actions ===

    /** \brief Various UI actions for tools and operations */
    QAction *setSingleMode, *setLineMode, *setFreehandMode, *_allChannelsVisible, *_allChannelsInvisible,
    *_allTracksAudible, *_allTracksMute, *_allChannelsAudible, *_allChannelsMute, *_allTracksVisible,
    *_allTracksInvisible, *stdToolAction, *undoAction, *redoAction, *_pasteAction;

    /** \brief Misc editor widget */
    MiscWidget *_miscWidget;

    /** \brief Lyric timeline widget */
    LyricTimelineWidget *_lyricTimeline;

    /** \brief Container widget for lyric timeline (with label) */
    QWidget *_lyricArea;

    /** \brief Action to toggle lyric timeline visibility */
    QAction *_toggleLyricTimeline;

    /** \brief Phase 32.2: action to toggle FFXIV voice-load overlay on the matrix */
    QAction *_toggleVoiceLoadOverlay = nullptr;

    /** \brief Phase 32.3: voice-load lane (graph beneath the velocity lane) */
    FfxivVoiceLaneWidget *_voiceLaneWidget = nullptr;
    QWidget *_voiceLaneArea = nullptr;
    QAction *_toggleVoiceLaneAction = nullptr;
    /** \brief 1.6.1 (UX-VOICE-LANE-002): auto-show the voice lane while FFXIV
     *  SoundFont Mode is active, hide it again when FFXIV mode flips off.
     *  Composes with `_toggleVoiceLaneAction` (always-show) via OR. */
    QAction *_voiceLaneAutoFollowAction = nullptr;

    /** \brief 1.6.1 (UX-VOICE-LANE-002): apply the (always-show OR auto+ffxiv-on)
     *  visibility rule to `_voiceLaneArea`. Safe to call from any signal. */
    void updateVoiceLaneVisibility();

    // === Setup and Toolbar Methods ===

    /**
     * \brief Sets up actions and returns the action widget.
     * \param parent The parent widget
     * \return Pointer to the created action widget
     */
    QWidget *setupActions(QWidget *parent);

    /**
     * \brief Rebuilds the toolbar from current settings.
     */
    void rebuildToolbar();

    /**
     * \brief Gets an action by its identifier.
     * \param actionId The action identifier string
     * \return Pointer to the QAction, or nullptr if not found
     */
    QAction *getActionById(const QString &actionId);

    /**
     * \brief Creates a custom toolbar widget.
     * \param parent The parent widget
     * \return Pointer to the created toolbar widget
     */
    QWidget *createCustomToolbar(QWidget *parent);

    /**
     * \brief Creates a simple custom toolbar widget.
     * \param parent The parent widget
     * \return Pointer to the created simple toolbar widget
     */
    QWidget *createSimpleCustomToolbar(QWidget *parent);

    /**
     * \brief Updates toolbar contents with current actions.
     * \param toolbarWidget The toolbar widget to update
     * \param layout The layout to update
     */
    void updateToolbarContents(QWidget *toolbarWidget, QGridLayout *layout);

    /**
     * \brief Gets default actions for toolbar placeholder.
     * \return List of ToolbarActionInfo for default actions
     */
    QList<ToolbarActionInfo> getDefaultActionsForPlaceholder();

    // === Quantization and Timing ===

    /** \brief Current quantization grid setting */
    int _quantizationGrid;

    /**
     * \brief Quantizes a time value to the nearest grid position.
     * \param t Time value to quantize
     * \param ticks List of available tick positions
     * \return Quantized time value
     */
    int quantize(int t, QList<int> ticks);

    // === Action Management ===

    /** \brief Actions that should be activated when selections are made */
    QList<QAction *> _activateWithSelections;

    /** \brief Main toolbar widget container */
    QWidget *_toolbarWidget;

    /** \brief Map of action IDs to QAction objects for quick lookup */
    QMap<QString, QAction *> _actionMap;

    /** \brief Map of action IDs to their default shortcuts */
    QMap<QString, QList<QKeySequence>> _defaultShortcuts;

    // === Advanced Features ===

    /** \brief Current tweak target for parameter adjustment */
    TweakTarget *currentTweakTarget;

    /** \brief Navigator for selection management */
    SelectionNavigator *selectionNavigator;

    /** \brief Update checker instance */
    UpdateChecker *_updateChecker;

    /** \brief Auto-updater for downloading and applying updates */
    AutoUpdater *_autoUpdater = nullptr;

    /** \brief When true, closeEvent skips all save dialogs (auto-update in progress) */
    bool _forceCloseForUpdate = false;

    /** \brief MidiPilot AI sidebar widget */
    MidiPilotWidget *_midiPilotWidget = nullptr;

#ifdef MIDIEDITOR_COLLAB_ENABLED
    /** \brief Phase 9.11 §15.3: in-session chat panel. Owned by
     *  lowerTabWidget which deletes it on tab destruction; we keep a
     *  raw pointer for signal/slot wiring + unread-badge mgmt. */
    class CollabChatWidget *_collabChatWidget = nullptr;
    /** \brief Phase 9.11 §15.3: unread-message count shown in the tab
     *  title when the user is on another tab. Reset to 0 on tab
     *  activation. */
    int _chatUnreadCount = 0;
    /** \brief Phase 9.11 §15.3 polish: alternates the Chat tab's text
     *  color while unread messages are pending and the user is on
     *  another tab. Stops on activation. Raw pointer is owned via
     *  Qt parent (this); created on demand. */
    class QTimer *_chatBlinkTimer = nullptr;
    /** \brief Blink-phase state: true → highlight color, false → normal. */
    bool _chatBlinkOn = false;

    /** \brief Phase 9.9c §15.2: top-level menus that contain MIDI-
     *  mutating actions and must be disabled when the local peer is a
     *  Show-mode viewer. Pointers captured at menubar build time so
     *  the applyShowModeLock lambda (defined earlier in the
     *  constructor) can toggle them without a deep refactor. */
    QMenu *_editMenuForShowLock = nullptr;
    QMenu *_toolsMenuForShowLock = nullptr;
    QMenu *_midiMenuForShowLock = nullptr;

    /** \brief Phase 9.9f §15.2 (follow-the-host): last viewport tuple
     *  captured from MatrixWidget::scrollChanged. Used by the
     *  presenter-side broadcast to ship its current scroll position
     *  alongside visibility state — the matrix doesn't expose
     *  query-time getters for these, so we shadow them as they fly by. */
    int _viewStartMs   = 0;
    int _viewMaxMs     = 0;
    int _viewStartLine = 0;
    int _viewMaxLine   = 0;
    /** \brief Re-entry guard: viewer-side apply of an incoming
     *  viewState would itself trigger scrollChanged + emit another
     *  broadcast, ping-ponging forever. Set while applying, checked
     *  by the host-side broadcaster. */
    bool _applyingRemoteViewState = false;
    /** \brief Same idea for the playback trigger: viewer's local
     *  play() / stop() shouldn't re-broadcast back to the presenter. */
    bool _applyingRemotePlayback = false;
#endif

    /** \brief MCP Server for external AI client integration */
    McpServer *_mcpServer = nullptr;

    /** \brief MIDI visualizer in toolbar */
    MidiVisualizerWidget *_visualizer = nullptr;

    /** \brief Retro cursor-time display in toolbar (Phase 41) */
    class TimeDisplayWidget *_timeDisplay = nullptr;

    /** \brief Lyric visualizer (karaoke display) in toolbar */
    LyricVisualizerWidget *_lyricVisualizer = nullptr;

    /** \brief MCP server toggle button in toolbar */
    McpToggleWidget *_mcpToggleWidget = nullptr;
    class FfxivToggleWidget *_ffxivToggleWidget = nullptr;
    class C64ToggleWidget *_c64ToggleWidget = nullptr;
    class C64ModeSwitchWidget *_c64ModeSwitch = nullptr;

    /** \brief FFXIV voice-load gauge in toolbar (Phase 32.1) */
    class FfxivVoiceGaugeWidget *_ffxivVoiceGauge = nullptr;

    /** \brief Status bar labels for cursor/selection/chord info */
    QLabel *_statusCursorLabel = nullptr;
    QLabel *_statusSelectionLabel = nullptr;
    QLabel *_statusChordLabel = nullptr;

    /** \brief Status bar indicator for active LAN Live Session (hosting or
     *  joined). Hidden when role is Idle. Built-in even when collab is
     *  compiled out, so the layout stays stable. */
    QLabel *_statusLiveSessionLabel = nullptr;

    /** \brief Dock widget containing MidiPilot */
    QDockWidget *_midiPilotDock = nullptr;

    /** \brief Action to toggle MidiPilot visibility */
    QAction *_toggleMidiPilotAction;

#ifdef FLUIDSYNTH_SUPPORT
    /** \brief Action to export audio */
    QAction *_exportAudioAction = nullptr;
    QAction *_exportMusicXmlAction = nullptr;

    /** \brief Progress dialog for audio export */
    QProgressDialog *_exportProgressDialog = nullptr;

    /** \brief Temp MIDI file path to clean up after export (for Guitar Pro etc.) */
    QString _exportTempMidiPath;

    /** \brief Cancel flag for the authentic-SID export worker (set by the
     *  progress dialog's Cancel; read by SidAudioPlayer::exportToFile / LAME). */
    std::atomic<bool> _sidExportCancel{false};

    void startExport(const ExportOptions &opts);
    /** \brief Render the ORIGINAL loaded .sid (libsidplayfp) to \a outputPath
     *  for [fromMs, toMs); wav/ogg/flac via libsndfile, mp3 via temp WAV + LAME. */
    void startSidExport(const QString &outputPath, const QString &fileType,
                        int fromMs, int toMs, int oggQuality, int mp3Bitrate);
    void onExportFinished(bool success, const QString &message);
    void onExportCancelled();
#endif

    /** \brief Whether the current update check should be silent (no UI if no update) */
    bool _silentUpdateCheck;

    // === Auto-Save ===

    /** \brief Debounce timer for auto-save — resets on every edit */
    QTimer *_autoSaveTimer = nullptr;

    /** \brief Performs auto-save to a sidecar backup file */
    void performAutoSave();

    /** \brief Returns the auto-save backup path for the current file */
    QString autoSavePath() const;

    /** \brief Removes auto-save sidecar files and stops the timer */
    void cleanupAutoSave();

    /** \brief Checks for leftover auto-save files on startup and offers recovery.
     *  \return true if a document was actually recovered (and is now open), so
     *  the caller must NOT also open a blank/initial document. */
    bool checkAutoSaveRecovery();
};

#endif // MAINWINDOW_H_
