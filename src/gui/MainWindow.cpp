/*
 * MidiEditor
 * Copyright (C) 2010  Markus Schwenk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.+
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QProgressDialog>
#include <QScopedPointer>
#include <QScopedValueRollback>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QKeyEvent>
#include <QTimer>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QStatusBar>
#include <QLabel>
#include <QLocale>
#include <QProcess>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <cmath>
#include <algorithm>
#include <optional>
#include <QComboBox>

#include "Appearance.h"
#include "AboutDialog.h"
#ifdef MIDIEDITOR_COLLAB_ENABLED
#include "../collab/CollabIdentity.h"
#include "../collab/CollabService.h"
#include "../collab/LanLiveSession.h"
#include "../collab/PrBundle.h"
#include "collab/CollabChatWidget.h"
#include "collab/CollabHistoryWidget.h"
#include "collab/LanLiveJoinDialog.h"
#include "collab/LanLiveStartDialog.h"
#include "collab/PrCreateDialog.h"
#include "collab/PrReviewDialog.h"
#include "collab/ReturningPeerDialog.h"
#include "collab/WelcomeBackDialog.h"
#endif
#ifdef MIDIEDITOR_WEBRTC_ENABLED
#include "../collab/WebRtcSmokeTest.h"
#include "collab/WebRtcStartDialog.h"
#include "collab/WebRtcJoinDialog.h"
#include "../collab/RtcRendezvousClient.h"
#endif
#include "ChannelVisibilityManager.h"
#include "ChannelListWidget.h"
#include "CompleteMidiSetupDialog.h"
#include "DeleteOverlapsDialog.h"
#include "ExplodeChordsDialog.h"
#include "EventWidget.h"
#include "FileLengthDialog.h"
#include "InstrumentChooser.h"
#include "LayoutSettingsWidget.h"
#include "SplitChannelsDialog.h"
#include "DrumKitPreset.h"
#include "MatrixWidget.h"
#include "OpenGLMatrixWidget.h"
#include "OpenGLMiscWidget.h"
#include "MiscWidget.h"
#include "LyricImportDialog.h"
#include "LyricSyncDialog.h"
#include "LyricTimelineWidget.h"
#include "FfxivVoiceLaneWidget.h"
#include "../midi/LyricManager.h"
#include "../converter/LrcExporter.h"
#include "PerformanceSettingsWidget.h"
#include "NToleQuantizationDialog.h"
#include "TempoConversionDialog.h"
#include "ProtocolWidget.h"
#include "RecordDialog.h"
#include "SelectionNavigator.h"
#include "SettingsDialog.h"
#include "StrummerDialog.h"
#include "TrackListWidget.h"
#include "TransposeDialog.h"
#include "TweakTarget.h"
#include "UpdateChecker.h"
#include "UpdateDialogs.h"
#include "AutoUpdater.h"
#include "MidiPilotWidget.h"
#include "DocumentManager.h"
#include "DocumentTabBar.h"
#include "MidiVisualizerWidget.h"
#include "TimeDisplayWidget.h"
#include "LyricVisualizerWidget.h"
#include "McpToggleWidget.h"
#include "FfxivToggleWidget.h"
#include "C64ToggleWidget.h"
#include "C64ModeSwitchWidget.h"
#include "C64Mode.h"
#include "C64SoundFontHelper.h"
#include "ImportOnlyFormats.h"
#include "FfxivVoiceGaugeWidget.h"
#include "AiSettingsWidget.h"

#include "../ai/McpServer.h"
#include "../ai/FfxivVoiceAnalyzer.h"

#include <QDockWidget>

#include "../tool/DeleteOverlapsTool.h"
#include "../tool/EraserTool.h"
#include "../tool/EventMoveTool.h"
#include "../tool/EventTool.h"
#include "../tool/GlueTool.h"
#include "../tool/MeasureTool.h"
#include "../tool/NewNoteTool.h"
#include "../tool/ScissorsTool.h"
#include "../tool/SelectTool.h"
#include "../tool/Selection.h"
#include "../tool/SharedClipboard.h"
#include "../tool/SizeChangeTool.h"
#include "../tool/StandardTool.h"
#include "../tool/StrummerTool.h"
#include "../tool/TempoTool.h"
#include "PasteSpecialDialog.h"
#include "../tool/TimeSignatureTool.h"
#include "../tool/EditorTool.h"
#include "../tool/Tool.h"
#include "../tool/ToolButton.h"

#include "../Terminal.h"
#include "../protocol/Protocol.h"
#include "../ai/FFXIVChannelFixer.h"
#include "FFXIVFixerDialog.h"
#include "FfxivEqualizerDialog.h"
#include "../converter/GuitarPro/GpImporter.h"
#include "../converter/MML/MmlImporter.h"
#include "../converter/MusicXml/MusicXmlImporter.h"
#include "../converter/MusicXml/MusicXmlWriter.h"
#include "../converter/Score/MidiToScore.h"
#include "../converter/Sid/SidImporter.h"
#include "../midi/SidAudioPlayer.h"
#include "../converter/MusicXml/MsczImporter.h"

#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/OnEvent.h"
#include "../MidiEvent/TextEvent.h"
#include "../MidiEvent/TimeSignatureEvent.h"
#include "../midi/ChordDetector.h"
#include "../MidiEvent/PitchBendEvent.h"
#include "../MidiEvent/ProgChangeEvent.h"
#include "../midi/Metronome.h"
#include "../midi/MidiChannel.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiInput.h"
#include "../midi/MidiOutput.h"
#include "../midi/MidiPlayer.h"
#include "../midi/MidiTrack.h"
#include "../midi/PlayerThread.h"
#include "../midi/InstrumentDefinitions.h"

#ifdef FLUIDSYNTH_SUPPORT
#include "../midi/FluidSynthEngine.h"
#include "../midi/FfxivEqualizerService.h"
#ifdef LAME_SUPPORT
#include "../midi/LameEncoder.h"
#endif
#include "ExportDialog.h"
#include <QFile>
#include <QProgressDialog>
#include <QThreadPool>
#endif

// Mirror the editor's channel/track mute state onto the 3 authentic-SID voices
// (defined below, used from the constructor + setFile + play()).
static void syncSidVoiceMutes(MidiFile *file);

MainWindow::MainWindow(QString initFile)
    : QMainWindow()
      , _initFile(initFile) {
    file = 0;
    _settings = new QSettings(QString("MidiEditor"), QString("NONE"));

    _moveSelectedEventsToChannelMenu = 0;
    _moveSelectedEventsToTrackMenu = 0;
    _copySelectedEventsToChannelMenu = 0;
    _copySelectedEventsToTrackMenu = 0;
    _toolbarWidget = nullptr;
    _updateChecker = nullptr;
    _silentUpdateCheck = false;

    Appearance::init(_settings);

    bool alternativeStop = _settings->value("alt_stop", false).toBool();
    MidiOutput::isAlternativePlayer = alternativeStop;
    bool ticksOK;
    int ticksPerQuarter = _settings->value("ticks_per_quarter", 192).toInt(&ticksOK);
    MidiFile::defaultTimePerQuarter = ticksPerQuarter;
    bool magnet = _settings->value("magnet", false).toBool();
    EventTool::enableMagnet(magnet);
    // Snap behaviour when magnet is on: Modern (hard-snap to nearest grid,
    // like a DAW; default) vs Legacy (magnetic pull within a few pixels).
    EventTool::setModernSnap(_settings->value("snap_modern", true).toBool());

    MidiInput::setThruEnabled(_settings->value("thru", false).toBool());

    // Initialize shared clipboard for inter-process copy/paste
    SharedClipboard::instance()->initialize();
    Metronome::setEnabled(_settings->value("metronome", false).toBool());
    bool loudnessOk;
    Metronome::setLoudness(_settings->value("metronome_loudness", 100).toInt(&loudnessOk));

#ifdef FLUIDSYNTH_SUPPORT
    FluidSynthEngine::instance()->loadSettings(_settings);
    // Phase 39 (FFXIV-EQ-001): seed the equalizer service from QSettings
    // so the active preset is restored before any playback starts.
    FfxivEqualizerService::instance()->loadFromSettings(_settings);
    connect(FluidSynthEngine::instance(), &FluidSynthEngine::engineRestarted, this, [this]() {
        if (this->file && MidiOutput::isConnected()) {
            MidiOutput::resetChannelPrograms();
            for (int ch = 0; ch < 16; ch++) {
                int prog = this->file->channel(ch)->progAtTick(0);
                if (prog >= 0) {
                    MidiOutput::sendProgram(ch, prog);
                }
            }
        }
    });

    // Phase 32.5 - Auto-bind FFXIV Voice Limiter analyser to FFXIV SoundFont Mode.
    // The analyser is a no-op when disabled (zero perf cost for non-FFXIV users).
    // The user can override with FFXIV/voiceLimiter/userOverride = "off" or "on".
    {
        FluidSynthEngine *engine = FluidSynthEngine::instance();
        QString override = _settings->value("FFXIV/voiceLimiter/userOverride").toString();
        bool initial;
        if (override == "off")      initial = false;
        else if (override == "on")  initial = true;
        else                        initial = engine && engine->ffxivSoundFontMode();
        FfxivVoiceAnalyzer::instance()->setEnabled(initial);

        if (engine) {
            connect(engine, &FluidSynthEngine::ffxivSoundFontModeChanged,
                    this, [this](bool ffxivOn) {
                QString ov = _settings->value("FFXIV/voiceLimiter/userOverride").toString();
                if (ov == "off" || ov == "on")
                    return; // user chose explicitly; don't auto-toggle
                FfxivVoiceAnalyzer::instance()->setEnabled(ffxivOn);
            });
        }
    }
#endif

    _quantizationGrid = _settings->value("quantization", 3).toInt();

    // Load instrument definitions
    QString insFile = _settings->value("InstrumentDefinitions/file").toString();
    if (!insFile.isEmpty()) {
        InstrumentDefinitions::instance()->load(insFile);
        QString insName = _settings->value("InstrumentDefinitions/instrument").toString();
        if (!insName.isEmpty()) {
            InstrumentDefinitions::instance()->selectInstrument(insName);
        }
    }
    // Always load overrides
    InstrumentDefinitions::instance()->loadOverrides(_settings);

    // metronome
    connect(MidiPlayer::playerThread(), SIGNAL(measureChanged(int, int)), Metronome::instance(), SLOT(measureUpdate(int, int)));
    connect(MidiPlayer::playerThread(), SIGNAL(measureUpdate(int, int)), Metronome::instance(), SLOT(measureUpdate(int, int)));
    connect(MidiPlayer::playerThread(), SIGNAL(meterChanged(int, int)), Metronome::instance(), SLOT(meterChanged(int, int)));
    connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), Metronome::instance(), SLOT(playbackStopped()));
    connect(MidiPlayer::playerThread(), SIGNAL(playerStarted()), Metronome::instance(), SLOT(playbackStarted()));

    // C64 engine handover safety: before switching the SF2<->EMU engine the
    // helper must stop the transport (it tears down the FluidSynth synth / MIDI
    // port, which use-after-frees if the player thread is still running). Route
    // that through the real Stop path so the player thread is joined and the UI
    // (cursor, panel locks, panic) resets exactly like the Stop button.
    C64Mode::setStopPlaybackHook([this] {
        if (MidiPlayer::isPlaying() || SidAudioPlayer::instance()->isPlaying())
            stop();
    });

    // MIDI visualizer â€” register a plain QAction for the toolbar customize list.
    // The actual widget is created fresh in each toolbar build (see createCustomToolbar/
    // updateToolbarContents) because QWidgetAction::setDefaultWidget() reparents the
    // widget to the toolbar, causing it to be destroyed on toolbar rebuild.
    _visualizer = nullptr;  // Created on-demand in toolbar build
    QAction *visualizerAction = new QAction(this);
    visualizerAction->setText(tr("MIDI Visualizer"));
    visualizerAction->setToolTip(tr("MIDI activity visualizer â€” shows per-channel velocity during playback"));
    _actionMap["midi_visualizer"] = visualizerAction;

    // Phase 41: retro cursor-time display. Registers a plain QAction for the
    // toolbar customize list; the widget itself is built on demand in each
    // toolbar build (same lifecycle reason as the visualizer above).
    _timeDisplay = nullptr;  // Created on-demand in toolbar build
    QAction *timeDisplayAction = new QAction(this);
    timeDisplayAction->setText(tr("Cursor Time"));
    timeDisplayAction->setToolTip(tr("Retro time display — cursor / playback time, "
                                     "length, remaining, BPM, bar; click to cycle"));
    _actionMap["time_display"] = timeDisplayAction;

    _lyricVisualizer = nullptr;  // Created on-demand in toolbar build
    QAction *lyricVisAction = new QAction(this);
    lyricVisAction->setText(tr("Lyric Visualizer"));
    lyricVisAction->setToolTip(tr("Live lyric display \u2014 shows current lyrics during playback (karaoke-style)"));
    _actionMap["lyric_visualizer"] = lyricVisAction;

    _mcpToggleWidget = nullptr;  // Created on-demand in toolbar build
    QAction *mcpToggleAction = new QAction(this);
    mcpToggleAction->setText(tr("MCP Server"));
    mcpToggleAction->setToolTip(tr("Toggle MCP Server on/off"));
    _actionMap["mcp_toggle"] = mcpToggleAction;

    _ffxivToggleWidget = nullptr;  // Created on-demand in toolbar build
    QAction *ffxivToggleAction = new QAction(this);
    ffxivToggleAction->setText(tr("FFXIV SoundFont Mode"));
    ffxivToggleAction->setToolTip(tr("Toggle FFXIV SoundFont Mode on/off"));
    _actionMap["ffxiv_toggle"] = ffxivToggleAction;

    // Phase 42.2: C64 SoundFont Mode toggle (created on-demand in toolbar build).
    _c64ToggleWidget = nullptr;
    QAction *c64ToggleAction = new QAction(this);
    c64ToggleAction->setText(tr("C64 SoundFont Mode"));
    c64ToggleAction->setToolTip(tr("Toggle C64 SoundFont Mode (SID waveform presets) on/off"));
    _actionMap["c64_toggle"] = c64ToggleAction;

    // Phase 42.3: retro SF2<>EMU engine switch (created on-demand in toolbar build).
    _c64ModeSwitch = nullptr;
    QAction *c64ModeSwitchAction = new QAction(this);
    c64ModeSwitchAction->setText(tr("C64 Engine Switch (SF2/EMU)"));
    c64ModeSwitchAction->setToolTip(tr("Retro toolbar switch to pick the C64 engine: SoundFont or Emulation"));
    _actionMap["c64_mode_switch"] = c64ModeSwitchAction;

    _ffxivVoiceGauge = nullptr;  // Created on-demand in toolbar build (Phase 32.1)
    QAction *ffxivVoiceGaugeAction = new QAction(this);
    ffxivVoiceGaugeAction->setText(tr("FFXIV Voice Gauge"));
    ffxivVoiceGaugeAction->setToolTip(tr("FFXIV voice-load gauge \u2014 shows how many of the 16 simultaneous voices are in use"));
    _actionMap["ffxiv_voice_gauge"] = ffxivVoiceGaugeAction;

    startDirectory = QDir::homePath();

    if (_settings->value("open_path").toString() != "") {
        startDirectory = _settings->value("open_path").toString();
    } else {
        _settings->setValue("open_path", startDirectory);
    }

    // read recent paths
    _recentFilePaths = _settings->value("recent_file_list").toStringList();

    EditorTool::setMainWindow(this);

    setWindowTitle(QApplication::applicationName() + " v" + QApplication::applicationVersion());
    // Note: setWindowIcon doesn't use QAction, so we keep the direct approach
    setWindowIcon(Appearance::adjustIconForDarkMode(":/run_environment/graphics/icon.png"));

    QWidget *central = new QWidget(this);
    QGridLayout *centralLayout = new QGridLayout(central);
    centralLayout->setContentsMargins(3, 3, 3, 5);

    // there is a vertical split
    mainSplitter = new QSplitter(Qt::Horizontal, central);
    //mainSplitter->setHandleWidth(0);

    // The left side
    QSplitter *leftSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    leftSplitter->setHandleWidth(2); // Enable handle width for misc widget collapse/expand
    mainSplitter->addWidget(leftSplitter);
    leftSplitter->setContentsMargins(0, 0, 0, 0);

    // The right side
    rightSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    //rightSplitter->setHandleWidth(0);
    mainSplitter->addWidget(rightSplitter);

    // Set the sizes of mainSplitter
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 0);
    mainSplitter->setContentsMargins(0, 0, 0, 0);

    // the channelWidget and the trackWidget are tabbed
    upperTabWidget = new QTabWidget(rightSplitter);
    rightSplitter->addWidget(upperTabWidget);
    rightSplitter->setContentsMargins(0, 0, 0, 0);

    // protocolList and EventWidget are tabbed
    lowerTabWidget = new QTabWidget(rightSplitter);
    rightSplitter->addWidget(lowerTabWidget);

    // MatrixArea
    QWidget *matrixArea = new QWidget(leftSplitter);
    leftSplitter->addWidget(matrixArea);
    matrixArea->setContentsMargins(0, 0, 0, 0);

    // Create MatrixWidget - use direct OpenGL acceleration approach
    bool useHardwareAcceleration = _settings->value("rendering/hardware_acceleration", false).toBool();

    // Check for high DPI scaling issues with OpenGL
    qreal dpr = devicePixelRatio();
    bool hasHighDpiScaling = (dpr != 1.0 && !Appearance::ignoreSystemScaling());

    if (useHardwareAcceleration && hasHighDpiScaling) {
        qWarning() << "MainWindow: High DPI scaling detected (DPR:" << dpr << ") with hardware acceleration enabled.";
        qWarning() << "MainWindow: Automatically disabling hardware acceleration due to Qt6 high DPI compatibility issues.";
        qWarning() << "MainWindow: To use hardware acceleration, enable 'Ignore system UI scaling' in Performance settings.";
        useHardwareAcceleration = false; // Automatically disable to prevent rendering issues
    }

    QWidget *matrixContainer;

    if (useHardwareAcceleration) {
        // Use direct OpenGL acceleration
        OpenGLMatrixWidget *openglMatrix = new OpenGLMatrixWidget(_settings, matrixArea);
        mw_matrixWidget = openglMatrix->getMatrixWidget(); // Get the internal MatrixWidget for data access
        _matrixWidgetContainer = openglMatrix; // Store the displayed widget for UI operations
        matrixContainer = openglMatrix;
        // Set up OpenGL container for cursor operations
        EditorTool::setOpenGLContainer(openglMatrix);
        // Direct OpenGL acceleration - no separate accelerator needed
        qDebug() << "Created MatrixWidget with direct OpenGL acceleration";
    } else {
        // Use software rendering
        mw_matrixWidget = new MatrixWidget(_settings, matrixArea);
        _matrixWidgetContainer = mw_matrixWidget; // Same widget for both data and UI
        matrixContainer = mw_matrixWidget;
        // No OpenGL container needed for software rendering
        EditorTool::setOpenGLContainer(nullptr);
        // Software rendering - no accelerator needed
        qDebug() << "Created MatrixWidget with software rendering";
    }

    // Phase 28 (B): clicking/focusing the primary view makes its document the
    // active one again (mirrors the compare pane's wiring). No-op in single
    // view since the primary already shows the active document.
    connect(mw_matrixWidget, &MatrixWidget::focusReceived, this, &MainWindow::onViewFocused);
    _activeView = mw_matrixWidget; // the primary view is focused by default

    vert = new QScrollBar(Qt::Vertical, matrixArea);

    // Phase 28 (editor groups): the editor area is a horizontal splitter of
    // editor GROUPS. Each group is a vertical [ tab strip | body ] stack with
    // its OWN tab bar, so a second group sits beside the first with its tab
    // strip at the same height (VS Code-style). Group 0 (the primary) is built
    // here; group 1 is created on demand in toggleCompareView().
    _documentManager = new DocumentManager();
    DocumentTabBar *group0Bar = new DocumentTabBar(matrixArea);
    _documentTabBar = group0Bar;
    configureDocumentTabBar(_documentTabBar);
    connect(_documentTabBar, &QTabBar::currentChanged, this, &MainWindow::onDocumentTabChanged);
    connect(_documentTabBar, &QTabBar::tabCloseRequested, this, &MainWindow::onDocumentTabCloseRequested);
    connect(group0Bar, &DocumentTabBar::tabMoveRequested, this, &MainWindow::onTabMoveRequested);

    // Phase 28: a "+" New-Tab button so a new document can be opened in a tab
    // from any toolbar layout (the freed toolbar space only exists in two-row
    // mode). It triggers the same path as File > New, which now opens a new
    // tab. Placed to the LEFT of the tabs so it always hugs the first tab - a
    // QTabBar reserves extra width with few tabs, so a trailing "+" would float
    // away from a single tab until more tabs fill that reserved space.
    QToolButton *newTabButton = new QToolButton(matrixArea);
    newTabButton->setIcon(Appearance::adjustIconForDarkMode(":/run_environment/graphics/tool/add.png"));
    newTabButton->setToolTip(tr("New tab (new empty document)"));
    newTabButton->setAutoRaise(true);
    // This "+" belongs to the primary group (group 0): focus it first so the new
    // tab opens here even if the secondary group currently has focus.
    connect(newTabButton, &QToolButton::clicked, this, [this] {
        _activeView = mw_matrixWidget;
        newFile();
    });

    // Group 0's tab strip ( [ + | tabs ] ), built the same way as group 1's so
    // the two strips have matching heights.
    QWidget *tabStripRow = buildGroupTabStrip(newTabButton, _documentTabBar);

    // Group 0's body keeps the original editor grid: the matrix view in column 0
    // and the vertical scrollbar in column 1, offset 50px down by a spacer so it
    // aligns just below the timeline ruler drawn at the top of the matrix view.
    QWidget *group0Body = new QWidget();
    QGridLayout *group0BodyLayout = new QGridLayout(group0Body);
    group0BodyLayout->setContentsMargins(0, 0, 0, 0);
    group0BodyLayout->setHorizontalSpacing(6);
    QWidget *placeholder0 = new QWidget(group0Body);
    placeholder0->setFixedHeight(50);
    group0BodyLayout->addWidget(matrixContainer, 0, 0, 2, 1);
    group0BodyLayout->addWidget(placeholder0, 0, 1, 1, 1);
    group0BodyLayout->addWidget(vert, 1, 1, 1, 1);
    group0BodyLayout->setColumnStretch(0, 1);

    // Group 0 container = [ tab strip | body ]. A 2px transparent border is
    // reserved so the drop-target highlight (a coloured border) can appear
    // without shifting the layout; the #objectName selector keeps the border
    // off the child widgets.
    QWidget *group0Container = new QWidget();
    _group0Container = group0Container;
    group0Container->setObjectName("editorGroup0");
    group0Container->setStyleSheet("#editorGroup0 { border: 2px solid transparent; }");
    QVBoxLayout *group0Layout = new QVBoxLayout(group0Container);
    group0Layout->setContentsMargins(0, 0, 0, 0);
    group0Layout->setSpacing(0);
    group0Layout->addWidget(tabStripRow, 0);
    group0Layout->addWidget(group0Body, 1);

    // The editor groups live inside a horizontal splitter. With a single group
    // it holds only group 0's container and looks identical to before.
    _viewSplitter = new QSplitter(Qt::Horizontal, matrixArea);
    _viewSplitter->setChildrenCollapsible(false);
    _viewSplitter->setHandleWidth(2);
    _viewSplitter->addWidget(group0Container);

    QGridLayout *matrixAreaLayout = new QGridLayout(matrixArea);
    matrixAreaLayout->setContentsMargins(0, 0, 0, 0);
    matrixAreaLayout->addWidget(_viewSplitter, 0, 0, 1, 1);
    matrixAreaLayout->setColumnStretch(0, 1);
    matrixArea->setLayout(matrixAreaLayout);

    bool screenLocked = _settings->value("screen_locked", false).toBool();
    mw_matrixWidget->setScreenLocked(screenLocked);
    int div = _settings->value("div", 2).toInt();
    mw_matrixWidget->setDiv(div);

    // VelocityArea
    QWidget *velocityArea = new QWidget(leftSplitter);
    velocityArea->setContentsMargins(0, 0, 0, 0);
    leftSplitter->addWidget(velocityArea);
    
    QGridLayout *velocityAreaLayout = new QGridLayout(velocityArea);
    velocityAreaLayout->setContentsMargins(0, 0, 0, 0);
    velocityAreaLayout->setHorizontalSpacing(6);
    _miscWidgetControl = new QWidget(velocityArea);
    _miscWidgetControl->setFixedWidth(110 - velocityAreaLayout->horizontalSpacing());

    velocityAreaLayout->addWidget(_miscWidgetControl, 0, 0, 1, 1);
    // there is a Scrollbar on the right side of the velocityWidget doing
    // nothing but making the VelocityWidget as big as the matrixWidget
    QScrollBar *scrollNothing = new QScrollBar(Qt::Vertical, velocityArea);
    scrollNothing->setMinimum(0);
    scrollNothing->setMaximum(0);
    velocityAreaLayout->addWidget(scrollNothing, 0, 2, 1, 1);
    velocityAreaLayout->setRowStretch(0, 1);
    velocityArea->setLayout(velocityAreaLayout);

    // Lyric Timeline area (between velocity and scrollbar)
    _lyricArea = new QWidget(leftSplitter);
    _lyricArea->setContentsMargins(0, 0, 0, 0);
    QGridLayout *lyricAreaLayout = new QGridLayout(_lyricArea);
    lyricAreaLayout->setContentsMargins(0, 0, 0, 0);
    lyricAreaLayout->setHorizontalSpacing(6);

    QLabel *lyricLabel = new QLabel(tr("Lyrics"), _lyricArea);
    lyricLabel->setFixedWidth(110 - lyricAreaLayout->horizontalSpacing());
    lyricLabel->setAlignment(Qt::AlignCenter);
    QFont lyricLabelFont = lyricLabel->font();
    lyricLabelFont.setBold(true);
    lyricLabel->setFont(lyricLabelFont);
    lyricAreaLayout->addWidget(lyricLabel, 0, 0, 1, 1);

    _lyricTimeline = new LyricTimelineWidget(mw_matrixWidget, _lyricArea);
    lyricAreaLayout->addWidget(_lyricTimeline, 0, 1, 1, 1);

    QScrollBar *lyricScrollNothing = new QScrollBar(Qt::Vertical, _lyricArea);
    lyricScrollNothing->setMinimum(0);
    lyricScrollNothing->setMaximum(0);
    lyricAreaLayout->addWidget(lyricScrollNothing, 0, 2, 1, 1);

    lyricAreaLayout->setRowStretch(0, 1);
    _lyricArea->setLayout(lyricAreaLayout);
    _lyricArea->setMinimumHeight(0);
    _lyricArea->setMaximumHeight(200);
    _lyricArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    leftSplitter->addWidget(_lyricArea);

    // Phase 32.3: FFXIV Voice Load Lane (read-only graph beneath the lyric timeline)
    _voiceLaneArea = new QWidget(leftSplitter);
    _voiceLaneArea->setContentsMargins(0, 0, 0, 0);
    QGridLayout *voiceLaneAreaLayout = new QGridLayout(_voiceLaneArea);
    voiceLaneAreaLayout->setContentsMargins(0, 0, 0, 0);
    voiceLaneAreaLayout->setHorizontalSpacing(6);

    QLabel *voiceLaneLabel = new QLabel(tr("FFXIV\nVoices"), _voiceLaneArea);
    voiceLaneLabel->setFixedWidth(110 - voiceLaneAreaLayout->horizontalSpacing());
    voiceLaneLabel->setAlignment(Qt::AlignCenter);
    QFont voiceLaneFont = voiceLaneLabel->font();
    voiceLaneFont.setBold(true);
    voiceLaneLabel->setFont(voiceLaneFont);
    voiceLaneAreaLayout->addWidget(voiceLaneLabel, 0, 0, 1, 1);

    _voiceLaneWidget = new FfxivVoiceLaneWidget(mw_matrixWidget, _voiceLaneArea);
    voiceLaneAreaLayout->addWidget(_voiceLaneWidget, 0, 1, 1, 1);

    QScrollBar *voiceLaneScrollNothing = new QScrollBar(Qt::Vertical, _voiceLaneArea);
    voiceLaneScrollNothing->setMinimum(0);
    voiceLaneScrollNothing->setMaximum(0);
    voiceLaneAreaLayout->addWidget(voiceLaneScrollNothing, 0, 2, 1, 1);

    voiceLaneAreaLayout->setRowStretch(0, 1);
    _voiceLaneArea->setLayout(voiceLaneAreaLayout);
    _voiceLaneArea->setMinimumHeight(0);
    _voiceLaneArea->setMaximumHeight(120);
    _voiceLaneArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    // 1.6.1 (UX-VOICE-LANE-002): initial visibility is decided by the
    // shared (always-show OR auto+ffxiv-on) rule, applied centrally in
    // updateVoiceLaneVisibility(). The View menu actions are wired up
    // later in createMenubar(); they will reapply the same rule on toggle.
    updateVoiceLaneVisibility();
    leftSplitter->addWidget(_voiceLaneArea);

    // Create horizontal scrollbar container as separate splitter widget - but make it non-resizable
    QWidget *scrollBarArea = new QWidget(leftSplitter);
    scrollBarArea->setContentsMargins(0, 0, 0, 0);
    scrollBarArea->setFixedHeight(20);
    scrollBarArea->setMinimumHeight(20);
    scrollBarArea->setMaximumHeight(20);
    scrollBarArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    leftSplitter->addWidget(scrollBarArea);
    
    // Make scrollbar area completely non-collapsible and non-resizable
    leftSplitter->setCollapsible(leftSplitter->count() - 1, false);
    leftSplitter->handle(leftSplitter->count() - 1)->setDisabled(true);
    leftSplitter->handle(leftSplitter->count() - 1)->hide();
    
    // Ensure the handle between matrixArea and velocityArea remains functional
    // and has higher priority than the scrollbar area
    if (leftSplitter->count() >= 2) {
        leftSplitter->handle(0)->setEnabled(true);
        leftSplitter->handle(0)->show();
        leftSplitter->handle(0)->raise(); // Bring to front for priority over scrollbar
    }
    
    hori = new QScrollBar(Qt::Horizontal, scrollBarArea);
    hori->setMinimum(0);
    hori->setValue(0);
    hori->setSingleStep(500);
    hori->setPageStep(5000);
    
    QHBoxLayout *scrollBarLayout = new QHBoxLayout(scrollBarArea);
    scrollBarLayout->setContentsMargins(110, 0, 0, 0); // Align with matrix widget
    scrollBarLayout->addWidget(hori);
    scrollBarArea->setLayout(scrollBarLayout);

    // Create MiscWidget
    QWidget *miscContainer;

    if (useHardwareAcceleration) {
        // Use direct OpenGL acceleration
        // Connect MiscWidget to the internal MatrixWidget for data access
        OpenGLMiscWidget *openglMisc = new OpenGLMiscWidget(mw_matrixWidget, _settings, velocityArea);
        _miscWidget = openglMisc->getMiscWidget(); // Get the internal MiscWidget for data access
        _miscWidgetContainer = openglMisc; // Store the displayed widget for UI operations
        miscContainer = openglMisc;
        // Direct OpenGL acceleration - no separate accelerator needed
        qDebug() << "Created MiscWidget with direct OpenGL acceleration";
    } else {
        // Use software rendering
        _miscWidget = new MiscWidget(mw_matrixWidget, velocityArea);
        _miscWidgetContainer = _miscWidget; // Same widget for both data and UI
        miscContainer = _miscWidget;
        // Software rendering - no accelerator needed
        qDebug() << "Created MiscWidget with software rendering";
    }

    _miscWidget->setContentsMargins(0, 0, 0, 0);
    velocityAreaLayout->addWidget(miscContainer, 0, 1, 1, 1);

    // controls for velocity widget
    _miscControlLayout = new QGridLayout(_miscWidgetControl);
    _miscControlLayout->setHorizontalSpacing(0);
    //_miscWidgetControl->setContentsMargins(0,0,0,0);
    //_miscControlLayout->setContentsMargins(0,0,0,0);
    _miscWidgetControl->setLayout(_miscControlLayout);
    _miscMode = new QComboBox(_miscWidgetControl);
    for (int i = 0; i < MiscModeEnd; i++) {
        _miscMode->addItem(MiscWidget::modeToString(i));
    }
    _miscMode->view()->setMinimumWidth(_miscMode->minimumSizeHint().width());
    //_miscControlLayout->addWidget(new QLabel("Mode:", _miscWidgetControl), 0, 0, 1, 3);
    _miscControlLayout->addWidget(_miscMode, 1, 0, 1, 3);
    connect(_miscMode, SIGNAL(currentIndexChanged(int)), this, SLOT(changeMiscMode(int)));

    //_miscControlLayout->addWidget(new QLabel("Control:", _miscWidgetControl), 2, 0, 1, 3);
    _miscController = new QComboBox(_miscWidgetControl);
    for (int i = 0; i < 128; i++) {
        QString name = MidiFile::controlChangeName(i);
        if (name == MidiFile::tr("undefined")) {
            _miscController->addItem(QString::number(i) + ": ");
        } else {
            _miscController->addItem(QString::number(i) + ": " + name);
        }
    }
    _miscController->view()->setMinimumWidth(_miscController->minimumSizeHint().width());
    _miscControlLayout->addWidget(_miscController, 3, 0, 1, 3);
    connect(_miscController, SIGNAL(currentIndexChanged(int)), _miscWidgetContainer, SLOT(setControl(int)));

    //_miscControlLayout->addWidget(new QLabel("Channel:", _miscWidgetControl), 4, 0, 1, 3);
    _miscChannel = new QComboBox(_miscWidgetControl);
    for (int i = 0; i < 15; i++) {
        _miscChannel->addItem("Channel " + QString::number(i));
    }
    _miscChannel->view()->setMinimumWidth(_miscChannel->minimumSizeHint().width());
    _miscControlLayout->addWidget(_miscChannel, 5, 0, 1, 3);
    connect(_miscChannel, SIGNAL(currentIndexChanged(int)), _miscWidgetContainer, SLOT(setChannel(int)));
    _miscControlLayout->setRowStretch(6, 1);
    _miscMode->setCurrentIndex(0);
    _miscChannel->setEnabled(false);
    _miscController->setEnabled(false);

    setSingleMode = new QAction("Single mode", this);
    Appearance::setActionIcon(setSingleMode, ":/run_environment/graphics/tool/misc_single.png");
    setSingleMode->setCheckable(true);
    setFreehandMode = new QAction("Free-hand mode", this);
    Appearance::setActionIcon(setFreehandMode, ":/run_environment/graphics/tool/misc_freehand.png");
    setFreehandMode->setCheckable(true);
    setLineMode = new QAction("Line mode", this);
    Appearance::setActionIcon(setLineMode, ":/run_environment/graphics/tool/misc_line.png");
    setLineMode->setCheckable(true);

    QActionGroup *group = new QActionGroup(this);
    group->setExclusive(true);
    group->addAction(setSingleMode);
    group->addAction(setFreehandMode);
    group->addAction(setLineMode);
    setSingleMode->setChecked(true);
    connect(group, SIGNAL(triggered(QAction*)), this, SLOT(selectModeChanged(QAction*)));

    QToolButton *btnSingle = new QToolButton(_miscWidgetControl);
    btnSingle->setDefaultAction(setSingleMode);
    QToolButton *btnHand = new QToolButton(_miscWidgetControl);
    btnHand->setDefaultAction(setFreehandMode);
    QToolButton *btnLine = new QToolButton(_miscWidgetControl);
    btnLine->setDefaultAction(setLineMode);

    _miscControlLayout->addWidget(btnSingle, 9, 0, 1, 1);
    _miscControlLayout->addWidget(btnHand, 9, 1, 1, 1);
    _miscControlLayout->addWidget(btnLine, 9, 2, 1, 1);

    // Set the sizes of leftSplitter
    leftSplitter->setStretchFactor(0, 8);  // matrixArea
    leftSplitter->setStretchFactor(1, 1);  // velocityArea
    leftSplitter->setStretchFactor(2, 0);  // lyricArea (collapsible)
    leftSplitter->setStretchFactor(3, 0);  // scrollBarArea (fixed height, non-resizable)

    // Start with lyric timeline at a reasonable default height (60px)
    QList<int> leftSizes;
    leftSizes << 600 << 120 << 60 << 20;
    leftSplitter->setSizes(leftSizes);

    // Track
    tracksWidget = new QWidget(upperTabWidget);
    QGridLayout *tracksLayout = new QGridLayout(tracksWidget);
    tracksWidget->setLayout(tracksLayout);
    QToolBar *tracksTB = new QToolBar(tracksWidget);
    tracksTB->setIconSize(QSize(20, 20));
    tracksLayout->addWidget(tracksTB, 0, 0, 1, 1);

    QAction *newTrack = new QAction(tr("Add track"), this);
    Appearance::setActionIcon(newTrack, ":/run_environment/graphics/tool/add.png");
    connect(newTrack, SIGNAL(triggered()), this,
            SLOT(addTrack()));
    tracksTB->addAction(newTrack);

    tracksTB->addSeparator();

    _allTracksAudible = new QAction(tr("All tracks audible"), this);
    Appearance::setActionIcon(_allTracksAudible, ":/run_environment/graphics/tool/all_audible.png");
    connect(_allTracksAudible, SIGNAL(triggered()), this,
            SLOT(unmuteAllTracks()));
    tracksTB->addAction(_allTracksAudible);

    _allTracksMute = new QAction(tr("Mute all tracks"), this);
    Appearance::setActionIcon(_allTracksMute, ":/run_environment/graphics/tool/all_mute.png");
    connect(_allTracksMute, SIGNAL(triggered()), this,
            SLOT(muteAllTracks()));
    tracksTB->addAction(_allTracksMute);

    tracksTB->addSeparator();

    _allTracksVisible = new QAction(tr("Show all tracks"), this);
    Appearance::setActionIcon(_allTracksVisible, ":/run_environment/graphics/tool/all_visible.png");
    connect(_allTracksVisible, SIGNAL(triggered()), this,
            SLOT(allTracksVisible()));
    tracksTB->addAction(_allTracksVisible);

    _allTracksInvisible = new QAction(tr("Hide all tracks"), this);
    Appearance::setActionIcon(_allTracksInvisible, ":/run_environment/graphics/tool/all_invisible.png");
    connect(_allTracksInvisible, SIGNAL(triggered()), this,
            SLOT(allTracksInvisible()));
    tracksTB->addAction(_allTracksInvisible);

    _trackWidget = new TrackListWidget(tracksWidget);
    connect(_trackWidget, SIGNAL(trackRenameClicked(int)), this, SLOT(renameTrack(int)), Qt::QueuedConnection);
    connect(_trackWidget, SIGNAL(trackRemoveClicked(int)), this, SLOT(removeTrack(int)), Qt::QueuedConnection);
    connect(_trackWidget, SIGNAL(trackClicked(MidiTrack*)), this, SLOT(editTrackAndChannel(MidiTrack*)), Qt::QueuedConnection);

    tracksLayout->addWidget(_trackWidget, 1, 0, 1, 1);
    upperTabWidget->addTab(tracksWidget, tr("Tracks"));

    // Channels
    channelsWidget = new QWidget(upperTabWidget);
    QGridLayout *channelsLayout = new QGridLayout(channelsWidget);
    channelsWidget->setLayout(channelsLayout);
    QToolBar *channelsTB = new QToolBar(channelsWidget);
    channelsTB->setIconSize(QSize(20, 20));
    channelsLayout->addWidget(channelsTB, 0, 0, 1, 1);

    _allChannelsAudible = new QAction(tr("All channels audible"), this);
    Appearance::setActionIcon(_allChannelsAudible, ":/run_environment/graphics/tool/all_audible.png");
    connect(_allChannelsAudible, SIGNAL(triggered()), this, SLOT(unmuteAllChannels()));
    channelsTB->addAction(_allChannelsAudible);

    _allChannelsMute = new QAction(tr("Mute all channels"), this);
    Appearance::setActionIcon(_allChannelsMute, ":/run_environment/graphics/tool/all_mute.png");
    connect(_allChannelsMute, SIGNAL(triggered()), this, SLOT(muteAllChannels()));
    channelsTB->addAction(_allChannelsMute);

    channelsTB->addSeparator();

    _allChannelsVisible = new QAction(tr("Show all channels"), this);
    Appearance::setActionIcon(_allChannelsVisible, ":/run_environment/graphics/tool/all_visible.png");
    connect(_allChannelsVisible, SIGNAL(triggered()), this, SLOT(allChannelsVisible()));
    channelsTB->addAction(_allChannelsVisible);

    _allChannelsInvisible = new QAction(tr("Hide all channels"), this);
    Appearance::setActionIcon(_allChannelsInvisible, ":/run_environment/graphics/tool/all_invisible.png");
    connect(_allChannelsInvisible, SIGNAL(triggered()), this, SLOT(allChannelsInvisible()));
    channelsTB->addAction(_allChannelsInvisible);

    channelWidget = new ChannelListWidget(channelsWidget);
    connect(channelWidget, SIGNAL(channelStateChanged()), this, SLOT(updateChannelMenu()), Qt::QueuedConnection);
    // Mirror channel + track mutes onto the 3 authentic-SID voices, live during
    // emulation playback (channels 0-2 = SID voices 0-2).
    connect(channelWidget, &ChannelListWidget::channelStateChanged, this,
            [this] { syncSidVoiceMutes(file); });
    connect(channelWidget, SIGNAL(selectInstrumentClicked(int)), this, SLOT(setInstrumentForChannel(int)), Qt::QueuedConnection);
    channelsLayout->addWidget(channelWidget, 1, 0, 1, 1);
    upperTabWidget->addTab(channelsWidget, tr("Channels"));

    // terminal
    Terminal::initTerminal(_settings->value("start_cmd", "").toString(),
                           _settings->value("in_port", "").toString(),
                           _settings->value("out_port", "").toString());
    //upperTabWidget->addTab(Terminal::terminal()->console(), "Terminal");

    // Protocollist
    protocolWidget = new ProtocolWidget(lowerTabWidget);
    lowerTabWidget->addTab(protocolWidget, tr("Protocol"));

    // EventWidget
    _eventWidget = new EventWidget(lowerTabWidget);
    Selection::_eventWidget = _eventWidget;
    lowerTabWidget->addTab(_eventWidget, tr("Event"));
    MidiEvent::setEventWidget(_eventWidget);

#ifdef MIDIEDITOR_COLLAB_ENABLED
    {
        CollabHistoryWidget *collabHistory = new CollabHistoryWidget(lowerTabWidget);
        int collabTabIndex = lowerTabWidget->addTab(collabHistory, tr("Collaboration"));
        QTabWidget *tabBar = lowerTabWidget;
        auto refreshCollabTab = [tabBar, collabTabIndex]() {
            CollabService *svc = CollabService::instance();
            bool show = svc->isEnabled() && svc->hasCurrentFile();
            tabBar->setTabVisible(collabTabIndex, show);
        };
        refreshCollabTab();
        connect(CollabService::instance(), &CollabService::enabledChanged, this,
                [refreshCollabTab](bool) { refreshCollabTab(); });
        connect(CollabService::instance(), &CollabService::currentFileStateChanged, this,
                refreshCollabTab);
        // When the user clicks a hunk and the widget updates the global
        // Selection, repaint the piano roll so the highlight shows.
        connect(collabHistory, &CollabHistoryWidget::selectionApplied, this, [this]() {
            if (_matrixWidgetContainer) _matrixWidgetContainer->update();
            if (eventWidget()) eventWidget()->reload();
        });

        // Phase 9.11 §15.3: in-session chat sidebar tab. Visible only
        // while a live session is active — outside a session there's
        // no transport to chat over. Unread-badge logic flips the tab
        // title to "Chat (N)" when messages arrive on another tab.
        _collabChatWidget = new CollabChatWidget(lowerTabWidget);
        int chatTabIndex = lowerTabWidget->addTab(_collabChatWidget, tr("Chat"));
        QTabWidget *tabBarCapture = lowerTabWidget;
        CollabChatWidget *chatCapture = _collabChatWidget;

        // Polish 2026-05-21 / theme-fix 2026-05-24: blink the Chat tab
        // while unread messages are pending and the user is on another
        // tab. Two parallel signals so it works in every theme:
        //   1. setTabTextColor() to accent orange — picked up by the
        //      Classic theme + any other Fusion-style build.
        //   2. A leading "● " marker prefixed to the tab text — picked
        //      up by ALL themes including the QSS-styled Sakura /
        //      MidiEditor AI themes, which override tabTextColor and
        //      previously left the blink invisible (reported 2026-05-24).
        // Both reset on tab activation or session end.
        _chatBlinkTimer = new QTimer(this);
        _chatBlinkTimer->setInterval(600);
        const QColor kBlinkAccent(QStringLiteral("#ff9933"));
        QTabBar *chatTabBar = lowerTabWidget->tabBar();
        // Remember the default color so we can restore it cleanly even
        // if a theme switch happens mid-session.
        QColor defaultColor = chatTabBar->tabTextColor(chatTabIndex);

        // Computes the current tab text given blink phase + unread count.
        // Captured by reference into the lambdas below so a changing
        // _chatUnreadCount always reflects in the next refresh.
        //
        // Bugfix 2026-05-24: toggle between a FILLED ● and a
        // HOLLOW ○ instead of inserting / removing the prefix every
        // tick. Both glyphs occupy the same width in every font Qt
        // ships with, so the tab no longer breathes wider/narrower on
        // every pulse — the visual change is just the glyph swap. The
        // no-unread state still has no prefix; the one-time width
        // jump on first-unread (Chat → ● Chat (1)) is unavoidable but
        // it's a single transition, not a continuous flicker.
        auto buildChatTabText = [this](bool blinkOn) {
            QString base = (_chatUnreadCount > 0)
                ? tr("Chat (%1)").arg(_chatUnreadCount)
                : tr("Chat");
            if (_chatUnreadCount == 0) return base;
            return (blinkOn ? QStringLiteral("● ") : QStringLiteral("○ ")) + base;
        };

        connect(_chatBlinkTimer, &QTimer::timeout, this,
                [this, tabBarCapture, chatTabBar, chatTabIndex,
                 kBlinkAccent, defaultColor, buildChatTabText]() {
                    _chatBlinkOn = !_chatBlinkOn;
                    chatTabBar->setTabTextColor(chatTabIndex,
                        _chatBlinkOn ? kBlinkAccent : defaultColor);
                    tabBarCapture->setTabText(chatTabIndex,
                        buildChatTabText(_chatBlinkOn));
                });
        auto stopChatBlink = [this, tabBarCapture, chatTabBar, chatTabIndex,
                              defaultColor, buildChatTabText]() {
            if (_chatBlinkTimer && _chatBlinkTimer->isActive())
                _chatBlinkTimer->stop();
            _chatBlinkOn = false;
            chatTabBar->setTabTextColor(chatTabIndex, defaultColor);
            // Restore the un-prefixed text in whichever form is current
            // (with or without unread count — caller may have just
            // cleared the count themselves).
            tabBarCapture->setTabText(chatTabIndex, buildChatTabText(false));
        };

        // Tab visibility + input-enable state both follow the live
        // session role: hidden + disabled while idle, visible + enabled
        // otherwise. Pre-existing scrollback is wiped on every entry
        // into / exit from a session (no persistence per §15.3).
        auto refreshChatTab = [this, tabBarCapture, chatTabIndex, chatCapture,
                               stopChatBlink]() {
            bool active = (LanLiveSession::instance()->role()
                           != LanLiveSession::Role::Idle);
            tabBarCapture->setTabVisible(chatTabIndex, active);
            chatCapture->setInputEnabled(active);
            if (!active) {
                chatCapture->clearChat();
                // Reset any unread-count suffix + stop blinking.
                _chatUnreadCount = 0;
                tabBarCapture->setTabText(chatTabIndex, tr("Chat"));
                stopChatBlink();
            }
        };
        refreshChatTab();
        connect(LanLiveSession::instance(), &LanLiveSession::roleChanged,
                this, [refreshChatTab](LanLiveSession::Role) { refreshChatTab(); });

        // Incoming chat → append to the widget.
        connect(LanLiveSession::instance(), &LanLiveSession::chatMessageReceived,
                this, [chatCapture](const QString &mid, const QString &name,
                                    const QString &text, qint64 ts) {
                    chatCapture->appendMessage(mid, name, text, ts);
                });

        // Dropped-message toast for the host's own messages: the host
        // optimistically appended to the local chat widget before the
        // wire send, but handleIncomingChat then dropped it on the
        // 4 KB cap or the 200 ms-per-sender rate limit. Tell the user
        // so they know their message didn't make it out — peers see
        // nothing. (bughunter MEDIUM finding 2026-05-24)
        connect(LanLiveSession::instance(), &LanLiveSession::chatMessageDropped,
                this, [this](const QString &senderMid, const QString &reason) {
                    if (senderMid != CollabIdentity::machineId()) return;
                    statusBar()->showMessage(
                        tr("Your last chat message wasn't delivered (%1). "
                           "Other peers didn't see it.").arg(reason),
                        8000);
                });

        // Unread-badge: track when messages land while the chat tab
        // isn't the current one, and reset on tab activation. New
        // messages also kick off the blink-timer so the change is
        // visually obvious even if the user is focused elsewhere.
        // Tab text goes through buildChatTabText so the blink prefix
        // stays in sync regardless of which timer phase fires next.
        connect(chatCapture, &CollabChatWidget::newMessageArrived,
                this, [this, tabBarCapture, chatTabIndex, buildChatTabText]() {
                    if (tabBarCapture->currentIndex() == chatTabIndex) return;
                    _chatUnreadCount++;
                    // Start in the "attention" phase (filled circle) so
                    // the very first frame after a message arrives
                    // doesn't show the calm hollow form. The timer
                    // toggles from there.
                    _chatBlinkOn = true;
                    tabBarCapture->setTabText(chatTabIndex,
                        buildChatTabText(_chatBlinkOn));
                    if (_chatBlinkTimer && !_chatBlinkTimer->isActive())
                        _chatBlinkTimer->start();
                });
        connect(lowerTabWidget, &QTabWidget::currentChanged, this,
                [this, tabBarCapture, chatTabIndex, stopChatBlink](int idx) {
                    if (idx != chatTabIndex) return;
                    _chatUnreadCount = 0;
                    tabBarCapture->setTabText(chatTabIndex, tr("Chat"));
                    stopChatBlink();
                });
    }
#endif

    // below add two rows for choosing track/channel new events shall be assigned to
    chooserWidget = new QWidget(rightSplitter);
    chooserWidget->setMinimumWidth(350);
    rightSplitter->addWidget(chooserWidget);
    QGridLayout *chooserLayout = new QGridLayout(chooserWidget);
    QLabel *trackchannelLabel = new QLabel(tr("Add new events to ..."));
    chooserLayout->addWidget(trackchannelLabel, 0, 0, 1, 2);
    QLabel *channelLabel = new QLabel(tr("Channel: "), chooserWidget);
    chooserLayout->addWidget(channelLabel, 2, 0, 1, 1);
    _chooseEditChannel = new QComboBox(chooserWidget);
    for (int i = 0; i < 16; i++) {
        if (i == 9) _chooseEditChannel->addItem(tr("Percussion channel"));
        else _chooseEditChannel->addItem(tr("Channel ") + QString::number(i)); // TODO: Display channel instrument | UDP: But **file** is *nullptr*
    }
    connect(_chooseEditChannel, SIGNAL(activated(int)), this, SLOT(editChannel(int)));

    chooserLayout->addWidget(_chooseEditChannel, 2, 1, 1, 1);
    QLabel *trackLabel = new QLabel(tr("Track: "), chooserWidget);
    chooserLayout->addWidget(trackLabel, 1, 0, 1, 1);
    _chooseEditTrack = new QComboBox(chooserWidget);
    chooserLayout->addWidget(_chooseEditTrack, 1, 1, 1, 1);
    connect(_chooseEditTrack, SIGNAL(activated(int)), this, SLOT(editTrack(int)));
    chooserLayout->setColumnStretch(1, 1);
    // connect Scrollbars and Widgets
    // Connect to the actual displayed widget (OpenGL or software)
    connect(vert, SIGNAL(valueChanged(int)), matrixContainer, SLOT(scrollYChanged(int)));
    connect(hori, SIGNAL(valueChanged(int)), matrixContainer, SLOT(scrollXChanged(int)));

    connect(channelWidget, SIGNAL(channelStateChanged()), matrixContainer, SLOT(update()));
    connect(mw_matrixWidget, SIGNAL(sizeChanged(int, int, int, int)), this, SLOT(matrixSizeChanged(int, int, int, int)));
    connect(mw_matrixWidget, SIGNAL(scrollChanged(int, int, int, int)), this, SLOT(scrollPositionsChanged(int, int, int, int)));

    setCentralWidget(central);

    // === MidiPilot Dock Widget (must be created before setupActions because View menu references it) ===
    _midiPilotWidget = new MidiPilotWidget(this);
    _midiPilotDock = new QDockWidget(tr("MidiPilot"), this);
    _midiPilotDock->setObjectName("midiPilotDock"); // Phase 37.2: targeted by brand QSS
    _midiPilotDock->setWidget(_midiPilotWidget);
    _midiPilotDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    _midiPilotDock->setMinimumWidth(280);
    addDockWidget(Qt::RightDockWidgetArea, _midiPilotDock);
    _midiPilotDock->setVisible(false);

    connect(_midiPilotWidget, &MidiPilotWidget::requestRepaint, this, [this]() {
        // Invalidate the cached pixmap so the matrix redraws from current MidiFile data.
        // Without this, update() just repaints the stale cache (normally invalidated
        // only by Protocol::actionFinished which doesn't fire mid-agent-run).
        mw_matrixWidget->registerRelayout();
        _matrixWidgetContainer->update();
        _miscWidgetContainer->update();
        _trackWidget->update();
    });

    // Create MCP Server early so the toolbar toggle widget can reference it
    _mcpServer = new McpServer(this);
    _mcpServer->setWidget(_midiPilotWidget);
    if (file) _mcpServer->setFile(file);

    QWidget *buttons = setupActions(central);

    rightSplitter->setStretchFactor(0, 5);
    rightSplitter->setStretchFactor(1, 5);

    // Add the Widgets to the central Layout
    centralLayout->setSpacing(0);
    centralLayout->addWidget(buttons, 0, 0);
    centralLayout->addWidget(mainSplitter, 1, 0);
    centralLayout->setRowStretch(1, 1);
    central->setLayout(centralLayout);

    if (_settings->value("colors_from_channel", false).toBool()) {
        colorsByChannel();
    } else {
        colorsByTrack();
    }
    copiedEventsChanged();

    setAcceptDrops(true);

    currentTweakTarget = new TimeTweakTarget(this);
    selectionNavigator = new SelectionNavigator(this);

    // Initialize shared clipboard immediately
    initializeSharedClipboard();

    // Initialize auto-save debounce timer
    _autoSaveTimer = new QTimer(this);
    _autoSaveTimer->setSingleShot(true);
    connect(_autoSaveTimer, &QTimer::timeout, this, &MainWindow::performAutoSave);

    // Apply widget size constraints based on settings
    applyWidgetSizeConstraints();

    // Status bar: persistent cursor/selection/chord labels
    _statusCursorLabel = new QLabel(this);
    _statusSelectionLabel = new QLabel(this);
    _statusChordLabel = new QLabel(this);
    _statusCursorLabel->setMinimumWidth(140);
    _statusSelectionLabel->setMinimumWidth(100);
    _statusChordLabel->setMinimumWidth(100);
    statusBar()->addPermanentWidget(_statusCursorLabel);
    statusBar()->addPermanentWidget(_statusSelectionLabel);
    statusBar()->addPermanentWidget(_statusChordLabel);

#ifdef MIDIEDITOR_COLLAB_ENABLED
    _statusLiveSessionLabel = new QLabel(this);
    _statusLiveSessionLabel->setStyleSheet(
        "QLabel { padding: 2px 8px; border-radius: 3px; "
        "background: #c33; color: white; font-weight: bold; }");
    _statusLiveSessionLabel->hide();
    statusBar()->addPermanentWidget(_statusLiveSessionLabel);

    auto refreshLiveLabel = [this]() {
        LanLiveSession *svc = LanLiveSession::instance();
        // Suffix for pending-review queue (Phase 9.5h, §11.10c).
        int pending = svc->pendingReviewHunkCount();
        QString reviewSuffix = pending > 0
            ? tr("  •  %1 pending review").arg(pending)
            : QString();
        // Phase 9.9e §15.2: Show-mode indicator suffix. 🎩 = presenter,
        // 👀 = viewer (with the presenter's display name when known).
        // Empty in Edit mode. Placed before reviewSuffix so the visual
        // hierarchy reads "session → role → review state".
        QString showSuffix;
        if (svc->mode() == LanLiveSession::SessionMode::Show) {
            if (svc->isPresenter()) {
                showSuffix = tr("  •  🎩 PRESENTING");
            } else {
                // Resolve the presenter's display name from the peer
                // list (host-side) or fall back to the host name (joiner
                // side when the presenter is the host) or the raw
                // machineId prefix.
                QString presenterName;
                if (svc->role() == LanLiveSession::Role::Hosting) {
                    for (const auto &p : svc->connectedPeerInfo()) {
                        if (p.first == svc->presenterMachineId()) {
                            presenterName = p.second;
                            break;
                        }
                    }
                } else if (svc->role() == LanLiveSession::Role::Joined) {
                    // On a joiner: if the presenter is the host, use the
                    // host's display name. Otherwise we don't have a
                    // peer-list (the joiner only sees the host), so fall
                    // back to a short machineId.
                    presenterName = svc->hostDisplayName();
                }
                if (presenterName.isEmpty()) {
                    presenterName = svc->presenterMachineId().left(8);
                }
                showSuffix = tr("  •  👀 Watching %1").arg(presenterName);
            }
        }
        // Transport tag: musicians don't think in LAN/WAN, so surface it
        // as Local/Online (Plan §11.10k).
        QString transportTag;
        switch (svc->transport()) {
            case LanLiveSession::Transport::Lan:  transportTag = tr("Local");  break;
            case LanLiveSession::Transport::Wan:  transportTag = tr("Online"); break;
            case LanLiveSession::Transport::None: transportTag = QString();    break;
        }
        QString prefix = transportTag.isEmpty()
            ? QStringLiteral("● LIVE")
            : QStringLiteral("● LIVE [%1]").arg(transportTag);
        switch (svc->role()) {
            case LanLiveSession::Role::Hosting: {
                int n = svc->peerCount();
                QString core = (n == 0)
                    ? tr("%1: hosting (%2) — waiting for peers").arg(prefix, svc->pairingCode())
                    : tr("%1: hosting (%2) — %3 peer(s)").arg(prefix, svc->pairingCode()).arg(n);
                _statusLiveSessionLabel->setText(core + showSuffix + reviewSuffix);
                _statusLiveSessionLabel->show();
                break;
            }
            case LanLiveSession::Role::Joined:
                _statusLiveSessionLabel->setText(
                    tr("%1: joined %2").arg(prefix, svc->hostDisplayName())
                        + showSuffix + reviewSuffix);
                _statusLiveSessionLabel->show();
                break;
            case LanLiveSession::Role::Idle:
                _statusLiveSessionLabel->hide();
                break;
        }
    };
    connect(LanLiveSession::instance(), &LanLiveSession::roleChanged,
            this, [refreshLiveLabel](LanLiveSession::Role) { refreshLiveLabel(); });
    connect(LanLiveSession::instance(), &LanLiveSession::peerCountChanged,
            this, [refreshLiveLabel](int) { refreshLiveLabel(); });
    connect(LanLiveSession::instance(), &LanLiveSession::peerLabelsChanged,
            this, refreshLiveLabel);
    connect(LanLiveSession::instance(), &LanLiveSession::pendingReviewChanged,
            this, [refreshLiveLabel](int) { refreshLiveLabel(); });

    // Phase 9.9c §15.2: Show Mode viewer lock. Single applier — refreshes
    // the matrix editing lock + (later) tool-button enable state from
    // the live session's current mode + presenter pointer. Re-invoked on
    // every signal that could move the lock state: role changes
    // (joining/leaving), hat transfers, and the host's own takeover.
    auto applyShowModeLock = [this]() {
        LanLiveSession *svc = LanLiveSession::instance();
        bool shouldLock =
            (svc->role() != LanLiveSession::Role::Idle)
            && (svc->mode() == LanLiveSession::SessionMode::Show)
            && !svc->isPresenter();
        if (mw_matrixWidget) mw_matrixWidget->setEditingLocked(shouldLock);
        if (_midiPilotWidget) _midiPilotWidget->setShowModeLocked(shouldLock);
        // Phase 9.9c §15.2 (Show-mode polish 2026-05-21): disable the
        // top-level edit / tools / midi menus while we're a viewer.
        // Mouse interaction in the matrix is already blocked by
        // setEditingLocked, but menu-driven actions (Tempo Conversion,
        // Channel Fixer, Quantize, etc.) wrote directly via Protocol
        // — they'd produce local edits that diverge from the rest of
        // the session. Greyed-out menus also surface visually that
        // the editor is in view-only mode.
        const bool menusEnabled = !shouldLock;
        if (_editMenuForShowLock)
            _editMenuForShowLock->menuAction()->setEnabled(menusEnabled);
        if (_toolsMenuForShowLock)
            _toolsMenuForShowLock->menuAction()->setEnabled(menusEnabled);
        if (_midiMenuForShowLock)
            _midiMenuForShowLock->menuAction()->setEnabled(menusEnabled);
        // Polish 2026-05-21: also disable the toolbar widget. Menu-
        // disable only greys out the menu-action — the same QActions
        // are also bound to toolbar buttons (Channel Fixer, Explode
        // Chords, Split Channels, etc.) AND to global keyboard
        // shortcuts. The QActions stay individually enabled (we'd have
        // to enumerate them all to flip each one), so disabling the
        // toolbar widget container is the simplest catch-all. Playback
        // controls live in the toolbar too, but the matching menu
        // actions in `Playback` stay enabled, so the Space-bar
        // play/pause shortcut still works for local audio preview.
        if (_toolbarWidget) _toolbarWidget->setEnabled(menusEnabled);
    };
    connect(LanLiveSession::instance(), &LanLiveSession::roleChanged,
            this, [applyShowModeLock](LanLiveSession::Role) { applyShowModeLock(); });
    connect(LanLiveSession::instance(), &LanLiveSession::hatTransferred,
            this, [applyShowModeLock, refreshLiveLabel](const QString &, const QString &, const QString &) {
                applyShowModeLock();
                refreshLiveLabel();
            });
    connect(LanLiveSession::instance(), &LanLiveSession::joined,
            this, [applyShowModeLock](const QString &) { applyShowModeLock(); });
    // bugfix 2026-05-21: sessionModeChanged is the authoritative trigger
    // — it fires AFTER sessionWelcome has been processed on the joiner
    // (joined fires too early, when _mode is still the Edit default).
    // Drive both the lock and the status-bar indicator off of it.
    connect(LanLiveSession::instance(), &LanLiveSession::sessionModeChanged,
            this, [applyShowModeLock, refreshLiveLabel]() {
                applyShowModeLock();
                refreshLiveLabel();
            });
    applyShowModeLock();

    // Phase 9.9f §15.2: presenter-side viewState broadcast on scroll.
    // The connect is unconditional — broadcastLocalViewState bails out
    // silently when not in Show mode / not the presenter. The throttle
    // in LanLiveSession coalesces a rapid drag-scroll burst into one
    // wire frame per 250 ms.
    connect(mw_matrixWidget, &MatrixWidget::scrollChanged, this,
            [this](int, int, int, int) { broadcastLocalViewState(); });
    // Viewer-side: apply the presenter's viewState when it lands.
    connect(LanLiveSession::instance(), &LanLiveSession::viewStateReceived,
            this, [this](const LiveSession::ViewportState &vp,
                         const QVector<bool> &tracks,
                         const QVector<bool> &channels) {
                applyRemoteViewState(vp, tracks, channels);
            });
    // Show-mode playback trigger from the presenter: seek to the
    // presenter's cursor tick then start / stop the local player.
    // _applyingRemotePlayback flag prevents the local play() / stop()
    // from re-broadcasting back to the presenter.
    connect(LanLiveSession::instance(), &LanLiveSession::playbackTriggerReceived,
            this, [this](const QString &action, int tickPosition) {
                if (!file) return;
                // QScopedValueRollback so re-entry (e.g. the modal
                // CompleteMidiSetupDialog play() opens spinning a
                // nested event loop that processes another inbound
                // trigger) restores the flag to the OUTER scope's
                // value, not unconditionally false. Without this an
                // inner cleanup would clear the flag while the outer
                // call is still running, and a concurrent local
                // user click would wrongly broadcast. (bughunter
                // MEDIUM finding 2026-05-24)
                QScopedValueRollback<bool> guard(_applyingRemotePlayback, true);
                if (action == QLatin1String("start")) {
                    if (tickPosition >= 0) file->setCursorTick(tickPosition);
                    play();
                } else if (action == QLatin1String("stop")) {
                    stop();
                }
            });
    // Also: when the presenter newly takes the hat (mode changes, hat
    // transfers), send an initial viewState so any current viewer
    // adopts the now-active view immediately. The throttle absorbs
    // the duplicate if the presenter scrolls right after.
    connect(LanLiveSession::instance(), &LanLiveSession::sessionModeChanged,
            this, [this]() { broadcastLocalViewState(); });
    // Phase 9.9f §15.2 (extension 2026-05-21): tool-change observer.
    // Wire a static callback (Tool.h doesn't depend on QObject) that
    // pushes a broadcast whenever the local user picks a new tool.
    // The function-pointer pattern keeps the tool module decoupled
    // from MainWindow / collab; the lambda → static-bridge pattern
    // captures `this` implicitly via the static MainWindow pointer
    // — instead we read s_mainWindowForTool below and avoid any
    // captures. Live for the rest of the process; never unregistered
    // because there's only ever one MainWindow.
    static MainWindow *s_mainWindowForTool = this;
    Tool::setToolChangedCallback(+[](EditorTool *) {
        if (s_mainWindowForTool) s_mainWindowForTool->broadcastLocalViewState();
    });

    refreshLiveLabel();
#endif

    // Load initial file immediately - no need for artificial delay
    loadInitFile();

    // Phase 28: the initial document's tab is added to the bar during the
    // constructor, before the nested splitter/tab-strip is first laid out, so
    // the QTabBar can cache a zero geometry and not paint that first tab until a
    // later relayout. Re-sync the active group's bar from its manager once the
    // event loop is running so the startup tab is reliably visible.
    QTimer::singleShot(0, this, [this] {
        if (_documentTabBar && _documentManager && _documentManager->count() > 0) {
            rebuildTabBar(_documentTabBar, _documentManager);
        }
    });

    // Start MCP Server if enabled in settings (server object created earlier, before setupActions)
    if (_settings->value("MCP/enabled", false).toBool()) {
        quint16 mcpPort = _settings->value("MCP/port", 9420).toInt();
        QString mcpToken = _settings->value("MCP/auth_token").toString();
        if (!mcpToken.isEmpty())
            _mcpServer->setAuthToken(mcpToken);
        _mcpServer->start(mcpPort);
    }

    // Check for updates silently on startup (1.6.1 / upstream 366a92f:
    // can be disabled via Performance Settings -> Updates).
    if (_settings->value("updater/check_on_startup", true).toBool()) {
        QTimer::singleShot(2000, this, [this](){ checkForUpdates(true); });
    }
}

MainWindow::~MainWindow() {
    // Ensure proper cleanup order to prevent QRhi resource leaks and QPixmap errors
    qDebug() << "MainWindow: Starting destructor cleanup sequence";

    // Drop the engine-switch stop hook so a late handover can't call stop() on
    // a half-destroyed window (the hook captured `this`).
    C64Mode::setStopPlaybackHook(nullptr);

    // Perform early cleanup if it hasn't been done already
    performEarlyCleanup();

    // Clean up shared clipboard resources
    SharedClipboard::instance()->cleanup();

    // Clean up static appearance resources to prevent QPixmap/QColor issues after QApplication shutdown
    Appearance::cleanup();
    InstrumentDefinitions::cleanup();

    qDebug() << "MainWindow: Destructor cleanup sequence completed";
}

void MainWindow::performEarlyCleanup() {
    static bool cleanupPerformed = false;
    if (cleanupPerformed) {
        return; // Prevent multiple cleanup calls
    }
    cleanupPerformed = true;

    qDebug() << "MainWindow: Performing early OpenGL cleanup";

    // Set shutdown flag immediately to prevent any QPixmap creation during cleanup
    Appearance::setShuttingDown(true);

    // Stop any ongoing MIDI operations first
    if (MidiPlayer::isPlaying()) {
        MidiPlayer::stop();
    }

    // End any ongoing MIDI input recording
    if (MidiInput::recording()) {
        MidiInput::endInput(nullptr); // Pass nullptr since we're just stopping, not saving
    }

    // Clean up OpenGL widgets explicitly while OpenGL context is still valid
    if (OpenGLMatrixWidget *openglMatrix = qobject_cast<OpenGLMatrixWidget*>(_matrixWidgetContainer)) {
        qDebug() << "MainWindow: Early cleanup of OpenGL matrix widget";
        openglMatrix->setParent(nullptr);
        delete openglMatrix;
        _matrixWidgetContainer = nullptr;
        mw_matrixWidget = nullptr;
    }

    if (_miscWidgetContainer && _miscWidgetContainer != _miscWidget) {
        qDebug() << "MainWindow: Early cleanup of OpenGL misc widget";
        _miscWidgetContainer->setParent(nullptr);
        delete _miscWidgetContainer;
        _miscWidgetContainer = nullptr;
        _miscWidget = nullptr;
    }

    // Force immediate processing of any pending events
    QApplication::processEvents(QEventLoop::AllEvents);

    qDebug() << "MainWindow: Early OpenGL cleanup completed";
}

void MainWindow::initializeSharedClipboard() {
    // Initialize shared clipboard
    SharedClipboard::instance()->initialize();

    // Update paste action state to check shared clipboard
    copiedEventsChanged();
}

void MainWindow::updatePasteActionState() {
    // Simple check - enable paste if shared clipboard is available
    if (_pasteAction && EventTool::copiedEvents->size() == 0) {
        SharedClipboard *clipboard = SharedClipboard::instance();
        bool sharedClipboardAvailable = clipboard->initialize();
        _pasteAction->setEnabled(sharedClipboardAvailable);
    }
}

void MainWindow::loadInitFile() {
    // Check for untitled auto-save recovery before loading
    checkAutoSaveRecovery();

    if (_initFile != "")
        loadFile(_initFile);
    else
        newFile();
}

void MainWindow::dropEvent(QDropEvent *ev) {
    highlightDropGroup(nullptr); // clear any drag-over highlight

    // Phase 28 (editor groups): a tab dragged out of a DocumentTabBar and
    // dropped on a pane's EDITOR AREA (i.e. not precisely on a tab bar, which
    // handles its own positional drop) moves into the group under the cursor,
    // appended. This makes the whole pane a drop target, so moving a tab to the
    // other group is forgiving (no need to hit the thin tab strip exactly).
    if (ev->mimeData()->hasFormat(DocumentTabBar::tabMimeType())) {
        DocumentTabBar *src = qobject_cast<DocumentTabBar *>(ev->source());
        if (src) {
            const int srcIndex =
                ev->mimeData()->data(DocumentTabBar::tabMimeType()).toInt();
            MatrixWidget *targetView = viewAtWindowPos(ev->position().toPoint());
            DocumentTabBar *targetBar =
                (targetView == _compareMatrixWidget)
                    ? qobject_cast<DocumentTabBar *>(_group1TabBar)
                    : qobject_cast<DocumentTabBar *>(_documentTabBar);
            // A pane drop appends to THAT group. Ignore a drop on the source's
            // own pane - it would otherwise yank the tab to the end for no reason
            // (the user was aiming at the other group and missed).
            if (targetBar && targetBar != src) {
                ev->acceptProposedAction();
                onTabMoveRequested(src, srcIndex, targetBar, targetBar->count());
            }
        }
        return;
    }

    // Otherwise: a file/URL drop -> open in the group under the cursor (when
    // split, not just whichever group happens to be focused). We focus that
    // group's view first; loadFile -> openInNewTab then opens into it.
    if (MatrixWidget *target = viewAtWindowPos(ev->position().toPoint())) {
        _activeView = target;
    }

    QList<QUrl> urls = ev->mimeData()->urls();
    foreach(QUrl url, urls) {
        QString newFile = url.toLocalFile();
        if (!newFile.isEmpty()) {
            loadFile(newFile);
            break;
        }
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *ev) {
    ev->accept();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *ev) {
    // Highlight the editor group the drop would go into (only meaningful when
    // split). Falls back to no highlight when there is a single group.
    if (_group1Container) {
        MatrixWidget *target = viewAtWindowPos(ev->position().toPoint());
        highlightDropGroup(target == _compareMatrixWidget ? _group1Container
                           : target == mw_matrixWidget    ? _group0Container
                                                          : nullptr);

        // For a TAB drag, also show the insertion caret at the append position
        // of the target group's bar - so the landing spot is visible while the
        // cursor is still over the editor area, not only once it reaches the bar.
        // (Over the bar itself, the bar's own dragMove shows the precise caret.)
        if (ev->mimeData()->hasFormat(DocumentTabBar::tabMimeType())) {
            DocumentTabBar *g0 = qobject_cast<DocumentTabBar *>(_documentTabBar);
            DocumentTabBar *g1 = qobject_cast<DocumentTabBar *>(_group1TabBar);
            DocumentTabBar *targetBar = (target == _compareMatrixWidget) ? g1 : g0;
            if (g0) {
                (g0 == targetBar) ? g0->showAppendDropIndicator() : g0->clearDropIndicator();
            }
            if (g1) {
                (g1 == targetBar) ? g1->showAppendDropIndicator() : g1->clearDropIndicator();
            }
        }
    }
    ev->accept();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *ev) {
    highlightDropGroup(nullptr);
    if (DocumentTabBar *g0 = qobject_cast<DocumentTabBar *>(_documentTabBar)) {
        g0->clearDropIndicator();
    }
    if (DocumentTabBar *g1 = qobject_cast<DocumentTabBar *>(_group1TabBar)) {
        g1->clearDropIndicator();
    }
    ev->accept();
}

MatrixWidget *MainWindow::viewAtWindowPos(const QPoint &windowPos) const {
    if (!_group1Container || !_compareMatrixWidget || _group1Collapsed ||
        !_group1Container->isVisible()) {
        return nullptr; // not split (or collapsed) -> single (primary) group
    }
    const QPoint inG1 = _group1Container->mapFrom(const_cast<MainWindow *>(this), windowPos);
    if (_group1Container->rect().contains(inG1)) {
        return _compareMatrixWidget;
    }
    return mw_matrixWidget;
}

void MainWindow::highlightDropGroup(QWidget *target) {
    if (target == _dropHighlightTarget) {
        return; // unchanged - dragMoveEvent fires continuously
    }
    _dropHighlightTarget = target;
    const QString accent = QStringLiteral("#3daee9");
    if (_group0Container) {
        _group0Container->setStyleSheet(
            QStringLiteral("#editorGroup0 { border: 2px solid %1; }")
                .arg(target == _group0Container ? accent : QStringLiteral("transparent")));
    }
    if (_group1Container) {
        _group1Container->setStyleSheet(
            QStringLiteral("#editorGroup1 { border: 2px solid %1; }")
                .arg(target == _group1Container ? accent : QStringLiteral("transparent")));
    }
}

void MainWindow::scrollPositionsChanged(int startMs, int maxMs, int startLine,
                                        int maxLine) {
    hori->setMinimum(0);
    hori->setMaximum(maxMs);
    // Force startMs to 0 if it's very close to 0 to eliminate dead space
    int clampedStartMs = (startMs < 10) ? 0 : startMs;
    hori->setValue(clampedStartMs);
    vert->setMaximum(maxLine);
    vert->setValue(startLine);
#ifdef MIDIEDITOR_COLLAB_ENABLED
    // Phase 9.9f §15.2: shadow the current viewport so the
    // presenter-side viewState broadcast can read it later. We don't
    // broadcast directly here — the actionFinished signal already does
    // (visibility changes), and a dedicated lambda connected to
    // mw_matrixWidget->scrollChanged handles the scroll case.
    _viewStartMs   = startMs;
    _viewMaxMs     = maxMs;
    _viewStartLine = startLine;
    _viewMaxLine   = maxLine;
#endif
}

#ifdef MIDIEDITOR_COLLAB_ENABLED
void MainWindow::broadcastLocalViewState() {
    if (!file) return;
    // Skip while we're applying a remote view-state — otherwise the
    // remote scroll would trigger our scrollChanged → broadcast →
    // host re-broadcast → us, looping forever.
    if (_applyingRemoteViewState) return;

    LiveSession::ViewportState vp;
    vp.startMs   = _viewStartMs;
    vp.maxMs     = _viewMaxMs;
    vp.startLine = _viewStartLine;
    vp.maxLine   = _viewMaxLine;
    // bugfix 2026-05-21 (user report): ship the zoom factors too,
    // so the viewer can run applyZoom() BEFORE scrollYChanged. Without
    // this, the viewer's wider visible-line count made scrollYChanged
    // clamp startLineY to 0 ("top of piano roll"), regardless of
    // where the presenter actually was.
    if (mw_matrixWidget) {
        vp.scaleX = mw_matrixWidget->currentScaleX();
        vp.scaleY = mw_matrixWidget->currentScaleY();
        // Fit-to-focus extents (2026-05-21 follow-up): ship the host's
        // visible-region end coordinates. Viewer uses these to fit
        // its own viewport to the SAME content area, independent of
        // window size. Without this the 1:1 scroll mirror snapped to
        // "top" whenever the viewer's window was wider than the host.
        vp.focusEndMs   = mw_matrixWidget->visibleEndMs();
        vp.focusEndLine = mw_matrixWidget->visibleEndLine();
    }
    // Phase 9.9f extension 2026-05-21: include the presenter's edit
    // cursor + active tool name so viewers see where the host is
    // working and which tool they're using.
    vp.cursorTick = file->cursorTick();
    EditorTool *t = Tool::currentTool();
    if (t) vp.activeToolName = t->toolTip();
    // Selection mirror (user follow-up, 2026-05-21): ship the
    // presenter's selected events as identity tuples so the viewer
    // can highlight the same notes. Empty when nothing's selected.
    QList<MidiEvent *> sel = Selection::instance()->selectedEvents();
    vp.selectedEvents.reserve(sel.size());
    for (MidiEvent *ev : sel) {
        if (!ev) continue;
        LiveSession::ViewportState::SelectedEventId id;
        id.tick    = ev->midiTime();
        id.channel = ev->channel();
        id.line    = ev->line();
        id.type    = ev->typeString();
        vp.selectedEvents.append(id);
    }

    QVector<bool> tracks, channels;
    tracks.reserve(file->numTracks());
    for (int i = 0; i < file->numTracks(); i++) {
        MidiTrack *t = file->track(i);
        tracks.append(t ? !t->hidden() : true);
    }
    channels.reserve(16);
    for (int i = 0; i < 16; i++) {
        MidiChannel *c = file->channel(i);
        channels.append(c ? c->visible() : true);
    }
    LanLiveSession::instance()->broadcastViewState(vp, tracks, channels);
}

void MainWindow::applyRemoteViewState(
        const LiveSession::ViewportState &viewport,
        const QVector<bool> &trackVisibility,
        const QVector<bool> &channelVisibility) {
    if (!file) return;
    // QScopedValueRollback so a nested re-entry (e.g. updateAll() or a
    // setCursorTick callback chain that spins a transient event loop)
    // restores the flag to the OUTER scope's value rather than
    // unconditionally clearing it on the inner return. See the parallel
    // fix in the playbackTriggerReceived lambda above. (bughunter
    // MEDIUM finding 2026-05-24)
    QScopedValueRollback<bool> guard(_applyingRemoteViewState, true);
    // Visibility first — fires no signals (silent setters), so we
    // batch them then issue a single repaint at the end.
    for (int i = 0; i < trackVisibility.size() && i < file->numTracks(); i++) {
        MidiTrack *t = file->track(i);
        if (t) t->setHiddenSilent(!trackVisibility[i]);
    }
    for (int i = 0; i < channelVisibility.size() && i < 16; i++) {
        MidiChannel *c = file->channel(i);
        if (c) c->setVisibleSilent(channelVisibility[i]);
    }
    if (mw_matrixWidget) {
        // Fit-to-focus mode (preferred design choice,
        // 2026-05-21): when the wire frame carries focus extents,
        // resize the local viewport to fit the presenter's visible
        // region. Computes the right scale locally based on the
        // VIEWER's pixel geometry, so window/zoom mismatches no
        // longer push startLineY to 0.
        const bool haveFocusExtents = (viewport.focusEndMs   >= 0
                                       && viewport.focusEndLine >= 0
                                       && viewport.focusEndMs   > viewport.startMs
                                       && viewport.focusEndLine > viewport.startLine);
        if (haveFocusExtents) {
            mw_matrixWidget->fitToFocus(viewport.startMs,
                                         viewport.focusEndMs,
                                         viewport.startLine,
                                         viewport.focusEndLine);
        } else {
            // Legacy fallback (initial-v1.7.2 builds without focus
            // extents): mirror scale + scroll 1:1. Imperfect when
            // window sizes differ but better than nothing.
            mw_matrixWidget->applyZoom(viewport.scaleX, viewport.scaleY);
            mw_matrixWidget->scrollXChanged(viewport.startMs);
            mw_matrixWidget->scrollYChanged(viewport.startLine);
        }
    }
    // Edit-cursor (Phase 9.9f extension 2026-05-21): mirror the
    // presenter's cursorTick so the viewer's playhead / edit-marker
    // sits on the same beat as the host's. -1 = "unknown" sentinel —
    // skip then so a legacy frame doesn't snap our cursor to position
    // -1 (which would error).
    if (viewport.cursorTick >= 0) {
        file->setCursorTick(viewport.cursorTick);
    }
    // Active tool indicator (Phase 9.9f extension 2026-05-21): a
    // transient status-bar message telling the viewer which tool the
    // presenter is using. Re-fires on every viewState update — the
    // status bar's auto-clear timeout means it stays visible while
    // the presenter is active, then fades when the throttle stops.
    if (!viewport.activeToolName.isEmpty()) {
        statusBar()->showMessage(
            tr("Presenter is using: %1").arg(viewport.activeToolName), 3000);
    }

    // Selection mirror (2026-05-21): rebuild the local selection from
    // the presenter's tuples so the same notes appear highlighted.
    // Empty tuple list clears the selection. We scan each channel
    // once and check each event against the tuple set — O(N_events *
    // N_selected). For typical FFXIV sessions both are small; if it
    // ever becomes hot we can build a (tick, channel, line)→event
    // index once per frame.
    {
        QList<MidiEvent *> mirrored;
        if (!viewport.selectedEvents.isEmpty()) {
            // Build a fast lookup keyed on the tuple so the per-event
            // check is O(1) instead of O(N_selected).
            QSet<QString> wanted;
            wanted.reserve(viewport.selectedEvents.size());
            for (const auto &id : viewport.selectedEvents) {
                wanted.insert(QStringLiteral("%1|%2|%3|%4")
                                  .arg(id.tick).arg(id.channel)
                                  .arg(id.line).arg(id.type));
            }
            for (int ch = 0; ch < 19; ch++) {
                MidiChannel *c = file->channel(ch);
                if (!c) continue;
                for (MidiEvent *ev : c->eventMap()->values()) {
                    if (!ev) continue;
                    QString key = QStringLiteral("%1|%2|%3|%4")
                        .arg(ev->midiTime()).arg(ev->channel())
                        .arg(ev->line()).arg(ev->typeString());
                    if (wanted.contains(key)) mirrored.append(ev);
                }
            }
        }
        Selection::instance()->setSelectionSilent(mirrored);
    }

    // Force redraw + sidebar refresh so the visibility flips are seen.
    updateAll();
    // _applyingRemoteViewState restored automatically by guard at scope exit.
}
#endif

void MainWindow::setFile(MidiFile *newFile) {
    // Single-document "replace" path: swap the one open document for newFile
    // and destroy the old one. (Phase 28: opening files in tabs goes through
    // activateDocument + closeDocumentFile instead, so the active document is
    // not destroyed on switch.) Behaviour here is unchanged from before the
    // split: every caller passes a freshly-constructed MidiFile, so it is
    // activated exactly once and then the previous file is closed.
    MidiFile *oldFile = this->file;

    // Clear the outgoing selection. (Channel visibility is per-document since
    // 28.1c: a fresh document defaults to all-visible via setActiveFile, so no
    // global reset is needed here - and a reset would wrongly target whichever
    // document is currently active.)
    EventTool::clearSelection();

    activateDocument(newFile);

    // Keep the DocumentManager + tab strip in sync. setFile is the "replace
    // the active document" path: update the active document's file (and its
    // tab label), or create the very first document/tab if none exist yet
    // (startup). Other open tabs are untouched.
    if (_documentManager) {
        Document *active = _documentManager->active();
        if (active) {
            active->setFile(newFile);
            if (_documentTabBar && _documentManager->activeIndex() >= 0) {
                _suppressTabSignals = true;
                _documentTabBar->setTabText(_documentManager->activeIndex(), documentTabTitle(newFile));
                _suppressTabSignals = false;
            }
        } else {
            Document *d = _documentManager->openAndActivate(newFile, documentTabTitle(newFile));
            if (_documentTabBar) {
                _suppressTabSignals = true;
                int idx = _documentTabBar->addTab(d->title());
                _documentTabBar->setCurrentIndex(idx);
                _suppressTabSignals = false;
            }
        }
    }

    if (oldFile && oldFile != newFile) {
        closeDocumentFile(oldFile);
    }
}

void MainWindow::setActiveDocument(MidiFile *newFile) {
    // Phase 28 (B): rebind sidebars / globals / selection / tools / transport /
    // MidiPilot / MCP to the active (focused) document - WITHOUT touching any
    // editor view's file binding (the views are bound separately so two panes
    // can show different documents). activateDocument() adds the primary-view
    // rebind on top of this for the single-view / tab-switch path.
    //
    // The one-time, per-file signal wiring must run exactly once per MidiFile.
    // In the single-document replace path each file is activated once, so this
    // matches the historical behaviour; for tab switching it prevents the
    // connections from stacking up every time the user returns to a tab.
    const bool firstActivation = newFile && !_connectedFiles.contains(newFile);

    Selection::setFile(newFile);
    // Phase 28.1c: channel visibility is per-document; make this document's
    // state active before the panels (channelWidget etc.) read it below. A new
    // document defaults to all-visible; returning to one restores its state.
    ChannelVisibilityManager::instance().setActiveFile(newFile);

    Metronome::instance()->setFile(newFile);
    protocolWidget->setFile(newFile);
    channelWidget->setFile(newFile);
    _trackWidget->setFile(newFile);
    eventWidget()->setFile(newFile);

    Tool::setFile(newFile);
    _midiPilotWidget->onFileChanged(newFile);
    if (_mcpServer) _mcpServer->setFile(newFile);

#ifdef MIDIEDITOR_COLLAB_ENABLED
    CollabService::instance()->onFileLoaded(newFile, newFile ? newFile->path() : QString());
#endif

    this->file = newFile;
    if (newFile) {
        setWindowTitle(QApplication::applicationName() + " v" + QApplication::applicationVersion() + " - " + newFile->path() + "[*]");
    }

    // ----- one-time per-file signal wiring -------------------------------
    if (firstActivation) {
        _connectedFiles.insert(newFile);

        // Live track-mute -> authentic-SID voice mute (channels already covered
        // by channelWidget::channelStateChanged). Connections to a file's tracks
        // fall away when that file is destroyed.
        for (MidiTrack *t : *(newFile->tracks()))
            connect(t, &MidiTrack::trackChanged, this,
                    [this] { syncSidVoiceMutes(file); });

        connect(newFile, SIGNAL(trackChanged()), this, SLOT(updateTrackMenu()));
        connect(newFile, SIGNAL(cursorPositionChanged()), channelWidget, SLOT(update()));
        connect(newFile, SIGNAL(recalcWidgetSize()), _matrixWidgetContainer, SLOT(calcSizes()));
        connect(newFile->protocol(), SIGNAL(actionFinished()), this, SLOT(markEdited()));
        connect(newFile->protocol(), SIGNAL(actionFinished()), eventWidget(), SLOT(reload()));
        connect(newFile->protocol(), SIGNAL(actionFinished()), this, SLOT(checkEnableActionsForSelection()));
        connect(newFile->protocol(), SIGNAL(actionFinished()), this, SLOT(updateStatusBar()));
#ifdef MIDIEDITOR_COLLAB_ENABLED
        // Phase 9.9f §15.2: piggyback on actionFinished to broadcast the
        // presenter's view-state (track/channel visibility primarily —
        // those changes go through Protocol via setHidden/setVisible).
        // broadcastViewState is internally guarded on role+mode+isPresenter,
        // so this connect is safe even outside Show mode (silent no-op).
        // The throttle in LanLiveSession coalesces rapid sequences.
        connect(newFile->protocol(), &Protocol::actionFinished, this,
                [this]() { broadcastLocalViewState(); });
        // Same for cursor moves — file emits cursorPositionChanged whenever
        // setCursorTick lands. Lets viewers see where the host is editing.
        connect(newFile, &MidiFile::cursorPositionChanged, this,
                [this]() { broadcastLocalViewState(); });
#endif
        // Refresh LyricManager from MIDI events after undo/redo so blocks stay in sync
        connect(newFile->protocol(), &Protocol::undoRedoPerformed, newFile->lyricManager(), &LyricManager::importFromTextEvents);
        // Update lyric timeline after undo/redo
        connect(newFile->protocol(), &Protocol::undoRedoPerformed, _lyricTimeline, QOverload<>::of(&QWidget::update));
        connect(newFile, SIGNAL(cursorPositionChanged()), this, SLOT(updateStatusBar()));
    }

    // ----- sidebar / aux-panel rebinds (every activation) ----------------
    // NB: the primary editor view is rebound in activateDocument(), not here -
    // setActiveDocument() must not touch any view so a side-by-side pane keeps
    // showing its own document when another pane is focused.

    // Update lyric timeline
    _lyricTimeline->setFile(newFile);

    // Phase 32.3: voice load lane
    if (_voiceLaneWidget) {
        _voiceLaneWidget->setFile(newFile);
    }

    // Update lyric visualizer
    if (_lyricVisualizer) {
        _lyricVisualizer->setFile(newFile);
    }

    // Phase 41: rebind the cursor-time display to the new file (re-wires
    // its cursorPositionChanged + protocol-actionFinished connections).
    if (_timeDisplay) {
        _timeDisplay->setFile(newFile);
    }

    // Update FFXIV voice gauge (Phase 32.1)
    if (_ffxivVoiceGauge) {
        _ffxivVoiceGauge->setFile(newFile);
    }
    // Always tell the analyser about the new file so cached results stay
    // valid across file switches even if the gauge widget isn't currently
    // in the toolbar.
    if (newFile) {
        FfxivVoiceAnalyzer::instance()->watchFile(newFile);
    }

    // Auto-show/hide lyric timeline based on lyrics presence
    if (Appearance::autoShowLyricTimeline()) {
        bool hasLyrics = newFile && newFile->lyricManager() && newFile->lyricManager()->hasLyrics();
        _lyricArea->setVisible(hasLyrics);
        if (_toggleLyricTimeline) {
            _toggleLyricTimeline->setChecked(hasLyrics);
        }
    }
    updateChannelMenu();
    updateTrackMenu();
    _matrixWidgetContainer->update();
    _miscWidgetContainer->update();

    // Update paste action state when file changes
    copiedEventsChanged();
    checkEnableActionsForSelection();

#ifdef FLUIDSYNTH_SUPPORT
    if (_exportAudioAction) {
        _exportAudioAction->setEnabled(newFile != nullptr);
    }
    // _exportMusicXmlAction is only declared/created under FLUIDSYNTH_SUPPORT
    // (see createMenuBar), so its use must stay inside the same guard - otherwise
    // a no-FluidSynth build fails with C2065 on the undeclared member.
    if (_exportMusicXmlAction) {
        _exportMusicXmlAction->setEnabled(newFile != nullptr);
    }
#endif

    // Reset MIDI output channel programs and apply initial program changes
    if (file && MidiOutput::isConnected()) {
        MidiOutput::resetChannelPrograms();
        // Send program change events from the beginning of the file
        for (int ch = 0; ch < 16; ch++) {
            int prog = file->channel(ch)->progAtTick(0);
            if (prog >= 0) {
                MidiOutput::sendProgram(ch, prog);
            }
        }
    }
}

void MainWindow::bindPrimaryView(MidiFile *f) {
    // Rebind ONLY the primary editor view to f (OpenGL wrapper or the software
    // MatrixWidget, depending on the rendering mode) - no sidebar/active changes.
    if (OpenGLMatrixWidget *openglMatrix = qobject_cast<OpenGLMatrixWidget*>(_matrixWidgetContainer)) {
        openglMatrix->setFile(f);
    } else {
        mw_matrixWidget->setFile(f);
    }
}

void MainWindow::activateDocument(MidiFile *newFile) {
    // Single-view / tab-switch path: make newFile the active document AND show
    // it in the primary editor view. (Side-by-side panes bind their views
    // separately and only call setActiveDocument() on focus.)
    setActiveDocument(newFile);
    bindPrimaryView(newFile);
}

void MainWindow::closeDocumentFile(MidiFile *oldFile) {
    if (!oldFile) {
        return;
    }
    // Phase 28: drop the closing document's per-file state so nothing leaks or
    // dangles past the delete. Deleting the MidiFile (a QObject) automatically
    // disconnects every signal connection made to it in activateDocument.
    // Phase 28 (editor groups): defensive - documents are partitioned between
    // the groups, so a file closed through a group-0 path is never the one shown
    // in the secondary group. If that invariant is ever broken, drop the
    // secondary group entirely so its view does not dangle on the deleted file.
    if (_compareFile == oldFile && _group1Docs) {
        _compareFile = nullptr;
        tearDownGroup1();
    }

    FfxivVoiceAnalyzer::instance()->forgetFile(oldFile);
    Selection::forgetFile(oldFile);
    ChannelVisibilityManager::instance().forgetFile(oldFile);
    _connectedFiles.remove(oldFile);
    delete oldFile;
}

void MainWindow::configureDocumentTabBar(QTabBar *bar) {
    if (!bar) {
        return;
    }
    // The built-in mover stays OFF - DocumentTabBar implements its own drag for
    // both in-bar reordering and moving tabs between editor groups.
    bar->setMovable(false);
    bar->setTabsClosable(true);
    bar->setExpanding(false);
    bar->setDrawBase(false);
    bar->setElideMode(Qt::ElideRight);
    bar->setUsesScrollButtons(true);
}

QWidget *MainWindow::buildGroupTabStrip(QToolButton *plusButton, QTabBar *bar) {
    QWidget *strip = new QWidget();
    // Only as tall as the tab bar itself - without a fixed vertical policy the
    // wrapper expands and the strip floats in the middle of the editor area.
    strip->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QHBoxLayout *layout = new QHBoxLayout(strip);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    // "+" to the LEFT of the tabs so it hugs the first tab (a QTabBar reserves
    // extra width with few tabs, so a trailing "+" would float away from them).
    layout->addWidget(plusButton, 0);
    layout->addWidget(bar, 0);
    layout->addStretch(1);
    return strip;
}

void MainWindow::toggleCompareView() {
    if (!_viewSplitter || !_documentManager) {
        return;
    }

    // Already split? -> the Split button now toggles the secondary group's
    // visibility: collapse it (keeping its tabs) when shown, restore it when
    // collapsed. Fully closing the group (with save prompts) is the group's own
    // close button.
    if (_group1Docs) {
        if (_group1Collapsed) {
            restoreGroup1();
        } else {
            collapseGroup1();
        }
        return;
    }
    _group1Collapsed = false;

    // Need a second document to move into the new group. (Splitting the same
    // file into two groups can come later; for now keep group 0 non-empty.)
    MidiFile *moveFile = nullptr;
    QString moveTitle;
    int moveIdx = -1;
    const int activeIdx = _documentManager->activeIndex();
    for (int i = 0; i < _documentManager->count(); ++i) {
        if (i != activeIdx) {
            Document *d = _documentManager->at(i);
            moveFile = d->file();
            moveTitle = d->title();
            moveIdx = i;
            break;
        }
    }
    if (!moveFile) {
        QMessageBox::information(this, tr("Editor Groups"),
            tr("Open a second file in another tab, then split to move it into a second editor group."));
        return;
    }

    // ----- build the secondary group (group 1) ---------------------------
    _group1Docs = new DocumentManager();

    DocumentTabBar *group1Bar = new DocumentTabBar();
    _group1TabBar = group1Bar;
    configureDocumentTabBar(_group1TabBar);
    connect(_group1TabBar, &QTabBar::currentChanged, this, &MainWindow::onGroup1TabChanged);
    connect(_group1TabBar, &QTabBar::tabCloseRequested, this, &MainWindow::onGroup1TabCloseRequested);
    connect(group1Bar, &DocumentTabBar::tabMoveRequested, this, &MainWindow::onTabMoveRequested);

    // A "+" that opens a new tab IN this (secondary) group.
    QToolButton *group1NewTab = new QToolButton();
    group1NewTab->setIcon(Appearance::adjustIconForDarkMode(":/run_environment/graphics/tool/add.png"));
    group1NewTab->setToolTip(tr("New tab in this editor group"));
    group1NewTab->setAutoRaise(true);
    connect(group1NewTab, &QToolButton::clicked, this, [this] {
        _activeView = _compareMatrixWidget;
        newFile();
    });

    // A lightweight software MatrixWidget (no second GL context). It is a FULLY
    // EDITABLE pane (defaults: claimsToolTarget=true, editingLocked=false);
    // clicking it makes its document active (focusReceived -> onViewFocused).
    _compareMatrixWidget = new MatrixWidget(_settings);
    connect(_compareMatrixWidget, &MatrixWidget::focusReceived,
            this, &MainWindow::onViewFocused);

    // Container = vertical [ tab strip | view ], built exactly like group 0 so
    // the two strips align in height. This group carries its own tabs (VS Code-
    // style editor group) and travels as one unit in the splitter.
    QWidget *g1Strip = buildGroupTabStrip(group1NewTab, _group1TabBar);
    // Far-right controls for the secondary group: collapse (hide, keep tabs) and
    // close (close the group + its tabs, with save prompts). buildGroupTabStrip
    // ends the strip with a stretch, so these land at the right edge.
    if (QHBoxLayout *sl = qobject_cast<QHBoxLayout *>(g1Strip->layout())) {
        QToolButton *collapseBtn = new QToolButton();
        collapseBtn->setText(QChar(0x2013)); // en dash = "minimize / retract"
        collapseBtn->setAutoRaise(true);
        collapseBtn->setToolTip(tr("Collapse this group (keep its tabs; Split restores it)"));
        connect(collapseBtn, &QToolButton::clicked, this, &MainWindow::collapseGroup1);
        sl->addWidget(collapseBtn, 0);

        QToolButton *closeBtn = new QToolButton();
        closeBtn->setText(QChar(0x2715)); // multiplication X = "close"
        closeBtn->setAutoRaise(true);
        closeBtn->setToolTip(tr("Close this group and all its tabs"));
        connect(closeBtn, &QToolButton::clicked, this, &MainWindow::closeGroup1);
        sl->addWidget(closeBtn, 0);
    }
    _group1Container = new QWidget();
    _group1Container->setObjectName("editorGroup1");
    _group1Container->setStyleSheet("#editorGroup1 { border: 2px solid transparent; }");
    QVBoxLayout *g1Layout = new QVBoxLayout(_group1Container);
    g1Layout->setContentsMargins(0, 0, 0, 0);
    g1Layout->setSpacing(0);
    g1Layout->addWidget(g1Strip, 0);
    g1Layout->addWidget(_compareMatrixWidget, 1);

    // Add to the splitter first so the view has a real width before setFile()
    // runs calcSizes(); mirror the primary's rendering settings.
    _viewSplitter->addWidget(_group1Container);
    _compareMatrixWidget->updateRenderingSettings();

    // ----- move the chosen document from group 0 into group 1 ------------
    _documentManager->removeAt(moveIdx);   // detaches (does NOT delete the file)
    _suppressTabSignals = true;
    _documentTabBar->removeTab(moveIdx);
    _documentTabBar->setCurrentIndex(_documentManager->activeIndex());
    _suppressTabSignals = false;

    Document *gd = _group1Docs->openAndActivate(moveFile, moveTitle);
    _suppressGroup1TabSignals = true;
    int gi = _group1TabBar->addTab(gd->title());
    _group1TabBar->setCurrentIndex(gi);
    _suppressGroup1TabSignals = false;
    _compareMatrixWidget->setFile(moveFile);
    _compareFile = moveFile;

    // Split the editor area evenly so the new group opens at a usable width.
    const int splitW = _viewSplitter->width();
    if (splitW > 0) {
        _viewSplitter->setSizes(QList<int>() << splitW / 2 << splitW / 2);
    }

    // The MatrixWidget ctor stole the static tool target; hand it back to the
    // primary view until the user clicks a pane (onViewFocused then routes it).
    if (mw_matrixWidget) EditorTool::setMatrixWidget(mw_matrixWidget);
    _activeView = mw_matrixWidget;

    statusBar()->showMessage(tr("Moved %1 into a second editor group (click a pane to focus it)").arg(moveTitle), 4000);
}

void MainWindow::tearDownGroup1() {
    if (!_group1Docs) {
        return; // not split
    }

    // _group1Docs owns only the Document handles (not the MidiFiles); deleting
    // it leaves the files intact (the caller deletes/keeps them as needed).
    delete _group1Docs;
    _group1Docs = nullptr;
    // Deleting the container deletes its children (the tab bar + the view). This
    // can run from inside the tab bar's own tabCloseRequested slot (collapse on
    // last-tab-close), so detach it from the splitter now and deleteLater() the
    // widget tree - deleting a signal sender mid-emission would crash.
    if (_group1Container) {
        _group1Container->setParent(nullptr); // removes it from the splitter + hides it
        _group1Container->deleteLater();
    }
    _group1Container = nullptr;
    _group1TabBar = nullptr;
    _compareMatrixWidget = nullptr;
    _compareFile = nullptr;
    _group1Collapsed = false;
    _viewSplitterSizes.clear();

    _activeView = mw_matrixWidget;
    if (mw_matrixWidget) EditorTool::setMatrixWidget(mw_matrixWidget);

    // The primary view is the only one left; make its active document current.
    Document *a = _documentManager ? _documentManager->active() : nullptr;
    if (a) {
        _suppressTabSignals = true;
        _documentTabBar->setCurrentIndex(_documentManager->activeIndex());
        _suppressTabSignals = false;
        activateDocument(a->file());
    }
}

void MainWindow::collapseGroup1() {
    if (!_group1Container || _group1Collapsed) {
        return;
    }
    // Remember the split so restore returns to the same widths, then hide the
    // pane. The documents/tabs stay alive in _group1Docs.
    _viewSplitterSizes = _viewSplitter->sizes();
    _group1Container->hide();
    _group1Collapsed = true;

    // Focus returns to the (now only visible) primary group.
    _activeView = mw_matrixWidget;
    if (mw_matrixWidget) EditorTool::setMatrixWidget(mw_matrixWidget);
    if (Document *a = _documentManager->active()) {
        activateDocument(a->file());
    }
    statusBar()->showMessage(
        tr("Second editor group collapsed - its tabs are kept (Split restores it)"), 4000);
}

void MainWindow::restoreGroup1() {
    if (!_group1Container || !_group1Collapsed) {
        return;
    }
    _group1Container->show();
    _group1Collapsed = false;
    if (_viewSplitterSizes.size() == _viewSplitter->count()) {
        _viewSplitter->setSizes(_viewSplitterSizes);
    } else {
        const int w = _viewSplitter->width();
        if (w > 0) {
            _viewSplitter->setSizes(QList<int>() << w / 2 << w / 2);
        }
    }
    statusBar()->showMessage(tr("Second editor group restored"), 3000);
}

void MainWindow::closeGroup1() {
    if (!_group1Docs) {
        return;
    }
    // Prompt to save each dirty document first (saveBeforeClose acts on the
    // active file, so make each one active in the secondary group before
    // asking). Abort the whole close if the user cancels any prompt.
    if (_group1Collapsed) {
        restoreGroup1(); // make the group visible so the prompts have context
    }
    // Snapshot the MidiFile* list UP FRONT, before any modal save prompt. The
    // files are only ever deleted by closeDocumentFile (never during a modal
    // prompt), so this list stays valid even if the Document handles were to
    // change - we delete exactly these files at the end.
    const QList<Document *> docs = _group1Docs->documents();
    QList<MidiFile *> files;
    for (Document *d : docs) {
        files.append(d->file());
    }
    for (Document *d : docs) {
        MidiFile *f = d->file();
        if (f && !f->saved()) {
            _activeView = _compareMatrixWidget;
            if (_compareFile != f) {
                _compareMatrixWidget->setFile(f);
                _compareFile = f;
            }
            const int idx = _group1Docs->indexOf(d);
            _group1Docs->setActiveIndex(idx);
            if (idx >= 0 && _group1TabBar) {
                _suppressGroup1TabSignals = true;
                _group1TabBar->setCurrentIndex(idx);
                _suppressGroup1TabSignals = false;
            }
            setActiveDocument(f);
            if (!saveBeforeClose()) {
                return; // user cancelled -> keep the group open
            }
        }
    }

    // Tear the group's view/bar/manager down, then delete the snapshot files.
    // (tearDownGroup1 deletes the Document handles, not the MidiFiles.)
    _compareFile = nullptr;
    tearDownGroup1();
    for (MidiFile *f : files) {
        closeDocumentFile(f);
    }
    statusBar()->showMessage(tr("Second editor group closed"), 3000);
}

void MainWindow::onViewFocused(MatrixWidget *view) {
    if (!view) {
        return;
    }
    // Remember which pane is focused: a "+"/tab/open loads its document here.
    _activeView = view;
    MidiFile *f = view->midiFile();
    if (!f) {
        return;
    }
    // Make the focused pane's document active. setActiveDocument rebinds the
    // sidebars/selection/tools/transport WITHOUT touching either view's file,
    // so each pane keeps showing its own document.
    if (f != this->file) {
        setActiveDocument(f);
    }
    // Keep the focused group's tab highlight in sync, without re-triggering a
    // tab switch (which would rebind a view).
    if (view == _compareMatrixWidget && _group1Docs && _group1TabBar) {
        const int gi = _group1Docs->indexOfFile(f);
        if (gi >= 0) {
            _group1Docs->setActiveIndex(gi);
            _suppressGroup1TabSignals = true;
            _group1TabBar->setCurrentIndex(gi);
            _suppressGroup1TabSignals = false;
        }
    } else if (_documentManager && _documentTabBar) {
        const int idx = _documentManager->indexOfFile(f);
        if (idx >= 0) {
            _documentManager->setActiveIndex(idx);
            _suppressTabSignals = true;
            _documentTabBar->setCurrentIndex(idx);
            _suppressTabSignals = false;
        }
    }
}

QString MainWindow::documentTabTitle(MidiFile *f) const {
    if (!f) {
        return tr("Untitled");
    }
    const QString p = f->path();
    if (p.isEmpty()) {
        return tr("Untitled");
    }
    return QFileInfo(p).fileName();
}

void MainWindow::openInNewTab(MidiFile *f) {
    if (!f) {
        return;
    }
    // Safety net: if the manager/tab bar somehow are not constructed yet, fall
    // back to the single-document replace path.
    if (!_documentManager || !_documentTabBar) {
        setFile(f);
        return;
    }

    // A freshly opened document starts with all channels visible: that is the
    // per-document default created lazily by ChannelVisibilityManager when the
    // new file becomes active in activateDocument()/setActiveDocument() below
    // (28.1c). No global reset here - it would clobber another tab's visibility.

    // Phase 28 (editor groups): open into the FOCUSED group. If the secondary
    // group has focus, the new document/tab is created there; otherwise it goes
    // into the primary group (group 0).
    if (_group1Docs && _group1TabBar && _compareMatrixWidget &&
        !_group1Collapsed && _activeView == _compareMatrixWidget) {
        Document *d = _group1Docs->openAndActivate(f, documentTabTitle(f));
        _suppressGroup1TabSignals = true;
        const int idx = _group1TabBar->addTab(d->title());
        _group1TabBar->setCurrentIndex(idx);
        _suppressGroup1TabSignals = false;
        _compareMatrixWidget->setFile(f);
        _compareFile = f;
        setActiveDocument(f);
        return;
    }

    Document *d = _documentManager->openAndActivate(f, documentTabTitle(f));
    _suppressTabSignals = true;
    const int idx = _documentTabBar->addTab(d->title());
    _documentTabBar->setCurrentIndex(idx);
    _suppressTabSignals = false;

    _activeView = mw_matrixWidget;
    activateDocument(f);
}

void MainWindow::onDocumentTabChanged(int index) {
    if (_suppressTabSignals || !_documentManager) {
        return;
    }
    Document *d = _documentManager->at(index);
    if (!d) {
        return;
    }
    MidiFile *f = d->file();
    // Single playback stream: stop when leaving a tab. Selection/visibility are
    // per-document (Selection is already keyed per file), so we do NOT clear
    // the selection on switch.
    stop();
    _documentManager->setActiveIndex(index);

    // Phase 28 (editor groups): group 0's tab bar always drives the primary
    // view (group 0 is the focused group now). The secondary group has its own
    // tab bar (onGroup1TabChanged) and never reacts to this one.
    _activeView = mw_matrixWidget;
    activateDocument(f);
}

void MainWindow::onGroup1TabChanged(int index) {
    if (_suppressGroup1TabSignals || !_group1Docs || !_compareMatrixWidget) {
        return;
    }
    Document *d = _group1Docs->at(index);
    if (!d) {
        return;
    }
    MidiFile *f = d->file();
    stop();
    _group1Docs->setActiveIndex(index);

    // The secondary group is now the focused one; load its tab into the
    // secondary view and make that document active (sidebars/tools follow).
    _activeView = _compareMatrixWidget;
    if (_compareFile != f) {
        _compareMatrixWidget->setFile(f);
        _compareFile = f;
    }
    setActiveDocument(f);
}

void MainWindow::onGroup1TabCloseRequested(int index) {
    if (!_group1Docs || !_group1TabBar) {
        return;
    }
    Document *d = _group1Docs->at(index);
    if (!d) {
        return;
    }
    MidiFile *f = d->file();

    // Prompt to save if the closing tab has unsaved changes. saveBeforeClose
    // acts on the active file, so make the closing tab active (in the secondary
    // group) first.
    if (f && !f->saved()) {
        _activeView = _compareMatrixWidget;
        if (_compareFile != f) {
            _compareMatrixWidget->setFile(f);
            _compareFile = f;
        }
        _group1Docs->setActiveIndex(index);
        _suppressGroup1TabSignals = true;
        _group1TabBar->setCurrentIndex(index);
        _suppressGroup1TabSignals = false;
        setActiveDocument(f);
        if (!saveBeforeClose()) {
            return; // user cancelled
        }
    }

    const bool closingActive = (index == _group1Docs->activeIndex());

    _group1Docs->removeAt(index);   // detaches (does NOT delete the file)
    _suppressGroup1TabSignals = true;
    _group1TabBar->removeTab(index);
    _suppressGroup1TabSignals = false;

    // Last tab in the secondary group closed -> collapse the group.
    if (_group1Docs->isEmpty()) {
        _compareFile = nullptr;         // stop the teardown guard double-acting
        tearDownGroup1();
        closeDocumentFile(f);
        return;
    }

    if (closingActive) {
        Document *na = _group1Docs->active();
        if (na) {
            _suppressGroup1TabSignals = true;
            _group1TabBar->setCurrentIndex(_group1Docs->activeIndex());
            _suppressGroup1TabSignals = false;
            stop();
            _compareMatrixWidget->setFile(na->file());
            _compareFile = na->file();
            setActiveDocument(na->file());
        }
    }

    // The closed file is no longer bound to any group/view, so tear it down.
    closeDocumentFile(f);
}

DocumentManager *MainWindow::managerForTabBar(QTabBar *bar) const {
    if (bar == _documentTabBar) {
        return _documentManager;
    }
    if (bar == _group1TabBar) {
        return _group1Docs;
    }
    return nullptr;
}

QToolBar *MainWindow::buildTabToolsBar(QWidget *parent, int iconSize) {
    // The tab-tools row that sits under the essential (New/Open/Save/Undo/Redo)
    // toolbar in two-row mode: New Tab / Split / Clone. Styled like the essential
    // bar (text beside icon) so it reads as a labelled second row.
    QToolBar *bar = new QToolBar("TabTools", parent);
    bar->setObjectName("tabToolsBar");
    bar->setFloatable(false);
    bar->setMovable(false);
    bar->setContentsMargins(0, 0, 0, 0);
    bar->layout()->setSpacing(3);
    bar->setIconSize(QSize(iconSize, iconSize));
    bar->setStyleSheet("QToolBar { border: 0px }");
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    QAction *newTabAct = new QAction(tr("New Tab"), bar);
    Appearance::setActionIcon(newTabAct, ":/run_environment/graphics/tool/add.png");
    newTabAct->setToolTip(tr("Open a new empty document in a tab (in the focused group)"));
    connect(newTabAct, &QAction::triggered, this, [this] { newFile(); });
    bar->addAction(newTabAct);

    QAction *splitAct = new QAction(tr("Split"), bar);
    splitAct->setIcon(makeSplitViewIcon(iconSize));
    splitAct->setToolTip(tr("Split into a second editor group / close it again"));
    connect(splitAct, &QAction::triggered, this, &MainWindow::toggleCompareView);
    bar->addAction(splitAct);

    QAction *cloneAct = new QAction(tr("Clone"), bar);
    Appearance::setActionIcon(cloneAct, ":/run_environment/graphics/tool/copy.png");
    cloneAct->setToolTip(tr("Duplicate the current document into a new tab"));
    connect(cloneAct, &QAction::triggered, this, &MainWindow::cloneCurrentDocument);
    bar->addAction(cloneAct);

    return bar;
}

QIcon MainWindow::makeSplitViewIcon(int size) const {
    if (size <= 0) {
        size = 16;
    }
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor fg = Appearance::foregroundColor();
    const qreal penW = qMax(1.0, size / 16.0);
    const qreal pad = qMax(2.0, size * 0.16);
    const QRectF frame(pad, pad, size - 2 * pad, size - 2 * pad);
    const qreal radius = qMax(2.0, size * 0.14);
    const qreal midX = frame.center().x();

    // Subtle fill in the right pane so the glyph clearly reads as two panes.
    QPainterPath clip;
    clip.addRoundedRect(frame, radius, radius);
    p.setClipPath(clip);
    QColor paneFill = fg;
    paneFill.setAlpha(55);
    p.fillRect(QRectF(midX, frame.top(), frame.right() - midX, frame.height()), paneFill);
    p.setClipping(false);

    // Rounded frame + the vertical divider between the two panes.
    p.setPen(QPen(fg, penW));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(frame, radius, radius);
    p.drawLine(QPointF(midX, frame.top()), QPointF(midX, frame.bottom()));
    p.end();

    return QIcon(pm);
}

void MainWindow::cloneCurrentDocument() {
    if (!file) {
        return;
    }
    // Round-trip the active document through a temp .mid to get an independent
    // copy. save() sets the original's saved-flag, so preserve+restore it -
    // cloning must not make unsaved work look saved.
    const QString tmp = QDir::temp().filePath(
        QStringLiteral("midieditor_clone_%1.mid").arg(QDateTime::currentMSecsSinceEpoch()));
    const bool wasSaved = file->saved();
    const bool savedOk = file->save(tmp);
    file->setSaved(wasSaved);
    if (!savedOk) {
        QMessageBox::warning(this, tr("Clone Tab"),
                             tr("Could not duplicate the current document."));
        return;
    }

    bool loadOk = false;
    QStringList log;
    MidiFile *clone = new MidiFile(tmp, &loadOk, &log);
    QFile::remove(tmp);
    if (!loadOk) {
        delete clone;
        QMessageBox::warning(this, tr("Clone Tab"),
                             tr("Could not duplicate the current document."));
        return;
    }
    // A fresh, untitled copy the user saves explicitly under a new name.
    clone->setPath(QString());
    clone->setSaved(false);
    openInNewTab(clone);
}

void MainWindow::rebuildTabBar(QTabBar *bar, DocumentManager *mgr) {
    if (!bar || !mgr) {
        return;
    }
    const bool group1 = (bar == _group1TabBar);
    bool &suppress = group1 ? _suppressGroup1TabSignals : _suppressTabSignals;
    suppress = true;
    while (bar->count() > 0) {
        bar->removeTab(0);
    }
    for (int i = 0; i < mgr->count(); ++i) {
        bar->addTab(mgr->at(i)->title());
    }
    if (mgr->activeIndex() >= 0) {
        bar->setCurrentIndex(mgr->activeIndex());
    }
    suppress = false;
}

void MainWindow::onTabMoveRequested(DocumentTabBar *source, int sourceIndex,
                                    DocumentTabBar *target, int targetIndex) {
    // A drop ends any drag feedback (group highlight + both bars' carets).
    highlightDropGroup(nullptr);
    if (DocumentTabBar *g0 = qobject_cast<DocumentTabBar *>(_documentTabBar)) {
        g0->clearDropIndicator();
    }
    if (DocumentTabBar *g1 = qobject_cast<DocumentTabBar *>(_group1TabBar)) {
        g1->clearDropIndicator();
    }
    DocumentManager *srcMgr = managerForTabBar(source);
    DocumentManager *tgtMgr = managerForTabBar(target);
    if (!srcMgr || !tgtMgr) {
        return;
    }
    Document *srcDoc = srcMgr->at(sourceIndex);
    if (!srcDoc) {
        return;
    }

    // ----- in-bar reorder ------------------------------------------------
    if (source == target) {
        // targetIndex is an insertion GAP (0..count); map it to the move() index
        // (removing the dragged tab shifts items after it left by one). Pure,
        // unit-tested logic - see DocumentManager::gapToMoveIndex.
        const int to = DocumentManager::gapToMoveIndex(sourceIndex, targetIndex, srcMgr->count());
        if (to == sourceIndex) {
            return;
        }
        srcMgr->move(sourceIndex, to);
        rebuildTabBar(source, srcMgr);
        return;
    }

    // ----- move between groups -------------------------------------------
    MidiFile *f = srcDoc->file();
    const QString title = srcDoc->title();

    // The primary group (group 0) must always keep at least one document - the
    // primary editor view needs something to show.
    if (srcMgr == _documentManager && srcMgr->count() <= 1) {
        statusBar()->showMessage(tr("The primary group must keep at least one tab"), 3000);
        return;
    }

    stop();
    srcMgr->removeAt(sourceIndex);            // detaches the file (Document deleted)
    Document *nd = tgtMgr->insert(targetIndex, f, title);
    tgtMgr->setActive(nd);                    // the dropped tab is active in its new group

    rebuildTabBar(source, srcMgr);
    rebuildTabBar(target, tgtMgr);

    // The drop focuses the target group and shows the moved document there.
    if (target == _group1TabBar && _compareMatrixWidget) {
        _activeView = _compareMatrixWidget;
        _compareMatrixWidget->setFile(f);
        _compareFile = f;
        setActiveDocument(f);
    } else { // target is the primary group
        _activeView = mw_matrixWidget;
        activateDocument(f);                  // binds primary view + makes f active
    }

    // Refresh the SOURCE group's view to its new active document - or collapse
    // the secondary group if it just lost its last tab.
    if (srcMgr == _group1Docs) {
        if (_group1Docs->isEmpty()) {
            _compareFile = nullptr;
            tearDownGroup1();
        } else if (_compareMatrixWidget) {
            Document *sa = _group1Docs->active();
            if (sa) {
                _compareMatrixWidget->setFile(sa->file());
                _compareFile = sa->file();
            }
        }
    } else { // source is the primary group: keep its pane on group 0's active
        Document *sa = _documentManager->active();
        if (sa) {
            bindPrimaryView(sa->file());      // view only - the active doc is in the target group
        }
    }
}

void MainWindow::onDocumentTabCloseRequested(int index) {
    if (!_documentManager || !_documentTabBar) {
        return;
    }
    // Keep at least one document open.
    if (_documentManager->count() <= 1) {
        return;
    }
    Document *d = _documentManager->at(index);
    if (!d) {
        return;
    }
    MidiFile *f = d->file();

    // Prompt to save if the tab being closed has unsaved changes. saveBeforeClose
    // acts on the active file, so make the closing tab active first.
    if (f && !f->saved()) {
        if (file != f) {
            _suppressTabSignals = true;
            _documentTabBar->setCurrentIndex(index);
            _suppressTabSignals = false;
            _documentManager->setActiveIndex(index);
            activateDocument(f);
        }
        if (!saveBeforeClose()) {
            return; // user cancelled
        }
    }

    const bool closingActive = (index == _documentManager->activeIndex());

    _documentManager->removeAt(index);
    _suppressTabSignals = true;
    _documentTabBar->removeTab(index);
    _suppressTabSignals = false;

    if (closingActive) {
        Document *na = _documentManager->active();
        if (na) {
            _suppressTabSignals = true;
            _documentTabBar->setCurrentIndex(_documentManager->activeIndex());
            _suppressTabSignals = false;
            stop();
            activateDocument(na->file());
        }
    }

    // The closed file is no longer bound to any panel/active document, so tear
    // it down (delete + forget). Guard against deleting the still-active file.
    if (f && f != file) {
        closeDocumentFile(f);
    }

    // If the secondary group is the focused/visible one, closing a primary-group
    // tab must NOT have stolen the active document: restore group 1's document as
    // active (its view already shows it) while the primary view keeps group 0's.
    if (_group1Docs && !_group1Collapsed && _activeView == _compareMatrixWidget &&
        _compareFile) {
        setActiveDocument(_compareFile);
    }
}

MidiFile *MainWindow::getFile() {
    return file;
}

MatrixWidget *MainWindow::matrixWidget() {
    return mw_matrixWidget;
}

void MainWindow::matrixSizeChanged(int maxScrollTime, int maxScrollLine,
                                   int vX, int vY) {
    // Set scroll bar ranges
    vert->setMaximum(maxScrollLine);
    hori->setMinimum(0);
    hori->setMaximum(maxScrollTime);
    
    // Set scroll bar values - ensure horizontal starts at 0 for new files
    vert->setValue(qMax(0, qMin(vY, maxScrollLine)));
    // For horizontal: clamp small values to 0 to eliminate dead space
    int clampedVX = (vX < 10) ? 0 : vX;
    hori->setValue(qMax(0, qMin(clampedVX, maxScrollTime)));

    // Update the matrix widget
    _matrixWidgetContainer->update();
}

void MainWindow::playStop() {
    if (MidiPlayer::isPlaying()) {
        stop();
    } else {
        play();
    }
}

// Mirror the editor's mute state onto the 3 authentic-SID voices: voice ch
// (= MIDI channel ch) is silenced if the channel is muted OR every track that
// carries notes on that channel is muted/hidden.
static void syncSidVoiceMutes(MidiFile *file) {
    if (!file) return;
    SidAudioPlayer *sid = SidAudioPlayer::instance();
    for (int ch = 0; ch < 3; ++ch) {
        bool silenced = file->channelMuted(ch);
        if (!silenced) {
            bool sawEvent = false, anyAudible = false;
            const QMultiMap<int, MidiEvent *> *map = file->channel(ch)->eventMap();
            if (map) {
                for (MidiEvent *e : map->values()) {
                    MidiTrack *t = e ? e->track() : nullptr;
                    if (!t) continue;
                    sawEvent = true;
                    // Only the track's mute (speaker) silences the SID voice;
                    // hiding a track is purely visual, like channel hide.
                    if (!t->muted()) { anyAudible = true; break; }
                }
            }
            if (sawEvent && !anyAudible) silenced = true;
        }
        sid->setVoiceMuted(ch, silenced);
    }
}

void MainWindow::feedSidVisualizer(int ms) {
    if (!file) return;
    const int tick = file->tick(ms);
    for (int ch = 0; ch < 16; ++ch) {
        int level = 0;
        if (!file->channelMuted(ch)) {
            const QMultiMap<int, MidiEvent *> *map = file->channel(ch)->eventMap();
            if (map) {
                for (auto it = map->begin(); it != map->end(); ++it) {
                    if (it.key() > tick) break; // tick-ordered: nothing later is active yet
                    NoteOnEvent *on = dynamic_cast<NoteOnEvent *>(it.value());
                    if (!on) continue;
                    OffEvent *off = on->offEvent();
                    if (on->midiTime() <= tick && off && off->midiTime() > tick)
                        level = std::max(level, on->velocity());
                }
            }
        }
        MidiOutput::channelActivity[ch].store(level, std::memory_order_relaxed);
    }
}

void MainWindow::play() {
    // Authentic SID playback: when Emulation mode is armed (C64 button) and a
    // .sid is loaded, the normal Play button plays the ORIGINAL tune through
    // libsidplayfp from the cursor position; the matrix cursor follows along.
    {
        SidAudioPlayer *sid = SidAudioPlayer::instance();
        if (file && sid->isArmed() && sid->hasSource() && !MidiInput::recording()
            && !MidiPlayer::isPlaying() && !sid->isPlaying()) {
            const int fromMs = file->msOfTick(file->cursorTick());
            connect(sid, SIGNAL(positionChanged(int)),
                    _matrixWidgetContainer, SLOT(timeMsChanged(int)),
                    Qt::UniqueConnection);
            // Drive the MIDI Visualizer from the SID position (no MIDI is sent
            // to the output in Emulation mode, so its bars would stay empty).
            connect(sid, &SidAudioPlayer::positionChanged,
                    this, &MainWindow::feedSidVisualizer, Qt::UniqueConnection);
            // Same for the retro time display - it's normally fed by the MIDI
            // player thread, which doesn't run in Emulation mode.
            if (_timeDisplay)
                connect(sid, &SidAudioPlayer::positionChanged,
                        _timeDisplay, &TimeDisplayWidget::onPlaybackPositionChanged,
                        Qt::UniqueConnection);
            // Stop at the end of the note roll (real playback, not an endless loop).
            connect(sid, SIGNAL(finished()), this, SLOT(stop()), Qt::UniqueConnection);
            // Mirror the editor's channel + track mutes onto the 3 SID voices.
            syncSidVoiceMutes(file);
            // maxTime() (ms) is the right unit here — lengthMs is the auto-stop
            // position in milliseconds. Surface a failure instead of a silent
            // dead transport (BUG-C64-002).
            if (!sid->play(fromMs, file->maxTime())) {
                statusBar()->showMessage(
                    tr("Could not start SID playback — check the audio output device."), 5000);
            }
            return;
        }
    }
    if (!MidiOutput::isConnected()) {
        CompleteMidiSetupDialog *d = new CompleteMidiSetupDialog(this, false, true);
        d->setModal(true);
        d->exec();
        delete d;
        return;
    }
    if (file && !MidiInput::recording() && !MidiPlayer::isPlaying()) {
        // Update playback cursor position using the appropriate widget type
        if (OpenGLMatrixWidget *openglMatrix = qobject_cast<OpenGLMatrixWidget*>(_matrixWidgetContainer)) {
            openglMatrix->timeMsChanged(file->msOfTick(file->cursorTick()), true);
        } else if (MatrixWidget *matrixWidget = qobject_cast<MatrixWidget*>(_matrixWidgetContainer)) {
            matrixWidget->timeMsChanged(file->msOfTick(file->cursorTick()), true);
        }

        // UX-PLAY-001: keep tracks/channels/event/protocol panels live during playback
        // by default so the user can toggle visibility while listening. Opt-out via
        // System & Performance settings ("Lock side panels during playback").
        const bool lockPanelsDuringPlayback =
            _settings->value("playback/lock_panels", false).toBool();
        if (lockPanelsDuringPlayback) {
            _miscWidgetContainer->setEnabled(false);
            channelWidget->setEnabled(false);
            protocolWidget->setEnabled(false);
            _trackWidget->setEnabled(false);
            eventWidget()->setEnabled(false);
        }
        // PERFORMANCE: Don't disable matrix widget during playback to allow mouse wheel scrolling
        // The widget itself handles playback state checks for editing operations
        // _matrixWidgetContainer->setEnabled(false);

        MidiPlayer::play(file);
        connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), this, SLOT(stop()));
#ifdef MIDIEDITOR_COLLAB_ENABLED
        // Show-mode follow-the-host: presenter's Play click → broadcast
        // so viewers can play along in their own editor. Guarded
        // against re-entry from a remote-trigger application path.
        if (!_applyingRemotePlayback) {
            LanLiveSession::instance()->broadcastPlayback(
                QStringLiteral("start"), file->cursorTick());
        }
#endif

        // Connect playback cursor updates for all platforms (not just Windows)
        // This is essential for the playback cursor to move during playback
        // Disconnect first to prevent accumulating connections across play/stop cycles
        disconnect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _matrixWidgetContainer, SLOT(timeMsChanged(int)));
        connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _matrixWidgetContainer, SLOT(timeMsChanged(int)));

        // Connect lyric timeline playback cursor
        disconnect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _lyricTimeline, SLOT(onPlaybackPositionChanged(int)));
        connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _lyricTimeline, SLOT(onPlaybackPositionChanged(int)));

        // 1.6.1 (UX-VOICE-LANE-001): keep the FFXIV Voices lane's playback
        // cursor in sync with the matrix widget. Re-establish on every
        // play() because PlayerThread is recreated on Windows.
        if (_voiceLaneWidget) {
            disconnect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _voiceLaneWidget, SLOT(onPlaybackPositionChanged(int)));
            connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _voiceLaneWidget, SLOT(onPlaybackPositionChanged(int)));
            disconnect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _voiceLaneWidget, SLOT(update()));
            connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _voiceLaneWidget, SLOT(update()));
        }

        // Connect lyric visualizer playback signals
        // On Windows, PlayerThread is destroyed/recreated each play(), breaking toolbar-time connections
        if (_lyricVisualizer) {
            disconnect(MidiPlayer::playerThread(), SIGNAL(playerStarted()), _lyricVisualizer, SLOT(playbackStarted()));
            connect(MidiPlayer::playerThread(), SIGNAL(playerStarted()), _lyricVisualizer, SLOT(playbackStarted()));
            disconnect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _lyricVisualizer, SLOT(playbackStopped()));
            connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _lyricVisualizer, SLOT(playbackStopped()));
            disconnect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _lyricVisualizer, SLOT(onPlaybackPositionChanged(int)));
            connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _lyricVisualizer, SLOT(onPlaybackPositionChanged(int)));
        }

        // Phase 41: cursor-time display follows playback. Re-establish on
        // every play() because PlayerThread is recreated on Windows.
        if (_timeDisplay) {
            disconnect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _timeDisplay, SLOT(onPlaybackPositionChanged(int)));
            connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _timeDisplay, SLOT(onPlaybackPositionChanged(int)));
            disconnect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _timeDisplay, SLOT(onPlaybackStopped()));
            connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _timeDisplay, SLOT(onPlaybackStopped()));
        }
    }
}

void MainWindow::record() {
    if (!MidiOutput::isConnected() || !MidiInput::isConnected()) {
        CompleteMidiSetupDialog *d = new CompleteMidiSetupDialog(this, !MidiInput::isConnected(), !MidiOutput::isConnected());
        d->setModal(true);
        d->exec();
        delete d;
        return;
    }

    if (!file) {
        newFile();
    }

    if (!MidiInput::recording() && !MidiPlayer::isPlaying()) {
        // play current file
        if (file) {
            if (file->pauseTick() >= 0) {
                file->setCursorTick(file->pauseTick());
                file->setPauseTick(-1);
            }

            // Update playback cursor position using the appropriate widget type
            if (OpenGLMatrixWidget *openglMatrix = qobject_cast<OpenGLMatrixWidget*>(_matrixWidgetContainer)) {
                openglMatrix->timeMsChanged(file->msOfTick(file->cursorTick()), true);
            } else if (MatrixWidget *matrixWidget = qobject_cast<MatrixWidget*>(_matrixWidgetContainer)) {
                matrixWidget->timeMsChanged(file->msOfTick(file->cursorTick()), true);
            }

            // UX-PLAY-001: see play() â€” same opt-out toggle gates panel locking here.
            const bool lockPanelsDuringPlayback =
                _settings->value("playback/lock_panels", false).toBool();
            if (lockPanelsDuringPlayback) {
                _miscWidgetContainer->setEnabled(false);
                channelWidget->setEnabled(false);
                protocolWidget->setEnabled(false);
                _trackWidget->setEnabled(false);
                eventWidget()->setEnabled(false);
            }
            // PERFORMANCE: Don't disable matrix widget during playback to allow mouse wheel scrolling
            // The widget itself handles playback state checks for editing operations
            // _matrixWidgetContainer->setEnabled(false);
            MidiPlayer::play(file);
            MidiInput::startInput();
            connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), this, SLOT(stop()));
            // Connect playback cursor updates for all platforms (not just Windows)
            // This is essential for the playback cursor to move during playback
            // Disconnect first to prevent accumulating connections across play/stop cycles
            disconnect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _matrixWidgetContainer, SLOT(timeMsChanged(int)));
            connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _matrixWidgetContainer, SLOT(timeMsChanged(int)));

            // Connect lyric timeline playback cursor (record mode)
            disconnect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _lyricTimeline, SLOT(onPlaybackPositionChanged(int)));
            connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _lyricTimeline, SLOT(onPlaybackPositionChanged(int)));

            // 1.6.1 (UX-VOICE-LANE-001): same FFXIV voice-lane reconnect for record mode.
            if (_voiceLaneWidget) {
                disconnect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _voiceLaneWidget, SLOT(onPlaybackPositionChanged(int)));
                connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _voiceLaneWidget, SLOT(onPlaybackPositionChanged(int)));
                disconnect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _voiceLaneWidget, SLOT(update()));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _voiceLaneWidget, SLOT(update()));
            }

            // Connect lyric visualizer playback signals (record mode)
            if (_lyricVisualizer) {
                disconnect(MidiPlayer::playerThread(), SIGNAL(playerStarted()), _lyricVisualizer, SLOT(playbackStarted()));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStarted()), _lyricVisualizer, SLOT(playbackStarted()));
                disconnect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _lyricVisualizer, SLOT(playbackStopped()));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _lyricVisualizer, SLOT(playbackStopped()));
                disconnect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _lyricVisualizer, SLOT(onPlaybackPositionChanged(int)));
                connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _lyricVisualizer, SLOT(onPlaybackPositionChanged(int)));
            }

            // Phase 41: cursor-time display follows playback (record mode).
            if (_timeDisplay) {
                disconnect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _timeDisplay, SLOT(onPlaybackPositionChanged(int)));
                connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _timeDisplay, SLOT(onPlaybackPositionChanged(int)));
                disconnect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _timeDisplay, SLOT(onPlaybackStopped()));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _timeDisplay, SLOT(onPlaybackStopped()));
            }
        }
    }
}

void MainWindow::pause() {
    if (file) {
        // Authentic SID: remember the position so the next Play resumes there.
        SidAudioPlayer *sid = SidAudioPlayer::instance();
        if (sid->isPlaying()) {
            file->setCursorTick(file->tick(sid->positionMs()));
            sid->stop();
            return;
        }
        if (MidiPlayer::isPlaying()) {
            file->setPauseTick(file->tick(MidiPlayer::timeMs()));
            stop(false, false, false);
        }
    }
}

void MainWindow::stop(bool autoConfirmRecord, bool addEvents, bool resetPause) {
    if (!file) {
        return;
    }

    // Authentic SID playback (Emulation mode): stop it too. The rest of this
    // function is a no-op when the MIDI player isn't running.
    SidAudioPlayer::instance()->stop();
    // Clear the visualizer's channel activity (the SID feed has stopped; bars
    // decay from here). Harmless for the MIDI path (already cleared on stop).
    MidiOutput::resetChannelActivity();
    // Snap the retro time display back to the cursor (the SID position feed
    // emits no "stopped" of its own; idempotent for the MIDI path).
    if (_timeDisplay)
        _timeDisplay->onPlaybackStopped();

    disconnect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), this, SLOT(stop()));
#ifdef MIDIEDITOR_COLLAB_ENABLED
    // Show-mode follow-the-host: presenter's Stop → broadcast. Same
    // re-entry guard as the play() path.
    if (!_applyingRemotePlayback && MidiPlayer::isPlaying()) {
        LanLiveSession::instance()->broadcastPlayback(
            QStringLiteral("stop"), file->cursorTick());
    }
#endif

    if (resetPause) {
        file->setPauseTick(-1);
        _matrixWidgetContainer->update();
    }
    if (!MidiInput::recording() && MidiPlayer::isPlaying()) {
        MidiPlayer::stop();
        _miscWidgetContainer->setEnabled(true);
        channelWidget->setEnabled(true);
        _trackWidget->setEnabled(true);
        protocolWidget->setEnabled(true);
        // Matrix widget was never disabled, so no need to re-enable
        // _matrixWidgetContainer->setEnabled(true);
        eventWidget()->setEnabled(true);
        // Update playback cursor position using the appropriate widget type
        if (OpenGLMatrixWidget *openglMatrix = qobject_cast<OpenGLMatrixWidget*>(_matrixWidgetContainer)) {
            openglMatrix->timeMsChanged(MidiPlayer::timeMs(), true);
        } else if (MatrixWidget *matrixWidget = qobject_cast<MatrixWidget*>(_matrixWidgetContainer)) {
            matrixWidget->timeMsChanged(MidiPlayer::timeMs(), true);
        }
        // Reset lyric timeline playback position
        _lyricTimeline->onPlaybackPositionChanged(-1);
        _trackWidget->setEnabled(true);
        panic();
    }

    MidiTrack *track = file->track(NewNoteTool::editTrack());
    if (!track) {
        return;
    }

    if (MidiInput::recording()) {
        MidiPlayer::stop();
        panic();
        _miscWidgetContainer->setEnabled(true);
        channelWidget->setEnabled(true);
        protocolWidget->setEnabled(true);
        _matrixWidgetContainer->setEnabled(true);
        _trackWidget->setEnabled(true);
        eventWidget()->setEnabled(true);
        QMultiMap<int, MidiEvent *> events = MidiInput::endInput(track);

        if (events.isEmpty() && !autoConfirmRecord) {
            QMessageBox::information(this, tr("Information"), tr("No events recorded."));
        } else {
            RecordDialog *dialog = new RecordDialog(file, events, _settings, this);
            dialog->setModal(true);
            if (!autoConfirmRecord) {
                dialog->show();
            } else {
                if (addEvents) {
                    dialog->enter();
                }
            }
        }
    }
}

void MainWindow::forward() {
    if (!file)
        return;

    QList<TimeSignatureEvent *> *eventlist = nullptr;
    int ticksleft;
    int oldTick = file->cursorTick();
    if (file->pauseTick() >= 0) {
        oldTick = file->pauseTick();
    }
    if (MidiPlayer::isPlaying() && !MidiInput::recording()) {
        oldTick = file->tick(MidiPlayer::timeMs());
        stop(true);
    }
    file->measure(oldTick, oldTick, &eventlist, &ticksleft);

    int newTick = oldTick - ticksleft + eventlist->last()->ticksPerMeasure();
    delete eventlist;
    file->setPauseTick(-1);
    if (newTick <= file->endTick()) {
        file->setCursorTick(newTick);
        // Update playback cursor position using the appropriate widget type
        if (OpenGLMatrixWidget *openglMatrix = qobject_cast<OpenGLMatrixWidget*>(_matrixWidgetContainer)) {
            openglMatrix->timeMsChanged(file->msOfTick(newTick), true);
        } else if (MatrixWidget *matrixWidget = qobject_cast<MatrixWidget*>(_matrixWidgetContainer)) {
            matrixWidget->timeMsChanged(file->msOfTick(newTick), true);
        }
    }
    _matrixWidgetContainer->update();
}

void MainWindow::back() {
    if (!file)
        return;

    QList<TimeSignatureEvent *> *eventlist = nullptr;
    int ticksleft;
    int oldTick = file->cursorTick();
    if (file->pauseTick() >= 0) {
        oldTick = file->pauseTick();
    }
    if (MidiPlayer::isPlaying() && !MidiInput::recording()) {
        oldTick = file->tick(MidiPlayer::timeMs());
        stop(true);
    }
    file->measure(oldTick, oldTick, &eventlist, &ticksleft);
    int newTick = oldTick;
    if (ticksleft > 0) {
        newTick -= ticksleft;
    } else {
        newTick -= eventlist->last()->ticksPerMeasure();
    }
    file->measure(newTick, newTick, &eventlist, &ticksleft);
    if (ticksleft > 0) {
        newTick -= ticksleft;
    }
    delete eventlist;
    file->setPauseTick(-1);
    if (newTick >= 0) {
        file->setCursorTick(newTick);
        // Update playback cursor position using the appropriate widget type
        if (OpenGLMatrixWidget *openglMatrix = qobject_cast<OpenGLMatrixWidget*>(_matrixWidgetContainer)) {
            openglMatrix->timeMsChanged(file->msOfTick(newTick), true);
        } else if (MatrixWidget *matrixWidget = qobject_cast<MatrixWidget*>(_matrixWidgetContainer)) {
            matrixWidget->timeMsChanged(file->msOfTick(newTick), true);
        }
    }
    _matrixWidgetContainer->update();
}

void MainWindow::backToBegin() {
    if (!file)
        return;

    file->setPauseTick(0);
    file->setCursorTick(0);

    _matrixWidgetContainer->update();
}

void MainWindow::forwardMarker() {
    if (!file)
        return;

    int oldTick = file->cursorTick();
    if (file->pauseTick() >= 0) {
        oldTick = file->pauseTick();
    }
    if (MidiPlayer::isPlaying() && !MidiInput::recording()) {
        oldTick = file->tick(MidiPlayer::timeMs());
        stop(true);
    }

    int newTick = -1;

    foreach(MidiEvent* event, file->channel(16)->eventMap()->values()) {
        int eventTick = event->midiTime();
        if (eventTick <= oldTick) continue;
        TextEvent *textEvent = dynamic_cast<TextEvent *>(event);

        if (textEvent && textEvent->type() == TextEvent::MARKER) {
            newTick = eventTick;
            break;
        }
    }

    if (newTick < 0) return;
    file->setPauseTick(newTick);
    file->setCursorTick(newTick);
    // Update playback cursor position using the appropriate widget type
    if (OpenGLMatrixWidget *openglMatrix = qobject_cast<OpenGLMatrixWidget*>(_matrixWidgetContainer)) {
        openglMatrix->timeMsChanged(file->msOfTick(newTick), true);
    } else if (MatrixWidget *matrixWidget = qobject_cast<MatrixWidget*>(_matrixWidgetContainer)) {
        matrixWidget->timeMsChanged(file->msOfTick(newTick), true);
    }
    _matrixWidgetContainer->update();
}

void MainWindow::backMarker() {
    if (!file)
        return;

    int oldTick = file->cursorTick();
    if (file->pauseTick() >= 0) {
        oldTick = file->pauseTick();
    }
    if (MidiPlayer::isPlaying() && !MidiInput::recording()) {
        oldTick = file->tick(MidiPlayer::timeMs());
        stop(true);
    }

    int newTick = 0;
    QList<MidiEvent *> events = file->channel(16)->eventMap()->values();

    for (int eventNumber = events.size() - 1; eventNumber >= 0; eventNumber--) {
        MidiEvent *event = events.at(eventNumber);
        int eventTick = event->midiTime();
        if (eventTick >= oldTick) continue;
        TextEvent *textEvent = dynamic_cast<TextEvent *>(event);

        if (textEvent && textEvent->type() == TextEvent::MARKER) {
            newTick = eventTick;
            break;
        }
    }

    file->setPauseTick(newTick);
    file->setCursorTick(newTick);
    // Update playback cursor position using the appropriate widget type
    if (OpenGLMatrixWidget *openglMatrix = qobject_cast<OpenGLMatrixWidget*>(_matrixWidgetContainer)) {
        openglMatrix->timeMsChanged(file->msOfTick(newTick), true);
    } else if (MatrixWidget *matrixWidget = qobject_cast<MatrixWidget*>(_matrixWidgetContainer)) {
        matrixWidget->timeMsChanged(file->msOfTick(newTick), true);
    }
    _matrixWidgetContainer->update();
}

void MainWindow::save() {
    if (!file)
        return;

    // Import-only formats can't be written back: MidiFile::save() emits Standard
    // MIDI bytes, which would overwrite the original (a .sid / .gp5 / .musicxml /
    // …) with garbage under its native extension - and the .sid would no longer
    // re-import. Redirect to Save As (forces .mid); the original stays intact.
    // (Classifier shared with the collab copy path + unit tests - see
    // ImportOnlyFormats.h.)
    if (ImportFormats::isImportOnly(file->path())) {
        QMessageBox::information(
            this, tr("Save as MIDI"),
            tr("This file was imported from a format that can't be saved back in "
               "place (SID / Guitar Pro / MusicXML / MuseScore).\n\n"
               "It will be saved as a Standard MIDI file (.mid). Your original "
               "file stays untouched."));
        saveas();
        return;
    }

    if (QFile(file->path()).exists()) {
        bool printMuteWarning = false;

        for (int i = 0; i < 16; i++) {
            MidiChannel *ch = file->channel(i);
            if (ch->mute()) {
                printMuteWarning = true;
            }
        }
        foreach(MidiTrack* track, *(file->tracks())) {
            if (track->muted()) {
                printMuteWarning = true;
            }
        }

        if (printMuteWarning) {
            QMessageBox::information(this, tr("Channels/Tracks mute"), tr("One or more channels/tracks are not audible. They will be audible in the saved file."), QMessageBox::Ok);
        }

        if (!file->save(file->path())) {
            QMessageBox::warning(this, tr("Error"), QString(tr("The file could not be saved. Please make sure that the destination directory exists and that you have the correct access rights to write into this directory.")));
        } else {
            setWindowModified(false);
            cleanupAutoSave();
#ifdef MIDIEDITOR_COLLAB_ENABLED
            CollabService::instance()->onFileSaved(file, file->path());
#endif
        }
    } else {
        saveas();
    }
}

bool MainWindow::saveas() {
    if (!file)
        return false;

    QString oldPath = file->path();
    QFileInfo oldInfo(oldPath);
    QString dir = startDirectory;
    if (oldInfo.exists()) {
        dir = oldInfo.dir().path();
    }
    // Pre-fill the dialog with the original song name + .mid so the user doesn't
    // have to retype it. Matters most when save() routed an import-only file
    // (SID / Guitar Pro / MusicXML / MuseScore) here: we already know its name,
    // so suggest "<original>.mid" instead of an empty field. Untitled files (no
    // path) keep the plain directory as the start location.
    QString startLocation = dir;
    if (!oldInfo.completeBaseName().isEmpty()) {
        startLocation = QDir(dir).filePath(oldInfo.completeBaseName() + QStringLiteral(".mid"));
    }
    QString newPath = QFileDialog::getSaveFileName(this, tr("Save file as..."), startLocation);

    if (newPath == "") {
        return false; // user cancelled the dialog
    }

    // automatically add '.mid' extension
    if (!newPath.endsWith(".mid", Qt::CaseInsensitive) && !newPath.endsWith(".midi", Qt::CaseInsensitive)) {
        newPath.append(".mid");
    }

    if (file->save(newPath)) {
        bool printMuteWarning = false;

        for (int i = 0; i < 16; i++) {
            MidiChannel *ch = file->channel(i);
            if (ch->mute() || !ch->visible()) {
                printMuteWarning = true;
            }
        }
        foreach(MidiTrack* track, *(file->tracks())) {
            if (track->muted() || track->hidden()) {
                printMuteWarning = true;
            }
        }

        if (printMuteWarning) {
            QMessageBox::information(this, tr("Channels/Tracks mute"), tr("One or more channels/tracks are not audible. They will be audible in the saved file."), QMessageBox::Ok);
        }

        file->setPath(newPath);
        setWindowTitle(QApplication::applicationName() + " v" + QApplication::applicationVersion() + " - " + file->path() + "[*]");
        updateRecentPathsList();
        setWindowModified(false);
        cleanupAutoSave();
#ifdef MIDIEDITOR_COLLAB_ENABLED
        CollabService::instance()->onFileSaved(file, newPath);
#endif
        return true;
    } else {
        QMessageBox::warning(this, tr("Error"), QString(tr("The file could not be saved. Please make sure that the destination directory exists and that you have the correct access rights to write into this directory.")));
        return false;
    }
}

void MainWindow::load() {
    // Phase 28: Open loads the chosen file into a new tab; the current document
    // stays open, so no save-prompt here (it lives on tab close). oldPath is
    // still used to seed the dialog's starting directory.
    QString oldPath = startDirectory;
    if (file) {
        oldPath = file->path();
    }

    QFile f(oldPath);
    QString dir = startDirectory;
    if (f.exists()) {
        dir = QFileInfo(f).dir().path();
    }
    QString midi = "*.mid *.midi";
#ifdef GP678_SUPPORT
    QString gp   = "*.gtp *.gp3 *.gp4 *.gp5 *.gp6 *.gp7 *.gp8 *.gpx *.gp";
#else
    QString gp   = "*.gtp *.gp3 *.gp4 *.gp5";
#endif
    QString mml  = "*.mml *.3mle";
    QString xml  = "*.musicxml *.xml *.mxl";
    QString msc  = "*.mscz *.mscx";
    QString sid  = "*.sid";
    QString filter = QString(
        "Music Files (%1 %2 %3 %4 %5 %6);;"
        "MIDI Files (%1);;"
        "Guitar Pro Files (%2);;"
        "MML Files (%3);;"
        "MusicXML Files (%4);;"
        "MuseScore Files (%5);;"
        "C64 SID Files (%6);;"
        "All Files (*)")
        .arg(midi, gp, mml, xml, msc).arg(sid);

    QString newPath = QFileDialog::getOpenFileName(this, tr("Open file"), dir, filter);

    if (!newPath.isEmpty()) {
        openFile(newPath);
    }
}

void MainWindow::loadFile(QString nfile) {
    // Phase 28: loads into a new tab (see load()); no save-prompt for the
    // current document here.
    if (!nfile.isEmpty()) {
        openFile(nfile);
    }
}

void MainWindow::openFile(QString filePath) {
    bool ok = true;

    QFile nf(filePath);

    if (!nf.exists()) {
        QMessageBox::warning(this, tr("Error"), QString(tr("The file [") + filePath + tr("]does not exist!")));
        return;
    }

    startDirectory = QFileInfo(nf).absoluteDir().path() + "/";

    // Check for auto-save recovery sidecar
    QString autoPath = filePath + ".autosave";
    bool useAutoSave = false;
    if (QFile::exists(autoPath)) {
        QFileInfo autoInfo(autoPath);
        QFileInfo origInfo(filePath);
        if (autoInfo.lastModified() > origInfo.lastModified()) {
            auto result = QMessageBox::question(this, tr("Auto-Save Recovery"),
                tr("A more recent auto-saved backup was found for:\n%1\n\n"
                   "Backup from: %2\nOriginal from: %3\n\n"
                   "Would you like to recover the backup?")
                    .arg(filePath)
                    .arg(QLocale().toString(autoInfo.lastModified(), QLocale::ShortFormat))
                    .arg(QLocale().toString(origInfo.lastModified(), QLocale::ShortFormat)),
                QMessageBox::Yes | QMessageBox::No);
            useAutoSave = (result == QMessageBox::Yes);
            if (!useAutoSave) {
                QFile::remove(autoPath);
            }
        } else {
            QFile::remove(autoPath);  // Stale auto-save, clean up
        }
    }

    MidiFile *mf = nullptr;
    QString lowerPath = filePath.toLower();

    // Detect Guitar Pro formats
    if (lowerPath.endsWith(".gtp") || lowerPath.endsWith(".gp3") ||
        lowerPath.endsWith(".gp4") || lowerPath.endsWith(".gp5") ||
        lowerPath.endsWith(".gp6") || lowerPath.endsWith(".gp7") ||
        lowerPath.endsWith(".gp8") || lowerPath.endsWith(".gpx") ||
        lowerPath.endsWith(".gp")) {
        mf = GpImporter::loadFile(filePath, &ok);
    } else if (lowerPath.endsWith(".mml") || lowerPath.endsWith(".3mle")) {
        mf = MmlImporter::loadFile(filePath, &ok);
    } else if (lowerPath.endsWith(".musicxml") || lowerPath.endsWith(".xml") ||
               lowerPath.endsWith(".mxl")) {
        mf = MusicXmlImporter::loadFile(filePath, &ok);
    } else if (lowerPath.endsWith(".mscz") || lowerPath.endsWith(".mscx")) {
        mf = MsczImporter::loadFile(filePath, &ok);
    } else if (lowerPath.endsWith(".sid")) {
        // RSID tunes are transcribed via the cycle-accurate libsidplayfp engine
        // (~1-2 s); show a busy indicator so the pause reads as "rendering",
        // not a freeze. PSID is fast (and may pop its own length dialog), so
        // skip the indicator there.
        const bool slowRender = SidImporter::isInterruptPlayer(filePath);
        QScopedPointer<QProgressDialog> sidProgress;
        std::function<void(int, int)> onProgress;
        if (slowRender) {
            sidProgress.reset(new QProgressDialog(
                tr("Rendering SID via the libsidplayfp engine…"),
                QString() /* no cancel button */, 0, 100, this));
            sidProgress->setWindowTitle(tr("Importing SID"));
            sidProgress->setWindowModality(Qt::ApplicationModal);
            sidProgress->setMinimumDuration(0);
            sidProgress->setValue(0);
            QProgressDialog *dlg = sidProgress.data();
            onProgress = [dlg](int done, int total) {
                if (total > 0) dlg->setValue(qBound(0, done * 100 / total, 100));
                QApplication::processEvents(); // keep the bar animating
            };
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QApplication::processEvents(); // paint the dialog before rendering
        }
        mf = SidImporter::loadFile(filePath, &ok, this, onProgress);
        if (slowRender)
            QApplication::restoreOverrideCursor();
    } else {
        mf = new MidiFile(useAutoSave ? autoPath : filePath, &ok);
    }

    if (ok && mf) {
        stop();
        if (useAutoSave) {
            mf->setPath(filePath);   // Point to original file path
            mf->setSaved(false);     // Mark as dirty â€” user should save explicitly
        }
        openInNewTab(mf);            // Phase 28: open the loaded file in a new tab
        if (useAutoSave) {
            setWindowModified(true);
            statusBar()->showMessage(tr("Recovered from auto-save backup"), 5000);
        }
        updateRecentPathsList();
        // Remember the original .sid for authentic libsidplayfp playback (C64
        // button in Emulation mode); clear it when any other file is loaded.
        SidAudioPlayer::instance()->setSource(
            lowerPath.endsWith(".sid") ? filePath : QString());
        // If no special mode is active, make sure a C64/FFXIV SoundFont left over
        // from a previous file doesn't keep playing the newly-loaded song (e.g.
        // a Guitar Pro file opened after a SID) - fall back to GM, else GS.
        C64SoundFontHelper::normalizeDefaultSoundFont(this);
    } else {
        QString detail;
        if (lowerPath.endsWith(".musicxml") || lowerPath.endsWith(".xml") ||
            lowerPath.endsWith(".mxl")) {
            detail = tr("The MusicXML file could not be imported. "
                        "It may be malformed, empty, or use unsupported features.");
        } else if (lowerPath.endsWith(".mscz") || lowerPath.endsWith(".mscx")) {
            detail = tr("The MuseScore file could not be imported. "
                        "It may be malformed, password-protected, or use an "
                        "unsupported version.");
        } else if (lowerPath.endsWith(".gtp") || lowerPath.endsWith(".gp3") ||
                   lowerPath.endsWith(".gp4") || lowerPath.endsWith(".gp5") ||
                   lowerPath.endsWith(".gp6") || lowerPath.endsWith(".gp7") ||
                   lowerPath.endsWith(".gp8") || lowerPath.endsWith(".gpx") ||
                   lowerPath.endsWith(".gp")) {
            detail = tr("The Guitar Pro file could not be imported. "
                        "It may be damaged or use an unsupported version.");
        } else if (lowerPath.endsWith(".mml") || lowerPath.endsWith(".3mle")) {
            detail = tr("The MML file could not be imported. "
                        "It may contain syntax errors.");
        } else if (lowerPath.endsWith(".sid")) {
            detail = tr("The SID tune could not be imported. It may use a "
                        "player the converter can't run yet (e.g. an RSID "
                        "that installs its own IRQ with no play address).");
        } else {
            detail = tr("The file is damaged and cannot be opened.");
        }
        QMessageBox::warning(this, tr("Error"), detail);
    }
}

void MainWindow::redo() {
    if (file)
        file->protocol()->redo(true);
    updateTrackMenu();
}

void MainWindow::undo() {
    if (file)
        file->protocol()->undo(true);
    updateTrackMenu();
}

EventWidget *MainWindow::eventWidget() {
    return _eventWidget;
}

void MainWindow::muteAllChannels() {
    if (!file)
        return;
    file->protocol()->startNewAction(tr("Mute all channels"));
    for (int i = 0; i < 19; i++) {
        file->channel(i)->setMute(true);
    }
    file->protocol()->endAction();
    channelWidget->update();
}

void MainWindow::unmuteAllChannels() {
    if (!file)
        return;
    file->protocol()->startNewAction(tr("All channels audible"));
    for (int i = 0; i < 19; i++) {
        file->channel(i)->setMute(false);
    }
    file->protocol()->endAction();
    channelWidget->update();
}

void MainWindow::allChannelsVisible() {
    if (!file)
        return;
    file->protocol()->startNewAction(tr("All channels visible"));

    // Use global visibility manager to avoid corrupted MidiChannel access
    for (int i = 0; i < 19; i++) {
        ChannelVisibilityManager::instance().setChannelVisible(i, true);

        // Also try to update MidiChannel object (with safety)
        try {
            file->channel(i)->setVisible(true);
        } catch (...) {
            // Ignore if MidiChannel is corrupted
        }
    }

    file->protocol()->endAction();
    channelWidget->update();
}

void MainWindow::allChannelsInvisible() {
    if (!file)
        return;
    file->protocol()->startNewAction(tr("Hide all channels"));

    // Use global visibility manager to avoid corrupted MidiChannel access
    for (int i = 0; i < 19; i++) {
        ChannelVisibilityManager::instance().setChannelVisible(i, false);

        // Also try to update MidiChannel object (with safety)
        try {
            file->channel(i)->setVisible(false);
        } catch (...) {
            // Ignore if MidiChannel is corrupted
        }
    }

    file->protocol()->endAction();
    channelWidget->update();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    bool shouldClose = false;

    // Auto-update in progress: skip save dialogs, just close
    if (_forceCloseForUpdate) {
        shouldClose = true;
        event->accept();
    } else if (!file || file->saved()) {
        shouldClose = true;
        event->accept();
    } else {
        bool sbc = saveBeforeClose();

        if (sbc) {
            shouldClose = true;
            event->accept();
        } else {
            event->ignore();
        }
    }

    // Only perform early cleanup if we're actually closing
    if (shouldClose) {
        cleanupAutoSave();
        performEarlyCleanup();
    }

    if (MidiOutput::outputPort() != "") {
        _settings->setValue("out_port", MidiOutput::outputPort());
    }
    if (MidiInput::inputPort() != "") {
        _settings->setValue("in_port", MidiInput::inputPort());
    }

    bool ok;
    int numStart = _settings->value("numStart_v3.5", -1).toInt(&ok);
    _settings->setValue("numStart_v3.5", numStart + 1);

    // save the current Path
    _settings->setValue("open_path", startDirectory);
    _settings->setValue("alt_stop", MidiOutput::isAlternativePlayer);
    _settings->setValue("ticks_per_quarter", MidiFile::defaultTimePerQuarter);

    // Only save matrix widget settings if the widget is still valid
    if (mw_matrixWidget) {
        _settings->setValue("screen_locked", mw_matrixWidget->screenLocked());
        _settings->setValue("div", mw_matrixWidget->div());
        _settings->setValue("colors_from_channel", mw_matrixWidget->colorsByChannel());
    }

    _settings->setValue("magnet", EventTool::magnetEnabled());
    _settings->setValue("metronome", Metronome::enabled());
    _settings->setValue("metronome_loudness", Metronome::loudness());
    _settings->setValue("thru", MidiInput::thru());
    _settings->setValue("quantization", _quantizationGrid);

#ifdef FLUIDSYNTH_SUPPORT
    FluidSynthEngine::instance()->saveSettings(_settings);
#endif

    Appearance::writeSettings(_settings);
    _settings->sync();

    // Cleanup shared clipboard resources
    SharedClipboard::instance()->cleanup();

    // Launch pending auto-update if scheduled
    if (shouldClose && _autoUpdater && _autoUpdater->hasPendingUpdate()) {
        QString midiPath = file ? file->path() : QString();
        _autoUpdater->launchPendingUpdate(midiPath);
    }
}

void MainWindow::about() {
    AboutDialog *d = new AboutDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setModal(true);

    // Ensure the about dialog inherits the current application style and palette
    d->setPalette(QApplication::palette());

    // Apply the same style as the application
    QApplication *app = qobject_cast<QApplication *>(QApplication::instance());
    if (app) {
        QString appStyleSheet = app->styleSheet();
        if (!appStyleSheet.isEmpty()) {
            d->setStyleSheet(appStyleSheet);
        }
    }

    // Force style refresh on the dialog
    QTimer::singleShot(0, [d]() {
        if (d) {
            d->style()->polish(d);
            d->update();
        }
    });

    d->show();
}

void MainWindow::setFileLengthMs() {
    if (!file)
        return;

    FileLengthDialog *d = new FileLengthDialog(file, this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setModal(true);
    d->show();
}

void MainWindow::setStartDir(QString dir) {
    startDirectory = dir;
}

bool MainWindow::saveBeforeClose() {
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Save file?"));
    msgBox.setText(tr("Save file ") + file->path() + tr(" before closing?"));
    msgBox.addButton(tr("Save"), QMessageBox::AcceptRole);
    msgBox.addButton(tr("Close without saving"), QMessageBox::DestructiveRole);
    msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
    msgBox.setDefaultButton(qobject_cast<QPushButton *>(msgBox.buttons().at(2))); // Cancel is default

    int result = msgBox.exec();

    // Map button roles to actions
    QAbstractButton *clickedButton = msgBox.clickedButton();
    QMessageBox::ButtonRole role = msgBox.buttonRole(clickedButton);

    switch (role) {
        case QMessageBox::AcceptRole:
            // Save; only proceed with the close when the save actually succeeds.
            // A failed write or a cancelled "Save As" must NOT discard the
            // document (this is what makes a Cancel in the file dialog safe).
            if (QFile(file->path()).exists())
                return file->save(file->path());
            return saveas();

        case QMessageBox::RejectRole:
            // cancel - break
            return false;

        default: // DestructiveRole - close without saving
            return true;
    }
}

void MainWindow::newFile() {
    // Phase 28: New opens a fresh document in its own tab; the current document
    // stays open in its tab, so there is nothing to save-prompt about here
    // (that prompt now lives on tab close).

    // Stop playback before switching the active file to prevent use-after-free
    // (PlayerThread runs on a separate thread and accesses the MidiFile)
    stop();

    // create new File
    MidiFile *f = new MidiFile();

    openInNewTab(f);

    editTrack(1);
    setWindowTitle(QApplication::applicationName() + " v" + QApplication::applicationVersion() + tr(" - Untitled Document[*]"));
}

void MainWindow::panic() {
    MidiPlayer::panic();
}

void MainWindow::screenLockPressed(bool enable) {
    mw_matrixWidget->setScreenLocked(enable);
}

void MainWindow::scaleSelection() {
    bool ok;
    double scale = QInputDialog::getDouble(this, tr("Scalefactor"), tr("Scalefactor:"), 1.0, 0, 2147483647, 17, &ok);
    if (ok && scale > 0 && Selection::instance()->selectedEvents().size() > 0 && file) {
        // find minimum
        int minTime = 2147483647;
        foreach(MidiEvent* e, Selection::instance()->selectedEvents()) {
            if (e->midiTime() < minTime) {
                minTime = e->midiTime();
            }
        }

        file->protocol()->startNewAction(tr("Scale events"), 0);
        foreach(MidiEvent* e, Selection::instance()->selectedEvents()) {
            e->setMidiTime((e->midiTime() - minTime) * scale + minTime);
            OnEvent *on = dynamic_cast<OnEvent *>(e);
            if (on) {
                MidiEvent *off = on->offEvent();
                off->setMidiTime((off->midiTime() - minTime) * scale + minTime);
            }
        }
        file->protocol()->endAction();
    }
}

void MainWindow::alignLeft() {
    if (Selection::instance()->selectedEvents().size() > 1 && file) {
        // find minimum
        int minTime = 2147483647;
        foreach(MidiEvent* e, Selection::instance()->selectedEvents()) {
            if (e->midiTime() < minTime) {
                minTime = e->midiTime();
            }
        }

        file->protocol()->startNewAction(tr("Align left"), new QImage(":/run_environment/graphics/tool/align_left.png"));
        foreach(MidiEvent* e, Selection::instance()->selectedEvents()) {
            int onTime = e->midiTime();
            e->setMidiTime(minTime);
            OnEvent *on = dynamic_cast<OnEvent *>(e);
            if (on) {
                MidiEvent *off = on->offEvent();
                off->setMidiTime(minTime + (off->midiTime() - onTime));
            }
        }
        file->protocol()->endAction();
    }
}

void MainWindow::alignRight() {
    if (Selection::instance()->selectedEvents().size() > 1 && file) {
        // find maximum
        int maxTime = 0;
        foreach(MidiEvent* e, Selection::instance()->selectedEvents()) {
            OnEvent *on = dynamic_cast<OnEvent *>(e);
            if (on) {
                MidiEvent *off = on->offEvent();
                if (off->midiTime() > maxTime) {
                    maxTime = off->midiTime();
                }
            }
        }

        file->protocol()->startNewAction(tr("Align right"), new QImage(":/run_environment/graphics/tool/align_right.png"));
        foreach(MidiEvent* e, Selection::instance()->selectedEvents()) {
            int onTime = e->midiTime();
            OnEvent *on = dynamic_cast<OnEvent *>(e);
            if (on) {
                MidiEvent *off = on->offEvent();
                e->setMidiTime(maxTime - (off->midiTime() - onTime));
                off->setMidiTime(maxTime);
            }
        }
        file->protocol()->endAction();
    }
}

void MainWindow::equalize() {
    if (Selection::instance()->selectedEvents().size() > 1 && file) {
        // find average
        int avgStart = 0;
        int avgTime = 0;
        int count = 0;
        foreach(MidiEvent* e, Selection::instance()->selectedEvents()) {
            OnEvent *on = dynamic_cast<OnEvent *>(e);
            if (on) {
                MidiEvent *off = on->offEvent();
                avgStart += e->midiTime();
                avgTime += (off->midiTime() - e->midiTime());
                count++;
            }
        }
        if (count > 1) {
            avgStart /= count;
            avgTime /= count;

            file->protocol()->startNewAction(tr("Equalize"), new QImage(":/run_environment/graphics/tool/equalize.png"));
            foreach(MidiEvent* e, Selection::instance()->selectedEvents()) {
                OnEvent *on = dynamic_cast<OnEvent *>(e);
                if (on) {
                    MidiEvent *off = on->offEvent();
                    e->setMidiTime(avgStart);
                    off->setMidiTime(avgStart + avgTime);
                }
            }
            file->protocol()->endAction();
        }
    }
}

void MainWindow::glueSelection() {
    if (!file) {
        return;
    }

    // Ensure the current file is set for the tool
    Tool::setFile(file);

    // Create a temporary GlueTool instance to perform the operation
    // Respect channels (only merge notes within the same channel)
    GlueTool glueTool;
    glueTool.performGlueOperation(true); // true = respect channels
    updateAll();
}

void MainWindow::glueSelectionAllChannels() {
    if (!file) {
        return;
    }

    // Ensure the current file is set for the tool
    Tool::setFile(file);

    // Create a temporary GlueTool instance to perform the operation
    // Don't respect channels (merge notes across all channels on the same track)
    GlueTool glueTool;
    glueTool.performGlueOperation(false); // false = ignore channels
    updateAll();
}

void MainWindow::strumNotes() {
    if (!file) {
        return;
    }

    StrummerDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        Tool::setFile(file);

        StrummerTool strummerTool;
        strummerTool.performStrum(
            dialog.getStartStrength(),
            dialog.getStartTension(),
            dialog.getEndStrength(),
            dialog.getEndTension(),
            dialog.getVelocityStrength(),
            dialog.getVelocityTension(),
            dialog.getPreserveEnd(),
            dialog.getAlternateDirection(),
            dialog.getUseStepStrength(),
            dialog.getIgnoreTrack()
        );
        updateAll();
    }
}

void MainWindow::fixFFXIVChannels() {
    if (!file) {
        QMessageBox::warning(this, tr("Fix X|V Channels"), tr("No file loaded."));
        return;
    }

    // Analyze file and show tier selection dialog
    QJsonObject analysis = FFXIVChannelFixer::analyzeFile(file);

    if (!analysis["valid"].toBool()) {
        QMessageBox::warning(this, tr("Fix X|V Channels"),
            tr("No FFXIV instrument names detected. "
               "Track names must match FFXIV instruments "
               "(e.g. Piano, Flute, ElectricGuitarOverdriven, Snare Drum, etc.)."));
        return;
    }

    FFXIVFixerDialog dialog(analysis, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    int tier = dialog.selectedTier();
    if (tier == 0)
        return;

    file->protocol()->startNewAction(tr("Fix X|V Channels"));

    // Show progress dialog during the fix operation
    QProgressDialog progressDlg(tr("Fixing channels..."), QString(), 0, 100, this);
    progressDlg.setWindowTitle(tr("Fix X|V Channels"));
    progressDlg.setWindowModality(Qt::WindowModal);
    progressDlg.setMinimumDuration(0);
    progressDlg.setValue(0);
    QApplication::processEvents();

    QJsonObject result = FFXIVChannelFixer::fixChannels(file, tier,
        [&](int pct, const QString &msg) {
            progressDlg.setLabelText(msg);
            progressDlg.setValue(pct);
            QApplication::processEvents();
        });

    progressDlg.close();
    file->protocol()->endAction();

    if (result["success"].toBool()) {
        updateAll();

        // Build rich HTML result summary
        QString mode = result["tierDescription"].toString();
        int trackCount = result["trackCount"].toInt();
        int removedPCs = result["removedProgramChanges"].toInt();
        int switchPCs  = result["guitarSwitchProgramChanges"].toInt();
        int velNorm    = result["velocityNormalized"].toInt();
        QJsonArray channelMap = result["channelMap"].toArray();
        QJsonArray renames    = result["trackRenames"].toArray();

        QString html = QStringLiteral(
            "<h3 style='color:#2e7d32; margin-bottom:4px;'>&#x2705; Success</h3>"
            "<p style='margin-top:0;'><b>Mode:</b> %1<br>"
            "<b>Tracks processed:</b> %2</p>").arg(mode).arg(trackCount);

        // Channel mapping table
        html += QStringLiteral(
            "<table cellpadding='3' cellspacing='0' style='border-collapse:collapse; font-size:11px; margin-bottom:8px;'>"
            "<tr style='background:#e0e0e0;'>"
            "<th align='left' style='padding:3px 8px;'>Track</th>"
            "<th align='left' style='padding:3px 8px;'>Instrument</th>"
            "<th align='center' style='padding:3px 8px;'>Channel</th>"
            "<th align='center' style='padding:3px 8px;'>Program</th></tr>");
        for (const auto &val : channelMap) {
            QJsonObject entry = val.toObject();
            html += QString(
                "<tr style='border-bottom:1px solid #ddd;'>"
                "<td style='padding:2px 8px;'>T%1</td>"
                "<td style='padding:2px 8px;'>%2</td>"
                "<td align='center' style='padding:2px 8px;'>CH%3</td>"
                "<td align='center' style='padding:2px 8px;'>%4</td></tr>")
                .arg(entry["track"].toInt())
                .arg(entry["name"].toString())
                .arg(entry["channel"].toInt())
                .arg(entry["program"].toInt());
        }
        html += QStringLiteral("</table>");

        // Changes summary
        html += QStringLiteral("<p style='font-size:11px; margin:4px 0;'>");
        html += QString("&#x1F5D1; Removed <b>%1</b> old program change(s)").arg(removedPCs);
        if (switchPCs > 0)
            html += QString("<br>&#x1F3B8; Inserted <b>%1</b> mid-song guitar switch(es)").arg(switchPCs);
        if (velNorm > 0)
            html += QString("<br>&#x1F50A; Normalized <b>%1</b> note velocity(ies) to 127").arg(velNorm);
        if (!renames.isEmpty()) {
            html += QString("<br>&#x1F4DD; Renamed <b>%1</b> track(s):").arg(renames.size());
            for (const auto &r : renames) {
                QJsonObject re = r.toObject();
                html += QString("<br>&nbsp;&nbsp;&nbsp;%1 &rarr; %2")
                    .arg(re["oldName"].toString())
                    .arg(re["newName"].toString());
            }
        }
        html += QStringLiteral("</p>");
        html += QStringLiteral("<p style='font-size:10px; color:gray; margin-top:8px;'>Press Ctrl+Z to undo all changes.</p>");

        QMessageBox infoBox(this);
        infoBox.setWindowTitle(tr("Fix X|V Channels â€” Result"));
        infoBox.setTextFormat(Qt::RichText);
        infoBox.setText(html);
        infoBox.setIcon(QMessageBox::Information);
        infoBox.exec();
    } else {
        QMessageBox::warning(this, tr("Fix X|V Channels"),
                             result["error"].toString());
    }
}

void MainWindow::openFfxivEqualizer() {
#ifdef FLUIDSYNTH_SUPPORT
    // Modal — the dialog is live-preview but reverts on Cancel, so a
    // user can twiddle while playing back without leaving stale state.
    FfxivEqualizerDialog dlg(this);
    dlg.exec();
#else
    QMessageBox::information(this, tr("FFXIV SoundFont Equalizer"),
        tr("FluidSynth support is not compiled in."));
#endif
}

// 1.6.1 (UX-VOICE-LANE-002): central visibility rule for the FFXIV Voice
// Lane. The lane is visible if EITHER the user explicitly turned it on in
// the View menu (View/showVoiceLane = "always show"), OR auto-follow is
// enabled (View/voiceLaneAutoFollowFfxiv, default on) AND FFXIV SoundFont
// Mode is currently active. This lets the lane appear/hide together with
// the FFXIV toolbar toggle without forcing users who want it permanently
// visible to give that up.
void MainWindow::updateVoiceLaneVisibility() {
    if (!_voiceLaneArea) return;

    QSettings s("MidiEditor", "NONE");
    bool alwaysShow = s.value("View/showVoiceLane", false).toBool();
    bool autoFollow = s.value("View/voiceLaneAutoFollowFfxiv", true).toBool();

    bool ffxivOn = false;
#ifdef FLUIDSYNTH_SUPPORT
    if (FluidSynthEngine *engine = FluidSynthEngine::instance()) {
        ffxivOn = engine->ffxivSoundFontMode();
    }
#endif

    const bool shouldShow = alwaysShow || (autoFollow && ffxivOn);
    if (_voiceLaneArea->isVisible() != shouldShow) {
        _voiceLaneArea->setVisible(shouldShow);
    }
}

void MainWindow::deleteOverlaps() {
    if (!file) {
        return;
    }

    // Show the delete overlaps dialog
    DeleteOverlapsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        // Ensure the current file is set for the tool
        Tool::setFile(file);

        // Get user selections from dialog
        DeleteOverlapsTool::OverlapMode mode = dialog.getSelectedMode();
        bool respectChannels = dialog.getRespectChannels();
        bool respectTracks = dialog.getRespectTracks();

        // Create a temporary DeleteOverlapsTool instance to perform the operation
        DeleteOverlapsTool deleteOverlapsTool;
        deleteOverlapsTool.performDeleteOverlapsOperation(mode, respectChannels, respectTracks);
        updateAll();
    }
}

void MainWindow::resetView() {
    if (!file) {
        return;
    }

    // Call the matrix widget's reset view function using the appropriate widget type
    if (OpenGLMatrixWidget *openglMatrix = qobject_cast<OpenGLMatrixWidget*>(_matrixWidgetContainer)) {
        openglMatrix->resetView();
    } else if (MatrixWidget *matrixWidget = qobject_cast<MatrixWidget*>(_matrixWidgetContainer)) {
        matrixWidget->resetView();
    }
}

void MainWindow::deleteSelectedEvents() {
    bool showsSelected = false;
    if (Tool::currentTool()) {
        EventTool *eventTool = dynamic_cast<EventTool *>(Tool::currentTool());
        if (eventTool) {
            showsSelected = eventTool->showsSelection();
        }
    }
    if (showsSelected && Selection::instance()->selectedEvents().size() > 0 && file) {
        file->protocol()->startNewAction(tr("Remove event(s)"));

        // Group events by channel to minimize protocol entries
        QMap<int, QList<MidiEvent *> > eventsByChannel;
        foreach(MidiEvent* ev, Selection::instance()->selectedEvents()) {
            eventsByChannel[ev->channel()].append(ev);
        }

        // Remove events channel by channel to create fewer protocol entries
        foreach(int channelNum, eventsByChannel.keys()) {
            MidiChannel *channel = file->channel(channelNum);
            ProtocolEntry *toCopy = channel->copy();

            // Remove all events from this channel at once
            foreach(MidiEvent* ev, eventsByChannel[channelNum]) {
                // 1.6.1 (upstream 21fe86b crash-fix slice): refuse to delete
                // the very last TempoChange (CH17) or TimeSignature (CH18)
                // event if it lives at tick 0 and is the only event on its
                // channel. MidiFile invariants assume at least one tempo /
                // time-signature anchor at tick 0; removing it crashed
                // playback and saving in upstream issue reports.
                if (channelNum == 17 || channelNum == 18) {
                    if (ev->midiTime() == 0
                        && channel->eventMap()->count(0) <= 1) {
                        continue;
                    }
                }

                // Handle track name events
                if (channelNum == 16 && (MidiEvent *) (ev->track()->nameEvent()) == ev) {
                    ev->track()->setNameEvent(0);
                }

                // Remove the event and its off event if it exists
                channel->eventMap()->remove(ev->midiTime(), ev);
                OnEvent *on = dynamic_cast<OnEvent *>(ev);
                if (on && on->offEvent()) {
                    channel->eventMap()->remove(on->offEvent()->midiTime(), on->offEvent());
                }
            }

            // Create single protocol entry for this channel
            channel->protocol(toCopy, channel);
        }

        Selection::instance()->clearSelection();
        eventWidget()->reportSelectionChangedByTool();
        file->protocol()->endAction();
    }
}

void MainWindow::deleteChannel(QAction *action) {
    if (!file) {
        return;
    }

    int num = action->data().toInt();
    file->protocol()->startNewAction(tr("Remove all events from channel ") + QString::number(num));
    foreach(MidiEvent* event, file->channel(num)->eventMap()->values()) {
        if (Selection::instance()->selectedEvents().contains(event)) {
            EventTool::deselectEvent(event);
        }
    }
    Selection::instance()->setSelection(Selection::instance()->selectedEvents());

    file->channel(num)->deleteAllEvents();
    file->protocol()->endAction();
}

void MainWindow::moveSelectedEventsToChannel(QAction *action) {
    if (!file) {
        return;
    }

    int num = action->data().toInt();
    MidiChannel *channel = file->channel(num);

    if (Selection::instance()->selectedEvents().size() > 0) {
        file->protocol()->startNewAction(tr("Move selected events to channel ") + QString::number(num));
        foreach(MidiEvent* ev, Selection::instance()->selectedEvents()) {
            file->channel(ev->channel())->removeEvent(ev);
            ev->setChannel(num, true);
            OnEvent *onevent = dynamic_cast<OnEvent *>(ev);
            if (onevent) {
                channel->insertEvent(onevent->offEvent(), onevent->offEvent()->midiTime());
                onevent->offEvent()->setChannel(num);
            }
            channel->insertEvent(ev, ev->midiTime());
        }

        file->protocol()->endAction();
    }
}

void MainWindow::moveSelectedEventsToTrack(QAction *action) {
    if (!file) {
        return;
    }

    int num = action->data().toInt();
    MidiTrack *track = file->track(num);

    if (Selection::instance()->selectedEvents().size() > 0) {
        file->protocol()->startNewAction(tr("Move selected events to track ") + QString::number(num));
        foreach(MidiEvent* ev, Selection::instance()->selectedEvents()) {
            ev->setTrack(track, true);
            OnEvent *onevent = dynamic_cast<OnEvent *>(ev);
            if (onevent) {
                onevent->offEvent()->setTrack(track);
            }
        }

        file->protocol()->endAction();
    }
}

// --------------------------------------------------------------------------
// Phase 36 -- Copy to Track / Copy to Channel slots.
// Thin wrappers that hand the chosen target index to EventTool, which
// owns the duplication + Protocol bookkeeping.
// --------------------------------------------------------------------------
void MainWindow::copySelectedEventsToChannel(QAction *action) {
    if (!file || !action) return;
    const int num = action->data().toInt();
    EventTool::copySelectionToChannel(num);
}

void MainWindow::copySelectedEventsToTrack(QAction *action) {
    if (!file || !action) return;
    const int num = action->data().toInt();
    MidiTrack *track = file->track(num);
    if (!track) return;
    EventTool::copySelectionToTrack(track);
}

void MainWindow::updateRecentPathsList() {
    // if file opened put it at the top of the list
    if (file) {
        QString currentPath = file->path();
        QStringList newList;
        newList.append(currentPath);

        foreach(QString str, _recentFilePaths) {
            if (str != currentPath && newList.size() < 10) {
                newList.append(str);
            }
        }

        _recentFilePaths = newList;
    }

    // save list
    QVariant list(_recentFilePaths);
    _settings->setValue("recent_file_list", list);

    // update menu
    _recentPathsMenu->clear();
    foreach(QString path, _recentFilePaths) {
        QFile f(path);
        QString name = QFileInfo(f).fileName();

        QVariant variant(path);
        QAction *openRecentFileAction = new QAction(name, this);
        openRecentFileAction->setData(variant);
        _recentPathsMenu->addAction(openRecentFileAction);
    }
}

void MainWindow::openRecent(QAction *action) {
    QString path = action->data().toString();

    if (file) {
        if (!file->saved()) {
            if (!saveBeforeClose()) {
                return;
            }
        }
    }

    openFile(path);
}

void MainWindow::updateChannelMenu() {
    // delete channel events menu
    foreach(QAction* action, _deleteChannelMenu->actions()) {
        int channel = action->data().toInt();
        if (file) {
            action->setText(QString::number(channel) + " " + MidiFile::instrumentName(file->channel(channel)->progAtTick(0)));
        }
    }

    // move events to channel...
    foreach(QAction* action, _moveSelectedEventsToChannelMenu->actions()) {
        int channel = action->data().toInt();
        if (file) {
            action->setText(QString::number(channel) + " " + MidiFile::instrumentName(file->channel(channel)->progAtTick(0)));
        }
    }

    // Phase 36 -- copy events to channel... mirror Move-to formatting
    // and add eye icons that mark hidden channels (same convention as
    // the matrix right-click menu).
    if (_copySelectedEventsToChannelMenu) {
        QIcon visIcon(":/run_environment/graphics/tool/all_visible.png");
        QIcon hidIcon(":/run_environment/graphics/tool/all_invisible.png");
        foreach(QAction* action, _copySelectedEventsToChannelMenu->actions()) {
            int channel = action->data().toInt();
            if (!file) continue;
            QString label = QString::number(channel) + ": "
                + MidiFile::instrumentName(file->channel(channel)->progAtTick(0));
            if (channel == 9 && !label.contains('('))
                label += " (Drums)";
            action->setText(label);
            action->setIcon(file->channel(channel)->visible() ? visIcon : hidIcon);
        }
    }

    // paste events to channel...
    foreach(QAction* action, _pasteToChannelMenu->actions()) {
        int channel = action->data().toInt();
        if (file && channel >= 0) {
            action->setText(QString::number(channel) + " " + MidiFile::instrumentName(file->channel(channel)->progAtTick(0)));
        }
    }

    // select all events from channel...
    foreach(QAction* action, _selectAllFromChannelMenu->actions()) {
        int channel = action->data().toInt();
        if (file) {
            action->setText(QString::number(channel) + " " + MidiFile::instrumentName(file->channel(channel)->progAtTick(0)));
        }
    }

    _chooseEditChannel->setCurrentIndex(NewNoteTool::editChannel());
}

void MainWindow::updateTrackMenu() {
    _moveSelectedEventsToTrackMenu->clear();
    if (_copySelectedEventsToTrackMenu) {
        _copySelectedEventsToTrackMenu->clear();
    }
    _chooseEditTrack->clear();
    _selectAllFromTrackMenu->clear();

    if (!file) {
        return;
    }

    for (int i = 0; i < file->numTracks(); i++) {
        QVariant variant(i);
        // PHASE36-016: parent QAction to the menu, not to MainWindow,
        // so that QMenu::clear() at the top of updateTrackMenu() actually
        // deletes the previous batch (otherwise we leak numTracks
        // QActions per refresh).
        QAction *moveToTrackAction = new QAction(QString::number(i) + " " + file->tracks()->at(i)->name(), _moveSelectedEventsToTrackMenu);
        moveToTrackAction->setData(variant);

        QString formattedKeySequence = QString("Shift+%1").arg(i);
        moveToTrackAction->setShortcut(QKeySequence::fromString(formattedKeySequence));

        _moveSelectedEventsToTrackMenu->addAction(moveToTrackAction);

        if (_copySelectedEventsToTrackMenu) {
            QIcon visIcon(":/run_environment/graphics/tool/all_visible.png");
            QIcon hidIcon(":/run_environment/graphics/tool/all_invisible.png");
            MidiTrack *trk = file->tracks()->at(i);
            QAction *copyToTrackAction = new QAction(
                QString::number(i) + ": " + trk->name(), _copySelectedEventsToTrackMenu);
            copyToTrackAction->setData(variant);
            copyToTrackAction->setIcon(trk->hidden() ? hidIcon : visIcon);
            _copySelectedEventsToTrackMenu->addAction(copyToTrackAction);
        }
    }

    for (int i = 0; i < file->numTracks(); i++) {
        QVariant variant(i);
        QAction *select = new QAction(QString::number(i) + " " + file->tracks()->at(i)->name(), this);
        select->setData(variant);
        _selectAllFromTrackMenu->addAction(select);
    }

    for (int i = 0; i < file->numTracks(); i++) {
        _chooseEditTrack->addItem(tr("Track ") + QString::number(i) + ": " + file->tracks()->at(i)->name());
    }
    if (NewNoteTool::editTrack() >= file->numTracks()) {
        NewNoteTool::setEditTrack(0);
    }
    _chooseEditTrack->setCurrentIndex(NewNoteTool::editTrack());

    _pasteToTrackMenu->clear();
    QActionGroup *pasteTrackGroup = new QActionGroup(this);
    pasteTrackGroup->setExclusive(true);

    bool checked = false;
    for (int i = -2; i < file->numTracks(); i++) {
        QVariant variant(i);
        QString text = QString::number(i);
        if (i == -2) {
            text = tr("Same as selected for new events");
        } else if (i == -1) {
            text = tr("Keep track");
        } else {
            text = tr("Track ") + QString::number(i) + ": " + file->tracks()->at(i)->name();
        }
        QAction *pasteToTrackAction = new QAction(text, this);
        pasteToTrackAction->setData(variant);
        pasteToTrackAction->setCheckable(true);
        _pasteToTrackMenu->addAction(pasteToTrackAction);
        pasteTrackGroup->addAction(pasteToTrackAction);
        if (i == EventTool::pasteTrack()) {
            pasteToTrackAction->setChecked(true);
            checked = true;
        }
    }
    if (!checked) {
        _pasteToTrackMenu->actions().first()->setChecked(true);
        EventTool::setPasteTrack(0);
    }
}

void MainWindow::muteChannel(QAction *action) {
    int channel = action->data().toInt();
    if (file) {
        file->protocol()->startNewAction(tr("Mute channel"));
        file->channel(channel)->setMute(action->isChecked());
        updateChannelMenu();
        channelWidget->update();
        file->protocol()->endAction();
    }
}

void MainWindow::soloChannel(QAction *action) {
    int channel = action->data().toInt();
    if (file) {
        file->protocol()->startNewAction(tr("Select solo channel"));
        for (int i = 0; i < 16; i++) {
            file->channel(i)->setSolo(i == channel && action->isChecked());
        }
        file->protocol()->endAction();
    }
    channelWidget->update();
    updateChannelMenu();
}

void MainWindow::viewChannel(QAction *action) {
    int channel = action->data().toInt();
    if (file) {
        file->protocol()->startNewAction(tr("Channel visibility changed"));
        file->channel(channel)->setVisible(action->isChecked());
        updateChannelMenu();
        channelWidget->update();
        file->protocol()->endAction();
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    // First, let Qt handle any shortcuts
    QMainWindow::keyPressEvent(event);

    // If the event wasn't accepted by a shortcut, forward it to the matrix widget
    if (!event->isAccepted()) {
        if (OpenGLMatrixWidget *openglMatrix = qobject_cast<OpenGLMatrixWidget*>(_matrixWidgetContainer)) {
            openglMatrix->takeKeyPressEvent(event);
        } else if (MatrixWidget *matrixWidget = qobject_cast<MatrixWidget*>(_matrixWidgetContainer)) {
            matrixWidget->takeKeyPressEvent(event);
        }
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *event) {
    // First, let Qt handle any shortcuts
    QMainWindow::keyReleaseEvent(event);

    // If the event wasn't accepted by a shortcut, forward it to the matrix widget
    if (!event->isAccepted()) {
        if (OpenGLMatrixWidget *openglMatrix = qobject_cast<OpenGLMatrixWidget*>(_matrixWidgetContainer)) {
            openglMatrix->takeKeyReleaseEvent(event);
        } else if (MatrixWidget *matrixWidget = qobject_cast<MatrixWidget*>(_matrixWidgetContainer)) {
            matrixWidget->takeKeyReleaseEvent(event);
        }
    }
}

void MainWindow::showEventWidget(bool show) {
    if (show) {
        lowerTabWidget->setCurrentIndex(1);
    } else {
        lowerTabWidget->setCurrentIndex(0);
    }
}

void MainWindow::renameTrackMenuClicked(QAction *action) {
    int track = action->data().toInt();
    renameTrack(track);
}

void MainWindow::renameTrack(int tracknumber) {
    if (!file) {
        return;
    }

    file->protocol()->startNewAction(tr("Edit Track Name"));

    bool ok;
    QString text = QInputDialog::getText(this, tr("Set Track Name"), tr("Track name (Track ") + QString::number(tracknumber) + tr(")"), QLineEdit::Normal, file->tracks()->at(tracknumber)->name(), &ok);
    if (ok && !text.isEmpty()) {
        file->tracks()->at(tracknumber)->setName(text);
    }

    file->protocol()->endAction();
    updateTrackMenu();
}

void MainWindow::removeTrackMenuClicked(QAction *action) {
    int track = action->data().toInt();
    removeTrack(track);
}

void MainWindow::removeTrack(int tracknumber) {
    if (!file) {
        return;
    }
    MidiTrack *track = file->track(tracknumber);
    file->protocol()->startNewAction(tr("Remove track"));
    foreach(MidiEvent* event, Selection::instance()->selectedEvents()) {
        if (event->track() == track) {
            EventTool::deselectEvent(event);
        }
    }
    Selection::instance()->setSelection(Selection::instance()->selectedEvents());
    if (!file->removeTrack(track)) {
        QMessageBox::warning(this, tr("Error"), QString(tr("The selected track can\'t be removed!\n It\'s the last track of the file.")));
    }
    file->protocol()->endAction();
    updateTrackMenu();
}

void MainWindow::addTrack() {
    if (file) {
        bool ok;
        QString text = QInputDialog::getText(this, tr("Set Track Name"), tr("Track name (New Track)"), QLineEdit::Normal, tr("New Track"), &ok);
        if (ok && !text.isEmpty()) {
            file->protocol()->startNewAction("Add track");
            file->addTrack();
            file->tracks()->at(file->numTracks() - 1)->setName(text);
            file->protocol()->endAction();

            updateTrackMenu();
        }
    }
}

void MainWindow::muteAllTracks() {
    if (!file)
        return;
    file->protocol()->startNewAction(tr("Mute all tracks"));
    foreach(MidiTrack* track, *(file->tracks())) {
        track->setMuted(true);
    }
    file->protocol()->endAction();
    _trackWidget->update();
}

void MainWindow::unmuteAllTracks() {
    if (!file)
        return;
    file->protocol()->startNewAction(tr("All tracks audible"));
    foreach(MidiTrack* track, *(file->tracks())) {
        track->setMuted(false);
    }
    file->protocol()->endAction();
    _trackWidget->update();
}

void MainWindow::allTracksVisible() {
    if (!file)
        return;
    file->protocol()->startNewAction(tr("Show all tracks"));
    foreach(MidiTrack* track, *(file->tracks())) {
        track->setHidden(false);
    }
    file->protocol()->endAction();
    _trackWidget->update();
}

void MainWindow::allTracksInvisible() {
    if (!file)
        return;
    file->protocol()->startNewAction(tr("Hide all tracks"));
    foreach(MidiTrack* track, *(file->tracks())) {
        track->setHidden(true);
    }
    file->protocol()->endAction();
    _trackWidget->update();
}

void MainWindow::showTrackMenuClicked(QAction *action) {
    int track = action->data().toInt();
    if (file) {
        file->protocol()->startNewAction(tr("Show track"));
        file->track(track)->setHidden(!(action->isChecked()));
        updateTrackMenu();
        _trackWidget->update();
        file->protocol()->endAction();
    }
}

void MainWindow::muteTrackMenuClicked(QAction *action) {
    int track = action->data().toInt();
    if (file) {
        file->protocol()->startNewAction(tr("Mute track"));
        file->track(track)->setMuted(action->isChecked());
        updateTrackMenu();
        _trackWidget->update();
        file->protocol()->endAction();
    }
}

void MainWindow::selectAllFromChannel(QAction *action) {
    if (!file) {
        return;
    }
    int channel = action->data().toInt();
    file->protocol()->startNewAction("Select all events from channel " + QString::number(channel));
    EventTool::clearSelection();
    file->channel(channel)->setVisible(true);

    // Collect events for batch selection
    QList<MidiEvent *> eventsToSelect;
    foreach(MidiEvent* e, file->channel(channel)->eventMap()->values()) {
        if (e->track()->hidden()) {
            e->track()->setHidden(false);
        }

        // Skip OffEvents
        OffEvent *offevent = dynamic_cast<OffEvent *>(e);
        if (!offevent) {
            eventsToSelect.append(e);
        }
    }

    // Batch select all events
    EventTool::batchSelectEvents(eventsToSelect);

    file->protocol()->endAction();
}

void MainWindow::selectAllFromTrack(QAction *action) {
    if (!file) {
        return;
    }

    int track = action->data().toInt();
    file->protocol()->startNewAction("Select all events from track " + QString::number(track));
    EventTool::clearSelection();
    file->track(track)->setHidden(false);

    // Collect events for batch selection
    QList<MidiEvent *> eventsToSelect;
    for (int channel = 0; channel < 16; channel++) {
        foreach(MidiEvent* e, file->channel(channel)->eventMap()->values()) {
            if (e->track()->number() == track) {
                file->channel(e->channel())->setVisible(true);

                // Skip OffEvents
                OffEvent *offevent = dynamic_cast<OffEvent *>(e);
                if (!offevent) {
                    eventsToSelect.append(e);
                }
            }
        }
    }

    // Batch select all events
    EventTool::batchSelectEvents(eventsToSelect);
    file->protocol()->endAction();
}

void MainWindow::selectAll() {
    if (!file) {
        return;
    }

    file->protocol()->startNewAction("Select all");

    // Collect all valid events in a single pass for better performance
    QList<MidiEvent *> eventsToSelect;

    // Estimate total events across all channels for better memory allocation
    int estimatedEvents = 0;
    for (int i = 0; i < 16; i++) {
        if (ChannelVisibilityManager::instance().isChannelVisible(i)) {
            estimatedEvents += file->channel(i)->eventMap()->size();
        }
    }
    eventsToSelect.reserve(estimatedEvents); // Reserve based on actual event count

    for (int i = 0; i < 16; i++) {
        // Only process visible channels using the global visibility manager
        if (!ChannelVisibilityManager::instance().isChannelVisible(i)) {
            continue;
        }

        const QMultiMap<int, MidiEvent *> *eventMap = file->channel(i)->eventMap();
        foreach(MidiEvent* event, eventMap->values()) {
            // Pre-filter events to avoid redundant checks later
            if (event->track()->hidden()) {
                continue;
            }

            // Skip OffEvents as they're handled by EventTool::selectEvent anyway
            OffEvent *offevent = dynamic_cast<OffEvent *>(event);
            if (offevent) {
                continue;
            }

            eventsToSelect.append(event);
        }
    }

    // Batch select all events at once to minimize UI updates
    EventTool::batchSelectEvents(eventsToSelect);

    file->protocol()->endAction();
}

void MainWindow::convertPitchBendToNotes() {
    if (!file) {
        return;
    }

    // Collect selected note-on events
    QList<NoteOnEvent *> notes;
    foreach (MidiEvent *event, Selection::instance()->selectedEvents()) {
        NoteOnEvent *on = dynamic_cast<NoteOnEvent *>(event);
        if (on && on->offEvent()) {
            notes.append(on);
        }
    }

    if (notes.isEmpty()) {
        return;
    }

    // Prompt user for pitch bend range
    bool ok;
    QStringList items;
    items << tr("Ã‚Â±2 semitones (General MIDI default)")
          << tr("Ã‚Â±12 semitones (Guitar/Bass VSTs)")
          << tr("Ã‚Â±24 semitones (Extreme pitch modulation)")
          << tr("Custom...");
    
    QString item = QInputDialog::getItem(this, 
                                         tr("Pitch Bend Range"),
                                         tr("Select pitch bend sensitivity range:"),
                                         items, 0, false, &ok);
    
    if (!ok) {
        return; // User cancelled
    }
    
    double bendRangeSemis = 2.0; // default
    
    if (item == items[0]) {
        bendRangeSemis = 2.0;
    } else if (item == items[1]) {
        bendRangeSemis = 12.0;
    } else if (item == items[2]) {
        bendRangeSemis = 24.0;
    } else { // Custom
        bendRangeSemis = QInputDialog::getDouble(this,
                                                  tr("Custom Pitch Bend Range"),
                                                  tr("Enter pitch bend range in semitones (Ã‚Â±):"),
                                                  2.0, 1.0, 96.0, 1, &ok);
        if (!ok) {
            return; // User cancelled
        }
    }

    file->protocol()->startNewAction(tr("Convert pitch bends to notes"));

    foreach (NoteOnEvent *on, notes) {
        int ch = on->channel();
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;

        int t0 = on->midiTime();
        int t1 = on->offEvent()->midiTime();
        if (t1 <= t0) continue;

        // Gather pitch bend events on this channel within [t0, t1)
        QMultiMap<int, MidiEvent *> *emap = channel->eventMap();

        // Find last pitch bend value at or before t0
        int startValue = 8192; // neutral
        if (emap && !emap->isEmpty()) {
            QMultiMap<int, MidiEvent *>::const_iterator it = emap->upperBound(t0);
            if (it != emap->begin()) {
                do {
                    --it;
                    PitchBendEvent *pb = dynamic_cast<PitchBendEvent *>(it.value());
                    if (pb) {
                        startValue = pb->value();
                        break;
                    }
                } while (it != emap->begin());
            }
        }

        // Collect change points (segment boundaries)
        QList<int> boundaries;
        boundaries.append(t0);
        QMap<int, int> bendAtTime; // time -> value

        if (emap && !emap->isEmpty()) {
            QMultiMap<int, MidiEvent *>::const_iterator it2 = emap->lowerBound(t0);
            while (it2 != emap->end() && it2.key() < t1) {
                PitchBendEvent *pb = dynamic_cast<PitchBendEvent *>(it2.value());
                if (pb) {
                    int tick = it2.key();
                    // Avoid duplicate consecutive boundaries at same tick
                    if (boundaries.isEmpty() || boundaries.last() != tick) {
                        boundaries.append(tick);
                    }
                    bendAtTime.insert(tick, pb->value());
                }
                ++it2;
            }
        }
        if (boundaries.last() != t1) {
            boundaries.append(t1);
        }

        // Helper to convert bend value to nearest semitone offset
        auto bendToSemi = [bendRangeSemis](int value) -> int {
            double norm = (static_cast<double>(value) - 8192.0) / 8192.0; // approx -1..+1
            double semis = norm * bendRangeSemis;
            int offset = static_cast<int>(semis >= 0 ? std::floor(semis + 0.5) : std::ceil(semis - 0.5));
            return offset;
        };

        // Create replacement notes for each segment
        for (int i = 0; i < boundaries.size() - 1; ++i) {
            int segStart = boundaries.at(i);
            int segEnd = boundaries.at(i + 1);
            if (segEnd <= segStart) continue;

            int bendVal = (i == 0 ? startValue : bendAtTime.value(segStart, startValue));
            int offset = bendToSemi(bendVal);
            int newNote = on->note() + offset;
            if (newNote < 0) newNote = 0;
            if (newNote > 127) newNote = 127;

            channel->insertNote(newNote, segStart, segEnd, on->velocity(), on->track());
        }

        // Remove original note
        channel->removeEvent(on);

        // Remove pitch bend events in [t0, t1) - they're now represented as discrete notes
        // Note: Each selected note is processed independently, so overlapping notes
        // will each remove only the pitch bends within their own time range
        QList<MidiEvent *> removeList;
        if (emap && !emap->isEmpty()) {
            QMultiMap<int, MidiEvent *>::const_iterator it3 = emap->lowerBound(t0);
            while (it3 != emap->end() && it3.key() < t1) {
                PitchBendEvent *pb = dynamic_cast<PitchBendEvent *>(it3.value());
                if (pb) {
                    removeList.append(pb);
                }
                ++it3;
            }
        }
        foreach (MidiEvent *ev, removeList) {
            channel->removeEvent(ev);
        }
    }

    file->protocol()->endAction();

    updateAll();
}

void MainWindow::explodeChordsToTracks() {
    if (!file) {
        return;
    }

    // Determine source scope: selected notes on current edit track, otherwise all notes on that track
    MidiTrack *sourceTrack = file->track(NewNoteTool::editTrack());
    if (!sourceTrack) {
        return;
    }

    // Show dialog
    ExplodeChordsDialog *dialog = new ExplodeChordsDialog(file, sourceTrack, this);
    if (dialog->exec() != QDialog::Accepted) {
        delete dialog;
        return;
    }

    // Get settings from dialog
    typedef ExplodeChordsDialog::SplitStrategy SplitStrategy;
    typedef ExplodeChordsDialog::GroupMode GroupMode;
    
    SplitStrategy strategy = dialog->splitStrategy();
    GroupMode groupMode = dialog->groupMode();
    int minNotes = dialog->minimumNotes();
    bool insertAtEnd = dialog->insertAtEnd();
    bool keepOriginal = dialog->keepOriginalNotes();
    
    delete dialog;

    // Collect candidate notes
    QList<NoteOnEvent *> selectedNotes;
    foreach (MidiEvent *ev, Selection::instance()->selectedEvents()) {
        NoteOnEvent *on = dynamic_cast<NoteOnEvent *>(ev);
        if (on && on->offEvent() && on->track() == sourceTrack) {
            selectedNotes.append(on);
        }
    }

    bool useSelection = !selectedNotes.isEmpty();

    // Build map: key -> list of notes (a chord group)
    // key is either start tick or start tick + length encoded
    struct NoteWrap { NoteOnEvent *on; int tick; int len; };
    QMap<qint64, QList<NoteWrap>> groups;

    auto addNoteToGroups = [&](NoteOnEvent *on){
        int t = on->midiTime();
        int l = on->offEvent() ? (on->offEvent()->midiTime() - on->midiTime()) : 0;
        if (l < 0) l = 0;
        qint64 key = (strategy == SplitStrategy::SAME_START) ? qint64(t) : ( (qint64(t) << 32) | qint64(l & 0xFFFFFFFF) );
        groups[key].append({on, t, l});
    };

    if (useSelection) {
        for (NoteOnEvent *on : selectedNotes) addNoteToGroups(on);
    } else {
        // All notes on source track across channels 0..15
        for (int ch = 0; ch < 16; ++ch) {
            QMultiMap<int, MidiEvent *> *emap = file->channel(ch)->eventMap();
            for (auto it = emap->begin(); it != emap->end(); ++it) {
                NoteOnEvent *on = dynamic_cast<NoteOnEvent *>(it.value());
                if (on && on->offEvent() && on->track() == sourceTrack) {
                    addNoteToGroups(on);
                }
            }
        }
    }

    // Filter to only groups with at least minNotes and same start (and length if chosen)
    QList<QList<NoteWrap>> chordGroups;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QList<NoteWrap> list = it.value();
        if (list.size() >= minNotes) {
            chordGroups.append(list);
        }
    }
    if (chordGroups.isEmpty()) {
        return; // Nothing to do
    }

    file->protocol()->startNewAction(tr("Explode chords to tracks"));

    // Find source track index for insertion
    int sourceTrackIdx = file->tracks()->indexOf(sourceTrack);
    QList<MidiTrack*> newTracks;

    // Helper to sort by pitch descending (top voice first)
    auto sortByPitchDesc = [](const NoteWrap &a, const NoteWrap &b){
        NoteOnEvent *A = a.on; NoteOnEvent *B = b.on;
        return A->note() > B->note();
    };

    // Helper to copy or move note
    auto processNote = [&](NoteOnEvent *on, MidiTrack *dst) {
        if (!on || !on->offEvent()) return;
        if (keepOriginal) {
            // Copy note to new track
            int ch = on->channel();
            file->channel(ch)->insertNote(on->note(), on->midiTime(), 
                                          on->offEvent()->midiTime(), 
                                          on->velocity(), dst);
        } else {
            // Move note to new track
            on->setTrack(dst);
            on->offEvent()->setTrack(dst);
        }
    };

    if (groupMode == ExplodeChordsDialog::ALL_CHORDS_ONE_TRACK) {
        // Single destination track
        file->addTrack();
        MidiTrack *dst = file->tracks()->last();
        dst->setName(sourceTrack->name() + tr(" - Chord 1"));
        newTracks.append(dst);
        for (const auto &grp : chordGroups) {
            for (const NoteWrap &nw : grp) {
                processNote(nw.on, dst);
            }
        }
    } else if (groupMode == ExplodeChordsDialog::EACH_CHORD_OWN_TRACK) {
        int idx = 1;
        for (auto grp : chordGroups) {
            std::sort(grp.begin(), grp.end(), sortByPitchDesc);
            file->addTrack();
            MidiTrack *dst = file->tracks()->last();
            dst->setName(sourceTrack->name() + tr(" - Chord ") + QString::number(idx++));
            newTracks.append(dst);
            for (const NoteWrap &nw : grp) {
                processNote(nw.on, dst);
            }
        }
    } else { // VOICES_ACROSS_CHORDS
        // Determine max chord size
        int maxSize = 0;
        for (const auto &grp : chordGroups) maxSize = std::max(maxSize, static_cast<int>(grp.size()));
        // Create destination tracks per voice
        QVector<MidiTrack*> voiceTracks;
        voiceTracks.reserve(maxSize);
        for (int v = 0; v < maxSize; ++v) {
            file->addTrack();
            MidiTrack *dst = file->tracks()->last();
            dst->setName(sourceTrack->name() + tr(" - Chord ") + QString::number(v + 1));
            voiceTracks.append(dst);
            newTracks.append(dst);
        }
        // Process notes: for each chord, sort and map nth by pitch to nth track
        for (auto grp : chordGroups) {
            std::sort(grp.begin(), grp.end(), sortByPitchDesc);
            for (int i = 0; i < grp.size(); ++i) {
                MidiTrack *dst = voiceTracks[i];
                processNote(grp[i].on, dst);
            }
        }
    }

    // Reorder tracks if not inserting at end
    if (!insertAtEnd && sourceTrackIdx >= 0) {
        // Move new tracks to directly after source track
        QList<MidiTrack*> *trackList = file->tracks();
        
        // Remove new tracks from their current positions (at end)
        for (MidiTrack *newTrack : newTracks) {
            trackList->removeOne(newTrack);
        }
        
        // Insert them right after the source track
        int insertPos = sourceTrackIdx + 1;
        for (MidiTrack *newTrack : newTracks) {
            trackList->insert(insertPos++, newTrack);
        }
        
        // Renumber all tracks
        int n = 0;
        foreach(MidiTrack* track, *trackList) {
            track->setNumber(n++);
        }
    }

    file->protocol()->endAction();

    updateAll();
}

void MainWindow::splitChannelsToTracks() {
    if (!file) {
        return;
    }

    // Determine source track: the current edit track
    MidiTrack *sourceTrack = file->track(NewNoteTool::editTrack());
    if (!sourceTrack) {
        return;
    }

    // Phase 1: Analyze â€” collect channel info for events on the source track
    QList<SplitChannelsDialog::ChannelInfo> activeChannels;

    for (int ch = 0; ch < 16; ++ch) {
        QMultiMap<int, MidiEvent *> *emap = file->channel(ch)->eventMap();
        int eventCount = 0;
        int noteCount = 0;
        int prog = -1;

        for (auto it = emap->begin(); it != emap->end(); ++it) {
            MidiEvent *ev = it.value();
            if (ev->track() != sourceTrack) {
                continue;
            }
            eventCount++;
            if (dynamic_cast<NoteOnEvent *>(ev)) {
                noteCount++;
            }
            if (prog < 0) {
                ProgChangeEvent *pc = dynamic_cast<ProgChangeEvent *>(ev);
                if (pc) {
                    prog = pc->program();
                }
            }
        }

        if (eventCount > 0) {
            QString name;
            if (ch == 9) {
                name = tr("Drums");
            } else if (prog >= 0) {
                name = MidiFile::gmInstrumentName(prog);
            } else {
                name = tr("Channel %1").arg(ch);
            }
            activeChannels.append({ch, eventCount, noteCount, prog, name});
        }
    }

    if (activeChannels.size() <= 1) {
        QMessageBox::information(this, tr("Split Channels to Tracks"),
            tr("This track only uses one channel â€” nothing to split."));
        return;
    }

    // Phase 2: Show dialog
    SplitChannelsDialog *dialog = new SplitChannelsDialog(file, sourceTrack, activeChannels, this);
    if (dialog->exec() != QDialog::Accepted) {
        delete dialog;
        return;
    }

    bool skipDrums = dialog->keepDrumsOnSource();
    bool removeSource = dialog->removeEmptySource();
    bool insertAtEnd = dialog->insertAtEnd();
    bool useDrumPreset = dialog->hasDrumPreset();
    DrumKitPreset drumPreset;
    if (useDrumPreset) drumPreset = dialog->selectedDrumPreset();
    delete dialog;

    // Phase 3: Create tracks and move events
    file->protocol()->startNewAction(tr("Split channels to tracks"));

    int sourceTrackIdx = file->tracks()->indexOf(sourceTrack);
    QList<MidiTrack *> newTracks;
    int movedEvents = 0;

    for (const auto &info : activeChannels) {
        if (skipDrums && info.channel == 9) {
            continue;
        }

        // Drum preset: split channel 9 into multiple group-based tracks
        if (info.channel == 9 && useDrumPreset) {
            QMultiMap<int, MidiEvent *> *emap = file->channel(9)->eventMap();

            // Create a track for each drum group
            for (const DrumGroup &group : drumPreset.groups) {
                QSet<int> noteSet(group.noteNumbers.begin(), group.noteNumbers.end());
                QList<MidiEvent *> toMove;

                for (auto it = emap->begin(); it != emap->end(); ++it) {
                    MidiEvent *ev = it.value();
                    if (ev->track() != sourceTrack) continue;
                    NoteOnEvent *noteOn = dynamic_cast<NoteOnEvent *>(ev);
                    if (noteOn && noteSet.contains(noteOn->note())) {
                        toMove.append(ev);
                        // Also move paired off-event
                        if (noteOn->offEvent()) toMove.append(noteOn->offEvent());
                    }
                }
                if (toMove.isEmpty()) continue;

                file->addTrack();
                MidiTrack *dst = file->tracks()->last();
                dst->setName(group.name);
                dst->assignChannel(9);
                newTracks.append(dst);

                for (MidiEvent *ev : toMove) {
                    ev->setTrack(dst);
                    movedEvents++;
                }
            }

            // Move remaining ch9 events (non-note or unmatched notes)
            QList<MidiEvent *> remaining;
            for (auto it = emap->begin(); it != emap->end(); ++it) {
                if (it.value()->track() == sourceTrack) {
                    remaining.append(it.value());
                }
            }
            if (!remaining.isEmpty()) {
                file->addTrack();
                MidiTrack *dst = file->tracks()->last();
                dst->setName(tr("Drums (Other)"));
                dst->assignChannel(9);
                newTracks.append(dst);
                for (MidiEvent *ev : remaining) {
                    ev->setTrack(dst);
                    movedEvents++;
                }
            }
            continue;
        }

        file->addTrack();
        MidiTrack *dst = file->tracks()->last();
        dst->setName(info.instrumentName);
        dst->assignChannel(info.channel);
        newTracks.append(dst);

        // Move all events on this channel from source track to new track
        QMultiMap<int, MidiEvent *> *emap = file->channel(info.channel)->eventMap();
        QList<MidiEvent *> toMove;
        for (auto it = emap->begin(); it != emap->end(); ++it) {
            if (it.value()->track() == sourceTrack) {
                toMove.append(it.value());
            }
        }
        for (MidiEvent *ev : toMove) {
            ev->setTrack(dst);
            movedEvents++;
        }
    }

    // Reorder tracks if not inserting at end
    if (!insertAtEnd && sourceTrackIdx >= 0) {
        QList<MidiTrack *> *trackList = file->tracks();
        for (MidiTrack *newTrack : newTracks) {
            trackList->removeOne(newTrack);
        }
        int insertPos = sourceTrackIdx + 1;
        for (MidiTrack *newTrack : newTracks) {
            trackList->insert(insertPos++, newTrack);
        }
        int n = 0;
        foreach (MidiTrack *track, *trackList) {
            track->setNumber(n++);
        }
    }

    // Remove empty source track if requested
    if (removeSource) {
        bool sourceEmpty = true;
        for (int ch = 0; ch < 16; ++ch) {
            QMultiMap<int, MidiEvent *> *emap = file->channel(ch)->eventMap();
            for (auto it = emap->begin(); it != emap->end(); ++it) {
                if (it.value()->track() == sourceTrack) {
                    sourceEmpty = false;
                    break;
                }
            }
            if (!sourceEmpty) break;
        }
        // Also check meta channel (17) for tempo/time sig events
        // Keep the source track if it has meta events
        if (sourceEmpty) {
            QMultiMap<int, MidiEvent *> *metaMap = file->channelEvents(17);
            if (metaMap) {
                for (auto it = metaMap->begin(); it != metaMap->end(); ++it) {
                    if (it.value()->track() == sourceTrack) {
                        sourceEmpty = false;
                        break;
                    }
                }
            }
        }
        if (sourceEmpty) {
            file->removeTrack(sourceTrack);
        }
    }

    file->protocol()->endAction();

    statusBar()->showMessage(tr("Split %1 channels into %2 tracks (%3 events moved)")
        .arg(activeChannels.size())
        .arg(newTracks.size())
        .arg(movedEvents), 5000);

    updateAll();
}

void MainWindow::transposeNSemitones() {
    if (!file) {
        return;
    }

    QList<NoteOnEvent *> events;
    foreach(MidiEvent* event, Selection::instance()->selectedEvents()) {
        NoteOnEvent *on = dynamic_cast<NoteOnEvent *>(event);
        if (on) {
            events.append(on);
        }
    }

    if (events.isEmpty()) {
        return;
    }

    TransposeDialog *d = new TransposeDialog(events, file, this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setModal(true);
    d->show();
}

// --------------------------------------------------------------------------
// Phase 34 — Paste Special session state.
//
// When the user ticks "Don't ask again — use this for the rest of the
// session" inside PasteSpecialDialog we silence the modal for any further
// Ctrl+V / Edit → Paste / Edit → Paste Special invocations until the app
// restarts. The persistent default lives in QSettings ("Editing/pasteSpecialDefault")
// and is handled separately via the dialog's "Make this the new default" toggle.
// --------------------------------------------------------------------------
static bool s_pasteSpecialDontAskSession = false;
static PasteAssignment s_pasteSpecialSessionAssignment =
    PasteAssignment::NewTracksPerSource;

void MainWindow::copy() {
    EventTool::copyAction();
}

void MainWindow::paste() {
#ifdef MIDIEDITOR_COLLAB_ENABLED
    // First-pass discriminator: if the clipboard text is a smart-paste
    // collab token, divert to the PR review flow. Anything that doesn't
    // start with the unique scheme prefix falls through to the existing
    // paste handler below — cross-instance event paste, etc.
    if (file && CollabService::instance()->isEnabled()) {
        QString cb = QApplication::clipboard()->text();
        // BUG-COLLAB-028: Discord wraps tokens in triple backticks (per
        // WebhookClient.cpp), and users frequently paste with trailing
        // newlines or surrounding spaces. looksLikeToken is strict
        // (text.startsWith), so without cleanup the most common Discord
        // copy-flow silently falls through to the regular paste path.
        cb = cb.trimmed();
        // Strip surrounding markdown code fences if present.
        if (cb.startsWith(QStringLiteral("```")) && cb.endsWith(QStringLiteral("```"))
            && cb.length() >= 6) {
            cb = cb.mid(3, cb.length() - 6).trimmed();
        }
        // Some clients use single backticks for inline code.
        else if (cb.startsWith(QLatin1Char('`')) && cb.endsWith(QLatin1Char('`'))
                 && cb.length() >= 2) {
            cb = cb.mid(1, cb.length() - 2).trimmed();
        }
        if (PrBundle::looksLikeToken(cb)) {
            QString error;
            PrBundle bundle = PrBundle::fromInlineToken(cb, &error);
            if (!bundle.isValid()) {
                // Decode failed (truncated payload, wrong base64, oversized
                // expansion, etc.). Don't swallow the user's paste — show a
                // brief warning and fall through to the regular paste path
                // so harmless near-tokens or accidental copies can't lock
                // out Ctrl+V on the matrix.
                QMessageBox::warning(this, tr("PR token"),
                    tr("Could not decode the smart-paste token.\n\n%1\n\nFalling back to a regular paste.").arg(error));
                // intentional fallthrough: do NOT return here
            } else {
                // Phase 9.5i transport-agnostic lookup: if the
                // bundle's sessionId belongs to a different local
                // file, offer to switch to it before reviewing.
                // Otherwise fall through to the existing flow where
                // PrReviewDialog shows the cross-session warning.
                QString currentSession = CollabService::instance()->sessionId();
                if (!bundle.sessionId.isEmpty()
                    && bundle.sessionId != currentSession) {
                    QString matchPath = CollabService::instance()
                                           ->findFileBySessionId(bundle.sessionId);
                    if (!matchPath.isEmpty()
                        && (!file || file->path() != matchPath)) {
                        auto answer = QMessageBox::question(this,
                            tr("Smart-paste token"),
                            tr("This PR was created against a different file "
                               "you have locally:\n\n%1\n\n"
                               "Switch to it and apply the PR there?")
                                .arg(matchPath),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::Yes);
                        if (answer == QMessageBox::Yes) {
                            openFile(matchPath);
                            // Fall through with the now-active file.
                        }
                    }
                }
                // Plan §11.10n auto-init for the Ctrl+V token-paste
                // path — same rationale as the Import-PR menu and the
                // Live-Session entry points. The user is about to
                // merge a PR; any commit it produces should land in
                // this file's collaboration history.
                if (file && !CollabService::instance()->isCurrentFileInitialized()
                    && !file->path().isEmpty()
                    && QFile(file->path()).exists()) {
                    CollabService::instance()->initializeCurrentFile(
                        file, tr("Auto-init for PR import"));
                }
                PrReviewDialog dlg(bundle, file, PrReviewDialog::Mode::PreApply, this);
                int result = dlg.exec();
                // After a successful merge the token is consumed — clear it
                // from the OS clipboard so subsequent Ctrl+V (e.g. an internal
                // event copy made between paste-and-paste) is not intercepted
                // again by the same stale token. We only clear when the
                // clipboard still holds the exact text we processed: if the
                // user copied something else in the meantime, we leave their
                // current clipboard alone.
                if (result == QDialog::Accepted) {
                    QClipboard *clipboard = QApplication::clipboard();
                    if (clipboard->text() == cb) {
                        clipboard->clear();
                    }
                }
                return;
            }
        }
    }
#endif

    // If the cross-instance clipboard has data, route Ctrl+V through the
    // Paste Special flow so the user picks a target mapping (or reuses
    // the silenced session/persistent default). The same dialog is also
    // shown when the local in-process clipboard contains a copy that
    // originated in a *different* MidiFile (e.g. user copied in file A,
    // opened file B in the same window, then pressed Ctrl+V) -- the
    // mapping question is just as relevant there as it is across
    // separate MidiEditor processes. Within the same file we keep the
    // legacy quick-paste behaviour so daily editing isn't interrupted.
    if (file && (EventTool::hasSharedClipboardData()
                 || EventTool::localCopyIsForeignTo(file))) {
        pasteSpecial();
        return;
    }
    EventTool::pasteAction();
}

void MainWindow::pasteSpecial() {
    if (!file) {
        return;
    }
    // Eligibility: either cross-process shared clipboard data, or a
    // local copy that originated in a different MidiFile in this same
    // process (Phase 36.x "copy in A, open B, paste" flow). When only
    // the latter applies we relax the cross-process guard inside
    // EventTool so the same buffer can be replayed through the dialog.
    const bool crossProcess = EventTool::hasSharedClipboardData();
    const bool localForeign = EventTool::localCopyIsForeignTo(file);
    if (!crossProcess && !localForeign) {
        QMessageBox::information(this, tr("Paste Special"),
                                 tr("No cross-instance clipboard data is available.\n"
                                    "Copy events in another MidiEditor window first."));
        return;
    }
    const bool allowSameProcess = !crossProcess && localForeign;

    // Build a summary purely from the clipboard metadata snapshot taken
    // when SharedClipboard last deserialized. The deserialize step runs
    // again inside EventTool::pasteFromSharedClipboardWithOptions(); we
    // intentionally peek the metadata here without consuming events.
    SharedClipboard *clipboard = SharedClipboard::instance();
    if (!clipboard->initialize()) {
        return;
    }
    // Trigger a metadata-only deserialize by doing a probe paste into a
    // throw-away list and then discarding it. We must keep this side
    // effect to a minimum: deserializeEvents() refreshes g_sourceTracks
    // and g_pasteSourceInfos.
    QList<MidiEvent *> probe;
    if (!clipboard->pasteEvents(file, probe, /*applyTempoConversion=*/false,
                                file->cursorTick())) {
        // PHASE36-002: deserializeEvents() can append events before
        // bailing on a sanity check (truncated buffer, bad nameLen,
        // etc.). Free them here so a malformed cross-process buffer
        // doesn't leak NoteOnEvents (which would also stay registered
        // in OffEvent::onEvents until process exit).
        qDeleteAll(probe);
        return;
    }
    PasteClipboardSummary summary;
    summary.totalEvents = probe.size();
    summary.sourceTracks = SharedClipboard::sourceTrackList();
    summary.sourceTrackCount = summary.sourceTracks.size();
    QSet<int> distinctChannels;
    int minTick = INT_MAX;
    int maxTick = INT_MIN;
    for (int i = 0; i < probe.size(); ++i) {
        const PasteSourceInfo info = SharedClipboard::getPasteSourceInfo(i);
        if (info.originalChannel >= 0) {
            distinctChannels.insert(info.originalChannel);
        }
        const int t = probe[i] ? probe[i]->midiTime() : -1;
        if (t >= 0) {
            minTick = qMin(minTick, t);
            maxTick = qMax(maxTick, t);
        }
    }
    summary.distinctChannels = distinctChannels.values();
    std::sort(summary.distinctChannels.begin(), summary.distinctChannels.end());
    if (minTick != INT_MAX && maxTick != INT_MIN) {
        const int spanTicks = qMax(0, maxTick - minTick);
        summary.approxDurationMs = file->msOfTick(spanTicks) - file->msOfTick(0);
        if (summary.approxDurationMs < 0) summary.approxDurationMs = 0;
    } else {
        summary.approxDurationMs = 0;
    }

    // Discard the probe events so we don't leak memory before the real paste.
    qDeleteAll(probe);

    // Resolve default assignment from QSettings.
    const int storedDefault = _settings->value("Editing/pasteSpecialDefault", -1).toInt();
    PasteAssignment defaultAssignment = (storedDefault >= 0 && storedDefault <= 2)
        ? static_cast<PasteAssignment>(storedDefault)
        : PasteAssignment::NewTracksPerSource;

    PasteAssignment chosen = defaultAssignment;
    if (s_pasteSpecialDontAskSession) {
        // User opted out of the dialog earlier this session — reuse their
        // last choice silently. Persistent default still wins if the user
        // explicitly chose "Make this the new default" back then (it was
        // already written to QSettings at that point).
        chosen = s_pasteSpecialSessionAssignment;
    } else {
        PasteSpecialDialog dlg(summary, defaultAssignment, this);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        chosen = dlg.chosenAssignment();

        if (dlg.makeThisTheNewDefault()) {
            _settings->setValue("Editing/pasteSpecialDefault",
                                static_cast<int>(chosen));
        }
        if (dlg.dontAskAgainThisSession()) {
            s_pasteSpecialDontAskSession = true;
            s_pasteSpecialSessionAssignment = chosen;
        }
    }

    PasteSpecialOptions opts;
    opts.assignment = chosen;
    opts.applyTempoConversion = true;
    opts.targetCursorTick = file->cursorTick();
    EventTool::pasteFromSharedClipboardWithOptions(opts, allowSameProcess);
}

void MainWindow::markEdited() {
    setWindowModified(true);

    // Reset auto-save debounce timer (saves after a quiet period, not mid-editing)
    if (_autoSaveTimer && _settings->value("autosave_enabled", true).toBool()) {
        int intervalSec = _settings->value("autosave_interval", 120).toInt();
        _autoSaveTimer->start(intervalSec * 1000);
    }
}

QString MainWindow::autoSavePath() const {
    if (!file) return QString();

    if (!file->path().isEmpty() && QFile::exists(file->path())) {
        // Named file â†’ sidecar: "MySong.mid" â†’ "MySong.mid.autosave"
        return file->path() + ".autosave";
    } else {
        // Untitled â†’ stable path in AppData
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                    + "/autosave";
        QDir().mkpath(dir);
        return dir + "/untitled.autosave.mid";
    }
}

void MainWindow::performAutoSave() {
    if (!file || file->saved()) return;

    QString backupPath = autoSavePath();
    if (backupPath.isEmpty()) return;

    if (file->save(backupPath)) {
        // CRITICAL: backup save must NOT mark the file as saved
        file->setSaved(false);
        file->protocol()->addEmptyAction(tr("Auto-saved"));
        statusBar()->showMessage(tr("Auto-saved"), 3000);
    }
}

void MainWindow::cleanupAutoSave() {
    if (_autoSaveTimer) {
        _autoSaveTimer->stop();
    }

    if (!file) return;

    // Remove sidecar for named file
    if (!file->path().isEmpty()) {
        QFile::remove(file->path() + ".autosave");
    }

    // Remove untitled backup
    QString untitledPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + "/autosave/untitled.autosave.mid";
    QFile::remove(untitledPath);
}

void MainWindow::checkAutoSaveRecovery() {
    // Check for untitled auto-save backup in AppData
    QString untitledPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + "/autosave/untitled.autosave.mid";

    if (!QFile::exists(untitledPath)) return;

    QFileInfo info(untitledPath);
    auto result = QMessageBox::question(this, tr("Auto-Save Recovery"),
        tr("An auto-saved backup of an untitled document was found.\n"
           "Last modified: %1\n\n"
           "Would you like to recover it?")
            .arg(QLocale().toString(info.lastModified(), QLocale::ShortFormat)),
        QMessageBox::Yes | QMessageBox::No);

    if (result == QMessageBox::Yes) {
        bool ok = true;
        MidiFile *mf = new MidiFile(untitledPath, &ok);
        if (ok) {
            stop();
            setFile(mf);
            mf->setPath(QString());   // Clear path â€” this is an untitled recovered doc
            mf->setSaved(false);
            setWindowTitle(QApplication::applicationName() + " v" +
                QApplication::applicationVersion() + tr(" - Recovered Document[*]"));
            setWindowModified(true);
            statusBar()->showMessage(tr("Recovered from auto-save backup"), 5000);
            QFile::remove(untitledPath);
            _initFile = "";  // Prevent loadInitFile from loading another file
            return;
        }
    }

    // Declined or failed â€” delete the stale backup
    QFile::remove(untitledPath);
}

void MainWindow::colorsByChannel() {
    mw_matrixWidget->setColorsByChannel();
    _colorsByChannel->setChecked(true);
    _colorsByTracks->setChecked(false);
    mw_matrixWidget->registerRelayout();
    _matrixWidgetContainer->update();
    _miscWidgetContainer->update();
}

void MainWindow::colorsByTrack() {
    mw_matrixWidget->setColorsByTracks();
    _colorsByChannel->setChecked(false);
    _colorsByTracks->setChecked(true);
    mw_matrixWidget->registerRelayout();
    _matrixWidgetContainer->update();
    _miscWidgetContainer->update();
}

void MainWindow::editChannel(int i, bool assign) {
    NewNoteTool::setEditChannel(i);

    // assign channel to track
    if (assign && file && file->track(NewNoteTool::editTrack())) {
        file->track(NewNoteTool::editTrack())->assignChannel(i);
    }

    MidiOutput::setStandardChannel(i);

    if (file && file->channel(i)) {
        int prog = file->channel(i)->progAtTick(file->cursorTick());
        MidiOutput::sendProgram(i, prog);
    }

    updateChannelMenu();
}

void MainWindow::editTrack(int i, bool assign) {
    NewNoteTool::setEditTrack(i);

    // assign channel to track
    if (assign && file && file->track(i)) {
        file->track(i)->assignChannel(NewNoteTool::editChannel());
    }
    updateTrackMenu();
}

void MainWindow::editTrackAndChannel(MidiTrack *track) {
    editTrack(track->number(), false);
    if (track->assignedChannel() > -1) {
        editChannel(track->assignedChannel(), false);
    }
}

void MainWindow::setInstrumentForChannel(int i) {
    InstrumentChooser *d = new InstrumentChooser(file, i, this);
    d->setModal(true);
    d->exec();

    if (i == NewNoteTool::editChannel()) {
        editChannel(i);
    }
    updateChannelMenu();
}

void MainWindow::instrumentChannel(QAction *action) {
    if (file) {
        setInstrumentForChannel(action->data().toInt());
    }
}

void MainWindow::spreadSelection() {
    if (!file) {
        return;
    }

    bool ok;
    float numMs = float(QInputDialog::getDouble(this, tr("Set spread-time"), tr("Spread time [ms]"), 10, 5, 500, 2, &ok));

    if (!ok) {
        numMs = 1;
    }

    QMultiMap<int, int> spreadChannel[19];

    foreach(MidiEvent* event, Selection::instance()->selectedEvents()) {
        if (!spreadChannel[event->channel()].values(event->line()).contains(event->midiTime())) {
            spreadChannel[event->channel()].insert(event->line(), event->midiTime());
        }
    }

    file->protocol()->startNewAction(tr("Spread events"));
    int numSpreads = 0;
    for (int i = 0; i < 19; i++) {
        MidiChannel *channel = file->channel(i);

        QList<int> seenBefore;

        foreach(int line, spreadChannel[i].keys()) {
            if (seenBefore.contains(line)) {
                continue;
            }

            seenBefore.append(line);

            foreach(int position, spreadChannel[i].values(line)) {
                QList<MidiEvent *> eventsWithAllLines = channel->eventMap()->values(position);

                QList<MidiEvent *> events;
                foreach(MidiEvent* event, eventsWithAllLines) {
                    if (event->line() == line) {
                        events.append(event);
                    }
                }

                //spread events for the channel at the given position
                int num = events.count();
                if (num > 1) {
                    float timeToInsert = file->msOfTick(position) + numMs * num / 2;

                    for (int y = 0; y < num; y++) {
                        MidiEvent *toMove = events.at(y);

                        toMove->setMidiTime(file->tick(timeToInsert), true);
                        numSpreads++;

                        timeToInsert -= numMs;
                    }
                }
            }
        }
    }
    file->protocol()->endAction();

    QMessageBox::information(this, tr("Spreading done"), QString(tr("Spreaded ") + QString::number(numSpreads) + tr(" events")));
}

void MainWindow::manual() {
    QDesktopServices::openUrl(QUrl("https://happytunesai.github.io/MidiEditor_AI/", QUrl::TolerantMode));
}

void MainWindow::changeMiscMode(int mode) {
    // Use the container widget for UI operations (handles both OpenGL and software)
    if (OpenGLMiscWidget *openglMisc = qobject_cast<OpenGLMiscWidget*>(_miscWidgetContainer)) {
        openglMisc->setMode(mode);
    } else if (MiscWidget *miscWidget = qobject_cast<MiscWidget*>(_miscWidgetContainer)) {
        miscWidget->setMode(mode);
    }
    if (mode == VelocityEditor || mode == TempoEditor) {
        _miscChannel->setEnabled(false);
    } else {
        _miscChannel->setEnabled(true);
    }
    if (mode == ControllEditor || mode == KeyPressureEditor) {
        _miscController->setEnabled(true);
        _miscController->clear();

        if (mode == ControllEditor) {
            for (int i = 0; i < 128; i++) {
                QString name = MidiFile::controlChangeName(i);
                if (name == MidiFile::tr("undefined")) {
                    _miscController->addItem(QString::number(i) + ": ");
                } else {
                    _miscController->addItem(QString::number(i) + ": " + name);
                }
            }
        } else {
            for (int i = 0; i < 128; i++) {
                _miscController->addItem(tr("Note: ") + QString::number(i));
            }
        }

        _miscController->view()->setMinimumWidth(_miscController->minimumSizeHint().width());
    } else {
        _miscController->setEnabled(false);
    }
}

void MainWindow::selectModeChanged(QAction *action) {
    // Use the container widget for UI operations (handles both OpenGL and software)
    if (action == setSingleMode) {
        if (OpenGLMiscWidget *openglMisc = qobject_cast<OpenGLMiscWidget*>(_miscWidgetContainer)) {
            openglMisc->setEditMode(SINGLE_MODE);
        } else if (MiscWidget *miscWidget = qobject_cast<MiscWidget*>(_miscWidgetContainer)) {
            miscWidget->setEditMode(SINGLE_MODE);
        }
    }
    if (action == setLineMode) {
        if (OpenGLMiscWidget *openglMisc = qobject_cast<OpenGLMiscWidget*>(_miscWidgetContainer)) {
            openglMisc->setEditMode(LINE_MODE);
        } else if (MiscWidget *miscWidget = qobject_cast<MiscWidget*>(_miscWidgetContainer)) {
            miscWidget->setEditMode(LINE_MODE);
        }
    }
    if (action == setFreehandMode) {
        if (OpenGLMiscWidget *openglMisc = qobject_cast<OpenGLMiscWidget*>(_miscWidgetContainer)) {
            openglMisc->setEditMode(MOUSE_MODE);
        } else if (MiscWidget *miscWidget = qobject_cast<MiscWidget*>(_miscWidgetContainer)) {
            miscWidget->setEditMode(MOUSE_MODE);
        }
    }
}

QWidget *MainWindow::setupActions(QWidget *parent) {
    // Menubar
    QMenu *fileMB = menuBar()->addMenu(tr("File"));
    QMenu *editMB = menuBar()->addMenu(tr("Edit"));
    QMenu *toolsMB = menuBar()->addMenu(tr("Tools"));
    QMenu *viewMB = menuBar()->addMenu(tr("View"));
    QMenu *playbackMB = menuBar()->addMenu(tr("Playback"));
    QMenu *midiMB = menuBar()->addMenu(tr("Midi"));
    QMenu *helpMB = menuBar()->addMenu(tr("Help"));

#ifdef MIDIEDITOR_COLLAB_ENABLED
    // Phase 9.9c §15.2: stash pointers so the applyShowModeLock lambda
    // (defined earlier in the constructor) can disable them when the
    // local peer is a Show-mode viewer. Edit / Tools / Midi all contain
    // MIDI-mutating actions; File stays enabled (Save, Collab submenu),
    // View / Playback / Help are read-only.
    _editMenuForShowLock  = editMB;
    _toolsMenuForShowLock = toolsMB;
    _midiMenuForShowLock  = midiMB;
#endif

    // File
    QAction *newAction = new QAction(tr("New"), this);
    newAction->setShortcut(QKeySequence::New);
    _defaultShortcuts["new"] = QList<QKeySequence>() << newAction->shortcut();
    Appearance::setActionIcon(newAction, ":/run_environment/graphics/tool/new.png");
    connect(newAction, SIGNAL(triggered()), this, SLOT(newFile()));
    fileMB->addAction(newAction);
    _actionMap["new"] = newAction;

    QAction *loadAction = new QAction(tr("Open..."), this);
    loadAction->setShortcut(QKeySequence::Open);
    _defaultShortcuts["open"] = QList<QKeySequence>() << loadAction->shortcut();
    Appearance::setActionIcon(loadAction, ":/run_environment/graphics/tool/load.png");
    connect(loadAction, SIGNAL(triggered()), this, SLOT(load()));
    fileMB->addAction(loadAction);
    _actionMap["open"] = loadAction;

    _recentPathsMenu = new QMenu(tr("Open recent..."), this);
    _recentPathsMenu->setIcon(Appearance::adjustIconForDarkMode(":/run_environment/graphics/tool/noicon.png"));
    fileMB->addMenu(_recentPathsMenu);
    connect(_recentPathsMenu, SIGNAL(triggered(QAction*)), this, SLOT(openRecent(QAction*)));

    updateRecentPathsList();

    fileMB->addSeparator();

    QAction *saveAction = new QAction(tr("Save"), this);
    saveAction->setShortcut(QKeySequence::Save);
    _defaultShortcuts["save"] = QList<QKeySequence>() << saveAction->shortcut();
    Appearance::setActionIcon(saveAction, ":/run_environment/graphics/tool/save.png");
    connect(saveAction, SIGNAL(triggered()), this, SLOT(save()));
    fileMB->addAction(saveAction);
    _actionMap["save"] = saveAction;

    QAction *saveAsAction = new QAction(tr("Save as..."), this);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    _defaultShortcuts["save_as"] = QList<QKeySequence>() << saveAsAction->shortcut();
    Appearance::setActionIcon(saveAsAction, ":/run_environment/graphics/tool/saveas.png");
    connect(saveAsAction, SIGNAL(triggered()), this, SLOT(saveas()));
    fileMB->addAction(saveAsAction);
    _actionMap["save_as"] = saveAsAction;

#ifdef FLUIDSYNTH_SUPPORT
    fileMB->addSeparator();
    _exportAudioAction = new QAction(tr("Export Audio..."), this);
    _exportAudioAction->setShortcut(QKeySequence(tr("Ctrl+Shift+E")));
    _defaultShortcuts["export_audio"] = QList<QKeySequence>() << _exportAudioAction->shortcut();
    Appearance::setActionIcon(_exportAudioAction, ":/run_environment/graphics/tool/noicon.png");
    connect(_exportAudioAction, &QAction::triggered, this, &MainWindow::exportAudio);
    fileMB->addAction(_exportAudioAction);
    _actionMap["export_audio"] = _exportAudioAction;
    _exportAudioAction->setEnabled(false);
#else
    fileMB->addSeparator();
#endif

    // Notation export (no FluidSynth dependency): write the current file as
    // MusicXML, openable in MuseScore / Finale / Sibelius / Dorico.
    _exportMusicXmlAction = new QAction(tr("Export MusicXML..."), this);
    Appearance::setActionIcon(_exportMusicXmlAction, ":/run_environment/graphics/tool/noicon.png");
    connect(_exportMusicXmlAction, &QAction::triggered, this, &MainWindow::exportMusicXml);
    fileMB->addAction(_exportMusicXmlAction);
    _actionMap["export_musicxml"] = _exportMusicXmlAction;
    _exportMusicXmlAction->setEnabled(false);

#ifdef MIDIEDITOR_COLLAB_ENABLED
    fileMB->addSeparator();
    QMenu *collabMenu = fileMB->addMenu(tr("Collaboration"));
    // Plan §11.10n: explicit "Initialize for this file…" action removed.
    // The master toggle in Settings is the user's opt-in; per-file
    // initialization is now done transparently the moment the file is
    // first used in a collab context (host/join session, Create PR).
    // Programmatic API stays in `CollabService::initializeCurrentFile`
    // for tests + the auto-init call sites below.

    QAction *collabCreatePrAction = new QAction(tr("Create PR..."), this);
    collabCreatePrAction->setStatusTip(tr("Create a shareable PR from the current file's latest commit — copy as token or save as bundle file."));
    collabMenu->addAction(collabCreatePrAction);
    connect(collabCreatePrAction, &QAction::triggered, this, [this]() {
        CollabService *svc = CollabService::instance();
        if (!svc->isEnabled()) return;
        // Auto-init on demand — clicking Create PR is itself the
        // explicit consent. Same rule as the live-session paths.
        if (!svc->isCurrentFileInitialized()) {
            if (!file || file->path().isEmpty() || !QFile(file->path()).exists()) {
                QMessageBox::information(this, tr("Save first"),
                    tr("Save the file first — collaboration history is stored in a sidecar next to the .mid on disk."));
                return;
            }
            if (!svc->initializeCurrentFile(file, tr("Auto-init for PR"))) {
                QMessageBox::warning(this, tr("Could not initialize collaboration"),
                    tr("Could not write the collaboration sidecar. Check directory "
                       "permissions for the folder containing the MIDI file."));
                return;
            }
        }
        PrCreateDialog dlg(this);
        dlg.exec();
    });

    QAction *collabCompactAction = new QAction(tr("Compact history..."), this);
    collabCompactAction->setStatusTip(tr("Strip diff data from older commits to shrink the sidecar. Authors, messages, and the hash chain are preserved."));
    collabMenu->addAction(collabCompactAction);
    connect(collabCompactAction, &QAction::triggered, this, [this]() {
        CollabService *svc = CollabService::instance();
        if (!svc->isEnabled() || !svc->isCurrentFileInitialized()) return;
        const int kKeepLast = 50;
        QMessageBox::StandardButton reply = QMessageBox::question(this,
            tr("Compact collaboration history?"),
            tr("This strips diff data (hunks) from commits older than the last %1.\n\n"
               "Author, message, and hash chain are preserved — only the bulky "
               "before/after event arrays are dropped. This is one-way: removed "
               "hunks cannot be restored.\n\n"
               "Continue?").arg(kKeepLast),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
        int n = svc->compactHistory(kKeepLast);
        QMessageBox::information(this, tr("Compact history"),
            n == 0 ? tr("No older commits had hunk data — nothing to compact.")
                   : tr("Stripped hunk data from %1 commit(s).").arg(n));
    });

    QAction *collabImportPrAction = new QAction(tr("Import PR..."), this);
    collabImportPrAction->setStatusTip(tr("Import a PR bundle file (.midiedit-pr.json) and review its hunks before merging."));
    collabMenu->addAction(collabImportPrAction);
    connect(collabImportPrAction, &QAction::triggered, this, [this]() {
        CollabService *svc = CollabService::instance();
        if (!svc->isEnabled() || !file) return;
        QString path = QFileDialog::getOpenFileName(this,
            tr("Import PR bundle"),
            QString(),
            tr("MidiEditor PR bundle (*.midiedit-pr.json);;All files (*.*)"));
        if (path.isEmpty()) return;
        QString error;
        PrBundle bundle = PrBundle::fromBundleFile(path, &error);
        if (!bundle.isValid()) {
            QMessageBox::warning(this, tr("Import failed"),
                tr("Could not load the PR bundle.\n\n%1").arg(error));
            return;
        }
        // Plan §11.10n auto-init: importing a PR is a collab action,
        // so transparent init makes the merge land in this file's
        // history. Without this, the apply succeeds but the
        // Collaboration tab shows nothing because onFileSaved
        // short-circuits on uninitialised files.
        if (!svc->isCurrentFileInitialized()
            && !file->path().isEmpty()
            && QFile(file->path()).exists()) {
            svc->initializeCurrentFile(file, tr("Auto-init for PR import"));
        }
        PrReviewDialog dlg(bundle, file, PrReviewDialog::Mode::PreApply, this);
        dlg.exec();
    });

    collabMenu->addSeparator();

    // Plan §11.10n hosting safety net: when `Collab/host/workOnCopy` is
    // ON, hosting saves the current file as Documents/MidiEditor_AI/shared/
    // <name>_shared.mid and switches the editor to the copy before the
    // session starts. Original stays untouched. Default off (existing
    // workflow keeps editing the original directly). Returns false if
    // the user wanted a copy but the save / open failed — caller bails.
    auto prepareHostFile = [this](const QString &flowName) -> bool {
        QSettings probe(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
        // Default ON: no one has shipped a session against this build
        // yet (Plan §11.10n revision 2026-05-07), so the safer default
        // doesn't break anyone's existing workflow. Users who want the
        // original-edit behaviour explicitly turn it off in Settings.
        if (!probe.value(QStringLiteral("Collab/host/workOnCopy"), true).toBool())
            return true;  // setting off — host on the original
        if (!file || file->path().isEmpty()) {
            // Untitled file → no path to base the copy on. Tell the user
            // to save first; the copy lives next to the original on disk.
            QMessageBox::information(this, flowName,
                tr("This file hasn't been saved yet. Save it first — "
                   "\"work on a copy when hosting\" needs the original "
                   "on disk to base the copy off."));
            return false;
        }
        QString sharedRoot = QDir(QStandardPaths::writableLocation(
                                      QStandardPaths::DocumentsLocation))
                                 .filePath(QStringLiteral("MidiEditor_AI/shared"));
        QDir().mkpath(sharedRoot);

        QFileInfo origInfo(file->path());
        QString stem = origInfo.completeBaseName();
        QString suffix = origInfo.suffix();
        // Import-only formats route through dedicated importers in openFile
        // (GuitarPro / MML / MusicXML / MuseScore / SID). MidiFile::save() always
        // writes MIDI bytes, so a copy with the original suffix would be
        // rejected by the importer on re-open ("Guitar Pro file could not
        // be imported"). Force `.mid` for the shared copy in those cases —
        // the original on disk stays untouched in its native format. (Shared
        // helper, so this list can't drift from save()'s.)
        const QString effectiveSuffix = ImportFormats::isImportOnly(file->path())
            ? QStringLiteral("mid") : suffix;
        QString sharedName;
        if (stem.endsWith(QStringLiteral("_shared"), Qt::CaseInsensitive)) {
            sharedName = effectiveSuffix.isEmpty()
                ? stem
                : stem + QStringLiteral(".") + effectiveSuffix;
        } else if (effectiveSuffix.isEmpty()) {
            sharedName = stem + QStringLiteral("_shared");
        } else {
            sharedName = stem + QStringLiteral("_shared.") + effectiveSuffix;
        }
        QString basePath = QDir(sharedRoot).filePath(sharedName);
        // Don't clobber a previous session's copy — append a numeric
        // suffix until unique.
        QString sharedPath = basePath;
        int n = 2;
        while (QFile::exists(sharedPath)) {
            QFileInfo bp(basePath);
            sharedPath = bp.absolutePath() + QStringLiteral("/")
                + bp.completeBaseName() + QStringLiteral("_") + QString::number(n)
                + (bp.suffix().isEmpty() ? QString() : QStringLiteral(".") + bp.suffix());
            n++;
        }

        if (!file->save(sharedPath)) {
            QMessageBox::warning(this, flowName,
                tr("Could not write the session copy to %1. The "
                   "session has not started.").arg(sharedPath));
            return false;
        }
        // Switch the editor to the copy. openFile updates `this->file`
        // via setFile so subsequent code sees the copy. The original is
        // preserved on disk untouched.
        openFile(sharedPath);
        if (!file || file->path() != sharedPath) {
            QMessageBox::warning(this, flowName,
                tr("Wrote the session copy to %1 but couldn't open it. "
                   "Try opening it manually.").arg(sharedPath));
            return false;
        }
        statusBar()->showMessage(
            tr("Hosting on a copy: %1 (original unchanged)").arg(sharedPath),
            6000);
        return true;
    };

    // Phase 9.9 §15.2: ask the user which editing-rights model to use
    // before spinning up the session. Returns std::nullopt if the user
    // cancelled. The picker is intentionally a small modal QMessageBox
    // here for the 9.9a MVP — once Phase 9.9d ships the hat-pass UI, the
    // existing LanLiveStartDialog / WebRtcStartDialog will absorb the
    // mode picker so the user only sees one dialog.
    auto pickSessionMode =
            [this](const QString &flowName) -> std::optional<LanLiveSession::SessionMode> {
        QMessageBox mb(this);
        mb.setIcon(QMessageBox::Question);
        mb.setWindowTitle(flowName);
        mb.setText(tr("How should peers be able to edit?"));
        mb.setInformativeText(
            tr("Edit mode: every peer can edit and broadcast changes "
               "(the original co-editing behaviour).\n\n"
               "Show mode: only you can edit; other peers see your "
               "edits live but cannot contribute. The hat (= editing "
               "rights) can be passed to a peer later, one at a time. "
               "Useful for tutorials, demos, and walkthroughs."));
        QPushButton *editBtn = mb.addButton(tr("Edit"), QMessageBox::AcceptRole);
        QPushButton *showBtn = mb.addButton(tr("Show"), QMessageBox::AcceptRole);
        mb.addButton(QMessageBox::Cancel);
        mb.setDefaultButton(editBtn);
        mb.exec();
        QAbstractButton *clicked = mb.clickedButton();
        if (clicked == editBtn) return LanLiveSession::SessionMode::Edit;
        if (clicked == showBtn) return LanLiveSession::SessionMode::Show;
        return std::nullopt;
    };

    QAction *lanStartAction = new QAction(tr("Start LAN Live Session..."), this);
    lanStartAction->setStatusTip(tr("Host a real-time co-editing session for peers on the same local network."));
    collabMenu->addAction(lanStartAction);
    connect(lanStartAction, &QAction::triggered, this, [this, prepareHostFile, pickSessionMode]() {
        if (!file) {
            QMessageBox::information(this, tr("LAN Live Session"),
                tr("Open a MIDI file first — there's nothing to sync yet."));
            return;
        }
        auto mode = pickSessionMode(tr("LAN Live Session"));
        if (!mode) return;  // user cancelled
        if (!prepareHostFile(tr("LAN Live Session"))) return;
        LanLiveSession *svc = LanLiveSession::instance();
        QString code = svc->startHosting(file, *mode);
        if (code.isEmpty()) {
            QMessageBox::warning(this, tr("LAN Live Session"),
                tr("Could not start the LAN session (TCP port or multicast bind failed)."));
            return;
        }
        // Non-modal — user keeps editing while the dialog is up.
        LanLiveStartDialog *dlg = new LanLiveStartDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });

    QAction *lanJoinAction = new QAction(tr("Join LAN Live Session..."), this);
    lanJoinAction->setStatusTip(tr("Connect to a peer on the same local network using their pairing code."));
    collabMenu->addAction(lanJoinAction);
    connect(lanJoinAction, &QAction::triggered, this, [this]() {
        // file may be null — host will offer to ship its .mid in that case.
        LanLiveJoinDialog dlg(file, this);
        dlg.exec();
    });

    // File-transfer-on-join: host always ships its .mid so the joining peer
    // ends up editing the host's file. We never touch the user's existing
    // files on disk — bytes are written to a private per-session cache dir,
    // and the open file (if any) is detached from the LAN session before
    // the swap so the about-to-be-deleted MidiFile pointer never lingers in
    // LanLiveSession::_file.
    connect(LanLiveSession::instance(), &LanLiveSession::fileTransferOffered,
            this, [this](const QString &filename, const QByteArray &bytes) {
                QString prompt = file
                    ? tr("The host wants to share this MIDI file:\n\n%1\n\n"
                         "Open it? It will be saved to your shared folder as "
                         "\"<name>_shared.mid\" — your currently loaded file is "
                         "not touched.\nChoosing No ends the session.").arg(filename)
                    : tr("The host wants to share this MIDI file with you:\n\n%1\n\n"
                         "Open it? It will be saved to your shared folder as "
                         "\"<name>_shared.mid\".\nChoosing No ends the session — "
                         "there's nothing to edit without a file.").arg(filename);
                auto answer = QMessageBox::question(
                    this, tr("LAN Live Session"), prompt,
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes);
                if (answer != QMessageBox::Yes) {
                    LanLiveSession::instance()->leaveSession();
                    return;
                }

                // Shared-files folder under Documents/MidiEditor_AI/shared/. Files
                // get a "_shared" suffix in the basename so the user can tell
                // at a glance they came from a peer rather than from their
                // own work. We never overwrite the user's own files because
                // this directory is only ever written to by the LAN flow.
                QString sharedRoot = QDir(QStandardPaths::writableLocation(
                                              QStandardPaths::DocumentsLocation))
                                         .filePath(QStringLiteral("MidiEditor_AI/shared"));
                QDir().mkpath(sharedRoot);

                // Sanitize the filename to avoid path traversal in case the
                // host sent something malicious, and append "_shared" to the
                // stem — but only if it's not already there. Without this
                // guard, re-sharing a previously-received file stacks the
                // suffix forever ("foo_shared_shared_shared.mid").
                QString rawName = QFileInfo(filename).fileName();
                if (rawName.isEmpty()) rawName = QStringLiteral("from-host.mid");
                QFileInfo rawInfo(rawName);
                QString stem = rawInfo.completeBaseName();
                QString suffix = rawInfo.suffix();
                // Defense against an older host that bundled MIDI bytes
                // under an import-only suffix (e.g. work-on-copy bug shipped
                // a `.gp3` whose contents were actually MIDI). Detect via
                // the SMF magic and rewrite the suffix to `.mid`, otherwise
                // openFile would route the bytes to GpImporter and fail.
                const bool isMidiBytes = bytes.startsWith("MThd");
                const QString lowerRawSuffix = suffix.toLower();
                static const QSet<QString> kImportOnlySuffixesRecv = {
                    QStringLiteral("gtp"),  QStringLiteral("gp3"),  QStringLiteral("gp4"),
                    QStringLiteral("gp5"),  QStringLiteral("gp6"),  QStringLiteral("gp7"),
                    QStringLiteral("gp8"),  QStringLiteral("gpx"),  QStringLiteral("gp"),
                    QStringLiteral("mml"),  QStringLiteral("3mle"),
                    QStringLiteral("musicxml"), QStringLiteral("xml"), QStringLiteral("mxl"),
                    QStringLiteral("mscz"), QStringLiteral("mscx"),
                };
                if (isMidiBytes && kImportOnlySuffixesRecv.contains(lowerRawSuffix)) {
                    suffix = QStringLiteral("mid");
                }
                QString sharedName;
                if (stem.endsWith(QStringLiteral("_shared"), Qt::CaseInsensitive)) {
                    sharedName = suffix.isEmpty()
                        ? stem
                        : stem + QStringLiteral(".") + suffix;
                } else if (suffix.isEmpty()) {
                    sharedName = stem + QStringLiteral("_shared");
                } else {
                    sharedName = stem + QStringLiteral("_shared.") + suffix;
                }
                QString savePath = QDir(sharedRoot).filePath(sharedName);

                QFile out(savePath);
                if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    QMessageBox::warning(this, tr("LAN Live Session"),
                        tr("Could not write the received MIDI to %1.").arg(savePath));
                    LanLiveSession::instance()->leaveSession();
                    return;
                }
                out.write(bytes);
                out.close();

                // Detach LanLiveSession from the old MidiFile *before*
                // openFile() destroys it. Then re-attach to the new one.
                MidiFile *preOpen = file;
                LanLiveSession::instance()->setActiveFile(nullptr);
                openFile(savePath);
                if (file && file != preOpen) {
                    LanLiveSession::instance()->setActiveFile(file);
                } else {
                    // openFile() failed (parse error, AV blocked write, etc.):
                    // setFile() never ran, so this->file still points at the
                    // pre-swap MidiFile (or is null if we joined empty). Don't
                    // re-attach — that would stream the wrong file to the host.
                    QMessageBox::warning(this, tr("LAN Live Session"),
                        tr("Could not open the received MIDI file. "
                           "The session has been ended."));
                    LanLiveSession::instance()->leaveSession();
                }
            });

    // Returning-peer reconciliation (Phase 9.5g): host-side dialog when
    // a peer rejoins with commits we don't have.
    connect(LanLiveSession::instance(), &LanLiveSession::returningPeerArrived,
            this, [this](const QString &peerName, const QString &peerToken,
                          const PrBundle &bundle) {
                ReturningPeerDialog *dlg = new ReturningPeerDialog(
                    peerName, peerToken, bundle, file, this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();
            });

    // Welcome-back summary on the peer side after a fast-forward or
    // host-decided merge.
    connect(LanLiveSession::instance(), &LanLiveSession::welcomeBackOffered,
            this, [this](const QString &hostName, int acceptedHunkCount,
                          int rejectedCommitCount, const QString &divergedFilePath) {
                WelcomeBackDialog *dlg = new WelcomeBackDialog(
                    hostName, acceptedHunkCount, rejectedCommitCount,
                    divergedFilePath, this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();
            });

    // Phase 9.5i: a host announcement matched a local sidecar — open
    // that file and bind it to LanLiveSession before the TCP connect
    // fires so `hello` carries the right sessionId. Synchronous slot:
    // by the time the emit returns in onPeerFound, the file is loaded
    // and bound, and connectToHost can proceed.
    connect(LanLiveSession::instance(), &LanLiveSession::switchToFileBeforeConnect,
            this, [this](const QString &midiPath) {
                if (file && file->path() == midiPath) return;  // already there
                openFile(midiPath);
                if (file && file->path() == midiPath) {
                    LanLiveSession::instance()->setActiveFile(file);
                    statusBar()->showMessage(
                        tr("Opened local copy of session file before connecting"),
                        4000);
                }
            });

    // The leave action serves both LAN and WAN sessions — its label
    // tracks the active transport so the menu doesn't say "LAN" while
    // you're on WAN. See Plan §11.10k. Placed at the bottom of the
    // collab menu (after Pause/Review entries) so destructive actions
    // sit furthest from the start/join entries that share a click
    // target — see the addAction call further down.
    QAction *lanLeaveAction = new QAction(tr("Leave Live Session"), this);
    lanLeaveAction->setStatusTip(tr("End the current Live Session."));
    connect(lanLeaveAction, &QAction::triggered, this, []() {
        LanLiveSession::instance()->leaveSession();
    });
    auto refreshLeaveLabel = [lanLeaveAction]() {
        LanLiveSession *svc = LanLiveSession::instance();
        switch (svc->transport()) {
            case LanLiveSession::Transport::Lan:
                lanLeaveAction->setText(tr("Leave Local Live Session"));
                lanLeaveAction->setStatusTip(tr("End the current Local Live Session."));
                break;
            case LanLiveSession::Transport::Wan:
                lanLeaveAction->setText(tr("Leave Online Live Session"));
                lanLeaveAction->setStatusTip(tr("End the current Online Live Session."));
                break;
            case LanLiveSession::Transport::None:
                lanLeaveAction->setText(tr("Leave Live Session"));
                lanLeaveAction->setStatusTip(tr("End the current Live Session."));
                break;
        }
    };
    connect(LanLiveSession::instance(), &LanLiveSession::roleChanged,
            this, [refreshLeaveLabel](LanLiveSession::Role) { refreshLeaveLabel(); });
    refreshLeaveLabel();

    // Hoisted to outer scope so the refreshCollabMenu lambda below can
    // grey them out during an active session. Left as nullptr in
    // non-WAN builds; lambda checks before dereferencing.
    QAction *rtcStartAction = nullptr;
    QAction *rtcJoinAction = nullptr;
#ifdef MIDIEDITOR_WEBRTC_ENABLED
    // Phase 9.6 — code-based WAN session entries. The 4-character code
    // is exchanged via the Cloudflare rendezvous worker; once the data
    // channel opens, edits flow over WebRTC peer-to-peer.
    collabMenu->addSeparator();
    rtcStartAction = new QAction(tr("Start WAN Live Session…"), this);
    rtcStartAction->setStatusTip(tr("Host a live session over the internet — "
                                     "share a 4-character code with one peer to connect."));
    collabMenu->addAction(rtcStartAction);
    // Plan §11.10l (Phase 9.7 polish): rendezvous pre-flight. Health-
    // check the configured URL with a 3 s budget before kicking off
    // ICE gathering so an unreachable rendezvous surfaces a clear
    // error in seconds, not after the full ICE+POST timeout (~15 s).
    auto preflightRendezvous = [this](const QString &flowName,
                                       std::function<void()> proceed) {
        statusBar()->showMessage(tr("Checking rendezvous…"));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        auto *probe = new RtcRendezvousClient(this);
        connect(probe, &RtcRendezvousClient::pingResult, this,
            [this, probe, flowName, proceedFn = std::move(proceed)]
            (bool ok, qint64 latencyMs, const QString &reason) mutable {
                QApplication::restoreOverrideCursor();
                statusBar()->clearMessage();
                probe->deleteLater();
                if (ok) {
                    qDebug() << "Rendezvous OK in" << latencyMs << "ms";
                    proceedFn();
                    return;
                }
                QMessageBox::warning(this, flowName,
                    tr("Could not reach the rendezvous service.\n\n"
                       "Reason: %1\n"
                       "URL:    %2\n\n"
                       "Open Settings → Collaboration → WAN Live Session — "
                       "rendezvous to switch to a different URL or clear "
                       "the field to use the bundled default.")
                        .arg(reason, RtcRendezvousClient::configuredUrl()));
            });
        probe->ping();
    };

    connect(rtcStartAction, &QAction::triggered, this,
            [this, preflightRendezvous, prepareHostFile, pickSessionMode]() {
        if (!file) {
            QMessageBox::information(this, tr("WAN Live Session"),
                tr("Open a MIDI file first — there's nothing to sync yet."));
            return;
        }
        auto mode = pickSessionMode(tr("WAN Live Session"));
        if (!mode) return;  // user cancelled
        if (!prepareHostFile(tr("WAN Live Session"))) return;
        preflightRendezvous(tr("WAN Live Session"), [this, mode]() {
            LanLiveSession *svc = LanLiveSession::instance();
            if (!svc->startHostingWan(file, *mode)) {
                QMessageBox::warning(this, tr("WAN Live Session"),
                    tr("Could not start the WAN session (transport setup failed)."));
                return;
            }
            // Non-modal — user keeps editing while the dialog is up.
            WebRtcStartDialog *dlg = new WebRtcStartDialog(this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        });
    });

    rtcJoinAction = new QAction(tr("Join WAN Live Session…"), this);
    rtcJoinAction->setStatusTip(tr("Connect to a peer over the internet using the "
                                    "4-character code they shared with you."));
    collabMenu->addAction(rtcJoinAction);
    connect(rtcJoinAction, &QAction::triggered, this,
            [this, preflightRendezvous]() {
        preflightRendezvous(tr("Join WAN Live Session"), [this]() {
            // file may be null — host will offer to ship its .mid in that case.
            WebRtcJoinDialog dlg(file, this);
            dlg.exec();
        });
    });
#endif

    // Review-mode toggle (§11.10c) — pause auto-apply of incoming live
    // edits so the user can review them in PrReviewDialog before they
    // land. Persisted across sessions via QSettings. Applies to both
    // LAN and WAN sessions; the QSettings key kept its `Collab/lan/`
    // prefix for backwards-compat with users who already enabled it.
    QAction *lanReviewModeAction = new QAction(tr("Pause incoming live edits for review"), this);
    lanReviewModeAction->setCheckable(true);
    lanReviewModeAction->setChecked(LanLiveSession::instance()->isReviewModeEnabled());
    lanReviewModeAction->setStatusTip(
        tr("Queue incoming live edits (LAN and WAN) instead of applying them "
           "immediately. Use 'Review pending live edits…' to inspect and apply."));
    collabMenu->addAction(lanReviewModeAction);
    connect(lanReviewModeAction, &QAction::toggled, this, [](bool on) {
        LanLiveSession::instance()->setReviewMode(on);
    });
    connect(LanLiveSession::instance(), &LanLiveSession::reviewModeChanged,
            this, [lanReviewModeAction](bool enabled) {
                if (lanReviewModeAction->isChecked() != enabled)
                    lanReviewModeAction->setChecked(enabled);
            });

    QAction *lanReviewPendingAction = new QAction(tr("Review pending live edits…"), this);
    lanReviewPendingAction->setStatusTip(
        tr("Open the cherry-pick review dialog for incoming live edits "
           "(LAN or WAN) that have been paused."));
    collabMenu->addAction(lanReviewPendingAction);
    connect(lanReviewPendingAction, &QAction::triggered, this, [this]() {
        LanLiveSession *svc = LanLiveSession::instance();
        if (svc->pendingReviewHunkCount() == 0) {
            QMessageBox::information(this, tr("Review pending live edits"),
                tr("No queued live edits to review."));
            return;
        }
        if (!file) {
            QMessageBox::warning(this, tr("Review pending live edits"),
                tr("Open a MIDI file first."));
            return;
        }
        PrBundle bundle = svc->pendingReviewBundle();
        PrReviewDialog dlg(bundle, file, PrReviewDialog::Mode::PreApply, this);
        if (dlg.exec() == QDialog::Accepted) {
            // PrReviewDialog applied the accepted hunks to our file
            // and recorded the merge in the collab log via
            // CollabService::onFileSaved. We just need to clear the
            // queue and re-baseline so the next sync tick doesn't
            // re-broadcast the just-applied delta to peers.
            svc->acknowledgeReviewApplied();
        }
        // Cancel: keep queue intact for later review.
    });

    // ---- Mid-session mode switch (host-only toggle) ------------------

    collabMenu->addSeparator();

    QAction *switchModeAction = new QAction(tr("Switch session mode"), this);
    switchModeAction->setStatusTip(
        tr("Host-only: flip the active session between Edit (everyone "
           "edits) and Show (you become the presenter, others watch). "
           "Useful for ad-hoc demos in the middle of a co-edit session."));
    switchModeAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_M)));
    _defaultShortcuts["switch_session_mode"] = QList<QKeySequence>() << switchModeAction->shortcut();
    _actionMap["switch_session_mode"] = switchModeAction;
    collabMenu->addAction(switchModeAction);
    connect(switchModeAction, &QAction::triggered, this, [this]() {
        LanLiveSession *svc = LanLiveSession::instance();
        if (svc->role() != LanLiveSession::Role::Hosting) return;
        LanLiveSession::SessionMode target =
            (svc->mode() == LanLiveSession::SessionMode::Show)
                ? LanLiveSession::SessionMode::Edit
                : LanLiveSession::SessionMode::Show;
        svc->switchSessionMode(target);
    });

    // ---- Hat-pass actions (Phase 9.9d §15.2) ------------------------

    collabMenu->addSeparator();

    // "Pass the hat to..." — visible only while we hold the hat.
    QAction *passHatAction = new QAction(tr("Pass the hat to..."), this);
    passHatAction->setStatusTip(
        tr("Show Mode: hand editing rights to a peer. Only the current "
           "presenter can transfer the hat."));
    collabMenu->addAction(passHatAction);
    connect(passHatAction, &QAction::triggered, this, [this]() {
        LanLiveSession *svc = LanLiveSession::instance();
        auto peers = svc->connectedPeerInfo();
        if (peers.isEmpty()) {
            QMessageBox::information(this, tr("Pass the hat"),
                tr("No peers are currently connected to pass the hat to."));
            return;
        }
        QStringList labels;
        for (const auto &p : peers) labels.append(p.second);
        bool ok = false;
        QString chosen = QInputDialog::getItem(this, tr("Pass the hat"),
            tr("Choose a peer to hand editing rights to:"),
            labels, 0, false, &ok);
        if (!ok || chosen.isEmpty()) return;
        int idx = labels.indexOf(chosen);
        if (idx < 0) return;
        svc->transferHatTo(peers[idx].first, peers[idx].second);
    });

    // "Yield the hat" — presenter (non-host) gives the hat back to
    // the host without a specific successor. The host then becomes
    // presenter and can re-distribute or hold.
    QAction *yieldHatAction = new QAction(tr("Yield the hat"), this);
    yieldHatAction->setStatusTip(
        tr("Show Mode: hand the hat back to the host without picking a "
           "specific successor. Useful when you're done presenting and "
           "have no particular next presenter in mind."));
    collabMenu->addAction(yieldHatAction);
    connect(yieldHatAction, &QAction::triggered, this, [this]() {
        LanLiveSession::instance()->yieldHat();
    });

    // "Request the hat" — visible only while we are a viewer.
    QAction *requestHatAction = new QAction(tr("Request the hat"), this);
    requestHatAction->setStatusTip(
        tr("Show Mode: ask the current presenter for editing rights. "
           "They will see a prompt and can accept or decline."));
    collabMenu->addAction(requestHatAction);
    connect(requestHatAction, &QAction::triggered, this, [this]() {
        LanLiveSession::instance()->requestHat();
        statusBar()->showMessage(tr("Hat request sent to the presenter."), 4000);
    });

    // "Take the hat" — host-only privilege when the presenter has been
    // silent past the heartbeat deadline.
    QAction *takeHatAction = new QAction(tr("Take the hat"), this);
    takeHatAction->setStatusTip(
        tr("Show Mode: host privilege — reclaim the hat when the current "
           "presenter has stopped responding."));
    collabMenu->addAction(takeHatAction);
    connect(takeHatAction, &QAction::triggered, this, [this]() {
        LanLiveSession::instance()->hostTakeHat();
    });

    // Presenter notification on incoming requestHat.
    connect(LanLiveSession::instance(), &LanLiveSession::hatRequested,
            this, [this](const QString &reqName, const QString &reqMid) {
                LanLiveSession *svc = LanLiveSession::instance();
                if (!svc->isPresenter()) return;  // not for us
                QMessageBox mb(this);
                mb.setIcon(QMessageBox::Question);
                mb.setWindowTitle(tr("Hat request"));
                mb.setText(tr("%1 is requesting the hat.").arg(reqName));
                mb.setInformativeText(
                    tr("Accept to hand them editing rights, or decline "
                       "to keep the hat."));
                QPushButton *acceptBtn = mb.addButton(tr("Accept"), QMessageBox::AcceptRole);
                mb.addButton(tr("Decline"), QMessageBox::RejectRole);
                mb.setDefaultButton(acceptBtn);
                mb.exec();
                if (mb.clickedButton() == acceptBtn) {
                    svc->transferHatTo(reqMid, reqName);
                } else {
                    statusBar()->showMessage(
                        tr("Hat request from %1 declined.").arg(reqName), 4000);
                }
            });

    // Transfer failed (e.g. target peer disconnected) — toast it.
    connect(LanLiveSession::instance(), &LanLiveSession::hatTransferRejected,
            this, [this](const QString &reason) {
                statusBar()->showMessage(reason, 6000);
            });

    // Successful transfer — toast on every peer (status bar already
    // refreshes via the connection wired earlier).
    connect(LanLiveSession::instance(), &LanLiveSession::hatTransferred,
            this, [this](const QString &mid, const QString &name, const QString &reason) {
                Q_UNUSED(mid);
                if (reason == QLatin1String("host-takeover")) {
                    statusBar()->showMessage(
                        tr("Host took the hat (presenter was silent)."), 4000);
                } else if (reason == QLatin1String("yield")) {
                    statusBar()->showMessage(
                        tr("Presenter yielded the hat back to %1.").arg(name), 4000);
                } else {
                    statusBar()->showMessage(
                        tr("Hat is now held by %1.").arg(name), 4000);
                }
            });

    // ---- v1.7.2 §15.4: version-mismatch warnings -------------------
    //
    // Three distinct cases, each with a tailored message:
    //   1. Host (us) sees a joiner with empty appVersion → pre-1.7.2.
    //      That peer's UI has NO reverse warning, so the local user
    //      is the only one who can act. Long timeout (12 s) so they
    //      have time to read + act.
    //   2. Host sees a joiner on a different but-not-empty version
    //      → both sides will see warnings, lower urgency, 8 s.
    //   3. Joiner detects a legacy host (no sessionWelcome at all)
    //      OR a mismatched-version host. The legacy case carries
    //      the same "older than 1.7.2" emphasis as case 1.

    // bugfix 2026-05-21: in-flight statusMessage emits from the
    // reconciliation / snapshot / sidecar paths land in the status bar
    // RIGHT AFTER our version warning and overwrite it. Two-pronged fix:
    //   - Blind-generation case (peerVer empty / legacy host) ⇒ modal
    //     QMessageBox::information. Important enough to acknowledge,
    //     and modal can't be overwritten.
    //   - Matched-but-different versions ⇒ status bar with a small
    //     delay so the other immediate emits land first.

    connect(LanLiveSession::instance(), &LanLiveSession::peerVersionMismatch,
            this, [this](const QString &peerName, const QString &,
                         const QString &peerVer, const QString &ourVer) {
                if (peerVer.isEmpty()) {
                    // Pre-1.7.2 "blind generation" — modal so the host
                    // user has to acknowledge. They're the only one
                    // who can act because the joiner's build has no
                    // reverse-warning code.
                    QMessageBox::information(this,
                        tr("Older peer connected"),
                        tr("Peer %1 is on an older build (pre-%2).\n\n"
                           "They will not see chat messages, Show Mode "
                           "locks, or version warnings on their side. "
                           "Their local edits won't reach the rest of the "
                           "session if you're in Show Mode.\n\n"
                           "Consider asking them to update and reconnect.")
                            .arg(peerName, ourVer));
                } else {
                    QString msg = tr("Peer %1 is on v%2 — you are on v%3. "
                                     "Some features may behave differently "
                                     "between the two builds.")
                                      .arg(peerName, peerVer, ourVer);
                    QTimer::singleShot(600, this, [this, msg]() {
                        statusBar()->showMessage(msg, 12000);
                    });
                }
            });

    connect(LanLiveSession::instance(), &LanLiveSession::hostVersionMismatch,
            this, [this](const QString &hostVer, const QString &ourVer) {
                QString msg = tr("Host is on v%1 — you are on v%2. Some "
                                 "features may behave differently between "
                                 "the two builds.")
                                  .arg(hostVer, ourVer);
                QTimer::singleShot(600, this, [this, msg]() {
                    statusBar()->showMessage(msg, 12000);
                });
            });

    connect(LanLiveSession::instance(), &LanLiveSession::legacyHostDetected,
            this, [this]() {
                QString ourVer;
#ifdef MIDIEDITOR_RELEASE_VERSION_STRING_DEF
                ourVer = QStringLiteral(MIDIEDITOR_RELEASE_VERSION_STRING_DEF);
#endif
                // Pre-1.7.2 host — modal, same rationale as the
                // peer-side blind-generation case above. Legacy host
                // won't see ANY of our warnings either, so the local
                // user is the only one who can act.
                QMessageBox::information(this,
                    tr("Host is on an older build"),
                    tr("Host is on an older build (pre-%1).\n\n"
                       "Chat and Show Mode features won't work — they "
                       "also can't see any warnings on their side.\n\n"
                       "Ask them to update and rejoin to get the full "
                       "experience.")
                        .arg(ourVer.isEmpty() ? QStringLiteral("1.7.2") : ourVer));
            });

    // Leave Live Session at the very bottom of the collab menu so the
    // destructive action sits furthest from accidental clicks.
    collabMenu->addSeparator();
    collabMenu->addAction(lanLeaveAction);

    auto refreshCollabMenu = [collabMenu, collabCreatePrAction, collabCompactAction, collabImportPrAction,
                              lanStartAction, lanJoinAction, lanLeaveAction,
                              rtcStartAction, rtcJoinAction,
                              lanReviewModeAction, lanReviewPendingAction,
                              passHatAction, requestHatAction, takeHatAction,
                              switchModeAction, yieldHatAction]() {
        CollabService *svc = CollabService::instance();
        bool enabled = svc->isEnabled();
        collabMenu->menuAction()->setVisible(enabled);
        // Plan §11.10n: Create PR / Compact don't require pre-init —
        // they auto-init on first use. Stay enabled whenever there's
        // a file open. (Compact still requires _initialized to do
        // anything useful, but its handler already short-circuits in
        // that case, and the user-visible action shouldn't grey out
        // before they ever clicked it.)
        collabCreatePrAction->setEnabled(enabled && svc->hasCurrentFile());
        collabCompactAction->setEnabled(enabled && svc->isCurrentFileInitialized());
        collabImportPrAction->setEnabled(enabled && svc->hasCurrentFile());

        LanLiveSession::Role lanRole = LanLiveSession::instance()->role();
        bool lanIdle = (lanRole == LanLiveSession::Role::Idle);
        // Hosting still needs a file (we ship its bytes to clients), but
        // joining is always available — the host will provide the .mid.
        lanStartAction->setEnabled(enabled && lanIdle && svc->hasCurrentFile());
        lanJoinAction->setEnabled(enabled && lanIdle);
        // 2026-05-24: WAN equivalents were missing from the enable
        // gate, so they stayed clickable during an active session.
        // Nullptr-guarded so non-WAN builds still compile.
        if (rtcStartAction) rtcStartAction->setEnabled(enabled && lanIdle && svc->hasCurrentFile());
        if (rtcJoinAction)  rtcJoinAction->setEnabled(enabled && lanIdle);
        lanLeaveAction->setEnabled(enabled && !lanIdle);
        lanReviewModeAction->setEnabled(enabled);
        lanReviewPendingAction->setEnabled(
            enabled && LanLiveSession::instance()->pendingReviewHunkCount() > 0);

        // Phase 9.9d §15.2: hat-pass action visibility. Each action
        // hides entirely when not applicable so the menu doesn't grow
        // a wall of perpetually-greyed-out entries in Edit mode.
        LanLiveSession *live = LanLiveSession::instance();
        bool inShow = (!lanIdle
                       && live->mode() == LanLiveSession::SessionMode::Show);
        bool localIsPresenter = inShow && live->isPresenter();
        bool localIsViewer = inShow && !localIsPresenter;
        bool isHost = (lanRole == LanLiveSession::Role::Hosting);

        passHatAction->setVisible(localIsPresenter);
        passHatAction->setEnabled(localIsPresenter && live->peerCount() > 0);

        // Yield-the-hat: visible when we're presenter AND not host
        // (yield-to-self is meaningless — host can just keep the hat).
        yieldHatAction->setVisible(localIsPresenter && !isHost);
        yieldHatAction->setEnabled(localIsPresenter && !isHost);

        requestHatAction->setVisible(localIsViewer);
        requestHatAction->setEnabled(localIsViewer);

        takeHatAction->setVisible(inShow && isHost && !localIsPresenter);
        takeHatAction->setEnabled(live->hostCanTakeHat());

        // Mid-session mode-switch toggle (host-only). Text reflects
        // the target mode so the user reads what's about to happen.
        switchModeAction->setVisible(!lanIdle && isHost);
        switchModeAction->setEnabled(!lanIdle && isHost);
        switchModeAction->setText(inShow
            ? tr("Switch to Edit mode")
            : tr("Switch to Show mode"));
    };
    // Pending count changes during a session — refresh the menu so
    // "Review pending…" toggles enabled/disabled as hunks arrive.
    connect(LanLiveSession::instance(), &LanLiveSession::pendingReviewChanged,
            this, [refreshCollabMenu](int) { refreshCollabMenu(); });
    QObject::connect(LanLiveSession::instance(), &LanLiveSession::roleChanged,
                     this, [refreshCollabMenu]() { refreshCollabMenu(); });
    // Phase 9.9d §15.2: refresh the menu when the hat moves or the
    // peer list changes — passHatAction needs ≥1 peer to be enabled,
    // takeHatAction's enable state derives from hostCanTakeHat which
    // itself watches the peer list.
    connect(LanLiveSession::instance(), &LanLiveSession::hatTransferred,
            this, [refreshCollabMenu](const QString &, const QString &, const QString &) {
                refreshCollabMenu();
            });
    connect(LanLiveSession::instance(), &LanLiveSession::peerCountChanged,
            this, [refreshCollabMenu](int) { refreshCollabMenu(); });
    // 2026-05-24 bugfix: also refresh on session-mode flips so the
    // Switch-to-Show/Edit text + Pass/Yield/Request/Take hat visibility
    // all flip live when the host toggles via Ctrl+Shift+M.
    connect(LanLiveSession::instance(), &LanLiveSession::sessionModeChanged,
            this, [refreshCollabMenu]() { refreshCollabMenu(); });
    refreshCollabMenu();
    connect(CollabService::instance(), &CollabService::enabledChanged, this,
            [refreshCollabMenu](bool) { refreshCollabMenu(); });
    connect(CollabService::instance(), &CollabService::currentFileStateChanged, this,
            refreshCollabMenu);
#endif

    fileMB->addSeparator();

    QAction *quitAction = new QAction(tr("Quit"), this);
    quitAction->setShortcut(QKeySequence::Quit);
    _defaultShortcuts["quit"] = QList<QKeySequence>() << quitAction->shortcut();
    Appearance::setActionIcon(quitAction, ":/run_environment/graphics/tool/noicon.png");
    connect(quitAction, SIGNAL(triggered()), this, SLOT(close()));
    fileMB->addAction(quitAction);
    _actionMap["quit"] = quitAction;

    // Edit
    undoAction = new QAction(tr("Undo"), this);
    undoAction->setShortcut(QKeySequence::Undo);
    _defaultShortcuts["undo"] = QList<QKeySequence>() << undoAction->shortcut();
    Appearance::setActionIcon(undoAction, ":/run_environment/graphics/tool/undo.png");
    connect(undoAction, SIGNAL(triggered()), this, SLOT(undo()));
    editMB->addAction(undoAction);
    _actionMap["undo"] = undoAction;

    redoAction = new QAction(tr("Redo"), this);
    redoAction->setShortcut(QKeySequence::Redo);
    _defaultShortcuts["redo"] = QList<QKeySequence>() << redoAction->shortcut();
    Appearance::setActionIcon(redoAction, ":/run_environment/graphics/tool/redo.png");
    connect(redoAction, SIGNAL(triggered()), this, SLOT(redo()));
    editMB->addAction(redoAction);
    _actionMap["redo"] = redoAction;

    editMB->addSeparator();

    QAction *selectAllAction = new QAction(tr("Select all"), this);
    selectAllAction->setToolTip(tr("Select all visible events"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    _defaultShortcuts["select_all"] = QList<QKeySequence>() << selectAllAction->shortcut();
    connect(selectAllAction, SIGNAL(triggered()), this, SLOT(selectAll()));
    editMB->addAction(selectAllAction);
    _actionMap["select_all"] = selectAllAction;

    _selectAllFromChannelMenu = new QMenu(tr("Select all events from channel..."), editMB);
    editMB->addMenu(_selectAllFromChannelMenu);
    connect(_selectAllFromChannelMenu, SIGNAL(triggered(QAction*)), this, SLOT(selectAllFromChannel(QAction*)));

    for (int i = 0; i < 16; i++) {
        QVariant variant(i);
        QAction *delChannelAction = new QAction(QString::number(i), this);
        delChannelAction->setData(variant);
        _selectAllFromChannelMenu->addAction(delChannelAction);
    }

    _selectAllFromTrackMenu = new QMenu(tr("Select all events from track..."), editMB);
    editMB->addMenu(_selectAllFromTrackMenu);
    connect(_selectAllFromTrackMenu, SIGNAL(triggered(QAction*)), this, SLOT(selectAllFromTrack(QAction*)));

    for (int i = 0; i < 16; i++) {
        QVariant variant(i);
        QAction *delChannelAction = new QAction(QString::number(i), this);
        delChannelAction->setData(variant);
        _selectAllFromTrackMenu->addAction(delChannelAction);
    }

    editMB->addSeparator();

    QAction *navigateSelectionUpAction = new QAction(tr("Navigate selection up"), editMB);
    navigateSelectionUpAction->setShortcut(QKeySequence(Qt::Key_Up));
    _defaultShortcuts["navigate_up"] = QList<QKeySequence>() << navigateSelectionUpAction->shortcut();
    connect(navigateSelectionUpAction, SIGNAL(triggered()), this, SLOT(navigateSelectionUp()));
    editMB->addAction(navigateSelectionUpAction);
    _actionMap["navigate_up"] = navigateSelectionUpAction;

    QAction *navigateSelectionDownAction = new QAction(tr("Navigate selection down"), editMB);
    navigateSelectionDownAction->setShortcut(QKeySequence(Qt::Key_Down));
    _defaultShortcuts["navigate_down"] = QList<QKeySequence>() << navigateSelectionDownAction->shortcut();
    connect(navigateSelectionDownAction, SIGNAL(triggered()), this, SLOT(navigateSelectionDown()));
    editMB->addAction(navigateSelectionDownAction);
    _actionMap["navigate_down"] = navigateSelectionDownAction;

    QAction *navigateSelectionLeftAction = new QAction(tr("Navigate selection left"), editMB);
    navigateSelectionLeftAction->setShortcut(QKeySequence(Qt::Key_Left));
    _defaultShortcuts["navigate_left"] = QList<QKeySequence>() << navigateSelectionLeftAction->shortcut();
    connect(navigateSelectionLeftAction, SIGNAL(triggered()), this, SLOT(navigateSelectionLeft()));
    editMB->addAction(navigateSelectionLeftAction);
    _actionMap["navigate_left"] = navigateSelectionLeftAction;

    QAction *navigateSelectionRightAction = new QAction(tr("Navigate selection right"), editMB);
    navigateSelectionRightAction->setShortcut(QKeySequence(Qt::Key_Right));
    _defaultShortcuts["navigate_right"] = QList<QKeySequence>() << navigateSelectionRightAction->shortcut();
    connect(navigateSelectionRightAction, SIGNAL(triggered()), this, SLOT(navigateSelectionRight()));
    editMB->addAction(navigateSelectionRightAction);
    _actionMap["navigate_right"] = navigateSelectionRightAction;

    editMB->addSeparator();

    QAction *copyAction = new QAction(tr("Copy events"), this);
    _activateWithSelections.append(copyAction);
    Appearance::setActionIcon(copyAction, ":/run_environment/graphics/tool/copy.png");
    copyAction->setShortcut(QKeySequence::Copy);
    _defaultShortcuts["copy"] = QList<QKeySequence>() << copyAction->shortcut();
    connect(copyAction, SIGNAL(triggered()), this, SLOT(copy()));
    editMB->addAction(copyAction);
    _actionMap["copy"] = copyAction;

    _pasteAction = new QAction(tr("Paste events"), this);
    _pasteAction->setToolTip(tr("Paste events at cursor position"));
    _pasteAction->setShortcut(QKeySequence::Paste);
    _defaultShortcuts["paste"] = QList<QKeySequence>() << _pasteAction->shortcut();
    Appearance::setActionIcon(_pasteAction, ":/run_environment/graphics/tool/paste.png");
    connect(_pasteAction, SIGNAL(triggered()), this, SLOT(paste()));
    _actionMap["paste"] = _pasteAction;

    _pasteToTrackMenu = new QMenu(tr("Paste to track..."));
    _pasteToChannelMenu = new QMenu(tr("Paste to channel..."));
    _pasteOptionsMenu = new QMenu(tr("Paste options..."));
    _pasteOptionsMenu->addMenu(_pasteToChannelMenu);
    QActionGroup *pasteChannelGroup = new QActionGroup(this);
    pasteChannelGroup->setExclusive(true);
    connect(_pasteToChannelMenu, SIGNAL(triggered(QAction*)), this, SLOT(pasteToChannel(QAction*)));
    connect(_pasteToTrackMenu, SIGNAL(triggered(QAction*)), this, SLOT(pasteToTrack(QAction*)));

    for (int i = -2; i < 16; i++) {
        QVariant variant(i);
        QString text = QString::number(i);
        if (i == -2) {
            text = tr("Same as selected for new events");
        }
        if (i == -1) {
            text = tr("Keep channel");
        }
        QAction *pasteToChannelAction = new QAction(text, this);
        pasteToChannelAction->setData(variant);
        pasteToChannelAction->setCheckable(true);
        _pasteToChannelMenu->addAction(pasteToChannelAction);
        pasteChannelGroup->addAction(pasteToChannelAction);
        pasteToChannelAction->setChecked(i < 0);
    }
    _pasteOptionsMenu->addMenu(_pasteToTrackMenu);
    editMB->addAction(_pasteAction);
    editMB->addMenu(_pasteOptionsMenu);

    QAction *pasteSpecialAction = new QAction(tr("Paste Special..."), this);
    pasteSpecialAction->setToolTip(tr("Choose how to map cross-instance clipboard tracks/channels"));
    connect(pasteSpecialAction, &QAction::triggered, this, &MainWindow::pasteSpecial);
    editMB->addAction(pasteSpecialAction);
    _actionMap["pasteSpecial"] = pasteSpecialAction;

    editMB->addSeparator();

    QAction *askMidiPilotAction = new QAction(tr("Ask MidiPilot..."), this);
    Appearance::setActionIcon(askMidiPilotAction, ":/run_environment/graphics/tool/midipilot.png");
    connect(askMidiPilotAction, &QAction::triggered, this, [this]() {
        if (_midiPilotDock) {
            _midiPilotDock->setVisible(true);
            _midiPilotDock->raise();
        }
        if (_midiPilotWidget) {
            _midiPilotWidget->focusInput();
        }
    });
    editMB->addAction(askMidiPilotAction);
    _actionMap["ask_midipilot"] = askMidiPilotAction;

    editMB->addSeparator();

    QAction *configAction = new QAction(tr("Settings"), this);
    Appearance::setActionIcon(configAction, ":/run_environment/graphics/tool/config.png");
    connect(configAction, SIGNAL(triggered()), this, SLOT(openConfig()));
    editMB->addAction(configAction);

    // Tools
    QMenu *toolsToolsMenu = new QMenu(tr("Current tool..."), toolsMB);
    StandardTool *tool = new StandardTool();
    Tool::setCurrentTool(tool);
    stdToolAction = new ToolButton(tool, QKeySequence(Qt::Key_F1), toolsToolsMenu);
    _defaultShortcuts["standard_tool"] = QList<QKeySequence>() << stdToolAction->shortcut();
    toolsToolsMenu->addAction(stdToolAction);
    tool->buttonClick();
    _actionMap["standard_tool"] = stdToolAction;

    QAction *newNoteAction = new ToolButton(new NewNoteTool(), QKeySequence(Qt::Key_F2), toolsToolsMenu);
    _defaultShortcuts["new_note"] = QList<QKeySequence>() << newNoteAction->shortcut();
    toolsToolsMenu->addAction(newNoteAction);
    _actionMap["new_note"] = newNoteAction;
    QAction *removeNotesAction = new ToolButton(new EraserTool(), QKeySequence(Qt::Key_F3), toolsToolsMenu);
    _defaultShortcuts["remove_notes"] = QList<QKeySequence>() << removeNotesAction->shortcut();
    toolsToolsMenu->addAction(removeNotesAction);
    _actionMap["remove_notes"] = removeNotesAction;

    toolsToolsMenu->addSeparator();

    QAction *selectSingleAction = new ToolButton(new SelectTool(SELECTION_TYPE_SINGLE), QKeySequence(Qt::Key_F4), toolsToolsMenu);
    _defaultShortcuts["select_single"] = QList<QKeySequence>() << selectSingleAction->shortcut();
    toolsToolsMenu->addAction(selectSingleAction);
    _actionMap["select_single"] = selectSingleAction;
    QAction *selectBoxAction = new ToolButton(new SelectTool(SELECTION_TYPE_BOX), QKeySequence(Qt::Key_F5), toolsToolsMenu);
    _defaultShortcuts["select_box"] = QList<QKeySequence>() << selectBoxAction->shortcut();
    toolsToolsMenu->addAction(selectBoxAction);
    _actionMap["select_box"] = selectBoxAction;
    QAction *selectLeftAction = new ToolButton(new SelectTool(SELECTION_TYPE_LEFT), QKeySequence(Qt::Key_F6), toolsToolsMenu);
    _defaultShortcuts["select_left"] = QList<QKeySequence>() << selectLeftAction->shortcut();
    toolsToolsMenu->addAction(selectLeftAction);
    _actionMap["select_left"] = selectLeftAction;
    QAction *selectRightAction = new ToolButton(new SelectTool(SELECTION_TYPE_RIGHT), QKeySequence(Qt::Key_F7), toolsToolsMenu);
    _defaultShortcuts["select_right"] = QList<QKeySequence>() << selectRightAction->shortcut();
    toolsToolsMenu->addAction(selectRightAction);
    _actionMap["select_right"] = selectRightAction;

    toolsToolsMenu->addSeparator();

    QAction *moveAllAction = new ToolButton(new EventMoveTool(true, true), QKeySequence(Qt::Key_F8), toolsToolsMenu);
    _defaultShortcuts["move_all"] = QList<QKeySequence>() << moveAllAction->shortcut();
    _activateWithSelections.append(moveAllAction);
    toolsToolsMenu->addAction(moveAllAction);
    _actionMap["move_all"] = moveAllAction;

    QAction *moveLRAction = new ToolButton(new EventMoveTool(false, true), QKeySequence(Qt::Key_F9), toolsToolsMenu);
    _defaultShortcuts["move_lr"] = QList<QKeySequence>() << moveLRAction->shortcut();
    _activateWithSelections.append(moveLRAction);
    toolsToolsMenu->addAction(moveLRAction);
    _actionMap["move_lr"] = moveLRAction;

    QAction *moveUDAction = new ToolButton(new EventMoveTool(true, false), QKeySequence(Qt::Key_F10), toolsToolsMenu);
    _defaultShortcuts["move_ud"] = QList<QKeySequence>() << moveUDAction->shortcut();
    _activateWithSelections.append(moveUDAction);
    toolsToolsMenu->addAction(moveUDAction);
    _actionMap["move_ud"] = moveUDAction;

    QAction *sizeChangeAction = new ToolButton(new SizeChangeTool(), QKeySequence(Qt::Key_F11), toolsToolsMenu);
    _defaultShortcuts["size_change"] = QList<QKeySequence>() << sizeChangeAction->shortcut();
    _activateWithSelections.append(sizeChangeAction);
    toolsToolsMenu->addAction(sizeChangeAction);
    _actionMap["size_change"] = sizeChangeAction;

    toolsToolsMenu->addSeparator();

    QAction *measureAction = new ToolButton(new MeasureTool(), QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_F1)), toolsToolsMenu);
    _defaultShortcuts["measure"] = QList<QKeySequence>() << measureAction->shortcut();
    toolsToolsMenu->addAction(measureAction);
    _actionMap["measure"] = measureAction;
    QAction *timeSignatureAction = new ToolButton(new TimeSignatureTool(), QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_F2)), toolsToolsMenu);
    _defaultShortcuts["time_signature"] = QList<QKeySequence>() << timeSignatureAction->shortcut();
    toolsToolsMenu->addAction(timeSignatureAction);
    _actionMap["time_signature"] = timeSignatureAction;
    QAction *tempoAction = new ToolButton(new TempoTool(), QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_F3)), toolsToolsMenu);
    _defaultShortcuts["tempo"] = QList<QKeySequence>() << tempoAction->shortcut();
    toolsToolsMenu->addAction(tempoAction);
    _actionMap["tempo"] = tempoAction;

    toolsMB->addMenu(toolsToolsMenu);

    // Tweak

    QMenu *tweakMenu = new QMenu(tr("Tweak..."), toolsMB);

    QAction *tweakTimeAction = new QAction(tr("Time"), tweakMenu);
    tweakTimeAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_1)));
    _defaultShortcuts["tweak_time"] = QList<QKeySequence>() << tweakTimeAction->shortcut();
    tweakTimeAction->setCheckable(true);
    connect(tweakTimeAction, SIGNAL(triggered()), this, SLOT(tweakTime()));
    tweakMenu->addAction(tweakTimeAction);
    _actionMap["tweak_time"] = tweakTimeAction;

    QAction *tweakStartTimeAction = new QAction(tr("Start time"), tweakMenu);
    tweakStartTimeAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_2)));
    _defaultShortcuts["tweak_start_time"] = QList<QKeySequence>() << tweakStartTimeAction->shortcut();
    tweakStartTimeAction->setCheckable(true);
    connect(tweakStartTimeAction, SIGNAL(triggered()), this, SLOT(tweakStartTime()));
    tweakMenu->addAction(tweakStartTimeAction);
    _actionMap["tweak_start_time"] = tweakStartTimeAction;

    QAction *tweakEndTimeAction = new QAction(tr("End time"), tweakMenu);
    tweakEndTimeAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_3)));
    _defaultShortcuts["tweak_end_time"] = QList<QKeySequence>() << tweakEndTimeAction->shortcut();
    tweakEndTimeAction->setCheckable(true);
    connect(tweakEndTimeAction, SIGNAL(triggered()), this, SLOT(tweakEndTime()));
    tweakMenu->addAction(tweakEndTimeAction);
    _actionMap["tweak_end_time"] = tweakEndTimeAction;

    QAction *tweakNoteAction = new QAction(tr("Note"), tweakMenu);
    tweakNoteAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_4)));
    _defaultShortcuts["tweak_note"] = QList<QKeySequence>() << tweakNoteAction->shortcut();
    tweakNoteAction->setCheckable(true);
    connect(tweakNoteAction, SIGNAL(triggered()), this, SLOT(tweakNote()));
    tweakMenu->addAction(tweakNoteAction);
    _actionMap["tweak_note"] = tweakNoteAction;

    QAction *tweakValueAction = new QAction(tr("Value"), tweakMenu);
    tweakValueAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_5)));
    _defaultShortcuts["tweak_value"] = QList<QKeySequence>() << tweakValueAction->shortcut();
    tweakValueAction->setCheckable(true);
    connect(tweakValueAction, SIGNAL(triggered()), this, SLOT(tweakValue()));
    tweakMenu->addAction(tweakValueAction);
    _actionMap["tweak_value"] = tweakValueAction;

    QActionGroup *tweakTargetActionGroup = new QActionGroup(this);
    tweakTargetActionGroup->setExclusive(true);
    tweakTargetActionGroup->addAction(tweakTimeAction);
    tweakTargetActionGroup->addAction(tweakStartTimeAction);
    tweakTargetActionGroup->addAction(tweakEndTimeAction);
    tweakTargetActionGroup->addAction(tweakNoteAction);
    tweakTargetActionGroup->addAction(tweakValueAction);
    tweakTimeAction->setChecked(true);

    tweakMenu->addSeparator();

    QAction *tweakSmallDecreaseAction = new QAction(tr("Small decrease"), tweakMenu);
    tweakSmallDecreaseAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_9)));
    _defaultShortcuts["tweak_small_decrease"] = QList<QKeySequence>() << tweakSmallDecreaseAction->shortcut();
    connect(tweakSmallDecreaseAction, SIGNAL(triggered()), this, SLOT(tweakSmallDecrease()));
    tweakMenu->addAction(tweakSmallDecreaseAction);
    _actionMap["tweak_small_decrease"] = tweakSmallDecreaseAction;

    QAction *tweakSmallIncreaseAction = new QAction(tr("Small increase"), tweakMenu);
    tweakSmallIncreaseAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_0)));
    _defaultShortcuts["tweak_small_increase"] = QList<QKeySequence>() << tweakSmallIncreaseAction->shortcut();
    connect(tweakSmallIncreaseAction, SIGNAL(triggered()), this, SLOT(tweakSmallIncrease()));
    tweakMenu->addAction(tweakSmallIncreaseAction);
    _actionMap["tweak_small_increase"] = tweakSmallIncreaseAction;

    QAction *tweakMediumDecreaseAction = new QAction(tr("Medium decrease"), tweakMenu);
    tweakMediumDecreaseAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL | Qt::ALT, Qt::Key_9)));
    _defaultShortcuts["tweak_medium_decrease"] = QList<QKeySequence>() << tweakMediumDecreaseAction->shortcut();
    connect(tweakMediumDecreaseAction, SIGNAL(triggered()), this, SLOT(tweakMediumDecrease()));
    tweakMenu->addAction(tweakMediumDecreaseAction);
    _actionMap["tweak_medium_decrease"] = tweakMediumDecreaseAction;

    QAction *tweakMediumIncreaseAction = new QAction(tr("Medium increase"), tweakMenu);
    tweakMediumIncreaseAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL | Qt::ALT, Qt::Key_0)));
    _defaultShortcuts["tweak_medium_increase"] = QList<QKeySequence>() << tweakMediumIncreaseAction->shortcut();
    connect(tweakMediumIncreaseAction, SIGNAL(triggered()), this, SLOT(tweakMediumIncrease()));
    tweakMenu->addAction(tweakMediumIncreaseAction);
    _actionMap["tweak_medium_increase"] = tweakMediumIncreaseAction;

    QAction *tweakLargeDecreaseAction = new QAction(tr("Large decrease"), tweakMenu);
    tweakLargeDecreaseAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL | Qt::ALT | Qt::SHIFT, Qt::Key_9)));
    _defaultShortcuts["tweak_large_decrease"] = QList<QKeySequence>() << tweakLargeDecreaseAction->shortcut();
    connect(tweakLargeDecreaseAction, SIGNAL(triggered()), this, SLOT(tweakLargeDecrease()));
    tweakMenu->addAction(tweakLargeDecreaseAction);
    _actionMap["tweak_large_decrease"] = tweakLargeDecreaseAction;

    QAction *tweakLargeIncreaseAction = new QAction(tr("Large increase"), tweakMenu);
    tweakLargeIncreaseAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL | Qt::ALT | Qt::SHIFT, Qt::Key_0)));
    _defaultShortcuts["tweak_large_increase"] = QList<QKeySequence>() << tweakLargeIncreaseAction->shortcut();
    connect(tweakLargeIncreaseAction, SIGNAL(triggered()), this, SLOT(tweakLargeIncrease()));
    tweakMenu->addAction(tweakLargeIncreaseAction);
    _actionMap["tweak_large_increase"] = tweakLargeIncreaseAction;

    toolsMB->addMenu(tweakMenu);

    // Note Duration Presets
    QMenu *noteDurationMB = new QMenu(tr("Note Duration..."), toolsMB);
    QActionGroup *noteDurationGroup = new QActionGroup(this);
    connect(noteDurationGroup, &QActionGroup::triggered, this, &MainWindow::noteDurationSelected);

    QAction *dragModeAction = new QAction(tr("Drag Mode"), this);
    dragModeAction->setShortcut(QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_QuoteLeft)));
    dragModeAction->setData(0);
    dragModeAction->setCheckable(true);
    dragModeAction->setChecked(true);
    _defaultShortcuts["duration_drag"] = QList<QKeySequence>() << dragModeAction->shortcut();
    noteDurationMB->addAction(dragModeAction);
    noteDurationGroup->addAction(dragModeAction);
    _actionMap["duration_drag"] = dragModeAction;

    noteDurationMB->addSeparator();

    QList<QString> stdNames = {
        tr("Whole Note (1/1)"), tr("Half Note (1/2)"), tr("Quarter Note (1/4)"),
        tr("8th Note (1/8)"), tr("16th Note (1/16)"), tr("32nd Note (1/32)"), tr("64th Note (1/64)")
    };
    QList<int> stdDivs = {1, 2, 4, 8, 16, 32, 64};
    QList<QKeySequence> stdShortcuts = {
        QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_1)),
        QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_2)),
        QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_3)),
        QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_4)),
        QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_5)),
        QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_6)),
        QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_7))
    };

    for (int i = 0; i < stdNames.size(); ++i) {
        int divisor = stdDivs[i];
        QAction *durationAction = new QAction(stdNames[i], this);
        durationAction->setShortcut(stdShortcuts[i]);
        durationAction->setData(divisor);
        durationAction->setCheckable(true);
        QString actionId = QString("duration_") + QString::number(divisor);
        _defaultShortcuts[actionId] = QList<QKeySequence>() << durationAction->shortcut();
        noteDurationMB->addAction(durationAction);
        noteDurationGroup->addAction(durationAction);
        _actionMap[actionId] = durationAction;
    }

    noteDurationMB->addSeparator();

    QList<QString> tupNames = {
        tr("Triplet (1/3)"), tr("Quintuplet (1/5)"), tr("Sextuplet (1/6)"),
        tr("Septuplet (1/7)"), tr("Nonuplet (1/9)"), tr("8th Triplet (1/12)"),
        tr("16th Triplet (1/24)"), tr("32nd Triplet (1/48)")
    };
    QList<int> tupDivs = {3, 5, 6, 7, 9, 12, 24, 48};
    QList<QKeySequence> tupShortcuts = {
        QKeySequence(QKeyCombination(Qt::ALT | Qt::SHIFT, Qt::Key_1)),
        QKeySequence(QKeyCombination(Qt::ALT | Qt::SHIFT, Qt::Key_2)),
        QKeySequence(QKeyCombination(Qt::ALT | Qt::SHIFT, Qt::Key_3)),
        QKeySequence(QKeyCombination(Qt::ALT | Qt::SHIFT, Qt::Key_4)),
        QKeySequence(QKeyCombination(Qt::ALT | Qt::SHIFT, Qt::Key_5)),
        QKeySequence(QKeyCombination(Qt::ALT | Qt::SHIFT, Qt::Key_6)),
        QKeySequence(QKeyCombination(Qt::ALT | Qt::SHIFT, Qt::Key_7)),
        QKeySequence(QKeyCombination(Qt::ALT | Qt::SHIFT, Qt::Key_8))
    };

    for (int i = 0; i < tupNames.size(); ++i) {
        int divisor = tupDivs[i];
        QAction *durationAction = new QAction(tupNames[i], this);
        durationAction->setShortcut(tupShortcuts[i]);
        durationAction->setData(divisor);
        durationAction->setCheckable(true);
        QString actionId = QString("duration_") + QString::number(divisor);
        _defaultShortcuts[actionId] = QList<QKeySequence>() << durationAction->shortcut();
        noteDurationMB->addAction(durationAction);
        noteDurationGroup->addAction(durationAction);
        _actionMap[actionId] = durationAction;
    }

    toolsMB->addMenu(noteDurationMB);

    QAction *deleteAction = new QAction(tr("Remove events"), this);
    _activateWithSelections.append(deleteAction);
    deleteAction->setToolTip(tr("Remove selected events"));
    deleteAction->setShortcut(QKeySequence::Delete);
    _defaultShortcuts["delete"] = QList<QKeySequence>() << deleteAction->shortcut();
    Appearance::setActionIcon(deleteAction, ":/run_environment/graphics/tool/eraser.png");
    connect(deleteAction, SIGNAL(triggered()), this, SLOT(deleteSelectedEvents()));
    toolsMB->addAction(deleteAction);
    _actionMap["delete"] = deleteAction;

    toolsMB->addSeparator();

    QAction *alignLeftAction = new QAction(tr("Align left"), this);
    _activateWithSelections.append(alignLeftAction);
    alignLeftAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_Left)));
    _defaultShortcuts["align_left"] = QList<QKeySequence>() << alignLeftAction->shortcut();
    Appearance::setActionIcon(alignLeftAction, ":/run_environment/graphics/tool/align_left.png");
    connect(alignLeftAction, SIGNAL(triggered()), this, SLOT(alignLeft()));
    toolsMB->addAction(alignLeftAction);
    _actionMap["align_left"] = alignLeftAction;

    QAction *alignRightAction = new QAction(tr("Align right"), this);
    _activateWithSelections.append(alignRightAction);
    Appearance::setActionIcon(alignRightAction, ":/run_environment/graphics/tool/align_right.png");
    alignRightAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_Right)));
    _defaultShortcuts["align_right"] = QList<QKeySequence>() << alignRightAction->shortcut();
    connect(alignRightAction, SIGNAL(triggered()), this, SLOT(alignRight()));
    toolsMB->addAction(alignRightAction);
    _actionMap["align_right"] = alignRightAction;

    QAction *equalizeAction = new QAction(tr("Equalize selection"), this);
    _activateWithSelections.append(equalizeAction);
    Appearance::setActionIcon(equalizeAction, ":/run_environment/graphics/tool/equalize.png");
    equalizeAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_Up)));
    _defaultShortcuts["equalize"] = QList<QKeySequence>() << equalizeAction->shortcut();
    connect(equalizeAction, SIGNAL(triggered()), this, SLOT(equalize()));
    toolsMB->addAction(equalizeAction);
    _actionMap["equalize"] = equalizeAction;

    toolsMB->addSeparator();

    QAction *glueNotesAction = new QAction(tr("Glue notes (same channel)"), this);
    glueNotesAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_G)));
    _defaultShortcuts["glue"] = QList<QKeySequence>() << glueNotesAction->shortcut();
    Appearance::setActionIcon(glueNotesAction, ":/run_environment/graphics/tool/glue.png");
    connect(glueNotesAction, SIGNAL(triggered()), this, SLOT(glueSelection()));
    _activateWithSelections.append(glueNotesAction);
    toolsMB->addAction(glueNotesAction);
    _actionMap["glue"] = glueNotesAction;

    QAction *glueNotesAllChannelsAction = new QAction(tr("Glue notes (all channels)"), this);
    glueNotesAllChannelsAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_G)));
    _defaultShortcuts["glue_all_channels"] = QList<QKeySequence>() << glueNotesAllChannelsAction->shortcut();
    Appearance::setActionIcon(glueNotesAllChannelsAction, ":/run_environment/graphics/tool/glue.png");
    connect(glueNotesAllChannelsAction, SIGNAL(triggered()), this, SLOT(glueSelectionAllChannels()));
    _activateWithSelections.append(glueNotesAllChannelsAction);
    toolsMB->addAction(glueNotesAllChannelsAction);
    _actionMap["glue_all_channels"] = glueNotesAllChannelsAction;

    QAction *scissorsAction = new ToolButton(new ScissorsTool(), QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_X)), toolsMB);
    _defaultShortcuts["scissors"] = QList<QKeySequence>() << scissorsAction->shortcut();
    toolsMB->addAction(scissorsAction);
    _actionMap["scissors"] = scissorsAction;

    QAction *deleteOverlapsAction = new QAction(tr("Delete overlaps"), this);
    deleteOverlapsAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_D)));
    _defaultShortcuts["delete_overlaps"] = QList<QKeySequence>() << deleteOverlapsAction->shortcut();
    Appearance::setActionIcon(deleteOverlapsAction, ":/run_environment/graphics/tool/deleteoverlap.png");
    connect(deleteOverlapsAction, SIGNAL(triggered()), this, SLOT(deleteOverlaps()));
    _activateWithSelections.append(deleteOverlapsAction);
    toolsMB->addAction(deleteOverlapsAction);
    _actionMap["delete_overlaps"] = deleteOverlapsAction;

    toolsMB->addSeparator();

    QAction *convertPitchBendAction = new QAction(tr("Convert pitch bends to notes"), this);
    convertPitchBendAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_B)));
    _defaultShortcuts["convert_pitch_bend_to_notes"] = QList<QKeySequence>() << convertPitchBendAction->shortcut();
    connect(convertPitchBendAction, SIGNAL(triggered()), this, SLOT(convertPitchBendToNotes()));
    toolsMB->addAction(convertPitchBendAction);
    _actionMap["convert_pitch_bend_to_notes"] = convertPitchBendAction;

    QAction *explodeChordsAction = new QAction(tr("Explode chords to tracks"), this);
    Appearance::setActionIcon(explodeChordsAction, ":/run_environment/graphics/tool/explode_chords_to_tracks.png");
    explodeChordsAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_E)));
    _defaultShortcuts["explode_chords_to_tracks"] = QList<QKeySequence>() << explodeChordsAction->shortcut();
    connect(explodeChordsAction, SIGNAL(triggered()), this, SLOT(explodeChordsToTracks()));
    toolsMB->addAction(explodeChordsAction);
    _actionMap["explode_chords_to_tracks"] = explodeChordsAction;

    QAction *splitChannelsAction = new QAction(tr("Split channels to tracks"), this);
    Appearance::setActionIcon(splitChannelsAction, ":/run_environment/graphics/tool/channel_split_28.png");
    splitChannelsAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_E)));
    _defaultShortcuts["split_channels_to_tracks"] = QList<QKeySequence>() << splitChannelsAction->shortcut();
    connect(splitChannelsAction, SIGNAL(triggered()), this, SLOT(splitChannelsToTracks()));
    toolsMB->addAction(splitChannelsAction);
    _actionMap["split_channels_to_tracks"] = splitChannelsAction;

    QAction *strumAction = new QAction(tr("Strum selection"), this);
    strumAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL | Qt::ALT, Qt::Key_S)));
    _defaultShortcuts["strum"] = QList<QKeySequence>() << strumAction->shortcut();
    connect(strumAction, SIGNAL(triggered()), this, SLOT(strumNotes()));
    toolsMB->addAction(strumAction);
    _actionMap["strum"] = strumAction;

    toolsMB->addSeparator();

    QAction *fixFFXIVAction = new QAction(tr("Fix X|V Channels"), this);
    Appearance::setActionIcon(fixFFXIVAction, ":/run_environment/graphics/tool/ffxiv_fix.png");
    connect(fixFFXIVAction, SIGNAL(triggered()), this, SLOT(fixFFXIVChannels()));
    toolsMB->addAction(fixFFXIVAction);
    _actionMap["fix_ffxiv_channels"] = fixFFXIVAction;

#ifdef FLUIDSYNTH_SUPPORT
    // Phase 39 (FFXIV-EQ-001): per-instrument volume mixer.
    // Auto-enabled only while FFXIV SoundFont Mode is active so the
    // entry doesn't confuse non-FFXIV users.
    QAction *equalizerAction = new QAction(tr("FFXIV SoundFont Equalizer..."), this);
    connect(equalizerAction, &QAction::triggered, this, &MainWindow::openFfxivEqualizer);
    toolsMB->addAction(equalizerAction);
    _actionMap["ffxiv_equalizer"] = equalizerAction;
    {
        FluidSynthEngine *engine = FluidSynthEngine::instance();
        bool ffxivOn = engine && engine->ffxivSoundFontMode();
        equalizerAction->setEnabled(ffxivOn);
        if (engine) {
            connect(engine, &FluidSynthEngine::ffxivSoundFontModeChanged,
                    equalizerAction, [equalizerAction](bool on) {
                equalizerAction->setEnabled(on);
            });
        }
    }
#endif

    toolsMB->addSeparator();

    // Lyrics submenu
    QMenu *lyricsMenu = new QMenu(tr("Lyrics..."), toolsMB);

    QAction *importSrtAction = new QAction(tr("Import Lyrics (SRT)..."), this);
    importSrtAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_L)));
    _defaultShortcuts["import_lyrics_srt"] = QList<QKeySequence>() << importSrtAction->shortcut();
    connect(importSrtAction, &QAction::triggered, this, &MainWindow::importLyricsSrt);
    lyricsMenu->addAction(importSrtAction);
    _actionMap["import_lyrics_srt"] = importSrtAction;

    QAction *importTextAction = new QAction(tr("Import Lyrics (Text)..."), this);
    connect(importTextAction, &QAction::triggered, this, &MainWindow::importLyricsText);
    lyricsMenu->addAction(importTextAction);
    _actionMap["import_lyrics_text"] = importTextAction;

    QAction *syncLyricsAction = new QAction(tr("Sync Lyrics (Tap-to-Sync)..."), this);
    connect(syncLyricsAction, &QAction::triggered, this, &MainWindow::syncLyrics);
    lyricsMenu->addAction(syncLyricsAction);
    _actionMap["sync_lyrics"] = syncLyricsAction;

    QAction *importLrcAction = new QAction(tr("Import Lyrics (LRC)..."), this);
    connect(importLrcAction, &QAction::triggered, this, &MainWindow::importLyricsLrc);
    lyricsMenu->addAction(importLrcAction);
    _actionMap["import_lyrics_lrc"] = importLrcAction;

    QAction *exportSrtAction = new QAction(tr("Export Lyrics (SRT)..."), this);
    connect(exportSrtAction, &QAction::triggered, this, &MainWindow::exportLyricsSrt);
    lyricsMenu->addAction(exportSrtAction);
    _actionMap["export_lyrics_srt"] = exportSrtAction;

    QAction *exportLrcAction = new QAction(tr("Export Lyrics (LRC)..."), this);
    connect(exportLrcAction, &QAction::triggered, this, &MainWindow::exportLyricsLrc);
    lyricsMenu->addAction(exportLrcAction);
    _actionMap["export_lyrics_lrc"] = exportLrcAction;

    lyricsMenu->addSeparator();

    QAction *embedLyricsAction = new QAction(tr("Embed Lyrics in MIDI"), this);
    connect(embedLyricsAction, &QAction::triggered, this, &MainWindow::embedLyricsInMidi);
    lyricsMenu->addAction(embedLyricsAction);
    _actionMap["embed_lyrics_midi"] = embedLyricsAction;

    lyricsMenu->addSeparator();

    QAction *clearLyricsAction = new QAction(tr("Clear All Lyrics"), this);
    connect(clearLyricsAction, &QAction::triggered, this, &MainWindow::clearAllLyrics);
    lyricsMenu->addAction(clearLyricsAction);
    _actionMap["clear_lyrics"] = clearLyricsAction;

    toolsMB->addMenu(lyricsMenu);

    toolsMB->addSeparator();

    // Phase 33 — Tempo Tools submenu
    QMenu *tempoToolsMenu = new QMenu(tr("Tempo Tools"), toolsMB);
    QAction *convertTempoAction = new QAction(tr("Convert Tempo, Preserve Duration..."), this);
    connect(convertTempoAction, SIGNAL(triggered()), this, SLOT(convertTempoPreserveDuration()));
    tempoToolsMenu->addAction(convertTempoAction);
    toolsMB->addMenu(tempoToolsMenu);
    _actionMap["convert_tempo_preserve_duration"] = convertTempoAction;

    toolsMB->addSeparator();

    QAction *quantizeAction = new QAction(tr("Quantify selection"), this);
    _activateWithSelections.append(quantizeAction);
    Appearance::setActionIcon(quantizeAction, ":/run_environment/graphics/tool/quantize.png");
    quantizeAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_Q)));
    _defaultShortcuts["quantize"] = QList<QKeySequence>() << quantizeAction->shortcut();
    connect(quantizeAction, SIGNAL(triggered()), this, SLOT(quantizeSelection()));
    toolsMB->addAction(quantizeAction);
    _actionMap["quantize"] = quantizeAction;

    QMenu *quantMenu = new QMenu(tr("Quantization fractions..."), viewMB);
    QActionGroup *quantGroup = new QActionGroup(viewMB);
    quantGroup->setExclusive(true);

    // Regular Notes submenu
    QMenu *quantRegularMenu = new QMenu(tr("Regular Notes"), quantMenu);
    quantMenu->addMenu(quantRegularMenu);

    for (int i = 0; i <= 5; i++) {
        QVariant variant(i);
        QString text;
        if (i == 0) {
            text = tr("Whole note");
        } else if (i == 1) {
            text = tr("Half note");
        } else if (i == 2) {
            text = tr("Quarter note");
        } else {
            int noteValue = (int) qPow(2, i);
            if (noteValue == 32) {
                text = tr("32nd note");
            } else {
                text = QString::number(noteValue) + tr("th note");
            }
        }
        QAction *a = new QAction(text, this);
        a->setData(variant);
        quantGroup->addAction(a);
        quantRegularMenu->addAction(a);
        a->setCheckable(true);
        a->setChecked(i == _quantizationGrid);
    }

    quantMenu->addSeparator();

    // Triplets submenu
    QMenu *quantTripletsMenu = new QMenu(tr("Triplets"), quantMenu);
    quantMenu->addMenu(quantTripletsMenu);

    QStringList tripletNames = QStringList()
                               << tr("Whole note triplets")
                               << tr("Half note triplets")
                               << tr("Quarter note triplets")
                               << tr("8th note triplets")
                               << tr("16th note triplets")
                               << tr("32nd note triplets");

    for (int i = 0; i < tripletNames.size(); i++) {
        QAction *tripletAction = new QAction(tripletNames[i], this);
        tripletAction->setData(QVariant(-100 - i));
        quantGroup->addAction(tripletAction);
        quantTripletsMenu->addAction(tripletAction);
        tripletAction->setCheckable(true);
        tripletAction->setChecked((-100 - i) == _quantizationGrid);
    }

    quantMenu->addSeparator();

    // Other Tuplets submenu
    QMenu *quantTupletsMenu = new QMenu(tr("Other Tuplets"), quantMenu);
    quantMenu->addMenu(quantTupletsMenu);

    // Quintuplets submenu (most commonly used tuplets after triplets)
    QMenu *quantQuintupletsMenu = new QMenu(tr("Quintuplets"), quantTupletsMenu);
    quantTupletsMenu->addMenu(quantQuintupletsMenu);

    QStringList quintupletNames = QStringList()
                                  << tr("Quarter quintuplets")
                                  << tr("8th quintuplets")
                                  << tr("16th quintuplets");
    for (int i = 0; i < quintupletNames.size(); i++) {
        QAction *quintupletAction = new QAction(quintupletNames[i], this);
        quintupletAction->setData(QVariant(-202 - i));
        quantGroup->addAction(quintupletAction);
        quantQuintupletsMenu->addAction(quintupletAction);
        quintupletAction->setCheckable(true);
        quintupletAction->setChecked((-202 - i) == _quantizationGrid);
    }

    // Sextuplets submenu (for advanced users)
    QMenu *quantSextupletsMenu = new QMenu(tr("Sextuplets"), quantTupletsMenu);
    quantTupletsMenu->addMenu(quantSextupletsMenu);

    QStringList quantSextupletNames = QStringList()
                                      << tr("Quarter sextuplets")
                                      << tr("8th sextuplets");
    for (int i = 0; i < quantSextupletNames.size(); i++) {
        QAction *sextupletAction = new QAction(quantSextupletNames[i], this);
        sextupletAction->setData(QVariant(-302 - i));
        quantGroup->addAction(sextupletAction);
        quantSextupletsMenu->addAction(sextupletAction);
        sextupletAction->setCheckable(true);
        sextupletAction->setChecked((-302 - i) == _quantizationGrid);
    }

    quantMenu->addSeparator();

    // Dotted Notes submenu (important for compound meters)
    QMenu *quantDottedMenu = new QMenu(tr("Dotted Notes"), quantMenu);
    quantMenu->addMenu(quantDottedMenu);

    QStringList quantDottedNames = QStringList()
                                   << tr("Dotted quarter notes")
                                   << tr("Dotted 8th notes");
    QList<int> quantDottedValues = QList<int>() << -502 << -503;

    for (int i = 0; i < quantDottedNames.size(); i++) {
        QAction *dottedAction = new QAction(quantDottedNames[i], this);
        dottedAction->setData(QVariant(quantDottedValues[i]));
        quantGroup->addAction(dottedAction);
        quantDottedMenu->addAction(dottedAction);
        dottedAction->setCheckable(true);
        dottedAction->setChecked(quantDottedValues[i] == _quantizationGrid);
    }

    connect(quantMenu, SIGNAL(triggered(QAction*)), this, SLOT(quantizationChanged(QAction*)));
    toolsMB->addMenu(quantMenu);

    QAction *quantizeNToleAction = new QAction(tr("Quantify tuplet"), this);
    _activateWithSelections.append(quantizeNToleAction);
    quantizeNToleAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_H)));
    _defaultShortcuts["quantize_ntuplet_dialog"] = QList<QKeySequence>() << quantizeNToleAction->shortcut();
    connect(quantizeNToleAction, SIGNAL(triggered()), this, SLOT(quantizeNtoleDialog()));
    toolsMB->addAction(quantizeNToleAction);
    _actionMap["quantize_ntuplet_dialog"] = quantizeNToleAction;

    QAction *quantizeNToleActionRepeat = new QAction(tr("Repeat tuplet quantization"), this);
    _activateWithSelections.append(quantizeNToleActionRepeat);
    quantizeNToleActionRepeat->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_H)));
    _defaultShortcuts["quantize_ntuplet_repeat"] = QList<QKeySequence>() << quantizeNToleActionRepeat->shortcut();
    connect(quantizeNToleActionRepeat, SIGNAL(triggered()), this, SLOT(quantizeNtole()));
    toolsMB->addAction(quantizeNToleActionRepeat);
    _actionMap["quantize_ntuplet_repeat"] = quantizeNToleActionRepeat;

    toolsMB->addSeparator();

    QAction *transposeAction = new QAction(tr("Transpose selection"), this);
    Appearance::setActionIcon(transposeAction, ":/run_environment/graphics/tool/transpose.png");
    _activateWithSelections.append(transposeAction);
    transposeAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_T)));
    _defaultShortcuts["transpose"] = QList<QKeySequence>() << transposeAction->shortcut();
    connect(transposeAction, SIGNAL(triggered()), this, SLOT(transposeNSemitones()));
    toolsMB->addAction(transposeAction);
    _actionMap["transpose"] = transposeAction;

    QAction *transposeOctaveUpAction = new QAction(tr("Transpose octave up"), this);
    Appearance::setActionIcon(transposeOctaveUpAction, ":/run_environment/graphics/tool/transpose_up.png");
    _activateWithSelections.append(transposeOctaveUpAction);
    transposeOctaveUpAction->setShortcut(QKeySequence(QKeyCombination(Qt::SHIFT, Qt::Key_Up)));
    _defaultShortcuts["transpose_up"] = QList<QKeySequence>() << transposeOctaveUpAction->shortcut();
    connect(transposeOctaveUpAction, SIGNAL(triggered()), this, SLOT(transposeSelectedNotesOctaveUp()));
    toolsMB->addAction(transposeOctaveUpAction);
    _actionMap["transpose_up"] = transposeOctaveUpAction;

    QAction *transposeOctaveDownAction = new QAction(tr("Transpose octave down"), this);
    Appearance::setActionIcon(transposeOctaveDownAction, ":/run_environment/graphics/tool/transpose_down.png");
    _activateWithSelections.append(transposeOctaveDownAction);
    transposeOctaveDownAction->setShortcut(QKeySequence(QKeyCombination(Qt::SHIFT, Qt::Key_Down)));
    _defaultShortcuts["transpose_down"] = QList<QKeySequence>() << transposeOctaveDownAction->shortcut();
    connect(transposeOctaveDownAction, SIGNAL(triggered()), this, SLOT(transposeSelectedNotesOctaveDown()));
    toolsMB->addAction(transposeOctaveDownAction);
    _actionMap["transpose_down"] = transposeOctaveDownAction;

    toolsMB->addSeparator();

    QAction *addTrackAction = new QAction(tr("Add track"), toolsMB);
    toolsMB->addAction(addTrackAction);
    connect(addTrackAction, SIGNAL(triggered()), this, SLOT(addTrack()));

    toolsMB->addSeparator();

    _deleteChannelMenu = new QMenu(tr("Remove events from channel..."), toolsMB);
    toolsMB->addMenu(_deleteChannelMenu);
    connect(_deleteChannelMenu, SIGNAL(triggered(QAction*)), this, SLOT(deleteChannel(QAction*)));

    for (int i = 0; i < 16; i++) {
        QVariant variant(i);
        QAction *delChannelAction = new QAction(QString::number(i), this);
        delChannelAction->setData(variant);
        _deleteChannelMenu->addAction(delChannelAction);
    }

    _moveSelectedEventsToChannelMenu = new QMenu(tr("Move events to channel..."), editMB);
    toolsMB->addMenu(_moveSelectedEventsToChannelMenu);
    connect(_moveSelectedEventsToChannelMenu, SIGNAL(triggered(QAction*)), this, SLOT(moveSelectedEventsToChannel(QAction*)));

    for (int i = 0; i < 16; i++) {
        QVariant variant(i);
        QAction *moveToChannelAction = new QAction(QString::number(i), this);
        moveToChannelAction->setData(variant);
        _moveSelectedEventsToChannelMenu->addAction(moveToChannelAction);
    }

    _moveSelectedEventsToTrackMenu = new QMenu(tr("Move events to track..."), editMB);
    toolsMB->addMenu(_moveSelectedEventsToTrackMenu);
    connect(_moveSelectedEventsToTrackMenu, SIGNAL(triggered(QAction*)), this, SLOT(moveSelectedEventsToTrack(QAction*)));

    // Phase 36 -- Copy to Channel / Copy to Track. Same Tools-menu slot,
    // disabled when the selection is empty (added to _activateWithSelections
    // below). Track menu is repopulated from updateTrackMenu().
    _copySelectedEventsToChannelMenu = new QMenu(tr("Copy events to channel..."), editMB);
    toolsMB->addMenu(_copySelectedEventsToChannelMenu);
    connect(_copySelectedEventsToChannelMenu, SIGNAL(triggered(QAction*)),
            this, SLOT(copySelectedEventsToChannel(QAction*)));
    for (int i = 0; i < 16; i++) {
        QVariant variant(i);
        QAction *copyToChannelAction = new QAction(QString::number(i), this);
        copyToChannelAction->setData(variant);
        _copySelectedEventsToChannelMenu->addAction(copyToChannelAction);
    }

    _copySelectedEventsToTrackMenu = new QMenu(tr("Copy events to track..."), editMB);
    toolsMB->addMenu(_copySelectedEventsToTrackMenu);
    connect(_copySelectedEventsToTrackMenu, SIGNAL(triggered(QAction*)),
            this, SLOT(copySelectedEventsToTrack(QAction*)));

    toolsMB->addSeparator();

    QAction *setFileLengthMs = new QAction(tr("Set file duration"), this);
    connect(setFileLengthMs, SIGNAL(triggered()), this, SLOT(setFileLengthMs()));
    toolsMB->addAction(setFileLengthMs);

    QAction *scaleSelection = new QAction(tr("Scale events"), this);
    _activateWithSelections.append(scaleSelection);
    connect(scaleSelection, SIGNAL(triggered()), this, SLOT(scaleSelection()));
    toolsMB->addAction(scaleSelection);
    _actionMap["scale_selection"] = scaleSelection;

    toolsMB->addSeparator();

    QAction *magnetAction = new QAction(tr("Magnet"), editMB);
    toolsMB->addAction(magnetAction);
    magnetAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_M)));
    _defaultShortcuts["magnet"] = QList<QKeySequence>() << magnetAction->shortcut();
    Appearance::setActionIcon(magnetAction, ":/run_environment/graphics/tool/magnet.png");
    magnetAction->setCheckable(true);
    magnetAction->setChecked(false);
    magnetAction->setChecked(EventTool::magnetEnabled());
    connect(magnetAction, SIGNAL(toggled(bool)), this, SLOT(enableMagnet(bool)));
    _actionMap["magnet"] = magnetAction;

    // View
    QMenu *zoomMenu = new QMenu(tr("Zoom..."), viewMB);
    QAction *zoomHorOutAction = new QAction(tr("Horizontal out"), this);
    zoomHorOutAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_Minus)));
    _defaultShortcuts["zoom_hor_out"] = QList<QKeySequence>() << zoomHorOutAction->shortcut();
    Appearance::setActionIcon(zoomHorOutAction, ":/run_environment/graphics/tool/zoom_hor_out.png");
    connect(zoomHorOutAction, SIGNAL(triggered()), _matrixWidgetContainer, SLOT(zoomHorOut()));
    zoomMenu->addAction(zoomHorOutAction);
    _actionMap["zoom_hor_out"] = zoomHorOutAction;

    QAction *zoomHorInAction = new QAction(tr("Horizontal in"), this);
    Appearance::setActionIcon(zoomHorInAction, ":/run_environment/graphics/tool/zoom_hor_in.png");
    zoomHorInAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_Equal)));
    _defaultShortcuts["zoom_hor_in"] = QList<QKeySequence>() << zoomHorInAction->shortcut();
    connect(zoomHorInAction, SIGNAL(triggered()), _matrixWidgetContainer, SLOT(zoomHorIn()));
    zoomMenu->addAction(zoomHorInAction);
    _actionMap["zoom_hor_in"] = zoomHorInAction;

    QAction *zoomVerOutAction = new QAction(tr("Vertical out"), this);
    Appearance::setActionIcon(zoomVerOutAction, ":/run_environment/graphics/tool/zoom_ver_out.png");
    zoomVerOutAction->setShortcut(QKeySequence(QKeyCombination(Qt::SHIFT, Qt::Key_Minus)));
    _defaultShortcuts["zoom_ver_out"] = QList<QKeySequence>() << zoomVerOutAction->shortcut();
    connect(zoomVerOutAction, SIGNAL(triggered()), _matrixWidgetContainer, SLOT(zoomVerOut()));
    zoomMenu->addAction(zoomVerOutAction);
    _actionMap["zoom_ver_out"] = zoomVerOutAction;

    QAction *zoomVerInAction = new QAction(tr("Vertical in"), this);
    Appearance::setActionIcon(zoomVerInAction, ":/run_environment/graphics/tool/zoom_ver_in.png");
    zoomVerInAction->setShortcut(QKeySequence(QKeyCombination(Qt::SHIFT, Qt::Key_Equal)));
    _defaultShortcuts["zoom_ver_in"] = QList<QKeySequence>() << zoomVerInAction->shortcut();
    connect(zoomVerInAction, SIGNAL(triggered()), _matrixWidgetContainer, SLOT(zoomVerIn()));
    zoomMenu->addAction(zoomVerInAction);
    _actionMap["zoom_ver_in"] = zoomVerInAction;

    zoomMenu->addSeparator();

    QAction *zoomStdAction = new QAction(tr("Restore default zoom"), this);
    zoomStdAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_Backspace)));
    _defaultShortcuts["zoom_std"] = QList<QKeySequence>() << zoomStdAction->shortcut();
    connect(zoomStdAction, SIGNAL(triggered()), _matrixWidgetContainer, SLOT(zoomStd()));
    zoomMenu->addAction(zoomStdAction);
    _actionMap["zoom_std"] = zoomStdAction;

    viewMB->addMenu(zoomMenu);

    viewMB->addSeparator();

    QAction *resetViewAction = new QAction(tr("Reset view"), this);
    resetViewAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_Backspace)));
    _defaultShortcuts["reset_view"] = QList<QKeySequence>() << resetViewAction->shortcut();
    resetViewAction->setToolTip(tr("Reset zoom, scroll position, and cursor to defaults"));
    connect(resetViewAction, SIGNAL(triggered()), this, SLOT(resetView()));
    viewMB->addAction(resetViewAction);
    _actionMap["reset_view"] = resetViewAction;

    viewMB->addSeparator();

    // Phase 28: side-by-side compare view (read-only second document).
    QAction *compareViewAction = new QAction(tr("Compare View (side by side)"), this);
    compareViewAction->setToolTip(tr("Show another open document next to the editor for comparison"));
    connect(compareViewAction, &QAction::triggered, this, &MainWindow::toggleCompareView);
    viewMB->addAction(compareViewAction);
    _actionMap["compare_view"] = compareViewAction;

    viewMB->addSeparator();

    viewMB->addAction(_allChannelsVisible);
    viewMB->addAction(_allChannelsInvisible);
    viewMB->addAction(_allTracksVisible);
    viewMB->addAction(_allTracksInvisible);

    viewMB->addSeparator();

    // MidiPilot toggle
    _toggleMidiPilotAction = _midiPilotDock->toggleViewAction();
    _toggleMidiPilotAction->setText(tr("MidiPilot"));
    Appearance::setActionIcon(_toggleMidiPilotAction, ":/run_environment/graphics/tool/midipilot.png");
    _toggleMidiPilotAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    _defaultShortcuts["toggle_midipilot"] = QList<QKeySequence>() << _toggleMidiPilotAction->shortcut();
    _toggleMidiPilotAction->setToolTip(tr("Toggle MidiPilot AI assistant panel (Ctrl+I)"));
    connect(_toggleMidiPilotAction, &QAction::triggered, this, [this](bool checked) {
        if (checked && _midiPilotWidget) {
            _midiPilotWidget->focusInput();
        }
    });
    viewMB->addAction(_toggleMidiPilotAction);
    _actionMap["toggle_midipilot"] = _toggleMidiPilotAction;

    // Lyric Timeline toggle
    _toggleLyricTimeline = new QAction(tr("Lyric Timeline"), this);
    _toggleLyricTimeline->setCheckable(true);
    _toggleLyricTimeline->setChecked(_lyricArea->isVisible());
    _toggleLyricTimeline->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    _defaultShortcuts["toggle_lyric_timeline"] = QList<QKeySequence>() << _toggleLyricTimeline->shortcut();
    _toggleLyricTimeline->setToolTip(tr("Toggle Lyric Timeline panel (Ctrl+L)"));
    connect(_toggleLyricTimeline, &QAction::toggled, this, [this](bool checked) {
        _lyricArea->setVisible(checked);
    });
    viewMB->addAction(_toggleLyricTimeline);
    _actionMap["toggle_lyric_timeline"] = _toggleLyricTimeline;

    // Phase 32.2 (matrix tint overlay) was removed in v1.5.4 — too noisy /
    // visually unreadable on dense scores.  The voice-load lane below the
    // velocity lane (Phase 32.3) remains the primary visualiser.  We still
    // make sure the overlay is force-disabled at startup so any leftover
    // state on existing installs cannot re-tint the piano roll.
    if (mw_matrixWidget) mw_matrixWidget->setShowVoiceLoadOverlay(false);
    {
        QSettings s("MidiEditor", "NONE");
        s.remove("View/showVoiceLoad");
    }

    // Phase 32.3: voice-load lane toggle.
    // 1.6.1 (UX-VOICE-LANE-002): two cooperating switches now drive the
    // lane's visibility, combined via OR in updateVoiceLaneVisibility():
    //   1. "Show FFXIV Voice Lane"            (View/showVoiceLane,            default off)
    //         -> always show the lane regardless of FFXIV SoundFont Mode.
    //   2. "Auto-show with FFXIV SoundFont Mode" (View/voiceLaneAutoFollowFfxiv, default on)
    //         -> show the lane while FFXIV mode is on, hide it when off.
    //   With both unchecked the lane stays hidden until the user opts back in.
    _toggleVoiceLaneAction = new QAction(tr("Show FFXIV Voice Lane"), this);
    _toggleVoiceLaneAction->setCheckable(true);
    {
        QSettings _vlSettings("MidiEditor", "NONE");
        _toggleVoiceLaneAction->setChecked(_vlSettings.value("View/showVoiceLane", false).toBool());
    }
    _toggleVoiceLaneAction->setToolTip(tr(
        "Always show the voice-load graph beneath the piano roll.\n"
        "Plots the simultaneous voice count vs the FFXIV 16-voice ceiling."));
    connect(_toggleVoiceLaneAction, &QAction::toggled, this, [this](bool checked) {
        QSettings s("MidiEditor", "NONE");
        s.setValue("View/showVoiceLane", checked);
        updateVoiceLaneVisibility();
    });
    viewMB->addAction(_toggleVoiceLaneAction);
    _actionMap["view_voice_lane"] = _toggleVoiceLaneAction;

    // 1.6.1 (UX-VOICE-LANE-002): companion auto-follow toggle.
    _voiceLaneAutoFollowAction = new QAction(tr("Auto-show FFXIV Voice Lane with FFXIV SoundFont Mode"), this);
    _voiceLaneAutoFollowAction->setCheckable(true);
    {
        QSettings _vlSettings("MidiEditor", "NONE");
        _voiceLaneAutoFollowAction->setChecked(_vlSettings.value("View/voiceLaneAutoFollowFfxiv", true).toBool());
    }
    _voiceLaneAutoFollowAction->setToolTip(tr(
        "When enabled, the FFXIV Voice Lane appears automatically while\n"
        "FFXIV SoundFont Mode is on and hides again when you turn it off.\n"
        "Independent of the explicit \"Show FFXIV Voice Lane\" toggle above."));
    connect(_voiceLaneAutoFollowAction, &QAction::toggled, this, [this](bool checked) {
        QSettings s("MidiEditor", "NONE");
        s.setValue("View/voiceLaneAutoFollowFfxiv", checked);
        updateVoiceLaneVisibility();
    });
    viewMB->addAction(_voiceLaneAutoFollowAction);
    _actionMap["view_voice_lane_auto"] = _voiceLaneAutoFollowAction;

#ifdef FLUIDSYNTH_SUPPORT
    // 1.6.1 (UX-VOICE-LANE-002): listen for the FFXIV SoundFont Mode flip
    // (toolbar XIV button, settings checkbox, or programmatic) and reapply
    // the visibility rule. Idempotent when auto-follow is off.
    if (FluidSynthEngine *engine = FluidSynthEngine::instance()) {
        connect(engine, &FluidSynthEngine::ffxivSoundFontModeChanged,
                this, [this](bool /*on*/) { updateVoiceLaneVisibility(); });
    }
#endif

    // Apply the rule once now that both actions exist.
    updateVoiceLaneVisibility();

    viewMB->addSeparator();

    QMenu *colorMenu = new QMenu(tr("Colors..."), viewMB);
    _colorsByChannel = new QAction(tr("From channels"), this);
    _colorsByChannel->setCheckable(true);
    connect(_colorsByChannel, SIGNAL(triggered()), this, SLOT(colorsByChannel()));
    colorMenu->addAction(_colorsByChannel);

    _colorsByTracks = new QAction(tr("From tracks"), this);
    _colorsByTracks->setCheckable(true);
    connect(_colorsByTracks, SIGNAL(triggered()), this, SLOT(colorsByTrack()));
    colorMenu->addAction(_colorsByTracks);

    viewMB->addMenu(colorMenu);

    viewMB->addSeparator();

    QMenu *divMenu = new QMenu(tr("Raster..."), viewMB);
    QActionGroup *divGroup = new QActionGroup(viewMB);
    divGroup->setExclusive(true);

    // Off option
    QAction *offAction = new QAction(tr("Off"), this);
    offAction->setData(QVariant(-1));
    divGroup->addAction(offAction);
    divMenu->addAction(offAction);
    offAction->setCheckable(true);
    offAction->setChecked(-1 == mw_matrixWidget->div());

    divMenu->addSeparator();

    // Regular Notes submenu
    QMenu *regularMenu = new QMenu(tr("Regular Notes"), divMenu);
    divMenu->addMenu(regularMenu);

    for (int i = 0; i <= 5; i++) {
        QVariant variant(i);
        QString text;
        if (i == 0) {
            text = tr("Whole note");
        } else if (i == 1) {
            text = tr("Half note");
        } else if (i == 2) {
            text = tr("Quarter note");
        } else {
            int noteValue = (int) qPow(2, i);
            if (noteValue == 32) {
                text = tr("32nd note");
            } else {
                text = QString::number(noteValue) + tr("th note");
            }
        }
        QAction *a = new QAction(text, this);
        a->setData(variant);
        divGroup->addAction(a);
        regularMenu->addAction(a);
        a->setCheckable(true);
        a->setChecked(i == mw_matrixWidget->div());
    }

    divMenu->addSeparator();

    // Triplets submenu
    QMenu *tripletsMenu = new QMenu(tr("Triplets"), divMenu);
    divMenu->addMenu(tripletsMenu);

    QStringList rasterTripletNames = QStringList()
                                     << tr("Whole note triplets")
                                     << tr("Half note triplets")
                                     << tr("Quarter note triplets")
                                     << tr("8th note triplets")
                                     << tr("16th note triplets")
                                     << tr("32nd note triplets");

    for (int i = 0; i < rasterTripletNames.size(); i++) {
        QAction *tripletAction = new QAction(rasterTripletNames[i], this);
        tripletAction->setData(QVariant(-100 - i));
        divGroup->addAction(tripletAction);
        tripletsMenu->addAction(tripletAction);
        tripletAction->setCheckable(true);
        tripletAction->setChecked((-100 - i) == mw_matrixWidget->div());
    }

    divMenu->addSeparator();

    // Other Tuplets submenu
    QMenu *tupletsMenu = new QMenu(tr("Other Tuplets"), divMenu);
    divMenu->addMenu(tupletsMenu);

    // Quintuplets submenu
    QMenu *quintupletsMenu = new QMenu(tr("Quintuplets"), tupletsMenu);
    tupletsMenu->addMenu(quintupletsMenu);

    QStringList rasterQuintupletNames = QStringList()
                                        << tr("Quarter quintuplets")
                                        << tr("8th quintuplets")
                                        << tr("16th quintuplets");
    for (int i = 0; i < rasterQuintupletNames.size(); i++) {
        QAction *quintupletAction = new QAction(rasterQuintupletNames[i], this);
        quintupletAction->setData(QVariant(-202 - i)); // -202, -203, -204
        divGroup->addAction(quintupletAction);
        quintupletsMenu->addAction(quintupletAction);
        quintupletAction->setCheckable(true);
        quintupletAction->setChecked((-202 - i) == mw_matrixWidget->div());
    }

    // Sextuplets submenu
    QMenu *sextupletsMenu = new QMenu(tr("Sextuplets"), tupletsMenu);
    tupletsMenu->addMenu(sextupletsMenu);

    QStringList rasterSextupletNames = QStringList()
                                       << tr("Quarter sextuplets")
                                       << tr("8th sextuplets")
                                       << tr("16th sextuplets");
    for (int i = 0; i < rasterSextupletNames.size(); i++) {
        QAction *sextupletAction = new QAction(rasterSextupletNames[i], this);
        sextupletAction->setData(QVariant(-302 - i)); // -302, -303, -304
        divGroup->addAction(sextupletAction);
        sextupletsMenu->addAction(sextupletAction);
        sextupletAction->setCheckable(true);
        sextupletAction->setChecked((-302 - i) == mw_matrixWidget->div());
    }

    // Septuplets submenu
    QMenu *septupletsMenu = new QMenu(tr("Septuplets"), tupletsMenu);
    tupletsMenu->addMenu(septupletsMenu);

    QStringList rasterSeptupletNames = QStringList()
                                       << tr("Quarter septuplets")
                                       << tr("8th septuplets")
                                       << tr("16th septuplets");
    for (int i = 0; i < rasterSeptupletNames.size(); i++) {
        QAction *septupletAction = new QAction(rasterSeptupletNames[i], this);
        septupletAction->setData(QVariant(-402 - i)); // -402, -403, -404
        divGroup->addAction(septupletAction);
        septupletsMenu->addAction(septupletAction);
        septupletAction->setCheckable(true);
        septupletAction->setChecked((-402 - i) == mw_matrixWidget->div());
    }

    divMenu->addSeparator();

    // Dotted Notes submenu
    QMenu *dottedMenu = new QMenu(tr("Dotted Notes"), divMenu);
    divMenu->addMenu(dottedMenu);

    // Regular dotted notes
    QStringList rasterDottedNames = QStringList()
                                    << tr("Dotted half notes")
                                    << tr("Dotted quarter notes")
                                    << tr("Dotted 8th notes");
    QList<int> rasterDottedValues = QList<int>() << -501 << -502 << -503;

    for (int i = 0; i < rasterDottedNames.size(); i++) {
        QAction *dottedAction = new QAction(rasterDottedNames[i], this);
        dottedAction->setData(QVariant(rasterDottedValues[i]));
        divGroup->addAction(dottedAction);
        dottedMenu->addAction(dottedAction);
        dottedAction->setCheckable(true);
        dottedAction->setChecked(rasterDottedValues[i] == mw_matrixWidget->div());
    }

    dottedMenu->addSeparator();

    // Double dotted notes (1.75x duration)
    QAction *doubleDottedQuarter = new QAction(tr("Double dotted quarters"), this);
    doubleDottedQuarter->setData(QVariant(-602));
    divGroup->addAction(doubleDottedQuarter);
    dottedMenu->addAction(doubleDottedQuarter);
    doubleDottedQuarter->setCheckable(true);
    doubleDottedQuarter->setChecked(-602 == mw_matrixWidget->div());

    connect(divMenu, SIGNAL(triggered(QAction*)), this, SLOT(divChanged(QAction*)));
    viewMB->addMenu(divMenu);

    // Playback
    QAction *playStopAction = new QAction("PlayStop", this);
    QList<QKeySequence> playStopActionShortcuts;
    playStopActionShortcuts << QKeySequence(Qt::Key_Space)
                            << QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_P));
    playStopAction->setShortcuts(playStopActionShortcuts);
    _defaultShortcuts["play_stop"] = playStopActionShortcuts;
    connect(playStopAction, SIGNAL(triggered()), this, SLOT(playStop()));
    playbackMB->addAction(playStopAction);
    _actionMap["play_stop"] = playStopAction;

    QAction *playAction = new QAction(tr("Play"), this);
    Appearance::setActionIcon(playAction, ":/run_environment/graphics/tool/play.png");
    connect(playAction, SIGNAL(triggered()), this, SLOT(play()));
    playbackMB->addAction(playAction);
    _actionMap["play"] = playAction;

    QAction *pauseAction = new QAction(tr("Pause"), this);
    Appearance::setActionIcon(pauseAction, ":/run_environment/graphics/tool/pause.png");
#ifdef Q_OS_MAC
    pauseAction->setShortcut(QKeySequence(QKeyCombination(Qt::META, Qt::Key_Space)));
#else
    pauseAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_Space)));
#endif
    _defaultShortcuts["pause"] = QList<QKeySequence>() << pauseAction->shortcut();
    connect(pauseAction, SIGNAL(triggered()), this, SLOT(pause()));
    playbackMB->addAction(pauseAction);
    _actionMap["pause"] = pauseAction;

    QAction *recAction = new QAction(tr("Record"), this);
    Appearance::setActionIcon(recAction, ":/run_environment/graphics/tool/record.png");
    recAction->setShortcut(QKeySequence(QKeyCombination(Qt::CTRL, Qt::Key_R)));
    _defaultShortcuts["record"] = QList<QKeySequence>() << recAction->shortcut();
    connect(recAction, SIGNAL(triggered()), this, SLOT(record()));
    playbackMB->addAction(recAction);
    _actionMap["record"] = recAction;

    QAction *stopAction = new QAction(tr("Stop"), this);
    Appearance::setActionIcon(stopAction, ":/run_environment/graphics/tool/stop.png");
    connect(stopAction, SIGNAL(triggered()), this, SLOT(stop()));
    playbackMB->addAction(stopAction);
    _actionMap["stop"] = stopAction;

    playbackMB->addSeparator();

    QAction *backToBeginAction = new QAction(tr("Back to begin"), this);
    Appearance::setActionIcon(backToBeginAction, ":/run_environment/graphics/tool/back_to_begin.png");
    QList<QKeySequence> backToBeginActionShortcuts;
    backToBeginActionShortcuts << QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_Up))
                               << QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_Home))
                               << QKeySequence(QKeyCombination(Qt::SHIFT, Qt::Key_J));
    backToBeginAction->setShortcuts(backToBeginActionShortcuts);
    _defaultShortcuts["back_to_begin"] = backToBeginActionShortcuts;
    connect(backToBeginAction, SIGNAL(triggered()), this, SLOT(backToBegin()));
    playbackMB->addAction(backToBeginAction);
    _actionMap["back_to_begin"] = backToBeginAction;

    QAction *backAction = new QAction(tr("Previous measure"), this);
    Appearance::setActionIcon(backAction, ":/run_environment/graphics/tool/back.png");
    QList<QKeySequence> backActionShortcuts;
    backActionShortcuts << QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_Left));
    backAction->setShortcuts(backActionShortcuts);
    _defaultShortcuts["back"] = backActionShortcuts;
    connect(backAction, SIGNAL(triggered()), this, SLOT(back()));
    playbackMB->addAction(backAction);
    _actionMap["back"] = backAction;

    QAction *forwAction = new QAction(tr("Next measure"), this);
    Appearance::setActionIcon(forwAction, ":/run_environment/graphics/tool/forward.png");
    QList<QKeySequence> forwActionShortcuts;
    forwActionShortcuts << QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_Right));
    forwAction->setShortcuts(forwActionShortcuts);
    _defaultShortcuts["forward"] = forwActionShortcuts;
    connect(forwAction, SIGNAL(triggered()), this, SLOT(forward()));
    playbackMB->addAction(forwAction);
    _actionMap["forward"] = forwAction;

    playbackMB->addSeparator();

    QAction *backMarkerAction = new QAction(tr("Previous marker"), this);
    Appearance::setActionIcon(backMarkerAction, ":/run_environment/graphics/tool/back_marker.png");
    QList<QKeySequence> backMarkerActionShortcuts;
    backMarkerAction->setShortcut(QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_Comma)));
    _defaultShortcuts["back_marker"] = QList<QKeySequence>() << backMarkerAction->shortcut();
    connect(backMarkerAction, SIGNAL(triggered()), this, SLOT(backMarker()));
    playbackMB->addAction(backMarkerAction);
    _actionMap["back_marker"] = backMarkerAction;

    QAction *forwMarkerAction = new QAction(tr("Next marker"), this);
    Appearance::setActionIcon(forwMarkerAction, ":/run_environment/graphics/tool/forward_marker.png");
    QList<QKeySequence> forwMarkerActionShortcuts;
    forwMarkerAction->setShortcut(QKeySequence(QKeyCombination(Qt::ALT, Qt::Key_Period)));
    _defaultShortcuts["forward_marker"] = QList<QKeySequence>() << forwMarkerAction->shortcut();
    connect(forwMarkerAction, SIGNAL(triggered()), this, SLOT(forwardMarker()));
    playbackMB->addAction(forwMarkerAction);
    _actionMap["forward_marker"] = forwMarkerAction;

    playbackMB->addSeparator();

    QMenu *speedMenu = new QMenu(tr("Playback speed..."));
    connect(speedMenu, SIGNAL(triggered(QAction*)), this, SLOT(setSpeed(QAction*)));

    QList<double> speeds;
    speeds.append(0.25);
    speeds.append(0.5);
    speeds.append(0.75);
    speeds.append(1);
    speeds.append(1.25);
    speeds.append(1.5);
    speeds.append(1.75);
    speeds.append(2);
    QActionGroup *speedGroup = new QActionGroup(this);
    speedGroup->setExclusive(true);

    foreach(double s, speeds) {
        QAction *speedAction = new QAction(QString::number(s), this);
        speedAction->setData(QVariant::fromValue(s));
        speedMenu->addAction(speedAction);
        speedGroup->addAction(speedAction);
        speedAction->setCheckable(true);
        speedAction->setChecked(s == 1);
    }

    playbackMB->addMenu(speedMenu);

    playbackMB->addSeparator();

    playbackMB->addAction(_allChannelsAudible);
    playbackMB->addAction(_allChannelsMute);
    playbackMB->addAction(_allTracksAudible);
    playbackMB->addAction(_allTracksMute);

    playbackMB->addSeparator();

    QAction *lockAction = new QAction(tr("Lock screen while playing"), this);
    Appearance::setActionIcon(lockAction, ":/run_environment/graphics/tool/screen_unlocked.png");
    lockAction->setCheckable(true);
    connect(lockAction, SIGNAL(toggled(bool)), this, SLOT(screenLockPressed(bool)));
    playbackMB->addAction(lockAction);
    lockAction->setChecked(mw_matrixWidget->screenLocked());
    _actionMap["lock"] = lockAction;

    QAction *metronomeAction = new QAction(tr("Metronome"), this);
    Appearance::setActionIcon(metronomeAction, ":/run_environment/graphics/tool/metronome.png");
    metronomeAction->setCheckable(true);
    metronomeAction->setChecked(Metronome::enabled());
    connect(metronomeAction, SIGNAL(toggled(bool)), this, SLOT(enableMetronome(bool)));
    playbackMB->addAction(metronomeAction);
    _actionMap["metronome"] = metronomeAction;

    QAction *pianoEmulationAction = new QAction(tr("Piano emulation"), this);
    pianoEmulationAction->setCheckable(true);
    pianoEmulationAction->setChecked(mw_matrixWidget->getPianoEmulation());
    connect(pianoEmulationAction, SIGNAL(toggled(bool)), this, SLOT(togglePianoEmulation(bool)));
    playbackMB->addAction(pianoEmulationAction);

    // Midi
    QAction *configAction2 = new QAction(tr("Settings"), this);
    Appearance::setActionIcon(configAction2, ":/run_environment/graphics/tool/config.png");
    connect(configAction2, SIGNAL(triggered()), this, SLOT(openConfig()));
    midiMB->addAction(configAction2);

    QAction *thruAction = new QAction(tr("Connect Midi In/Out"), this);
    Appearance::setActionIcon(thruAction, ":/run_environment/graphics/tool/connection.png");
    thruAction->setCheckable(true);
    thruAction->setChecked(MidiInput::thru());
    connect(thruAction, SIGNAL(toggled(bool)), this, SLOT(enableThru(bool)));
    midiMB->addAction(thruAction);
    _actionMap["thru"] = thruAction;

    midiMB->addSeparator();

    QAction *panicAction = new QAction(tr("Midi panic"), this);
    Appearance::setActionIcon(panicAction, ":/run_environment/graphics/tool/panic.png");
    panicAction->setShortcut(QKeySequence(Qt::Key_Escape));
    _defaultShortcuts["panic"] = QList<QKeySequence>() << panicAction->shortcut();
    connect(panicAction, SIGNAL(triggered()), this, SLOT(panic()));
    midiMB->addAction(panicAction);
    _actionMap["panic"] = panicAction;

    // Help
    QAction *aboutAction = new QAction(tr("About MidiEditor"), this);
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(about()));
    helpMB->addAction(aboutAction);

    QAction *manualAction = new QAction(tr("Manual"), this);
    connect(manualAction, SIGNAL(triggered()), this, SLOT(manual()));
    helpMB->addAction(manualAction);

    QAction *checkUpdatesAction = new QAction(tr("Check for updates"), this);
    connect(checkUpdatesAction, &QAction::triggered, this, [this](){ checkForUpdates(false); });
    helpMB->addAction(checkUpdatesAction);

#ifdef MIDIEDITOR_WEBRTC_ENABLED
    // Phase 9.6 / §11.10g: developer-only WebRTC diagnostics. End users
    // get the production-handshake "Test connection" button in
    // Settings → Collaboration instead. These two entries appear only
    // when MIDIEDITOR_DEV is set in the environment so contributors can
    // flip them on without rebuilding.
    if (qEnvironmentVariableIsSet("MIDIEDITOR_DEV")) {
        helpMB->addSeparator();
        QAction *webrtcSmokeAction = new QAction(tr("WebRTC STUN Test (debug)"), this);
        connect(webrtcSmokeAction, &QAction::triggered, this, [this]() {
            WebRtcSmokeTest::runStunPing();
            QMessageBox::information(this, tr("WebRTC STUN Test"),
                tr("ICE gathering started. Watch the log for 'midieditor.collab.rtc.smoke' "
                   "entries — a candidate with 'typ srflx' confirms STUN works."));
        });
        helpMB->addAction(webrtcSmokeAction);

        QAction *webrtcLoopbackAction = new QAction(tr("WebRTC Loopback Test (debug)"), this);
        connect(webrtcLoopbackAction, &QAction::triggered, this, [this]() {
            WebRtcSmokeTest::runLoopback();
            QMessageBox::information(this, tr("WebRTC Loopback Test"),
                tr("Two in-process transports started. Watch the log — "
                   "look for 'ROUND-TRIP COMPLETE' confirming framed "
                   "messages flow bidirectionally over DTLS."));
        });
        helpMB->addAction(webrtcLoopbackAction);
    }
#endif

    // Use full custom toolbar with settings integration
    // Apply any stored custom shortcuts from settings after defining defaults
    applyStoredShortcuts();

    _toolbarWidget = createCustomToolbar(parent);
    return _toolbarWidget;
}

QWidget *MainWindow::createSimpleCustomToolbar(QWidget *parent) {
    // Step 1: Simple custom toolbar with hard-coded safe layout
    // No settings dependencies, no complex logic - just basic customization

    QWidget *buttonBar = new QWidget(parent);
    QGridLayout *btnLayout = new QGridLayout(buttonBar);

    buttonBar->setLayout(btnLayout);
    btnLayout->setSpacing(0);
    buttonBar->setContentsMargins(0, 0, 0, 0);

    QToolBar *toolBar = new QToolBar("Custom", buttonBar);
    toolBar->setFloatable(false);
    toolBar->setContentsMargins(0, 0, 0, 0);
    toolBar->layout()->setSpacing(3);
    int iconSize = Appearance::toolbarIconSize();
    toolBar->setIconSize(QSize(iconSize, iconSize));
    toolBar->setStyleSheet(Appearance::toolbarInlineStyle());

    // Add actions manually to ensure proper layout and functionality
    // This replicates the essential parts of the original toolbar

    // File actions
    if (_actionMap.contains("new")) toolBar->addAction(_actionMap["new"]);

    // Simple open action (recent files menu will be added in later steps)
    if (_actionMap.contains("open")) {
        toolBar->addAction(_actionMap["open"]);
    }

    if (_actionMap.contains("save")) toolBar->addAction(_actionMap["save"]);
    toolBar->addSeparator();

    // Edit actions
    if (_actionMap.contains("undo")) toolBar->addAction(_actionMap["undo"]);
    if (_actionMap.contains("redo")) toolBar->addAction(_actionMap["redo"]);
    toolBar->addSeparator();

    // Tool actions
    if (_actionMap.contains("standard_tool")) toolBar->addAction(_actionMap["standard_tool"]);
    if (_actionMap.contains("new_note")) toolBar->addAction(_actionMap["new_note"]);
    if (_actionMap.contains("copy")) toolBar->addAction(_actionMap["copy"]);

    // Simple paste action (menu will be added in later steps)
    if (_actionMap.contains("paste")) {
        toolBar->addAction(_actionMap["paste"]);
    }

    toolBar->addSeparator();

    // Playback actions
    if (_actionMap.contains("play")) toolBar->addAction(_actionMap["play"]);
    if (_actionMap.contains("pause")) toolBar->addAction(_actionMap["pause"]);
    if (_actionMap.contains("stop")) toolBar->addAction(_actionMap["stop"]);

    // Remove any trailing separators
    removeTrailingSeparators(toolBar);

    btnLayout->setColumnStretch(4, 1);
    btnLayout->addWidget(toolBar, 0, 0, 1, 1);

    return buttonBar;
}

void MainWindow::pasteToChannel(QAction *action) {
    EventTool::setPasteChannel(action->data().toInt());
}

void MainWindow::pasteToTrack(QAction *action) {
    EventTool::setPasteTrack(action->data().toInt());
}

void MainWindow::divChanged(QAction *action) {
    mw_matrixWidget->setDiv(action->data().toInt());
}

void MainWindow::enableMagnet(bool enable) {
    EventTool::enableMagnet(enable);
}

void MainWindow::checkForUpdates(bool silent) {
    _silentUpdateCheck = silent;
    if (!_updateChecker) {
        _updateChecker = new UpdateChecker(this);
        connect(_updateChecker, &UpdateChecker::updateAvailable, this,
            [this](QString version, QString releaseUrl, QString zipDownloadUrl, qint64 zipSize){

            // Create the Update Available dialog
            auto *dlg = new UpdateAvailableDialog(version, QCoreApplication::applicationVersion(), this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->setChangelogLoading();

            // Fetch changelog from website (async)
            auto *fetcher = new UpdateChecker(dlg);
            connect(fetcher, &UpdateChecker::changelogReady, dlg,
                [dlg](const ChangelogSummary &summary) {
                dlg->setChangelogSummary(summary);
            });
            connect(fetcher, &UpdateChecker::changelogFetchFailed, dlg,
                [dlg]() {
                dlg->setChangelogUnavailable();
            });
            fetcher->fetchChangelog(version);

            // Show dialog and handle result
            if (dlg->exec() == QDialog::Accepted) {
                auto choice = dlg->userChoice();

                if (choice == UpdateAvailableDialog::DownloadManual) {
                    QDesktopServices::openUrl(QUrl("https://midieditor-ai.de/download.html"));
                }
                else if (choice == UpdateAvailableDialog::UpdateNow ||
                         choice == UpdateAvailableDialog::AfterExit) {
                    bool updateNow = (choice == UpdateAvailableDialog::UpdateNow);

                    if (zipDownloadUrl.isEmpty()) {
                        QMessageBox::information(this, tr("Auto-Update"),
                            tr("No downloadable ZIP found in this release.\nOpening the release page instead."));
                        QDesktopServices::openUrl(QUrl(releaseUrl));
                        return;
                    }

                    if (!_autoUpdater) {
                        _autoUpdater = new AutoUpdater(this, _settings, this);
                    }

                    connect(_autoUpdater, &AutoUpdater::downloadComplete, this,
                        [this, updateNow](const QString &zipPath) {
                        if (updateNow) {
                            QString midiPath;
                            if (file) {
                                midiPath = file->path();
                                if (!file->saved() && !midiPath.isEmpty()) {
                                    file->save(midiPath);
                                }
                            }
                            _forceCloseForUpdate = true;
                            _autoUpdater->executeUpdateNow(midiPath);
                        } else {
                            _autoUpdater->scheduleUpdateOnExit();
                            QMessageBox::information(this, tr("Update Scheduled"),
                                tr("The update will be applied when you close the application."));
                        }
                    }, Qt::SingleShotConnection);

                    connect(_autoUpdater, &AutoUpdater::downloadFailed, this,
                        [this](const QString &error) {
                        QMessageBox::warning(this, tr("Download Failed"), error);
                    }, Qt::SingleShotConnection);

                    _autoUpdater->downloadUpdate(zipDownloadUrl, zipSize);
                }
            }
            // else: Skip â€” do nothing
        });
        connect(_updateChecker, &UpdateChecker::noUpdateAvailable, this, [this](){
            if (!_silentUpdateCheck) {
                QMessageBox::information(this, tr("Update Check"), tr("You are using the latest version."));
            }
        });
        connect(_updateChecker, &UpdateChecker::errorOccurred, this, [this](QString error){
            if (!_silentUpdateCheck) {
                QMessageBox::warning(this, tr("Update Check Failed"), tr("Could not check for updates: %1").arg(error));
            } else {
                qWarning() << "Silent update check failed:" << error;
            }
        });
    }
    _updateChecker->checkForUpdates();
}

void MainWindow::showPostUpdateDialog(const QString &updatedFromVersion) {
    QString currentVersion = QCoreApplication::applicationVersion();
    auto *dlg = new PostUpdateDialog(currentVersion, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setChangelogLoading();

    // Fetch changelog from website (async)
    auto *fetcher = new UpdateChecker(dlg);
    connect(fetcher, &UpdateChecker::changelogReady, dlg,
        [dlg](const ChangelogSummary &summary) {
        dlg->setChangelogSummary(summary);
    });
    connect(fetcher, &UpdateChecker::changelogFetchFailed, dlg,
        [dlg]() {
        dlg->setChangelogUnavailable();
    });
    fetcher->fetchChangelog(currentVersion);

    dlg->show();
}

void MainWindow::openConfig() {
    SettingsDialog *d = new SettingsDialog(tr("Settings"), _settings, this);
    connect(d, SIGNAL(settingsChanged()), this, SLOT(updateAll()));
    if (_midiPilotWidget) {
        connect(d, SIGNAL(settingsChanged()), _midiPilotWidget, SLOT(onSettingsChanged()));
    }
    // Note: We don't connect settingsChanged() to updateRenderingMode() here because
    // we connect directly to PerformanceSettingsWidget for immediate updates

    // Connect to PerformanceSettingsWidget for immediate updates
    // Find the PerformanceSettingsWidget in the dialog and connect to its renderingModeChanged signal
    QList<PerformanceSettingsWidget*> perfWidgets = d->findChildren<PerformanceSettingsWidget*>();
    if (!perfWidgets.isEmpty()) {
        PerformanceSettingsWidget *perfWidget = perfWidgets.first();
        connect(perfWidget, &PerformanceSettingsWidget::renderingModeChanged,
                this, &MainWindow::updateRenderingMode);
        qDebug() << "Connected PerformanceSettingsWidget for immediate rendering updates";
    }

    // Pass MCP server reference so the settings UI can show live status
    QList<AiSettingsWidget*> aiWidgets = d->findChildren<AiSettingsWidget*>();
    if (!aiWidgets.isEmpty() && _mcpServer) {
        aiWidgets.first()->setMcpServer(_mcpServer);
    }

    d->show();
}

void MainWindow::restartForThemeChange() {
    // Save current file if modified
    if (file && !file->saved()) {
        if (!saveBeforeClose()) {
            return; // User cancelled â€” abort restart
        }
    }

    // Persist all settings to disk
    Appearance::writeSettings(_settings);
    _settings->sync();

    // Build command-line arguments for the restarted instance
    QStringList args;
    if (file && QFile::exists(file->path())) {
        args << "--open" << file->path();
    }
    args << "--open-settings";

    QString exePath = QCoreApplication::applicationFilePath();
    QString appDir = QCoreApplication::applicationDirPath();

    qDebug() << "Restarting for theme change:" << exePath << args;
    bool launched = QProcess::startDetached(exePath, args, appDir);

    if (launched) {
#ifdef Q_OS_WIN
        ExitProcess(0);
#else
        _exit(0);
#endif
    }
}

void MainWindow::openConfigOnAppearanceTab() {
    SettingsDialog *d = new SettingsDialog(tr("Settings"), _settings, this);
    connect(d, SIGNAL(settingsChanged()), this, SLOT(updateAll()));
    if (_midiPilotWidget) {
        connect(d, SIGNAL(settingsChanged()), _midiPilotWidget, SLOT(onSettingsChanged()));
    }

    QList<PerformanceSettingsWidget*> perfWidgets = d->findChildren<PerformanceSettingsWidget*>();
    if (!perfWidgets.isEmpty()) {
        connect(perfWidgets.first(), &PerformanceSettingsWidget::renderingModeChanged,
                this, &MainWindow::updateRenderingMode);
    }

    // Navigate to the Appearance tab (index 4)
    d->setCurrentTab(4);
    d->show();
}

void MainWindow::enableMetronome(bool enable) {
    Metronome::setEnabled(enable);
}

void MainWindow::enableThru(bool enable) {
    MidiInput::setThruEnabled(enable);
}

void MainWindow::nullOnDemandToolbarWidgets() {
    // Deleting a toolbar destroys the on-demand widgets parented to it; clear
    // the member pointers so the guarded derefs in setFile()/stop() don't touch
    // freed memory (BUG-CORE-001). Enabled widgets are recreated right after.
    _visualizer = nullptr;
    _timeDisplay = nullptr;
    _lyricVisualizer = nullptr;
    _ffxivVoiceGauge = nullptr;
    _c64ToggleWidget = nullptr;
    _c64ModeSwitch = nullptr;
    _mcpToggleWidget = nullptr;
    _ffxivToggleWidget = nullptr;
}

void MainWindow::rebuildToolbar() {
    if (_toolbarWidget) {
        try {
            // Remove the old toolbar
            QWidget *parent = _toolbarWidget->parentWidget();
            if (!parent) {
                return; // No parent, can't rebuild
            }

            _toolbarWidget->setParent(nullptr);
            delete _toolbarWidget;
            _toolbarWidget = nullptr;
            nullOnDemandToolbarWidgets(); // their toolbars were just destroyed

            // Create new toolbar
            _toolbarWidget = createCustomToolbar(parent);
            if (!_toolbarWidget) {
                return; // Failed to create toolbar
            }

            // Add it back to the layout
            QGridLayout *layout = qobject_cast<QGridLayout *>(parent->layout());
            if (layout) {
                layout->addWidget(_toolbarWidget, 0, 0);
            }
        } catch (...) {
            // If rebuild fails, create a minimal toolbar
            _toolbarWidget = nullptr; // Ensure it's not left dangling
            try {
                // Try to get the parent from the central widget
                QWidget *central = centralWidget();
                if (central) {
                    _toolbarWidget = new QWidget(central);
                    QGridLayout *layout = qobject_cast<QGridLayout *>(central->layout());
                    if (layout) {
                        layout->addWidget(_toolbarWidget, 0, 0);
                    }
                }
            } catch (...) {
                // If even that fails, just leave _toolbarWidget as nullptr
                _toolbarWidget = nullptr;
            }
        }
    }
}

QAction *MainWindow::getActionById(const QString &actionId) {
    return _actionMap.value(actionId, nullptr);
}

QWidget *MainWindow::createCustomToolbar(QWidget *parent) {
    QWidget *buttonBar = new QWidget(parent);
    QGridLayout *btnLayout = new QGridLayout(buttonBar);

    buttonBar->setLayout(btnLayout);
    btnLayout->setSpacing(0);
    buttonBar->setContentsMargins(0, 0, 0, 0);

    // Safety check - if Appearance is not initialized, create a simple toolbar
    bool twoRowMode = false;
    QStringList actionOrder;
    QStringList enabledActions;

    bool customizeEnabled = false;
    try {
        twoRowMode = Appearance::toolbarTwoRowMode();
        customizeEnabled = Appearance::toolbarCustomizeEnabled();
        actionOrder = Appearance::toolbarActionOrder();
        enabledActions = Appearance::toolbarEnabledActions();
    } catch (...) {
        // If there's any issue with settings, use safe defaults
        twoRowMode = false;
        customizeEnabled = false;
        actionOrder.clear();
        enabledActions.clear();
    }

    // If no custom order is set, use default based on row mode
    if (actionOrder.isEmpty()) {
        // These are the old defaults that include essential actions - they cause duplicates
        // We should only use the new defaults that don't include essential actions
        actionOrder.clear(); // Force use of new defaults
    }

    // If no enabled actions are set, enable all by default
    if (enabledActions.isEmpty()) {
        for (const QString &actionId: actionOrder) {
            if (!actionId.startsWith("separator") && actionId != "row_separator") {
                enabledActions << actionId;
            }
        }
    }

    // Essential actions that can't be disabled (only for single row mode)
    QStringList essentialActions;
    if (!twoRowMode) {
        essentialActions = LayoutSettingsWidget::getEssentialActionIds();
    }

    // Use custom settings only if customization is enabled AND we have valid action order
    // If customization is disabled, always use defaults regardless of stored action order
    // Fresh installs (or first launch after settings reset) also fall through to the
    // default loader so the toolbar isn't empty when customization is on but no order
    // has ever been persisted yet (TOOLBAR-DEFAULT-001 follow-up).
    const bool firstRunNoOrder = actionOrder.isEmpty();
    if (!customizeEnabled || firstRunNoOrder) {
        // Use minimal default toolbar (not comprehensive - that's for customize UI only)
        actionOrder = LayoutSettingsWidget::getDefaultToolbarOrder();

        if (twoRowMode) {
            // Add row separator for double row mode using default toolbar row distribution
            QStringList row1Actions, row2Actions;
            LayoutSettingsWidget::getDefaultToolbarRowDistribution(row1Actions, row2Actions);

            // Rebuild action order with row separator
            actionOrder.clear();
            actionOrder << row1Actions << "row_separator" << row2Actions;
        }

        // Use default toolbar enabled actions (all actions in default toolbar are enabled)
        enabledActions = LayoutSettingsWidget::getDefaultToolbarEnabledActions();
    } else {
        // Migration: add new actions to saved custom settings.
        // Only enable by default when the action is genuinely new (not yet in actionOrder).
        // If it's already in actionOrder but not in enabledActions, the user disabled it.
        if (!actionOrder.contains("toggle_midipilot")) {
            actionOrder << "separator14" << "toggle_midipilot";
            if (!enabledActions.contains("toggle_midipilot"))
                enabledActions << "toggle_midipilot";
        }
        if (!actionOrder.contains("fix_ffxiv_channels")) {
            int sep14Idx = actionOrder.indexOf("separator14");
            if (sep14Idx >= 0)
                actionOrder.insert(sep14Idx, "fix_ffxiv_channels");
            else
                actionOrder << "fix_ffxiv_channels";
            if (!enabledActions.contains("fix_ffxiv_channels"))
                enabledActions << "fix_ffxiv_channels";
        }
        if (!actionOrder.contains("explode_chords_to_tracks")) {
            int splitIdx = actionOrder.indexOf("split_channels_to_tracks");
            if (splitIdx >= 0)
                actionOrder.insert(splitIdx, "explode_chords_to_tracks");
            else
                actionOrder << "explode_chords_to_tracks";
            if (!enabledActions.contains("explode_chords_to_tracks"))
                enabledActions << "explode_chords_to_tracks";
        }
        if (!actionOrder.contains("split_channels_to_tracks")) {
            int ffxivIdx = actionOrder.indexOf("fix_ffxiv_channels");
            if (ffxivIdx >= 0)
                actionOrder.insert(ffxivIdx, "split_channels_to_tracks");
            else
                actionOrder << "split_channels_to_tracks";
            if (!enabledActions.contains("split_channels_to_tracks"))
                enabledActions << "split_channels_to_tracks";
        }
        if (!actionOrder.contains("midi_visualizer")) {
            int sep14Idx = actionOrder.indexOf("separator14");
            if (sep14Idx >= 0)
                actionOrder.insert(sep14Idx, "midi_visualizer");
            else
                actionOrder << "midi_visualizer";
            if (!enabledActions.contains("midi_visualizer"))
                enabledActions << "midi_visualizer";
        }
        if (!actionOrder.contains("lyric_visualizer")) {
            int visIdx = actionOrder.indexOf("midi_visualizer");
            if (visIdx >= 0)
                actionOrder.insert(visIdx + 1, "lyric_visualizer");
            else
                actionOrder << "lyric_visualizer";
            if (!enabledActions.contains("lyric_visualizer"))
                enabledActions << "lyric_visualizer";
        }
        if (!actionOrder.contains("mcp_toggle")) {
            int lyricIdx = actionOrder.indexOf("lyric_visualizer");
            if (lyricIdx >= 0)
                actionOrder.insert(lyricIdx + 1, "mcp_toggle");
            else
                actionOrder << "mcp_toggle";
            if (!enabledActions.contains("mcp_toggle"))
                enabledActions << "mcp_toggle";
        }
        if (!actionOrder.contains("ffxiv_toggle")) {
            int mcpIdx = actionOrder.indexOf("mcp_toggle");
            if (mcpIdx >= 0)
                actionOrder.insert(mcpIdx + 1, "ffxiv_toggle");
            else
                actionOrder << "ffxiv_toggle";
            if (!enabledActions.contains("ffxiv_toggle"))
                enabledActions << "ffxiv_toggle";
        }
        // Phase 42.2: migrate existing layouts to include the C64 toggle.
        if (!actionOrder.contains("c64_toggle")) {
            int xivToggleIdx = actionOrder.indexOf("ffxiv_toggle");
            if (xivToggleIdx >= 0)
                actionOrder.insert(xivToggleIdx + 1, "c64_toggle");
            else
                actionOrder << "c64_toggle";
            if (!enabledActions.contains("c64_toggle"))
                enabledActions << "c64_toggle";
        }
        if (!actionOrder.contains("c64_mode_switch")) {
            int c64Idx = actionOrder.indexOf("c64_toggle");
            if (c64Idx >= 0)
                actionOrder.insert(c64Idx + 1, "c64_mode_switch");
            else
                actionOrder << "c64_mode_switch";
            if (!enabledActions.contains("c64_mode_switch"))
                enabledActions << "c64_mode_switch";
        }
        if (!actionOrder.contains("ffxiv_voice_gauge")) {
            int xivIdx = actionOrder.indexOf("ffxiv_toggle");
            if (xivIdx >= 0)
                actionOrder.insert(xivIdx + 1, "ffxiv_voice_gauge");
            else
                actionOrder << "ffxiv_voice_gauge";
            if (!enabledActions.contains("ffxiv_voice_gauge"))
                enabledActions << "ffxiv_voice_gauge";
        }
        // Phase 41: migrate existing saved layouts so the cursor-time
        // display appears (and is enabled) without a Reset to Default.
        if (!actionOrder.contains("time_display")) {
            int gaugeIdx = actionOrder.indexOf("ffxiv_voice_gauge");
            if (gaugeIdx >= 0)
                actionOrder.insert(gaugeIdx + 1, "time_display");
            else
                actionOrder << "time_display";
            if (!enabledActions.contains("time_display"))
                enabledActions << "time_display";
        }
    }

    // Only prepend essential actions for single row mode
    if (!twoRowMode) {
        QStringList finalActionOrder = essentialActions;
        finalActionOrder.append(actionOrder);
        actionOrder = finalActionOrder;
    }

    // Always enable essential actions (including their separators)
    for (const QString &actionId: essentialActions) {
        if (!enabledActions.contains(actionId)) {
            enabledActions << actionId;
        }
    }

    int iconSize = Appearance::toolbarIconSize();

    // CRITICAL: Reset all grid layout constraints before setting up new layout
    // This prevents issues when switching between single/double row modes

    for (int col = 0; col < 10; col++) { // Clear up to 10 columns
        btnLayout->setColumnStretch(col, 0);
        btnLayout->setColumnMinimumWidth(col, 0);
    }
    for (int row = 0; row < 5; row++) { // Clear up to 5 rows
        btnLayout->setRowStretch(row, 0);
        btnLayout->setRowMinimumHeight(row, 0);
    }

    if (twoRowMode) {
        // Create three separate toolbars: essential (larger), top row, bottom row
        QToolBar *essentialToolBar = new QToolBar("Essential", buttonBar);
        QToolBar *topToolBar = new QToolBar("Top", buttonBar);
        QToolBar *bottomToolBar = new QToolBar("Bottom", buttonBar);

        // Essential toolbar setup (larger icons)
        int essentialIconSize = iconSize;
        essentialToolBar->setFloatable(false);
        essentialToolBar->setContentsMargins(0, 0, 0, 0);
        essentialToolBar->layout()->setSpacing(3);
        essentialToolBar->setIconSize(QSize(essentialIconSize, essentialIconSize));
        essentialToolBar->setStyleSheet("QToolBar { border: 0px }");
        essentialToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

        // Top toolbar setup
        topToolBar->setFloatable(false);
        topToolBar->setContentsMargins(0, 0, 0, 0);
        topToolBar->layout()->setSpacing(3);
        topToolBar->setIconSize(QSize(iconSize, iconSize));
        topToolBar->setStyleSheet("QToolBar { border: 0px }");
        topToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        topToolBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        // Bottom toolbar setup
        bottomToolBar->setFloatable(false);
        bottomToolBar->setContentsMargins(0, 0, 0, 0);
        bottomToolBar->layout()->setSpacing(3);
        bottomToolBar->setIconSize(QSize(iconSize, iconSize));
        bottomToolBar->setStyleSheet("QToolBar { border: 0px }");
        bottomToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        bottomToolBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        QToolBar *currentToolBar = topToolBar;

        // First, add essential actions to essential toolbar
        QStringList essentialActions = LayoutSettingsWidget::getEssentialActionIds();

        for (const QString &actionId: essentialActions) {
            if (actionId.startsWith("separator")) {
                essentialToolBar->addSeparator();
                continue;
            }

            QAction *action = getActionById(actionId);
            if (action) {
                // Special handling for open action to add recent files menu
                if (actionId == "open") {
                    QAction *openWithRecentAction = new QAction(action->text(), essentialToolBar);
                    Appearance::setActionIcon(openWithRecentAction, ":/run_environment/graphics/tool/load.png");
                    openWithRecentAction->setShortcut(action->shortcut());
                    openWithRecentAction->setToolTip(action->toolTip());
                    connect(openWithRecentAction, SIGNAL(triggered()), this, SLOT(load()));
                    if (_recentPathsMenu) {
                        openWithRecentAction->setMenu(_recentPathsMenu);
                    }
                    action = openWithRecentAction;
                }

                essentialToolBar->addAction(action);

                // Set text under icon for essential actions
                QWidget *toolButton = essentialToolBar->widgetForAction(action);
                if (QToolButton *button = qobject_cast<QToolButton *>(toolButton)) {
                    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                }
            }
        }

        // Add actions to appropriate toolbar
        QStringList essentialIds = LayoutSettingsWidget::getEssentialActionIds();
        for (const QString &actionId: actionOrder) {
            // Essential actions are already handled above, skip them here
            if (essentialIds.contains(actionId)) {
                continue;
            }

            if (actionId == "row_separator") {
                currentToolBar = bottomToolBar;
                continue;
            }

            if (actionId.startsWith("separator")) {
                // Only add separators if they are enabled and there are already actions in the toolbar
                if (enabledActions.contains(actionId) && currentToolBar->actions().count() > 0) {
                    QAction *lastAction = currentToolBar->actions().last();
                    if (!lastAction->isSeparator()) {
                        currentToolBar->addSeparator();
                    }
                }
                continue;
            }

            // Skip disabled actions only if customization is enabled
            if (customizeEnabled && !enabledActions.isEmpty() && !enabledActions.contains(actionId)) {
                continue; // Skip disabled actions
            }

            // Use current toolbar for all non-essential actions

            QAction *action = getActionById(actionId);

            // Special handling for open action to add recent files menu (for non-essential open actions)
            if (actionId == "open" && action) {
                // Create a new action with the recent files menu for the toolbar
                QAction *openWithRecentAction = new QAction(action->text(), currentToolBar);
                Appearance::setActionIcon(openWithRecentAction, ":/run_environment/graphics/tool/load.png");
                openWithRecentAction->setShortcut(action->shortcut());
                openWithRecentAction->setToolTip(action->toolTip());
                connect(openWithRecentAction, SIGNAL(triggered()), this, SLOT(load()));
                if (_recentPathsMenu) {
                    openWithRecentAction->setMenu(_recentPathsMenu);
                }
                action = openWithRecentAction;
            }

            if (!action) {
                // Handle special actions that need custom creation
                if (actionId == "open") {
                    // Create special open action with recent files menu
                    action = new QAction(tr("Open..."), currentToolBar);
                    Appearance::setActionIcon(action, ":/run_environment/graphics/tool/load.png");
                    connect(action, SIGNAL(triggered()), this, SLOT(load()));
                    if (_recentPathsMenu) {
                        action->setMenu(_recentPathsMenu);
                    }
                } else if (actionId == "paste") {
                    // Create special paste action with options menu
                    action = new QAction(tr("Paste events"), currentToolBar);
                    action->setToolTip(tr("Paste events at cursor position"));
                    Appearance::setActionIcon(action, ":/run_environment/graphics/tool/paste.png");
                    connect(action, SIGNAL(triggered()), this, SLOT(paste()));
                    if (_pasteOptionsMenu) {
                        action->setMenu(_pasteOptionsMenu);
                    }
                } else {
                    // Create a placeholder action if the real one doesn't exist
                    action = new QAction(actionId, currentToolBar);
                    action->setEnabled(false);
                    action->setToolTip("Action not yet implemented: " + actionId);

                    // Try to find the icon path for this action from our default list
                    try {
                        QList<ToolbarActionInfo> defaultActions = getDefaultActionsForPlaceholder();
                        for (const ToolbarActionInfo &info: defaultActions) {
                            if (info.id == actionId && !info.iconPath.isEmpty()) {
                                Appearance::setActionIcon(action, info.iconPath);
                                break;
                            }
                        }
                    } catch (...) {
                        // If icon setting fails, just continue without icon
                    }
                }
            }

            // Special handling for midi_visualizer: create widget directly
            if (actionId == "midi_visualizer") {
                _visualizer = new MidiVisualizerWidget(currentToolBar);
                connect(MidiPlayer::playerThread(), SIGNAL(playerStarted()), _visualizer, SLOT(playbackStarted()));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _visualizer, SLOT(playbackStopped()));
                currentToolBar->addWidget(_visualizer);
                continue;
            }
            // Phase 41: retro cursor-time display.
            if (actionId == "time_display") {
                _timeDisplay = new TimeDisplayWidget(currentToolBar);
                _timeDisplay->setFile(file);
                connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _timeDisplay, SLOT(onPlaybackPositionChanged(int)));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _timeDisplay, SLOT(onPlaybackStopped()));
                currentToolBar->addWidget(_timeDisplay);
                continue;
            }
            // Special handling for lyric_visualizer: create karaoke widget
            if (actionId == "lyric_visualizer") {
                _lyricVisualizer = new LyricVisualizerWidget(currentToolBar);
                _lyricVisualizer->setFile(file);
                connect(MidiPlayer::playerThread(), SIGNAL(playerStarted()), _lyricVisualizer, SLOT(playbackStarted()));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _lyricVisualizer, SLOT(playbackStopped()));
                connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _lyricVisualizer, SLOT(onPlaybackPositionChanged(int)));
                currentToolBar->addWidget(_lyricVisualizer);
                continue;
            }
            // Special handling for mcp_toggle: create MCP server toggle widget
            if (actionId == "mcp_toggle" && _mcpServer) {
                _mcpToggleWidget = new McpToggleWidget(_mcpServer, currentToolBar);
                currentToolBar->addWidget(_mcpToggleWidget);
                continue;
            }
            // Special handling for ffxiv_toggle: create FFXIV SoundFont Mode toggle widget
            if (actionId == "ffxiv_toggle") {
                _ffxivToggleWidget = new FfxivToggleWidget(currentToolBar);
                currentToolBar->addWidget(_ffxivToggleWidget);
                continue;
            }
            if (actionId == "c64_toggle") {
                _c64ToggleWidget = new C64ToggleWidget(currentToolBar);
                currentToolBar->addWidget(_c64ToggleWidget);
                continue;
            }
            if (actionId == "c64_mode_switch") {
                _c64ModeSwitch = new C64ModeSwitchWidget(currentToolBar);
                _c64ModeSwitch->setToolbarAction(currentToolBar->addWidget(_c64ModeSwitch));
                continue;
            }
            // Special handling for ffxiv_voice_gauge: FFXIV voice-load gauge (Phase 32.1)
            if (actionId == "ffxiv_voice_gauge") {
                _ffxivVoiceGauge = new FfxivVoiceGaugeWidget(currentToolBar);
                _ffxivVoiceGauge->setFile(file);
                currentToolBar->addWidget(_ffxivVoiceGauge);
                continue;
            }

            if (action) {
                try {
                    currentToolBar->addAction(action);
                } catch (...) {
                    // If adding action fails, skip it
                }
            }
        }

        // Remove any trailing separators from content toolbars only (not essential toolbar)
        removeTrailingSeparators(topToolBar);
        removeTrailingSeparators(bottomToolBar);

        // Layout: Essential toolbar on left, content toolbars stacked on right
        btnLayout->setColumnStretch(1, 1);
        btnLayout->setColumnMinimumWidth(1, 800); // Ensure content toolbars have adequate width for double row
        btnLayout->addWidget(essentialToolBar, 0, 0, 1, 1, Qt::AlignTop | Qt::AlignLeft); // Essential bar in the top row of column 0...
        btnLayout->addWidget(buildTabToolsBar(btnLayout->parentWidget(), essentialIconSize), 1, 0, 1, 1, Qt::AlignTop | Qt::AlignLeft); // ...with the Phase 28 tab-tools row (New Tab / Split / Clone) below it
        btnLayout->addWidget(topToolBar, 0, 1, 1, 1);
        btnLayout->addWidget(bottomToolBar, 1, 1, 1, 1);
    } else {
        // Single-row mode
        QToolBar *toolBar = new QToolBar("Main", buttonBar);
        toolBar->setFloatable(false);
        toolBar->setContentsMargins(0, 0, 0, 0);
        toolBar->layout()->setSpacing(3);
        toolBar->setIconSize(QSize(iconSize, iconSize));
        toolBar->setStyleSheet("QToolBar { border: 0px }");

        // Add actions to toolbar based on order and enabled state
        for (const QString &actionId: actionOrder) {
            if (actionId.startsWith("separator") || actionId == "row_separator") {
                if (actionId != "row_separator") {
                    // Only add separator if it's enabled and there are actions in the toolbar and the last action isn't already a separator
                    if (enabledActions.contains(actionId) && toolBar->actions().count() > 0) {
                        QAction *lastAction = toolBar->actions().last();
                        if (!lastAction->isSeparator()) {
                            toolBar->addSeparator();
                        }
                    }
                }
                continue;
            }

            // Skip disabled actions only if customization is enabled
            if (customizeEnabled && !enabledActions.isEmpty() && !enabledActions.contains(actionId)) {
                continue; // Skip disabled actions
            }

            QAction *action = getActionById(actionId);

            // Special handling for open action to add recent files menu
            if (actionId == "open" && action) {
                // Create a new action with the recent files menu for the toolbar
                QAction *openWithRecentAction = new QAction(action->text(), toolBar);
                Appearance::setActionIcon(openWithRecentAction, ":/run_environment/graphics/tool/load.png");
                openWithRecentAction->setShortcut(action->shortcut());
                openWithRecentAction->setToolTip(action->toolTip());
                connect(openWithRecentAction, SIGNAL(triggered()), this, SLOT(load()));
                if (_recentPathsMenu) {
                    openWithRecentAction->setMenu(_recentPathsMenu);
                }
                action = openWithRecentAction;
            }

            if (!action) {
                // Handle special actions that need custom creation
                if (actionId == "open") {
                    // Create special open action with recent files menu
                    action = new QAction(tr("Open..."), toolBar);
                    Appearance::setActionIcon(action, ":/run_environment/graphics/tool/load.png");
                    connect(action, SIGNAL(triggered()), this, SLOT(load()));
                    if (_recentPathsMenu) {
                        action->setMenu(_recentPathsMenu);
                    }
                } else if (actionId == "paste") {
                    // Create special paste action with options menu
                    action = new QAction(tr("Paste events"), toolBar);
                    action->setToolTip(tr("Paste events at cursor position"));
                    Appearance::setActionIcon(action, ":/run_environment/graphics/tool/paste.png");
                    connect(action, SIGNAL(triggered()), this, SLOT(paste()));
                    if (_pasteOptionsMenu) {
                        action->setMenu(_pasteOptionsMenu);
                    }
                } else {
                    // Create a placeholder action if the real one doesn't exist
                    action = new QAction(actionId, toolBar);
                    action->setEnabled(false);
                    action->setToolTip("Action not yet implemented: " + actionId);

                    // Try to find the icon path for this action from our default list
                    try {
                        QList<ToolbarActionInfo> defaultActions = getDefaultActionsForPlaceholder();
                        for (const ToolbarActionInfo &info: defaultActions) {
                            if (info.id == actionId && !info.iconPath.isEmpty()) {
                                Appearance::setActionIcon(action, info.iconPath);
                                break;
                            }
                        }
                    } catch (...) {
                        // If icon setting fails, just continue without icon
                    }
                }
            }

            // Special handling for midi_visualizer: create widget directly
            if (actionId == "midi_visualizer") {
                _visualizer = new MidiVisualizerWidget(toolBar);
                connect(MidiPlayer::playerThread(), SIGNAL(playerStarted()), _visualizer, SLOT(playbackStarted()));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _visualizer, SLOT(playbackStopped()));
                toolBar->addWidget(_visualizer);
                continue;
            }
            // Phase 41: retro cursor-time display.
            if (actionId == "time_display") {
                _timeDisplay = new TimeDisplayWidget(toolBar);
                _timeDisplay->setFile(file);
                connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _timeDisplay, SLOT(onPlaybackPositionChanged(int)));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _timeDisplay, SLOT(onPlaybackStopped()));
                toolBar->addWidget(_timeDisplay);
                continue;
            }
            // Special handling for lyric_visualizer: create karaoke widget
            if (actionId == "lyric_visualizer") {
                _lyricVisualizer = new LyricVisualizerWidget(toolBar);
                _lyricVisualizer->setFile(file);
                connect(MidiPlayer::playerThread(), SIGNAL(playerStarted()), _lyricVisualizer, SLOT(playbackStarted()));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _lyricVisualizer, SLOT(playbackStopped()));
                connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _lyricVisualizer, SLOT(onPlaybackPositionChanged(int)));
                toolBar->addWidget(_lyricVisualizer);
                continue;
            }
            // Special handling for mcp_toggle: create MCP server toggle widget
            if (actionId == "mcp_toggle" && _mcpServer) {
                _mcpToggleWidget = new McpToggleWidget(_mcpServer, toolBar);
                toolBar->addWidget(_mcpToggleWidget);
                continue;
            }
            // Special handling for ffxiv_toggle: create FFXIV SoundFont Mode toggle widget
            if (actionId == "ffxiv_toggle") {
                _ffxivToggleWidget = new FfxivToggleWidget(toolBar);
                toolBar->addWidget(_ffxivToggleWidget);
                continue;
            }
            if (actionId == "c64_toggle") {
                _c64ToggleWidget = new C64ToggleWidget(toolBar);
                toolBar->addWidget(_c64ToggleWidget);
                continue;
            }
            if (actionId == "c64_mode_switch") {
                _c64ModeSwitch = new C64ModeSwitchWidget(toolBar);
                _c64ModeSwitch->setToolbarAction(toolBar->addWidget(_c64ModeSwitch));
                continue;
            }
            // Special handling for ffxiv_voice_gauge: FFXIV voice-load gauge (Phase 32.1)
            if (actionId == "ffxiv_voice_gauge") {
                _ffxivVoiceGauge = new FfxivVoiceGaugeWidget(toolBar);
                _ffxivVoiceGauge->setFile(file);
                toolBar->addWidget(_ffxivVoiceGauge);
                continue;
            }
            // Special handling for ffxiv_toggle: create FFXIV SoundFont Mode toggle widget
            if (actionId == "ffxiv_toggle") {
                _ffxivToggleWidget = new FfxivToggleWidget(toolBar);
                toolBar->addWidget(_ffxivToggleWidget);
                continue;
            }
            if (actionId == "c64_toggle") {
                _c64ToggleWidget = new C64ToggleWidget(toolBar);
                toolBar->addWidget(_c64ToggleWidget);
                continue;
            }
            if (actionId == "c64_mode_switch") {
                _c64ModeSwitch = new C64ModeSwitchWidget(toolBar);
                _c64ModeSwitch->setToolbarAction(toolBar->addWidget(_c64ModeSwitch));
                continue;
            }
            // Special handling for ffxiv_voice_gauge: FFXIV voice-load gauge (Phase 32.1)
            if (actionId == "ffxiv_voice_gauge") {
                _ffxivVoiceGauge = new FfxivVoiceGaugeWidget(toolBar);
                _ffxivVoiceGauge->setFile(file);
                toolBar->addWidget(_ffxivVoiceGauge);
                continue;
            }

            if (action) {
                try {
                    toolBar->addAction(action);

                    // In two-row mode, add text labels only for essential actions
                    QStringList essentialIds = LayoutSettingsWidget::getEssentialActionIds();
                    if (twoRowMode && essentialIds.contains(actionId) && !actionId.startsWith("separator")) {
                        // Set the toolbar style for this specific action
                        QWidget *toolButton = toolBar->widgetForAction(action);
                        if (QToolButton *button = qobject_cast<QToolButton *>(toolButton)) {
                            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                            // Phase 28: keep essential icons at row size so the
                            // block stays flush with the two-row toolbar.
                            button->setIconSize(QSize(iconSize, iconSize));
                        }
                    }
                } catch (...) {
                    // If adding action fails, skip it
                }
            }
        }

        // Remove any trailing separators
        removeTrailingSeparators(toolBar);

        btnLayout->setColumnStretch(4, 1);
        btnLayout->addWidget(toolBar, 0, 0, 1, 1);
    }

    return buttonBar;
}

void MainWindow::updateToolbarContents(QWidget *toolbarWidget, QGridLayout *btnLayout) {
    // This method updates the contents of an existing toolbar widget without replacing it
    // It uses the same logic as createCustomToolbar but works with an existing widget

    btnLayout->setSpacing(0);
    toolbarWidget->setContentsMargins(0, 0, 0, 0);

    // Get current settings
    bool twoRowMode = false;
    QStringList actionOrder;
    QStringList enabledActions;
    bool customizeEnabled = false;

    try {
        twoRowMode = Appearance::toolbarTwoRowMode();
        customizeEnabled = Appearance::toolbarCustomizeEnabled();
        actionOrder = Appearance::toolbarActionOrder();
        enabledActions = Appearance::toolbarEnabledActions();
    } catch (...) {
        // If there's any issue with settings, use safe defaults
        twoRowMode = false;
        customizeEnabled = false;
        actionOrder.clear();
        enabledActions.clear();
    }

    // Essential actions that can't be disabled (only for single row mode)
    QStringList essentialActions;
    if (!twoRowMode) {
        essentialActions = LayoutSettingsWidget::getEssentialActionIds();
    }

    // Use custom settings only if customization is enabled AND we have valid action order
    // If customization is disabled, always use defaults regardless of stored action order
    // Fresh installs (or first launch after settings reset) also fall through to the
    // default loader so the toolbar isn't empty when customization is on but no order
    // has ever been persisted yet (TOOLBAR-DEFAULT-001 follow-up).
    const bool firstRunNoOrder = actionOrder.isEmpty();
    if (!customizeEnabled || firstRunNoOrder) {
        // Use minimal default toolbar (not comprehensive - that's for customize UI only)
        actionOrder = LayoutSettingsWidget::getDefaultToolbarOrder();

        if (twoRowMode) {
            // Add row separator for double row mode using default toolbar row distribution
            QStringList row1Actions, row2Actions;
            LayoutSettingsWidget::getDefaultToolbarRowDistribution(row1Actions, row2Actions);

            // Rebuild action order with row separator
            actionOrder.clear();
            actionOrder << row1Actions << "row_separator" << row2Actions;
        }

        // Use default toolbar enabled actions (all actions in default toolbar are enabled)
        enabledActions = LayoutSettingsWidget::getDefaultToolbarEnabledActions();
    } else {
        // Migration: add new actions to saved custom settings.
        // Only enable by default when the action is genuinely new (not yet in actionOrder).
        // If it's already in actionOrder but not in enabledActions, the user disabled it.
        if (!actionOrder.contains("toggle_midipilot")) {
            actionOrder << "separator14" << "toggle_midipilot";
            if (!enabledActions.contains("toggle_midipilot"))
                enabledActions << "toggle_midipilot";
        }
        if (!actionOrder.contains("fix_ffxiv_channels")) {
            int sep14Idx = actionOrder.indexOf("separator14");
            if (sep14Idx >= 0)
                actionOrder.insert(sep14Idx, "fix_ffxiv_channels");
            else
                actionOrder << "fix_ffxiv_channels";
            if (!enabledActions.contains("fix_ffxiv_channels"))
                enabledActions << "fix_ffxiv_channels";
        }
        if (!actionOrder.contains("explode_chords_to_tracks")) {
            int splitIdx = actionOrder.indexOf("split_channels_to_tracks");
            if (splitIdx >= 0)
                actionOrder.insert(splitIdx, "explode_chords_to_tracks");
            else
                actionOrder << "explode_chords_to_tracks";
            if (!enabledActions.contains("explode_chords_to_tracks"))
                enabledActions << "explode_chords_to_tracks";
        }
        if (!actionOrder.contains("split_channels_to_tracks")) {
            int ffxivIdx = actionOrder.indexOf("fix_ffxiv_channels");
            if (ffxivIdx >= 0)
                actionOrder.insert(ffxivIdx, "split_channels_to_tracks");
            else
                actionOrder << "split_channels_to_tracks";
            if (!enabledActions.contains("split_channels_to_tracks"))
                enabledActions << "split_channels_to_tracks";
        }
        if (!actionOrder.contains("midi_visualizer")) {
            int sep14Idx = actionOrder.indexOf("separator14");
            if (sep14Idx >= 0)
                actionOrder.insert(sep14Idx, "midi_visualizer");
            else
                actionOrder << "midi_visualizer";
            if (!enabledActions.contains("midi_visualizer"))
                enabledActions << "midi_visualizer";
        }
        if (!actionOrder.contains("lyric_visualizer")) {
            int visIdx = actionOrder.indexOf("midi_visualizer");
            if (visIdx >= 0)
                actionOrder.insert(visIdx + 1, "lyric_visualizer");
            else
                actionOrder << "lyric_visualizer";
            if (!enabledActions.contains("lyric_visualizer"))
                enabledActions << "lyric_visualizer";
        }
        if (!actionOrder.contains("mcp_toggle")) {
            int lyricIdx = actionOrder.indexOf("lyric_visualizer");
            if (lyricIdx >= 0)
                actionOrder.insert(lyricIdx + 1, "mcp_toggle");
            else
                actionOrder << "mcp_toggle";
            if (!enabledActions.contains("mcp_toggle"))
                enabledActions << "mcp_toggle";
        }
        if (!actionOrder.contains("ffxiv_toggle")) {
            int mcpIdx = actionOrder.indexOf("mcp_toggle");
            if (mcpIdx >= 0)
                actionOrder.insert(mcpIdx + 1, "ffxiv_toggle");
            else
                actionOrder << "ffxiv_toggle";
            if (!enabledActions.contains("ffxiv_toggle"))
                enabledActions << "ffxiv_toggle";
        }
        // Phase 42.2: migrate existing layouts to include the C64 toggle.
        if (!actionOrder.contains("c64_toggle")) {
            int xivToggleIdx = actionOrder.indexOf("ffxiv_toggle");
            if (xivToggleIdx >= 0)
                actionOrder.insert(xivToggleIdx + 1, "c64_toggle");
            else
                actionOrder << "c64_toggle";
            if (!enabledActions.contains("c64_toggle"))
                enabledActions << "c64_toggle";
        }
        if (!actionOrder.contains("c64_mode_switch")) {
            int c64Idx = actionOrder.indexOf("c64_toggle");
            if (c64Idx >= 0)
                actionOrder.insert(c64Idx + 1, "c64_mode_switch");
            else
                actionOrder << "c64_mode_switch";
            if (!enabledActions.contains("c64_mode_switch"))
                enabledActions << "c64_mode_switch";
        }
        if (!actionOrder.contains("ffxiv_voice_gauge")) {
            int xivIdx = actionOrder.indexOf("ffxiv_toggle");
            if (xivIdx >= 0)
                actionOrder.insert(xivIdx + 1, "ffxiv_voice_gauge");
            else
                actionOrder << "ffxiv_voice_gauge";
            if (!enabledActions.contains("ffxiv_voice_gauge"))
                enabledActions << "ffxiv_voice_gauge";
        }
        // Phase 41: migrate existing saved layouts so the cursor-time
        // display appears (and is enabled) without a Reset to Default.
        if (!actionOrder.contains("time_display")) {
            int gaugeIdx = actionOrder.indexOf("ffxiv_voice_gauge");
            if (gaugeIdx >= 0)
                actionOrder.insert(gaugeIdx + 1, "time_display");
            else
                actionOrder << "time_display";
            if (!enabledActions.contains("time_display"))
                enabledActions << "time_display";
        }
    }

    // Only prepend essential actions for single row mode
    if (!twoRowMode) {
        QStringList finalActionOrder = essentialActions;
        finalActionOrder.append(actionOrder);
        actionOrder = finalActionOrder;
    }

    // Always enable essential actions (including their separators)
    for (const QString &actionId: essentialActions) {
        if (!enabledActions.contains(actionId)) {
            enabledActions << actionId;
        }
    }

    int iconSize = Appearance::toolbarIconSize();

    // CRITICAL: Reset all grid layout constraints before setting up new layout
    // This prevents issues when switching between single/double row modes

    for (int col = 0; col < 10; col++) { // Clear up to 10 columns
        btnLayout->setColumnStretch(col, 0);
        btnLayout->setColumnMinimumWidth(col, 0);
    }
    for (int row = 0; row < 5; row++) { // Clear up to 5 rows
        btnLayout->setRowStretch(row, 0);
        btnLayout->setRowMinimumHeight(row, 0);
    }

    if (twoRowMode) {
        // Create three separate toolbars: essential (larger), top row, bottom row
        QToolBar *essentialToolBar = new QToolBar("Essential", toolbarWidget);
        QToolBar *topToolBar = new QToolBar("Top", toolbarWidget);
        QToolBar *bottomToolBar = new QToolBar("Bottom", toolbarWidget);

        // Essential toolbar setup (larger icons)
        int essentialIconSize = iconSize;
        essentialToolBar->setFloatable(false);
        essentialToolBar->setContentsMargins(0, 0, 0, 0);
        essentialToolBar->layout()->setSpacing(3);
        essentialToolBar->setIconSize(QSize(essentialIconSize, essentialIconSize));
        essentialToolBar->setStyleSheet(Appearance::toolbarInlineStyle());
        essentialToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

        // Top toolbar setup
        topToolBar->setFloatable(false);
        topToolBar->setContentsMargins(0, 0, 0, 0);
        topToolBar->layout()->setSpacing(3);
        topToolBar->setIconSize(QSize(iconSize, iconSize));
        topToolBar->setStyleSheet(Appearance::toolbarInlineStyle());
        topToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        topToolBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        // Bottom toolbar setup
        bottomToolBar->setFloatable(false);
        bottomToolBar->setContentsMargins(0, 0, 0, 0);
        bottomToolBar->layout()->setSpacing(3);
        bottomToolBar->setIconSize(QSize(iconSize, iconSize));
        bottomToolBar->setStyleSheet(Appearance::toolbarInlineStyle());
        bottomToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        bottomToolBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        QToolBar *currentToolBar = topToolBar;

        // First, add essential actions to essential toolbar
        QStringList essentialActionsList = LayoutSettingsWidget::getEssentialActionIds();

        for (const QString &actionId: essentialActionsList) {
            if (actionId.startsWith("separator")) {
                essentialToolBar->addSeparator();
                continue;
            }

            QAction *action = getActionById(actionId);
            if (action) {
                // Special handling for open action to add recent files menu
                if (actionId == "open") {
                    QAction *openWithRecentAction = new QAction(action->text(), essentialToolBar);
                    Appearance::setActionIcon(openWithRecentAction, ":/run_environment/graphics/tool/load.png");
                    openWithRecentAction->setShortcut(action->shortcut());
                    openWithRecentAction->setToolTip(action->toolTip());
                    connect(openWithRecentAction, SIGNAL(triggered()), this, SLOT(load()));
                    if (_recentPathsMenu) {
                        openWithRecentAction->setMenu(_recentPathsMenu);
                    }
                    action = openWithRecentAction;
                }

                essentialToolBar->addAction(action);

                // Set text under icon for essential actions
                QWidget *toolButton = essentialToolBar->widgetForAction(action);
                if (QToolButton *button = qobject_cast<QToolButton *>(toolButton)) {
                    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                }
            }
        }

        // Add actions to appropriate toolbar
        QStringList essentialIds = LayoutSettingsWidget::getEssentialActionIds();
        for (const QString &actionId: actionOrder) {
            // Essential actions are already handled above, skip them here
            if (essentialIds.contains(actionId)) {
                continue;
            }

            if (actionId == "row_separator") {
                currentToolBar = bottomToolBar;
                continue;
            }

            if (actionId.startsWith("separator")) {
                if (enabledActions.contains(actionId) && !currentToolBar->actions().isEmpty()) {
                    QAction *lastAction = currentToolBar->actions().last();
                    // Avoid adding a separator if the last action was already one.
                    if (lastAction && !lastAction->isSeparator()) {
                        currentToolBar->addSeparator();
                    }
                }
                continue;
            }

            // Skip disabled actions only if customization is enabled
            if (customizeEnabled && !enabledActions.isEmpty() && !enabledActions.contains(actionId)) {
                continue; // Skip disabled actions
            }

            // Use current toolbar for all non-essential actions

            QAction *action = getActionById(actionId);

            // Special handling for open action to add recent files menu (for non-essential open actions)
            if (actionId == "open" && action) {
                // Create a new action with the recent files menu for the toolbar
                QAction *openWithRecentAction = new QAction(action->text(), currentToolBar);
                Appearance::setActionIcon(openWithRecentAction, ":/run_environment/graphics/tool/load.png");
                openWithRecentAction->setShortcut(action->shortcut());
                openWithRecentAction->setToolTip(action->toolTip());
                connect(openWithRecentAction, SIGNAL(triggered()), this, SLOT(load()));
                if (_recentPathsMenu) {
                    openWithRecentAction->setMenu(_recentPathsMenu);
                }
                action = openWithRecentAction;
            }

            if (!action) {
                // Handle special actions that need custom creation
                if (actionId == "open") {
                    // Create special open action with recent files menu
                    action = new QAction(tr("Open..."), currentToolBar);
                    Appearance::setActionIcon(action, ":/run_environment/graphics/tool/load.png");
                    connect(action, SIGNAL(triggered()), this, SLOT(load()));
                    if (_recentPathsMenu) {
                        action->setMenu(_recentPathsMenu);
                    }
                } else if (actionId == "paste") {
                    // Create paste action if not found
                    action = new QAction(tr("Paste"), currentToolBar);
                    Appearance::setActionIcon(action, ":/run_environment/graphics/tool/paste.png");
                    connect(action, SIGNAL(triggered()), this, SLOT(paste()));
                }
            }

            // Special handling for midi_visualizer: create widget directly
            if (actionId == "midi_visualizer") {
                _visualizer = new MidiVisualizerWidget(currentToolBar);
                connect(MidiPlayer::playerThread(), SIGNAL(playerStarted()), _visualizer, SLOT(playbackStarted()));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _visualizer, SLOT(playbackStopped()));
                currentToolBar->addWidget(_visualizer);
                continue;
            }
            // Phase 41: retro cursor-time display.
            if (actionId == "time_display") {
                _timeDisplay = new TimeDisplayWidget(currentToolBar);
                _timeDisplay->setFile(file);
                connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _timeDisplay, SLOT(onPlaybackPositionChanged(int)));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _timeDisplay, SLOT(onPlaybackStopped()));
                currentToolBar->addWidget(_timeDisplay);
                continue;
            }
            // Special handling for lyric_visualizer: create karaoke widget
            if (actionId == "lyric_visualizer") {
                _lyricVisualizer = new LyricVisualizerWidget(currentToolBar);
                _lyricVisualizer->setFile(file);
                connect(MidiPlayer::playerThread(), SIGNAL(playerStarted()), _lyricVisualizer, SLOT(playbackStarted()));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _lyricVisualizer, SLOT(playbackStopped()));
                connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _lyricVisualizer, SLOT(onPlaybackPositionChanged(int)));
                currentToolBar->addWidget(_lyricVisualizer);
                continue;
            }
            // Special handling for mcp_toggle: create MCP server toggle widget
            if (actionId == "mcp_toggle" && _mcpServer) {
                _mcpToggleWidget = new McpToggleWidget(_mcpServer, currentToolBar);
                currentToolBar->addWidget(_mcpToggleWidget);
                continue;
            }
            // Special handling for ffxiv_toggle: create FFXIV SoundFont Mode toggle widget
            if (actionId == "ffxiv_toggle") {
                _ffxivToggleWidget = new FfxivToggleWidget(currentToolBar);
                currentToolBar->addWidget(_ffxivToggleWidget);
                continue;
            }
            if (actionId == "c64_toggle") {
                _c64ToggleWidget = new C64ToggleWidget(currentToolBar);
                currentToolBar->addWidget(_c64ToggleWidget);
                continue;
            }
            if (actionId == "c64_mode_switch") {
                _c64ModeSwitch = new C64ModeSwitchWidget(currentToolBar);
                _c64ModeSwitch->setToolbarAction(currentToolBar->addWidget(_c64ModeSwitch));
                continue;
            }
            // Special handling for ffxiv_voice_gauge: FFXIV voice-load gauge (Phase 32.1)
            if (actionId == "ffxiv_voice_gauge") {
                _ffxivVoiceGauge = new FfxivVoiceGaugeWidget(currentToolBar);
                _ffxivVoiceGauge->setFile(file);
                currentToolBar->addWidget(_ffxivVoiceGauge);
                continue;
            }

            if (action) {
                try {
                    currentToolBar->addAction(action);
                } catch (...) {
                    // If adding action fails, skip it
                }
            }
        }

        // Remove any trailing separators from content toolbars only (not essential toolbar)
        removeTrailingSeparators(topToolBar);
        removeTrailingSeparators(bottomToolBar);

        // Layout: Essential toolbar on left, content toolbars stacked on right
        btnLayout->setColumnStretch(1, 1);
        btnLayout->setColumnMinimumWidth(1, 800); // Ensure content toolbars have adequate width for double row
        btnLayout->addWidget(essentialToolBar, 0, 0, 1, 1, Qt::AlignTop | Qt::AlignLeft); // Essential bar in the top row of column 0...
        btnLayout->addWidget(buildTabToolsBar(btnLayout->parentWidget(), essentialIconSize), 1, 0, 1, 1, Qt::AlignTop | Qt::AlignLeft); // ...with the Phase 28 tab-tools row (New Tab / Split / Clone) below it
        btnLayout->addWidget(topToolBar, 0, 1, 1, 1);
        btnLayout->addWidget(bottomToolBar, 1, 1, 1, 1);
    } else {
        // Single-row mode: Create one toolbar
        QToolBar *toolBar = new QToolBar("Main", toolbarWidget);
        toolBar->setFloatable(false);
        toolBar->setContentsMargins(0, 0, 0, 0);
        toolBar->layout()->setSpacing(3);
        toolBar->setIconSize(QSize(iconSize, iconSize));
        toolBar->setStyleSheet(Appearance::toolbarInlineStyle());

        // Add actions to toolbar based on order and enabled state
        for (const QString &actionId: actionOrder) {
            if (actionId.startsWith("separator") || actionId == "row_separator") {
                if (actionId != "row_separator") {
                    // Only add separator if it's enabled and there are actions in the toolbar and the last action isn't already a separator
                    if (enabledActions.contains(actionId) && toolBar->actions().count() > 0) {
                        QAction *lastAction = toolBar->actions().last();
                        if (!lastAction->isSeparator()) {
                            toolBar->addSeparator();
                        }
                    }
                }
                continue;
            }

            // Skip disabled actions only if customization is enabled
            if (customizeEnabled && !enabledActions.isEmpty() && !enabledActions.contains(actionId)) {
                continue; // Skip disabled actions
            }

            QAction *action = getActionById(actionId);

            // Special handling for open action to add recent files menu
            if (actionId == "open" && action) {
                // Create a new action with the recent files menu for the toolbar
                QAction *openWithRecentAction = new QAction(action->text(), toolBar);
                Appearance::setActionIcon(openWithRecentAction, ":/run_environment/graphics/tool/load.png");
                openWithRecentAction->setShortcut(action->shortcut());
                openWithRecentAction->setToolTip(action->toolTip());
                connect(openWithRecentAction, SIGNAL(triggered()), this, SLOT(load()));
                if (_recentPathsMenu) {
                    openWithRecentAction->setMenu(_recentPathsMenu);
                }
                action = openWithRecentAction;
            }

            if (!action) {
                // Handle special actions that need custom creation
                if (actionId == "open") {
                    // Create special open action with recent files menu
                    action = new QAction(tr("Open..."), toolBar);
                    Appearance::setActionIcon(action, ":/run_environment/graphics/tool/load.png");
                    connect(action, SIGNAL(triggered()), this, SLOT(load()));
                    if (_recentPathsMenu) {
                        action->setMenu(_recentPathsMenu);
                    }
                } else if (actionId == "paste") {
                    // Create paste action if not found
                    action = new QAction(tr("Paste"), toolBar);
                    Appearance::setActionIcon(action, ":/run_environment/graphics/tool/paste.png");
                    connect(action, SIGNAL(triggered()), this, SLOT(paste()));
                }
            }

            // Special handling for midi_visualizer: create widget directly
            if (actionId == "midi_visualizer") {
                _visualizer = new MidiVisualizerWidget(toolBar);
                connect(MidiPlayer::playerThread(), SIGNAL(playerStarted()), _visualizer, SLOT(playbackStarted()));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _visualizer, SLOT(playbackStopped()));
                toolBar->addWidget(_visualizer);
                continue;
            }
            // Phase 41: retro cursor-time display.
            if (actionId == "time_display") {
                _timeDisplay = new TimeDisplayWidget(toolBar);
                _timeDisplay->setFile(file);
                connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _timeDisplay, SLOT(onPlaybackPositionChanged(int)));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _timeDisplay, SLOT(onPlaybackStopped()));
                toolBar->addWidget(_timeDisplay);
                continue;
            }
            // Special handling for lyric_visualizer: create karaoke widget
            if (actionId == "lyric_visualizer") {
                _lyricVisualizer = new LyricVisualizerWidget(toolBar);
                _lyricVisualizer->setFile(file);
                connect(MidiPlayer::playerThread(), SIGNAL(playerStarted()), _lyricVisualizer, SLOT(playbackStarted()));
                connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()), _lyricVisualizer, SLOT(playbackStopped()));
                connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)), _lyricVisualizer, SLOT(onPlaybackPositionChanged(int)));
                toolBar->addWidget(_lyricVisualizer);
                continue;
            }
            // Special handling for mcp_toggle: create MCP server toggle widget
            if (actionId == "mcp_toggle" && _mcpServer) {
                _mcpToggleWidget = new McpToggleWidget(_mcpServer, toolBar);
                toolBar->addWidget(_mcpToggleWidget);
                continue;
            }
            // Special handling for ffxiv_toggle: create FFXIV SoundFont Mode toggle widget
            if (actionId == "ffxiv_toggle") {
                _ffxivToggleWidget = new FfxivToggleWidget(toolBar);
                toolBar->addWidget(_ffxivToggleWidget);
                continue;
            }
            if (actionId == "c64_toggle") {
                _c64ToggleWidget = new C64ToggleWidget(toolBar);
                toolBar->addWidget(_c64ToggleWidget);
                continue;
            }
            if (actionId == "c64_mode_switch") {
                _c64ModeSwitch = new C64ModeSwitchWidget(toolBar);
                _c64ModeSwitch->setToolbarAction(toolBar->addWidget(_c64ModeSwitch));
                continue;
            }
            // Special handling for ffxiv_voice_gauge: FFXIV voice-load gauge (Phase 32.1)
            if (actionId == "ffxiv_voice_gauge") {
                _ffxivVoiceGauge = new FfxivVoiceGaugeWidget(toolBar);
                _ffxivVoiceGauge->setFile(file);
                toolBar->addWidget(_ffxivVoiceGauge);
                continue;
            }

            if (action) {
                try {
                    toolBar->addAction(action);
                } catch (...) {
                    // If adding action fails, skip it
                }
            }
        }

        // Remove any trailing separators
        removeTrailingSeparators(toolBar);

        btnLayout->setColumnStretch(4, 1);
        btnLayout->addWidget(toolBar, 0, 0, 1, 1);
    }
}

QList<ToolbarActionInfo> MainWindow::getDefaultActionsForPlaceholder() {
    // This is a simplified version of the default actions list for placeholder icons
    // We include this here to avoid circular dependencies with LayoutSettingsWidget
    QList<ToolbarActionInfo> actions;

    actions << ToolbarActionInfo{"new", "New", ":/run_environment/graphics/tool/new.png", nullptr, true, true, "File"};
    actions << ToolbarActionInfo{"open", "Open", ":/run_environment/graphics/tool/load.png", nullptr, true, true, "File"};
    actions << ToolbarActionInfo{"save", "Save", ":/run_environment/graphics/tool/save.png", nullptr, true, true, "File"};
    actions << ToolbarActionInfo{"undo", "Undo", ":/run_environment/graphics/tool/undo.png", nullptr, true, true, "Edit"};
    actions << ToolbarActionInfo{"redo", "Redo", ":/run_environment/graphics/tool/redo.png", nullptr, true, true, "Edit"};
    actions << ToolbarActionInfo{"standard_tool", "Standard Tool", ":/run_environment/graphics/tool/select.png", nullptr, true, false, "Tools"};
    actions << ToolbarActionInfo{"select_left", "Select Left", ":/run_environment/graphics/tool/select_left.png", nullptr, true, false, "Tools"};
    actions << ToolbarActionInfo{"select_right", "Select Right", ":/run_environment/graphics/tool/select_right.png", nullptr, true, false, "Tools"};
    actions << ToolbarActionInfo{"new_note", "New Note", ":/run_environment/graphics/tool/newnote.png", nullptr, true, false, "Edit"};
    actions << ToolbarActionInfo{"remove_notes", "Remove Notes", ":/run_environment/graphics/tool/eraser.png", nullptr, true, false, "Edit"};
    actions << ToolbarActionInfo{"copy", "Copy", ":/run_environment/graphics/tool/copy.png", nullptr, true, false, "Edit"};
    actions << ToolbarActionInfo{"paste", "Paste", ":/run_environment/graphics/tool/paste.png", nullptr, true, false, "Edit"};
    actions << ToolbarActionInfo{"glue", "Glue Notes (Same Channel)", ":/run_environment/graphics/tool/glue.png", nullptr, true, false, "Tools"};
    actions << ToolbarActionInfo{"glue_all_channels", "Glue Notes (All Channels)", ":/run_environment/graphics/tool/glue.png", nullptr, true, false, "Tools"};
    actions << ToolbarActionInfo{"scissors", "Scissors", ":/run_environment/graphics/tool/scissors.png", nullptr, true, false, "Tools"};
    actions << ToolbarActionInfo{"delete_overlaps", "Delete Overlaps", ":/run_environment/graphics/tool/deleteoverlap.png", nullptr, true, false, "Tools"};
    actions << ToolbarActionInfo{ "size_change", "Size Change", ":/run_environment/graphics/tool/change_size.png", nullptr, true, false, "Tools"};
    actions << ToolbarActionInfo{"back_to_begin", "Back to Begin", ":/run_environment/graphics/tool/back_to_begin.png", nullptr, true, false, "Playback"};
    actions << ToolbarActionInfo{"back_marker", "Back Marker", ":/run_environment/graphics/tool/back_marker.png", nullptr, true, false, "Playback"};
    actions << ToolbarActionInfo{"back", "Back", ":/run_environment/graphics/tool/back.png", nullptr, true, false, "Playback"};
    actions << ToolbarActionInfo{"play", "Play", ":/run_environment/graphics/tool/play.png", nullptr, true, false, "Playback"};
    actions << ToolbarActionInfo{"pause", "Pause", ":/run_environment/graphics/tool/pause.png", nullptr, true, false, "Playback"};
    actions << ToolbarActionInfo{"stop", "Stop", ":/run_environment/graphics/tool/stop.png", nullptr, true, false, "Playback"};
    actions << ToolbarActionInfo{"record", "Record", ":/run_environment/graphics/tool/record.png", nullptr, true, false, "Playback"};
    actions << ToolbarActionInfo{"forward", "Forward", ":/run_environment/graphics/tool/forward.png", nullptr, true, false, "Playback"};
    actions << ToolbarActionInfo{"forward_marker", "Forward Marker", ":/run_environment/graphics/tool/forward_marker.png", nullptr, true, false, "Playback"};
    actions << ToolbarActionInfo{"metronome", "Metronome", ":/run_environment/graphics/tool/metronome.png", nullptr, true, false, "Playback"};
    actions << ToolbarActionInfo{"align_left", "Align Left", ":/run_environment/graphics/tool/align_left.png", nullptr, true, false, "Tools"};
    actions << ToolbarActionInfo{"equalize", "Equalize", ":/run_environment/graphics/tool/equalize.png", nullptr, true, false, "Tools"};
    actions << ToolbarActionInfo{"align_right", "Align Right", ":/run_environment/graphics/tool/align_right.png", nullptr, true, false, "Tools"};
    actions << ToolbarActionInfo{"zoom_hor_in", "Zoom Horizontal In", ":/run_environment/graphics/tool/zoom_hor_in.png", nullptr, true, false, "View"};
    actions << ToolbarActionInfo{"zoom_hor_out", "Zoom Horizontal Out", ":/run_environment/graphics/tool/zoom_hor_out.png", nullptr, true, false, "View"};
    actions << ToolbarActionInfo{"zoom_ver_in", "Zoom Vertical In", ":/run_environment/graphics/tool/zoom_ver_in.png", nullptr, true, false, "View"};
    actions << ToolbarActionInfo{"zoom_ver_out", "Zoom Vertical Out", ":/run_environment/graphics/tool/zoom_ver_out.png", nullptr, true, false, "View"};
    actions << ToolbarActionInfo{"lock", "Lock Screen", ":/run_environment/graphics/tool/screen_unlocked.png", nullptr, true, false, "View"};
    actions << ToolbarActionInfo{"quantize", "Quantize", ":/run_environment/graphics/tool/quantize.png", nullptr, true, false, "Tools"};
    actions << ToolbarActionInfo{"magnet", "Magnet", ":/run_environment/graphics/tool/magnet.png", nullptr, true, false, "Tools"};
    actions << ToolbarActionInfo{"thru", "MIDI Thru", ":/run_environment/graphics/tool/connection.png", nullptr, true, false, "MIDI"};
    actions << ToolbarActionInfo{"measure", "Measure", ":/run_environment/graphics/tool/measure.png", nullptr, true, false, "View"};
    actions << ToolbarActionInfo{"time_signature", "Time Signature", ":/run_environment/graphics/tool/meter.png", nullptr, true, false, "View"};
    actions << ToolbarActionInfo{"tempo", "Tempo", ":/run_environment/graphics/tool/tempo.png", nullptr, true, false, "View"};

    return actions;
}

void MainWindow::applyStoredShortcuts() {
    if (!_settings) return;
    _settings->beginGroup("shortcuts");
    for (auto it = _actionMap.constBegin(); it != _actionMap.constEnd(); ++it) {
        const QString &id = it.key();
        QAction *action = it.value();
        if (!action) continue;
        QVariant v = _settings->value(id);
        if (!v.isValid()) continue;
        QStringList seqStrings = v.toStringList();
        QList<QKeySequence> seqs;
        for (const QString &s: seqStrings) {
            if (s.trimmed().isEmpty()) continue;
            seqs << QKeySequence::fromString(s.trimmed());
        }
        if (!seqs.isEmpty()) {
            if (seqs.size() == 1) action->setShortcut(seqs.first());
            else action->setShortcuts(seqs);
        }
    }
    _settings->endGroup();
}

void MainWindow::setActionShortcuts(const QString &actionId, const QList<QKeySequence> &seqs) {
    QAction *action = getActionById(actionId);
    if (!action) return;
    if (seqs.size() <= 1) {
        if (seqs.isEmpty()) action->setShortcut(QKeySequence());
        else action->setShortcut(seqs.first());
    } else {
        action->setShortcuts(seqs);
    }
}

void MainWindow::togglePianoEmulation(bool mode) {
    mw_matrixWidget->setPianoEmulation(mode);
}

void MainWindow::quantizationChanged(QAction *action) {
    _quantizationGrid = action->data().toInt();
}

void MainWindow::convertTempoPreserveDuration() {
    if (!file) {
        return;
    }
    TempoConversionScopeHint hint;
    hint.scope = TempoConversionScope::WholeProject;
    TempoConversionDialog dialog(file, hint, this);
    dialog.exec();
}

void MainWindow::convertTempoForSelection() {
    if (!file) {
        return;
    }
    TempoConversionScopeHint hint;
    const QList<MidiEvent *> sel = Selection::instance()->selectedEvents();
    if (sel.isEmpty()) {
        hint.scope = TempoConversionScope::WholeProject;
    } else {
        hint.scope = TempoConversionScope::SelectedEvents;
        for (MidiEvent *ev : sel) {
            if (ev) {
                hint.selectedEventPtrs.insert(reinterpret_cast<quintptr>(ev));
            }
        }
    }
    TempoConversionDialog dialog(file, hint, this);
    dialog.exec();
}

void MainWindow::convertTempoForTrack(int trackNumber) {
    if (!file) {
        return;
    }
    TempoConversionScopeHint hint;
    hint.scope = TempoConversionScope::SelectedTracks;
    hint.trackIds.insert(trackNumber);
    TempoConversionDialog dialog(file, hint, this);
    dialog.exec();
}

void MainWindow::convertTempoForChannel(int channel) {
    if (!file) {
        return;
    }
    TempoConversionScopeHint hint;
    hint.scope = TempoConversionScope::SelectedChannels;
    hint.channelIds.insert(channel);
    TempoConversionDialog dialog(file, hint, this);
    dialog.exec();
}

void MainWindow::quantizeSelection() {
    if (!file) {
        return;
    }

    // get list with all quantization ticks
    QList<int> ticks = file->quantization(_quantizationGrid);

    file->protocol()->startNewAction(tr("Quantify events"), new QImage(":/run_environment/graphics/tool/quantize.png"));
    foreach(MidiEvent* e, Selection::instance()->selectedEvents()) {
        int onTime = e->midiTime();
        e->setMidiTime(quantize(onTime, ticks));
        OnEvent *on = dynamic_cast<OnEvent *>(e);
        if (on) {
            MidiEvent *off = on->offEvent();
            off->setMidiTime(quantize(off->midiTime(), ticks) - 1);
            if (off->midiTime() <= on->midiTime()) {
                int idx = ticks.indexOf(off->midiTime() + 1);
                if ((idx >= 0) && (ticks.size() > idx + 1)) {
                    off->setMidiTime(ticks.at(idx + 1) - 1);
                }
            }
        }
    }
    file->protocol()->endAction();
}

int MainWindow::quantize(int t, QList<int> ticks) {
    int min = -1;

    for (int j = 0; j < ticks.size(); j++) {
        if (min < 0) {
            min = j;
            continue;
        }

        int i = ticks.at(j);

        int dist = t - i;

        int a = std::abs(dist);
        int b = std::abs(t - ticks.at(min));

        if (a < b) {
            min = j;
        }

        if (dist < 0) {
            return ticks.at(min);
        }
    }
    return ticks.last();
}

void MainWindow::quantizeNtoleDialog() {
    if (!file || Selection::instance()->selectedEvents().isEmpty()) {
        return;
    }

    NToleQuantizationDialog *d = new NToleQuantizationDialog(this);
    d->setModal(true);
    if (d->exec()) {
        quantizeNtole();
    }
}

void MainWindow::quantizeNtole() {
    if (!file || Selection::instance()->selectedEvents().isEmpty()) {
        return;
    }

    // get list with all quantization ticks
    QList<int> ticks = file->quantization(_quantizationGrid);

    file->protocol()->startNewAction(tr("Quantify tuplet"), new QImage(":/run_environment/graphics/tool/quantize.png"));

    // find minimum starting time
    int startTick = -1;
    foreach(MidiEvent* e, Selection::instance()->selectedEvents()) {
        int onTime = e->midiTime();
        if ((startTick < 0) || (onTime < startTick)) {
            startTick = onTime;
        }
    }

    // quantize start tick
    startTick = quantize(startTick, ticks);

    // compute new quantization grid
    QList<int> ntoleTicks;
    int ticksDuration = (NToleQuantizationDialog::replaceNumNum * file->ticksPerQuarter() * 4) / (qPow(2, NToleQuantizationDialog::replaceDenomNum));
    int fractionSize = ticksDuration / NToleQuantizationDialog::ntoleNNum;

    for (int i = 0; i <= NToleQuantizationDialog::ntoleNNum; i++) {
        ntoleTicks.append(startTick + i * fractionSize);
    }

    // quantize
    foreach(MidiEvent* e, Selection::instance()->selectedEvents()) {
        int onTime = e->midiTime();
        e->setMidiTime(quantize(onTime, ntoleTicks));
        OnEvent *on = dynamic_cast<OnEvent *>(e);
        if (on) {
            MidiEvent *off = on->offEvent();
            off->setMidiTime(quantize(off->midiTime(), ntoleTicks));
            if (off->midiTime() == on->midiTime()) {
                int idx = ntoleTicks.indexOf(off->midiTime());
                if ((idx >= 0) && (ntoleTicks.size() > idx + 1)) {
                    off->setMidiTime(ntoleTicks.at(idx + 1));
                } else if ((ntoleTicks.size() == idx + 1)) {
                    on->setMidiTime(ntoleTicks.at(idx - 1));
                }
            }
        }
    }
    file->protocol()->endAction();
}

void MainWindow::setSpeed(QAction *action) {
    double d = action->data().toDouble();
    MidiPlayer::setSpeedScale(d);
}

void MainWindow::checkEnableActionsForSelection() {
    bool enabled = Selection::instance()->selectedEvents().size() > 0;
    foreach(QAction* action, _activateWithSelections) {
        action->setEnabled(enabled);
    }
    if (_moveSelectedEventsToChannelMenu) {
        _moveSelectedEventsToChannelMenu->setEnabled(enabled);
    }
    if (_moveSelectedEventsToTrackMenu) {
        _moveSelectedEventsToTrackMenu->setEnabled(enabled);
    }
    if (_copySelectedEventsToChannelMenu) {
        _copySelectedEventsToChannelMenu->setEnabled(enabled);
    }
    if (_copySelectedEventsToTrackMenu) {
        _copySelectedEventsToTrackMenu->setEnabled(enabled);
    }
    if (Tool::currentTool() && Tool::currentTool()->button() && !Tool::currentTool()->button()->isEnabled()) {
        stdToolAction->trigger();
    }
    if (file) {
        undoAction->setEnabled(file->protocol()->stepsBack() > 1);
        redoAction->setEnabled(file->protocol()->stepsForward() > 0);
    }
    if (_midiPilotWidget) {
        _midiPilotWidget->refreshContext();
    }
}

void MainWindow::toolChanged() {
    checkEnableActionsForSelection();
    _miscWidgetContainer->update();
    _matrixWidgetContainer->update();
}

void MainWindow::updateStatusBar() {
    if (!file) return;

    // Cursor position: measure and tick
    int tick = file->cursorTick();
    int startOfMeasure = 0, endOfMeasure = 0;
    int measureNum = file->measure(tick, &startOfMeasure, &endOfMeasure);
    int ticksInMeasure = (endOfMeasure > startOfMeasure) ? (endOfMeasure - startOfMeasure) : 1;
    int tickInMeasure = tick - startOfMeasure;
    int beat = (tickInMeasure * 4 / ticksInMeasure) + 1;
    _statusCursorLabel->setText(QString("M:%1 B:%2 | T:%3").arg(measureNum).arg(beat).arg(tick));

    // Selection info + chord detection
    const QList<MidiEvent *> sel = Selection::instance()->selectedEvents();
    QList<int> notes;
    for (MidiEvent *ev : sel) {
        NoteOnEvent *on = dynamic_cast<NoteOnEvent *>(ev);
        if (on) notes.append(on->note());
    }
    _statusSelectionLabel->setText(QString("%1 events").arg(sel.size()));

    if (!notes.isEmpty()) {
        QString chord = ChordDetector::detectChord(notes);
        _statusChordLabel->setText(chord.isEmpty() ? "" : chord);
    } else {
        _statusChordLabel->setText("");
    }
}

void MainWindow::noteDurationSelected(QAction *action) {
    if (!action) return;

    int divisor = action->data().toInt();
    NewNoteTool::setDurationDivisor(divisor);

    if (divisor > 0) {
        if (_actionMap.contains("new_note")) {
            _actionMap["new_note"]->trigger();
        }
    }
}

void MainWindow::copiedEventsChanged() {
    // If shared clipboard is available, always enable paste
    // The paste operation itself will handle whether there's data or not
    SharedClipboard *clipboard = SharedClipboard::instance();
    bool sharedClipboardAvailable = clipboard->initialize();

    bool enable = EventTool::copiedEvents->size() > 0 || sharedClipboardAvailable;

    if (_pasteAction) {
        _pasteAction->setEnabled(enable);
    }
}

void MainWindow::updateAll() {
    // Update cached rendering settings when settings change
    // This ensures MatrixWidget uses the latest performance settings without
    // expensive QSettings I/O operations during paint events
    mw_matrixWidget->updateRenderingSettings();

    // Update all widgets
    channelWidget->update();
    _trackWidget->update();
    _miscWidgetContainer->update();

    // Refresh channel names in context menus (Move to channel, Delete channel, etc.)
    updateChannelMenu();

    // Reload MCP Server settings (start/stop as needed)
    if (_mcpServer) {
        bool wantEnabled = _settings->value("MCP/enabled", false).toBool();
        quint16 wantPort = _settings->value("MCP/port", 9420).toInt();
        QString wantToken = _settings->value("MCP/auth_token").toString();

        _mcpServer->setAuthToken(wantToken.isEmpty() ? QString() : wantToken);

        if (wantEnabled) {
            // Restart if port changed or not running
            if (!_mcpServer->isRunning() || _mcpServer->port() != wantPort) {
                _mcpServer->stop();
                _mcpServer->start(wantPort);
            }
        } else if (_mcpServer->isRunning()) {
            _mcpServer->stop();
        }
    }
}

void MainWindow::updateRenderingMode() {
    // SIMPLIFIED: Hardware acceleration toggle - requires restart for now
    bool hardwareAccelEnabled = _settings->value("rendering/hardware_acceleration", false).toBool();

    qDebug() << "MainWindow::updateRenderingMode() called - Hardware acceleration" << (hardwareAccelEnabled ? "enabled" : "disabled");

    // For now, just log the change - the new OpenGL widgets are created at startup
    // Runtime switching can be implemented later if needed
    if (hardwareAccelEnabled) {
        qDebug() << "MainWindow: Hardware acceleration enabled - using direct OpenGL widgets";
    } else {
        qDebug() << "MainWindow: Hardware acceleration disabled - using software widgets";
    }
}

void MainWindow::rebuildToolbarFromSettings() {
    // Dedicated method for rebuilding toolbar when settings change
    // Reentrancy guard only â€” no time-based debounce, because
    // refreshColors() needs this to run synchronously on every theme switch.
    static bool isRebuilding = false;

    if (isRebuilding) {
        return;
    }

    isRebuilding = true;

    if (_toolbarWidget) {
        try {
            // Clear existing toolbar contents while keeping the widget in place
            QGridLayout *toolbarLayout = qobject_cast<QGridLayout *>(_toolbarWidget->layout());
            if (toolbarLayout) {
                bool twoRowMode = Appearance::toolbarTwoRowMode();

                // Find and immediately delete all child toolbars
                QList<QToolBar *> childToolbars = _toolbarWidget->findChildren<QToolBar *>();

                // These widgets are children of the toolbars about to be
                // deleted; null every on-demand pointer (not just _visualizer)
                // so later guarded derefs don't hit freed memory (BUG-CORE-001).
                nullOnDemandToolbarWidgets();

                for (QToolBar *toolbar: childToolbars) {
                    toolbar->setParent(nullptr); // Remove from parent immediately
                    delete toolbar; // Delete immediately instead of deleteLater()
                }

                // Remove all layout items
                while (toolbarLayout->count() > 0) {
                    QLayoutItem *item = toolbarLayout->takeAt(0);
                    if (item) {
                        delete item;
                    }
                }

                // Reset toolbar size constraints to allow proper recalculation
                _toolbarWidget->setMinimumSize(0, 0);
                _toolbarWidget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

                // Force-refresh all registered action icons BEFORE rebuilding toolbar
                // so that dynamically created actions (like "Open with recent")
                // inherit the correct updated icon.
                Appearance::refreshAllIcons();

                // Now rebuild the toolbar contents directly in the existing widget
                updateToolbarContents(_toolbarWidget, toolbarLayout);

                // Update geometry without forcing window resize
                _toolbarWidget->updateGeometry();

                // Let the layout system recalculate naturally
                QTimer::singleShot(0, [this]() {
                    if (_toolbarWidget && _toolbarWidget->layout()) {
                        _toolbarWidget->layout()->invalidate();
                        _toolbarWidget->layout()->activate();
                    }
                });

                refreshToolbarIcons();
            } else {
                // If no layout found, fall back to complete rebuild
                rebuildToolbar();
                Appearance::refreshAllIcons();
                refreshToolbarIcons();
            }
        } catch (...) {
            // If update fails, fall back to complete rebuild
            rebuildToolbar();
            Appearance::refreshAllIcons();
            refreshToolbarIcons();
        }
    }

    // Reset the rebuilding guard
    isRebuilding = false;
}

void MainWindow::refreshToolbarIcons() {
    // The individual actions will be refreshed by Appearance::refreshAllIcons()
    // We just need to make sure our toolbar is up to date
    if (_toolbarWidget) {
        try {
            _toolbarWidget->update();

            // Also refresh any child toolbars
            QList<QToolBar *> toolbars = _toolbarWidget->findChildren<QToolBar *>();
            for (QToolBar *toolbar: toolbars) {
                if (toolbar) {
                    toolbar->update();
                }
            }
        } catch (...) {
            // Error refreshing toolbar icons
        }
    }
}

void MainWindow::tweakTime() {
    delete currentTweakTarget;
    currentTweakTarget = new TimeTweakTarget(this);
}

void MainWindow::tweakStartTime() {
    delete currentTweakTarget;
    currentTweakTarget = new StartTimeTweakTarget(this);
}

void MainWindow::tweakEndTime() {
    delete currentTweakTarget;
    currentTweakTarget = new EndTimeTweakTarget(this);
}

void MainWindow::tweakNote() {
    delete currentTweakTarget;
    currentTweakTarget = new NoteTweakTarget(this);
}

void MainWindow::tweakValue() {
    delete currentTweakTarget;
    currentTweakTarget = new ValueTweakTarget(this);
}

void MainWindow::tweakSmallDecrease() {
    currentTweakTarget->smallDecrease();
}

void MainWindow::tweakSmallIncrease() {
    currentTweakTarget->smallIncrease();
}

void MainWindow::tweakMediumDecrease() {
    currentTweakTarget->mediumDecrease();
}

void MainWindow::tweakMediumIncrease() {
    currentTweakTarget->mediumIncrease();
}

void MainWindow::tweakLargeDecrease() {
    currentTweakTarget->largeDecrease();
}

void MainWindow::tweakLargeIncrease() {
    currentTweakTarget->largeIncrease();
}

void MainWindow::navigateSelectionUp() {
    selectionNavigator->up();
}

void MainWindow::navigateSelectionDown() {
    selectionNavigator->down();
}

void MainWindow::navigateSelectionLeft() {
    selectionNavigator->left();
}

void MainWindow::navigateSelectionRight() {
    selectionNavigator->right();
}

void MainWindow::transposeSelectedNotesOctaveUp() {
    if (!file) {
        return;
    }

    QList<MidiEvent *> selectedEvents = Selection::instance()->selectedEvents();
    if (selectedEvents.isEmpty()) {
        return;
    }

    file->protocol()->startNewAction("Transpose octave up");

    foreach(MidiEvent* event, selectedEvents) {
        NoteOnEvent *noteOnEvent = dynamic_cast<NoteOnEvent *>(event);
        if (noteOnEvent) {
            int newNote = noteOnEvent->note() + 12; // Move up one octave (12 semitones)
            if (newNote <= 127) { // MIDI note range is 0-127
                noteOnEvent->setNote(newNote);
            }
        }
    }

    file->protocol()->endAction();
    updateAll();
}

void MainWindow::transposeSelectedNotesOctaveDown() {
    if (!file) {
        return;
    }

    QList<MidiEvent *> selectedEvents = Selection::instance()->selectedEvents();
    if (selectedEvents.isEmpty()) {
        return;
    }

    file->protocol()->startNewAction("Transpose octave down");

    foreach(MidiEvent* event, selectedEvents) {
        NoteOnEvent *noteOnEvent = dynamic_cast<NoteOnEvent *>(event);
        if (noteOnEvent) {
            int newNote = noteOnEvent->note() - 12; // Move down one octave (12 semitones)
            if (newNote >= 0) { // MIDI note range is 0-127
                noteOnEvent->setNote(newNote);
            }
        }
    }

    file->protocol()->endAction();
    updateAll();
}

void MainWindow::removeTrailingSeparators(QToolBar *toolbar) {
    if (!toolbar) return;

    QList<QAction *> actions = toolbar->actions();

    // Remove trailing separators from the end
    while (!actions.isEmpty() && actions.last()->isSeparator()) {
        QAction *lastAction = actions.takeLast();
        toolbar->removeAction(lastAction);
    }
}

void MainWindow::applyWidgetSizeConstraints() {
    // Check if widget size unlocking is enabled (requires restart)
    bool unlockSizes = _settings->value("unlock_widget_sizes", false).toBool();

    if (unlockSizes) {
        // Remove minimum size constraints to allow full resizing
        if (upperTabWidget) {
            upperTabWidget->setMinimumSize(0, 0);
            upperTabWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        }
        if (lowerTabWidget) {
            lowerTabWidget->setMinimumSize(0, 0);
            lowerTabWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        }
        if (chooserWidget) {
            chooserWidget->setMinimumWidth(0); // Allow width to resize/clip
            // Fix the height to prevent stretching when there are gaps
            chooserWidget->setMaximumHeight(chooserWidget->sizeHint().height());
            chooserWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        }
        if (tracksWidget) {
            tracksWidget->setMinimumSize(0, 0);
            tracksWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        }
        if (channelsWidget) {
            channelsWidget->setMinimumSize(0, 0);
            channelsWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        }
        if (rightSplitter) {
            rightSplitter->setChildrenCollapsible(true);
            // Allow all widgets in the splitter to collapse to very small sizes
            for (int i = 0; i < rightSplitter->count(); ++i) {
                rightSplitter->setCollapsible(i, true);
            }
            // Allow the rightSplitter itself to resize smaller and clip content
            rightSplitter->setMinimumSize(0, 0);
            rightSplitter->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        }
        if (mainSplitter) {
            mainSplitter->setChildrenCollapsible(true);
            // Allow the right side (index 1) to be collapsible but don't change stretch factors
            mainSplitter->setCollapsible(1, true);
        }
    }
    else
    {
        if (chooserWidget)
        {
            chooserWidget->setMaximumHeight(chooserWidget->sizeHint().height());
            chooserWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        }

        // Ensure the splitter can still collapse chooserWidget
        if (rightSplitter)
        {
            rightSplitter->setChildrenCollapsible(true);

            // Find chooserWidget's index in the splitter
            int chooserIndex = rightSplitter->indexOf(chooserWidget);
            if (chooserIndex != -1)
            {
                rightSplitter->setCollapsible(chooserIndex, true);
            }
        }
    }
}

// ============================================================================
// Lyrics
// ============================================================================

void MainWindow::importLyricsSrt() {
    if (!file) {
        QMessageBox::warning(this, tr("Import Lyrics"), tr("No file loaded."));
        return;
    }

    QString path = QFileDialog::getOpenFileName(this, tr("Import SRT Lyrics"),
        startDirectory, tr("SRT Subtitle Files (*.srt);;All Files (*)"));
    if (path.isEmpty())
        return;

    LyricManager *mgr = file->lyricManager();
    if (!mgr)
        return;

    mgr->importFromSrt(path);

    // Show lyric timeline
    _lyricArea->setVisible(true);
    if (_toggleLyricTimeline)
        _toggleLyricTimeline->setChecked(true);
    _lyricTimeline->update();
    updateAll();

    QMessageBox::information(this, tr("Import Lyrics"),
        tr("Imported %1 lyric blocks from SRT file.").arg(mgr->count()));
}

void MainWindow::importLyricsText() {
    if (!file) {
        QMessageBox::warning(this, tr("Import Lyrics"), tr("No file loaded."));
        return;
    }

    LyricManager *mgr = file->lyricManager();
    if (!mgr)
        return;

    int fileDurationMs = file->msOfTick(file->endTick());
    LyricImportDialog dialog(fileDurationMs, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QStringList phrases = dialog.parsedPhrases();
    if (phrases.isEmpty())
        return;

    int startOffsetMs = static_cast<int>(dialog.startOffsetSec() * 1000.0);
    int startTick = file->tick(startOffsetMs);

    if (dialog.importMode() == LyricImportDialog::EvenSpacing) {
        // Distribute evenly across file duration
        int defaultDurationMs = static_cast<int>(dialog.phraseDurationSec() * 1000.0);
        int defaultDurationTicks = file->tick(startOffsetMs + defaultDurationMs) - startTick;
        if (defaultDurationTicks < 1) defaultDurationTicks = 480;

        // Calculate total available ticks
        int endTick = file->endTick();
        int availableTicks = endTick - startTick;
        int spacing = (phrases.size() > 1) ? (availableTicks / phrases.size()) : availableTicks;

        // Use the smaller of even spacing or default duration
        int phraseTicks = qMin(spacing, defaultDurationTicks);
        if (phraseTicks < 1) phraseTicks = 480;

        // Build text with one phrase per line, import via LyricManager
        QString text = phrases.join('\n');
        mgr->importFromPlainText(text, startTick, phraseTicks, true);
    } else {
        // Sync Later: placeholder timings with default duration
        int defaultDurationMs = static_cast<int>(dialog.phraseDurationSec() * 1000.0);
        int defaultDurationTicks = file->tick(startOffsetMs + defaultDurationMs) - startTick;
        if (defaultDurationTicks < 1) defaultDurationTicks = 480;

        QString text = phrases.join('\n');
        mgr->importFromPlainText(text, startTick, defaultDurationTicks, true);
    }

    // Show lyric timeline
    _lyricArea->setVisible(true);
    if (_toggleLyricTimeline)
        _toggleLyricTimeline->setChecked(true);
    _lyricTimeline->update();
    updateAll();

    QMessageBox::information(this, tr("Import Lyrics"),
        tr("Imported %1 lyric phrases.").arg(mgr->count()));

    // If user chose "Import & Sync Now", launch the sync dialog immediately
    if (dialog.importMode() == LyricImportDialog::SyncNow) {
        syncLyrics();
    }
}

void MainWindow::syncLyrics() {
    if (!file) {
        QMessageBox::warning(this, tr("Sync Lyrics"), tr("No file loaded."));
        return;
    }

    LyricManager *mgr = file->lyricManager();
    if (!mgr || !mgr->hasLyrics()) {
        QMessageBox::warning(this, tr("Sync Lyrics"),
            tr("No lyrics to sync. Import lyrics first using Import Lyrics (SRT) or Import Lyrics (Text)."));
        return;
    }

    // Stop any current playback
    if (MidiPlayer::isPlaying()) {
        stop();
    }

    // Reset cursor to beginning
    file->setCursorTick(0);
    file->setPauseTick(-1);

    LyricSyncDialog dialog(file, this);
    if (dialog.exec() == QDialog::Accepted) {
        _lyricTimeline->update();
        updateAll();
        markEdited();
    }

    // Reset file state after sync
    file->setPauseTick(-1);
}

void MainWindow::exportLyricsSrt() {
    if (!file) {
        QMessageBox::warning(this, tr("Export Lyrics"), tr("No file loaded."));
        return;
    }

    LyricManager *mgr = file->lyricManager();
    if (!mgr || !mgr->hasLyrics()) {
        QMessageBox::warning(this, tr("Export Lyrics"), tr("No lyrics to export."));
        return;
    }

    QString path = QFileDialog::getSaveFileName(this, tr("Export SRT Lyrics"),
        startDirectory, tr("SRT Subtitle Files (*.srt)"));
    if (path.isEmpty())
        return;

    if (mgr->exportToSrt(path)) {
        QMessageBox::information(this, tr("Export Lyrics"),
            tr("Exported %1 lyric blocks to SRT file.").arg(mgr->count()));
    } else {
        QMessageBox::warning(this, tr("Export Lyrics"),
            tr("Failed to export lyrics to SRT file."));
    }
}

void MainWindow::importLyricsLrc() {
    if (!file) {
        QMessageBox::warning(this, tr("Import Lyrics"), tr("No file loaded."));
        return;
    }

    QString path = QFileDialog::getOpenFileName(this, tr("Import LRC Lyrics"),
        startDirectory, tr("LRC Lyric Files (*.lrc)"));
    if (path.isEmpty())
        return;

    LyricMetadata importedMeta;
    QList<LyricBlock> blocks = LrcExporter::importLrc(path, file, &importedMeta);
    if (blocks.isEmpty()) {
        QMessageBox::warning(this, tr("Import Lyrics"),
            tr("No lyrics found in the LRC file."));
        return;
    }

    LyricManager *mgr = file->lyricManager();
    if (!importedMeta.isEmpty()) {
        mgr->setMetadata(importedMeta);
    }

    // Wrap entire import in a single Protocol action (P3-002)
    // Use direct insertion instead of addBlock() which creates nested actions
    file->protocol()->startNewAction("Import Lyrics (LRC)");
    MidiTrack *defaultTrack = (file->numTracks() > 0) ? file->track(0) : nullptr;
    for (const LyricBlock &block : blocks) {
        LyricBlock b = block;
        if (defaultTrack) {
            MidiTrack *track = defaultTrack;
            if (b.trackIndex >= 0 && b.trackIndex < file->numTracks())
                track = file->track(b.trackIndex);
            TextEvent *te = new TextEvent(16, track);
            te->setText(b.text);
            te->setType(TextEvent::LYRIK);
            file->channel(16)->insertEvent(te, b.startTick);
            b.sourceEvent = te;
        }
        mgr->insertSorted(b);
    }
    file->protocol()->endAction();
    emit mgr->lyricsChanged();

    // Show lyric timeline
    _lyricArea->setVisible(true);
    if (_toggleLyricTimeline)
        _toggleLyricTimeline->setChecked(true);
    _lyricTimeline->update();
    updateAll();
    markEdited();

    QMessageBox::information(this, tr("Import Lyrics"),
        tr("Imported %1 lyric blocks from LRC file.").arg(blocks.size()));
}

void MainWindow::exportLyricsLrc() {
    if (!file) {
        QMessageBox::warning(this, tr("Export Lyrics"), tr("No file loaded."));
        return;
    }

    LyricManager *mgr = file->lyricManager();
    if (!mgr || !mgr->hasLyrics()) {
        QMessageBox::warning(this, tr("Export Lyrics"), tr("No lyrics to export."));
        return;
    }

    QString path = QFileDialog::getSaveFileName(this, tr("Export LRC Lyrics"),
        startDirectory, tr("LRC Lyric Files (*.lrc)"));
    if (path.isEmpty())
        return;

    LyricMetadata metadata = mgr->metadata();
    // If no title set in metadata, fall back to filename
    if (metadata.title.isEmpty())
        metadata.title = QFileInfo(file->path()).fileName();

    if (LrcExporter::exportLrc(path, mgr->allBlocks(), file, metadata)) {
        QMessageBox::information(this, tr("Export Lyrics"),
            tr("Exported %1 lyric blocks to LRC file.").arg(mgr->count()));
    } else {
        QMessageBox::warning(this, tr("Export Lyrics"),
            tr("Failed to export lyrics to LRC file."));
    }
}

void MainWindow::exportMusicXml() {
    if (!file) {
        QMessageBox::warning(this, tr("Export MusicXML"), tr("No file loaded."));
        return;
    }

    // Build the score first so we can refuse an empty export before prompting
    // for a path — a note-less file would otherwise yield invalid MusicXML
    // (empty <part-list>, no <part>) (BUG-XML-001).
    score::Score s = score::build(file);
    if (s.parts.isEmpty()) {
        QMessageBox::information(this, tr("Export MusicXML"),
            tr("Nothing to export — this file has no notes."));
        return;
    }
    if (!file->path().isEmpty())
        s.title = QFileInfo(file->path()).completeBaseName();

    QString suggested = startDirectory;
    if (!file->path().isEmpty()) {
        QFileInfo fi(file->path());
        suggested = fi.absolutePath() + "/" + fi.completeBaseName() + ".musicxml";
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Export MusicXML"),
        suggested, tr("MusicXML (*.musicxml)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".musicxml", Qt::CaseInsensitive) &&
        !path.endsWith(".xml", Qt::CaseInsensitive))
        path += ".musicxml";

    const QByteArray xml = MusicXmlWriter::write(s);

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export MusicXML"),
            tr("Could not write to:\n%1").arg(path));
        return;
    }
    out.write(xml);
    out.close();

    statusBar()->showMessage(
        tr("Exported %1 part(s) to MusicXML: %2").arg(s.parts.size()).arg(QFileInfo(path).fileName()),
        5000);
}

void MainWindow::embedLyricsInMidi() {
    if (!file) {
        QMessageBox::warning(this, tr("Embed Lyrics"), tr("No file loaded."));
        return;
    }

    LyricManager *mgr = file->lyricManager();
    if (!mgr || !mgr->hasLyrics()) {
        QMessageBox::warning(this, tr("Embed Lyrics"), tr("No lyrics to embed."));
        return;
    }

    mgr->exportToTextEvents();
    markEdited();
    updateAll();

    QMessageBox::information(this, tr("Embed Lyrics"),
        tr("Embedded %1 lyric events into the MIDI file.").arg(mgr->count()));
}

void MainWindow::clearAllLyrics() {
    if (!file) {
        QMessageBox::warning(this, tr("Clear Lyrics"), tr("No file loaded."));
        return;
    }

    LyricManager *mgr = file->lyricManager();
    if (!mgr || !mgr->hasLyrics()) {
        QMessageBox::warning(this, tr("Clear Lyrics"), tr("No lyrics to clear."));
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(this,
        tr("Clear All Lyrics"),
        tr("Are you sure you want to remove all %1 lyric blocks?\nThis action can be undone with Ctrl+Z.").arg(mgr->count()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes)
        return;

    mgr->clearAllBlocks();
    _lyricTimeline->update();
    updateAll();
}

// ============================================================================
// Audio Export
// ============================================================================

#ifdef FLUIDSYNTH_SUPPORT

void MainWindow::exportAudio() {
    if (!file) return;

    ExportDialog dlg(file, this);
    if (dlg.exec() != QDialog::Accepted) return;

    ExportOptions opts = dlg.exportOptions();

    // Authentic-SID export: render the ORIGINAL .sid through libsidplayfp (no
    // MIDI / FluidSynth / SoundFont involved), bypassing the whole MIDI render
    // path below. Only offered when a .sid is loaded.
    if (dlg.exportOriginalSid()) {
        const int fromMs = (opts.startTick >= 0) ? file->msOfTick(opts.startTick) : 0;
        const int toMs   = (opts.endTick > 0)   ? file->msOfTick(opts.endTick)
                                                 : static_cast<int>(file->maxTime());
        startSidExport(opts.outputFilePath, opts.fileType, fromMs, toMs,
                       static_cast<int>(opts.encodingQuality * 100.0 + 0.5), opts.mp3Bitrate);
        return;
    }

    // Mirror live-playback mute behaviour (MidiFile::channelMuted handles
    // both per-channel mute and solo): build a bitmask of channels the
    // export should drop. Without this, exporting with some channels
    // muted still produces all instruments in the rendered file.
    quint32 mutedMask = 0;
    for (int ch = 0; ch < 16; ++ch) {
        if (file->channelMuted(ch)) mutedMask |= (1u << ch);
    }
    opts.mutedChannelsMask = mutedMask;

    // If any *track* is muted we must rewrite a temp MIDI file with
    // those track events stripped, because the offline FluidSynth
    // player loses track identity once it's flat events on channels.
    bool anyTrackMuted = false;
    if (file->tracks()) {
        for (MidiTrack *t : *file->tracks()) {
            if (t && t->muted()) { anyTrackMuted = true; break; }
        }
    }

    // Build a drum-track-name → FFXIV percussion program map. When
    // FFXIV mode is on, MidiFile::save() injects a CH9 PC before each
    // NoteOn from a track in this map so the offline render hits the
    // correct percussion preset (mirrors the live MidiOutput per-note
    // PC injection). Fixes "Snare Drum exports as Bongo" when snare
    // hits don't sit on the GM standard keys.
    QHash<QString, int> drumProgramMap;
#ifdef FLUIDSYNTH_SUPPORT
    const bool ffxivOn = FluidSynthEngine::instance() &&
                         FluidSynthEngine::instance()->ffxivSoundFontMode();
    if (ffxivOn && file->tracks()) {
        for (MidiTrack *t : *file->tracks()) {
            if (!t) continue;
            int prog = FluidSynthEngine::instance()->drumProgramForTrackName(t->name());
            if (prog >= 0) drumProgramMap.insert(t->name(), prog);
        }
    }
#else
    const bool ffxivOn = false;
#endif
    const bool needTempFile = anyTrackMuted || !drumProgramMap.isEmpty();

    // FluidSynth needs a standard MIDI file. If the loaded file is a Guitar Pro
    // or other non-MIDI format, save the in-memory data to a temp .mid first.
    QString filePath = file->path();
    QString ext = QFileInfo(filePath).suffix().toLower();
    if ((ext == "mid" || ext == "midi") && !needTempFile) {
        opts.midiFilePath = filePath;
    } else {
        QString tempMidi = QDir::tempPath() + "/midieditor_export_temp.mid";
        if (!file->save(tempMidi, /*skipMutedTrackEvents=*/anyTrackMuted, drumProgramMap)) {
            QMessageBox::critical(this, tr("Export Failed"),
                                  tr("Could not prepare MIDI data for export."));
            return;
        }
        opts.midiFilePath = tempMidi;
        opts.deleteMidiFileAfterExport = true;
    }
    startExport(opts);
}

void MainWindow::exportAudioSelection() {
    if (!file) return;

    QList<MidiEvent *> sel = Selection::instance()->selectedEvents();
    if (sel.isEmpty()) return;

    int minTick = INT_MAX, maxTick = 0;
    for (MidiEvent *ev : sel) {
        if (ev->midiTime() < minTick) minTick = ev->midiTime();
        int end = ev->midiTime();
        if (NoteOnEvent *on = dynamic_cast<NoteOnEvent *>(ev)) {
            if (on->offEvent()) end = on->offEvent()->midiTime();
        }
        if (end > maxTick) maxTick = end;
    }

    ExportDialog dlg(file, this);
    dlg.setSelectionRange(minTick, maxTick);
    if (dlg.exec() != QDialog::Accepted) return;

    ExportOptions opts = dlg.exportOptions();

    // Authentic-SID export (see exportAudio): render the original .sid directly.
    if (dlg.exportOriginalSid()) {
        const int fromMs = (opts.startTick >= 0) ? file->msOfTick(opts.startTick) : 0;
        const int toMs   = (opts.endTick > 0)   ? file->msOfTick(opts.endTick)
                                                 : static_cast<int>(file->maxTime());
        startSidExport(opts.outputFilePath, opts.fileType, fromMs, toMs,
                       static_cast<int>(opts.encodingQuality * 100.0 + 0.5), opts.mp3Bitrate);
        return;
    }

    // Mirror live-playback mute behaviour: drop muted (or solo'd-out)
    // channels from the rendered audio.
    quint32 mutedMask = 0;
    for (int ch = 0; ch < 16; ++ch) {
        if (file->channelMuted(ch)) mutedMask |= (1u << ch);
    }
    opts.mutedChannelsMask = mutedMask;

    bool anyTrackMuted = false;
    if (file->tracks()) {
        for (MidiTrack *t : *file->tracks()) {
            if (t && t->muted()) { anyTrackMuted = true; break; }
        }
    }

    QHash<QString, int> drumProgramMap;
#ifdef FLUIDSYNTH_SUPPORT
    const bool ffxivOn = FluidSynthEngine::instance() &&
                         FluidSynthEngine::instance()->ffxivSoundFontMode();
    if (ffxivOn && file->tracks()) {
        for (MidiTrack *t : *file->tracks()) {
            if (!t) continue;
            int prog = FluidSynthEngine::instance()->drumProgramForTrackName(t->name());
            if (prog >= 0) drumProgramMap.insert(t->name(), prog);
        }
    }
#else
    const bool ffxivOn = false;
#endif
    const bool needTempFile = anyTrackMuted || !drumProgramMap.isEmpty();

    QString filePath = file->path();
    QString ext = QFileInfo(filePath).suffix().toLower();
    if ((ext == "mid" || ext == "midi") && !needTempFile) {
        opts.midiFilePath = filePath;
    } else {
        QString tempMidi = QDir::tempPath() + "/midieditor_export_temp.mid";
        if (!file->save(tempMidi, /*skipMutedTrackEvents=*/anyTrackMuted, drumProgramMap)) {
            QMessageBox::critical(this, tr("Export Failed"),
                                  tr("Could not prepare MIDI data for export."));
            return;
        }
        opts.midiFilePath = tempMidi;
        opts.deleteMidiFileAfterExport = true;
    }
    startExport(opts);
}

void MainWindow::startExport(const ExportOptions &opts) {
    FluidSynthEngine *engine = FluidSynthEngine::instance();

    // Track temp MIDI file for cleanup
    _exportTempMidiPath = opts.deleteMidiFileAfterExport ? opts.midiFilePath : QString();

    // Create progress dialog
    _exportProgressDialog = new QProgressDialog(tr("Exporting audio..."), tr("Cancel"), 0, 100, this);
    _exportProgressDialog->setWindowModality(Qt::WindowModal);
    _exportProgressDialog->setMinimumDuration(0);
    _exportProgressDialog->setValue(0);

    connect(engine, &FluidSynthEngine::exportProgress, this, [this](int pct) {
        if (_exportProgressDialog) {
            // For MP3: WAV render is 0â€“70%, LAME encode is 70â€“100%
            // For others: 0â€“100% directly
            _exportProgressDialog->setValue(pct);
        }
    });
    connect(engine, &FluidSynthEngine::exportFinished, this, &MainWindow::onExportFinished);
    connect(engine, &FluidSynthEngine::exportCancelled, this, &MainWindow::onExportCancelled);
    connect(_exportProgressDialog, &QProgressDialog::canceled, engine, &FluidSynthEngine::cancelExport);

#ifdef LAME_SUPPORT
    bool isMp3 = (opts.fileType == "mp3");
#else
    bool isMp3 = false;
#endif

    if (isMp3) {
#ifdef LAME_SUPPORT
        // MP3 pipeline: render WAV to temp â†’ encode MP3 â†’ delete temp
        ExportOptions wavOpts = opts;
        wavOpts.fileType = "wav";
        wavOpts.sampleFormat = "s16"; // LAME needs 16-bit PCM
        wavOpts.outputFilePath = QDir::tempPath() + "/midieditor_export_temp.wav";

        QString finalMp3Path = opts.outputFilePath;
        int mp3Bitrate = opts.mp3Bitrate;

        // Override progress to scale WAV phase to 0â€“70%
        disconnect(engine, &FluidSynthEngine::exportProgress, nullptr, nullptr);
        connect(engine, &FluidSynthEngine::exportProgress, this, [this](int pct) {
            if (_exportProgressDialog) {
                _exportProgressDialog->setValue(pct * 70 / 100);
            }
        });

        // Override finish to chain LAME encoding
        disconnect(engine, &FluidSynthEngine::exportFinished, nullptr, nullptr);
        connect(engine, &FluidSynthEngine::exportFinished, this,
                [this, wavOpts, finalMp3Path, mp3Bitrate](bool success, const QString &) {
            if (!success) {
                QFile::remove(wavOpts.outputFilePath);
                onExportFinished(false, finalMp3Path);
                return;
            }

            // Phase 2: encode WAV â†’ MP3 in background
            if (_exportProgressDialog) {
                _exportProgressDialog->setLabelText(tr("Encoding MP3..."));
                _exportProgressDialog->setValue(70);
            }

            QString tempWav = wavOpts.outputFilePath;
            QThreadPool::globalInstance()->start([this, tempWav, finalMp3Path, mp3Bitrate]() {
                bool ok = LameEncoder::encode(tempWav, finalMp3Path, mp3Bitrate,
                    [this](int pct) {
                        // Scale LAME progress 0â€“100 to dialog 70â€“100
                        int scaled = 70 + pct * 30 / 100;
                        QMetaObject::invokeMethod(this, [this, scaled]() {
                            if (_exportProgressDialog) {
                                _exportProgressDialog->setValue(scaled);
                            }
                        }, Qt::QueuedConnection);
                    },
                    FluidSynthEngine::instance()->cancelExportFlag());

                // Clean up temp WAV
                QFile::remove(tempWav);

                QMetaObject::invokeMethod(this, [this, ok, finalMp3Path]() {
                    if (!ok && FluidSynthEngine::instance()->cancelExportFlag()->load()) {
                        onExportCancelled();
                    } else {
                        onExportFinished(ok, finalMp3Path);
                    }
                }, Qt::QueuedConnection);
            });
        });

        // Run WAV render in background thread
        QThreadPool::globalInstance()->start([engine, wavOpts]() {
            engine->exportAudio(wavOpts);
        });
#endif // LAME_SUPPORT
    } else {
        // Standard pipeline: direct render
        ExportOptions optsCopy = opts;
        QThreadPool::globalInstance()->start([engine, optsCopy]() {
            engine->exportAudio(optsCopy);
        });
    }
}

void MainWindow::startSidExport(const QString &outputPath, const QString &fileType,
                                int fromMs, int toMs, int oggQuality, int mp3Bitrate) {
    SidAudioPlayer *sid = SidAudioPlayer::instance();
    if (!sid->hasSource()) {
        onExportFinished(false, outputPath);
        return;
    }

    // This path renders no temp MIDI; clear so the shared finish handlers'
    // temp-MIDI cleanup is a no-op. _sidExportCancel is the dialog's Cancel flag.
    _exportTempMidiPath.clear();
    _sidExportCancel.store(false);

    _exportProgressDialog = new QProgressDialog(tr("Exporting SID audio..."), tr("Cancel"), 0, 100, this);
    _exportProgressDialog->setWindowModality(Qt::WindowModal);
    _exportProgressDialog->setMinimumDuration(0);
    _exportProgressDialog->setValue(0);
    connect(_exportProgressDialog, &QProgressDialog::canceled, this, [this]() {
        _sidExportCancel.store(true);
    });

    // Push progress from the worker thread to the dialog on the GUI thread.
    auto pushProgress = [this](int pct) {
        QMetaObject::invokeMethod(this, [this, pct]() {
            if (_exportProgressDialog) _exportProgressDialog->setValue(pct);
        }, Qt::QueuedConnection);
    };

#ifdef LAME_SUPPORT
    const bool isMp3 = (fileType.compare(QStringLiteral("mp3"), Qt::CaseInsensitive) == 0);
#else
    const bool isMp3 = false;
#endif

    if (isMp3) {
#ifdef LAME_SUPPORT
        // MP3: SID -> temp WAV (0-70%) -> LAME encode (70-100%).
        const QString tempWav = QDir::tempPath() + QStringLiteral("/midieditor_sid_export_temp.wav");
        const QString finalMp3 = outputPath;
        QThreadPool::globalInstance()->start(
            [this, sid, tempWav, finalMp3, fromMs, toMs, mp3Bitrate, pushProgress]() {
                bool wavOk = sid->exportToFile(tempWav, QStringLiteral("wav"), fromMs, toMs, 60,
                    [pushProgress](int pct) { pushProgress(pct * 70 / 100); },
                    &_sidExportCancel);
                if (!wavOk) {
                    QFile::remove(tempWav);
                    QMetaObject::invokeMethod(this, [this, finalMp3]() {
                        if (_sidExportCancel.load()) onExportCancelled();
                        else onExportFinished(false, finalMp3);
                    }, Qt::QueuedConnection);
                    return;
                }
                QMetaObject::invokeMethod(this, [this]() {
                    if (_exportProgressDialog) {
                        _exportProgressDialog->setLabelText(tr("Encoding MP3..."));
                        _exportProgressDialog->setValue(70);
                    }
                }, Qt::QueuedConnection);
                bool ok = LameEncoder::encode(tempWav, finalMp3, mp3Bitrate,
                    [pushProgress](int pct) { pushProgress(70 + pct * 30 / 100); },
                    &_sidExportCancel);
                QFile::remove(tempWav);
                QMetaObject::invokeMethod(this, [this, ok, finalMp3]() {
                    if (!ok && _sidExportCancel.load()) onExportCancelled();
                    else onExportFinished(ok, finalMp3);
                }, Qt::QueuedConnection);
            });
#endif // LAME_SUPPORT
    } else {
        // WAV / OGG / FLAC: rendered directly by libsndfile.
        QThreadPool::globalInstance()->start(
            [this, sid, outputPath, fileType, fromMs, toMs, oggQuality, pushProgress]() {
                bool ok = sid->exportToFile(outputPath, fileType, fromMs, toMs, oggQuality,
                                            pushProgress, &_sidExportCancel);
                QMetaObject::invokeMethod(this, [this, ok, outputPath]() {
                    if (!ok && _sidExportCancel.load()) onExportCancelled();
                    else onExportFinished(ok, outputPath);
                }, Qt::QueuedConnection);
            });
    }
}

void MainWindow::onExportFinished(bool success, const QString &message) {
    FluidSynthEngine *engine = FluidSynthEngine::instance();
    disconnect(engine, &FluidSynthEngine::exportProgress, nullptr, nullptr);
    disconnect(engine, &FluidSynthEngine::exportFinished, nullptr, nullptr);
    disconnect(engine, &FluidSynthEngine::exportCancelled, nullptr, nullptr);

    if (_exportProgressDialog) {
        _exportProgressDialog->close();
        _exportProgressDialog->deleteLater();
        _exportProgressDialog = nullptr;
    }

    // Clean up temp MIDI file (for Guitar Pro etc.)
    if (!_exportTempMidiPath.isEmpty()) {
        QFile::remove(_exportTempMidiPath);
        _exportTempMidiPath.clear();
    }

    if (success) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Export Complete"));
        msgBox.setText(tr("Audio exported successfully to:\n%1").arg(message));
        msgBox.setIcon(QMessageBox::Information);
        QPushButton *openBtn = msgBox.addButton(tr("Open"), QMessageBox::ActionRole);
        QPushButton *openFolderBtn = msgBox.addButton(tr("Open Folder"), QMessageBox::ActionRole);
        msgBox.addButton(tr("Close"), QMessageBox::RejectRole);
        msgBox.exec();
        if (msgBox.clickedButton() == openBtn) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(message));
        } else if (msgBox.clickedButton() == openFolderBtn) {
            QString folder = QFileInfo(message).absolutePath();
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        }
    } else {
        QMessageBox::critical(this, tr("Export Failed"),
                              tr("Failed to export audio to:\n%1\n\n"
                                 "Check that SoundFonts are loaded and the output path is writable.").arg(message));
    }
}

void MainWindow::onExportCancelled() {
    FluidSynthEngine *engine = FluidSynthEngine::instance();
    disconnect(engine, &FluidSynthEngine::exportProgress, nullptr, nullptr);
    disconnect(engine, &FluidSynthEngine::exportFinished, nullptr, nullptr);
    disconnect(engine, &FluidSynthEngine::exportCancelled, nullptr, nullptr);

    if (_exportProgressDialog) {
        _exportProgressDialog->close();
        _exportProgressDialog->deleteLater();
        _exportProgressDialog = nullptr;
    }

    // Clean up temp MIDI file (for Guitar Pro etc.)
    if (!_exportTempMidiPath.isEmpty()) {
        QFile::remove(_exportTempMidiPath);
        _exportTempMidiPath.clear();
    }
}

#endif // FLUIDSYNTH_SUPPORT
