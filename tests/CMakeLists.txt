# MidiEditorAI unit tests
#
# Each test is a small self-contained Qt Test executable so the test binary
# can be kept narrow (no need to link the entire MidiEditorAI codebase just
# to exercise one module).

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Widgets)

# Compile-define so the Tier-2 corpus-test pattern documented in
# tests/data/README.md actually works. No test currently uses this (all 41
# tests are programmatic per the README's "prefer programmatic fixtures"
# recommendation), but if a corpus test gets added, this is the default
# directory it falls back to when the MIDIEDITOR_FIXTURES env var is unset.
add_compile_definitions(
    MIDIEDITOR_TESTS_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

# -----------------------------------------------------------------------------
# test_midi_diff
# -----------------------------------------------------------------------------
# Validates the MidiDiff algorithm in src/collab/MidiDiff. MidiDiff is pure
# JSON-in/JSON-out and depends only on Qt's JSON / container types, so the
# test executable links only the single .cpp.
add_executable(test_midi_diff
    test_midi_diff.cpp
    ../src/collab/MidiDiff.cpp
)
target_include_directories(test_midi_diff PRIVATE
    ../src/collab
)
target_link_libraries(test_midi_diff PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME MidiDiff COMMAND test_midi_diff)

# -----------------------------------------------------------------------------
# test_pr_bundle
# -----------------------------------------------------------------------------
# Validates the PrBundle codec (smart-paste token + JSON bundle file) in
# src/collab/PrBundle. Pure QString / QJson / QByteArray; no MidiFile dep.
add_executable(test_pr_bundle
    test_pr_bundle.cpp
    ../src/collab/PrBundle.cpp
)
target_include_directories(test_pr_bundle PRIVATE
    ../src/collab
)
target_link_libraries(test_pr_bundle PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME PrBundle COMMAND test_pr_bundle)

# -----------------------------------------------------------------------------
# test_midi_hash
# -----------------------------------------------------------------------------
# Validates MidiHash::sha256OfFile in src/collab/MidiHash. Pure
# QCryptographicHash + QFile - no MidiFile dep. Test compiles only the
# single .cpp.
add_executable(test_midi_hash
    test_midi_hash.cpp
    ../src/collab/MidiHash.cpp
)
target_include_directories(test_midi_hash PRIVATE
    ../src/collab
)
target_link_libraries(test_midi_hash PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME MidiHash COMMAND test_midi_hash)

# -----------------------------------------------------------------------------
# test_collab_identity
# -----------------------------------------------------------------------------
# Validates CollabIdentity (display name + machine UUID) in
# src/collab/CollabIdentity. Sandboxes QSettings via
# QStandardPaths::setTestModeEnabled(true) so the developer's real
# Collab/identity/* keys are never touched. Pure Qt - no MidiFile, no
# network deps.
add_executable(test_collab_identity
    test_collab_identity.cpp
    ../src/collab/CollabIdentity.cpp
)
target_include_directories(test_collab_identity PRIVATE
    ../src/collab
)
target_link_libraries(test_collab_identity PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME CollabIdentity COMMAND test_collab_identity)

# -----------------------------------------------------------------------------
# test_history_reconciliation
# -----------------------------------------------------------------------------
# Validates the pure logic helpers in src/collab/HistoryReconciliation:
# findMergeBase, tailHashes, commitsSinceFork, synthesizeBundle, classify.
# synthesizeBundle returns a PrBundle by value, so PrBundle.cpp is linked
# in (same .cpp the test_pr_bundle harness already pulls in). No MidiFile
# dep, no network, no QSettings.
add_executable(test_history_reconciliation
    test_history_reconciliation.cpp
    ../src/collab/HistoryReconciliation.cpp
    ../src/collab/PrBundle.cpp
)
target_include_directories(test_history_reconciliation PRIVATE
    ../src/collab
)
target_link_libraries(test_history_reconciliation PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME HistoryReconciliation COMMAND test_history_reconciliation)

# -----------------------------------------------------------------------------
# test_ice_config
# -----------------------------------------------------------------------------
# Validates IceConfig (STUN/TURN URI list I/O via QSettings) in
# src/collab/IceConfig. The IceConfig header gates its declarations on
# MIDIEDITOR_WEBRTC_ENABLED, so the test target defines it. No
# libdatachannel symbols are referenced from IceConfig.cpp, so the test
# stays a single-cpp build with just Qt6::Core. Sandboxed via
# QStandardPaths::setTestModeEnabled.
add_executable(test_ice_config
    test_ice_config.cpp
    ../src/collab/IceConfig.cpp
)
target_include_directories(test_ice_config PRIVATE
    ../src/collab
)
target_compile_definitions(test_ice_config PRIVATE
    MIDIEDITOR_WEBRTC_ENABLED
)
target_link_libraries(test_ice_config PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME IceConfig COMMAND test_ice_config)

# -----------------------------------------------------------------------------
# test_rtc_signaling_token
# -----------------------------------------------------------------------------
# Validates RtcSignalingToken (base64 + zlib codec for WebRTC offer/answer
# tokens, Plan §11.10) in src/collab/RtcSignalingToken. The header is
# gated on MIDIEDITOR_WEBRTC_ENABLED so the test target defines it.
# qCompress/qUncompress + QByteArray::toBase64 are the only deps; no
# libdatachannel linkage. Symmetric to test_pr_bundle.
add_executable(test_rtc_signaling_token
    test_rtc_signaling_token.cpp
    ../src/collab/RtcSignalingToken.cpp
)
target_include_directories(test_rtc_signaling_token PRIVATE
    ../src/collab
)
target_compile_definitions(test_rtc_signaling_token PRIVATE
    MIDIEDITOR_WEBRTC_ENABLED
)
target_link_libraries(test_rtc_signaling_token PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME RtcSignalingToken COMMAND test_rtc_signaling_token)

# -----------------------------------------------------------------------------
# test_session_mode
# -----------------------------------------------------------------------------
# Validates the LiveSession::Mode wire helpers in src/collab/SessionMode.h
# (Phase 9.9 §15.2 - Show Mode). SessionMode.h is header-only so this test
# links only Qt6::Core/Test - no SessionMode.cpp, no LanLiveSession
# dependency tree. Covers enum <-> wire-string round-trip, case-
# insensitive parsing, unknown-value forward-compatibility fallback, and
# the sessionWelcome JSON shape used on the wire.
add_executable(test_session_mode
    test_session_mode.cpp
)
target_include_directories(test_session_mode PRIVATE
    ../src/collab
)
target_link_libraries(test_session_mode PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME SessionMode COMMAND test_session_mode)

# -----------------------------------------------------------------------------
# test_time_display_format
# -----------------------------------------------------------------------------
# Validates the pure format/mode helpers in src/gui/TimeDisplayFormat.h
# (Phase 41 - Cursor Time Display): adaptive MM:SS / H:MM:SS clock, remaining
# countdown, bar.beat shape, and the click-cycle mode helpers. Header-only,
# so this links only Qt6::Core/Test - no TimeDisplayWidget, no MidiFile.
add_executable(test_time_display_format
    test_time_display_format.cpp
)
target_link_libraries(test_time_display_format PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME TimeDisplayFormat COMMAND test_time_display_format)

# -----------------------------------------------------------------------------
# test_import_only_formats
# -----------------------------------------------------------------------------
# Validates the import-only format classifier in src/gui/ImportOnlyFormats.h:
# Guitar Pro / MML / 3MLE / MusicXML / MuseScore / SID are recognised (so
# MainWindow::save() redirects them to Save As .mid instead of overwriting the
# original on disk), while real MIDI containers are not. Regression guard for
# the 1.8.0 data-loss fix (a saved .sid used to clobber the original). Header-
# only -> links only Qt Core/Test.
add_executable(test_import_only_formats
    test_import_only_formats.cpp
)
target_link_libraries(test_import_only_formats PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME ImportOnlyFormats COMMAND test_import_only_formats)

# -----------------------------------------------------------------------------
# test_sid_file
# -----------------------------------------------------------------------------
# Validates the PSID/RSID parser (Phase 42, C64 mode) - header fields +
# program-image resolution (load address in header vs embedded in data) +
# rejection of malformed input. The SID core is plain C++, so this compiles
# SidFile.cpp directly and links only Qt6::Core/Test.
add_executable(test_sid_file
    test_sid_file.cpp
    ../src/converter/Sid/SidFile.cpp
)
target_include_directories(test_sid_file PRIVATE
    ../src/converter/Sid
)
target_link_libraries(test_sid_file PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME SidFile COMMAND test_sid_file)

# -----------------------------------------------------------------------------
# test_sid_cpu
# -----------------------------------------------------------------------------
# Validates the MOS 6502 emulator (Phase 42) with hand-assembled programs:
# indexed store loop, ADC carry/zero flags, JSR/RTS nesting, runaway guard.
add_executable(test_sid_cpu
    test_sid_cpu.cpp
    ../src/converter/Sid/Mos6502.cpp
)
target_include_directories(test_sid_cpu PRIVATE
    ../src/converter/Sid
)
target_link_libraries(test_sid_cpu PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME SidCpu COMMAND test_sid_cpu)

# -----------------------------------------------------------------------------
# test_sid_capture
# -----------------------------------------------------------------------------
# End-to-end (Phase 42): parser -> 6502 emulator -> register capture. A
# synthetic PSID with an embedded player drives the SID registers; the test
# checks the captured per-frame register progression + PAL/NTSC timing.
add_executable(test_sid_capture
    test_sid_capture.cpp
    ../src/converter/Sid/SidCapture.cpp
    ../src/converter/Sid/SidFile.cpp
    ../src/converter/Sid/Mos6502.cpp
)
target_include_directories(test_sid_capture PRIVATE
    ../src/converter/Sid
)
target_link_libraries(test_sid_capture PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME SidCapture COMMAND test_sid_capture)

# -----------------------------------------------------------------------------
# test_sid_reconstruct
# -----------------------------------------------------------------------------
# Validates note reconstruction (Phase 42.1b): SID frequency -> MIDI pitch,
# gate-run -> note duration, mid-gate pitch change -> arpeggio split, and
# vibrato staying a single note. Structs are header-only, so this only needs
# SidReconstruct.cpp.
add_executable(test_sid_reconstruct
    test_sid_reconstruct.cpp
    ../src/converter/Sid/SidReconstruct.cpp
)
target_include_directories(test_sid_reconstruct PRIVATE
    ../src/converter/Sid
)
target_link_libraries(test_sid_reconstruct PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME SidReconstruct COMMAND test_sid_reconstruct)

# -----------------------------------------------------------------------------
# test_sid_midiwriter
# -----------------------------------------------------------------------------
# Validates the SID -> SMF writer (Phase 42.1) emits a well-formed format-1
# MIDI file (MThd header + 4 chunk-consistent MTrk tracks).
add_executable(test_sid_midiwriter
    test_sid_midiwriter.cpp
    ../src/converter/Sid/SidMidiWriter.cpp
)
target_include_directories(test_sid_midiwriter PRIVATE
    ../src/converter/Sid
)
target_link_libraries(test_sid_midiwriter PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME SidMidiWriter COMMAND test_sid_midiwriter)

# -----------------------------------------------------------------------------
# test_xml_score_to_midi
# -----------------------------------------------------------------------------
# Validates the shared SMF (Standard MIDI File) byte writer used by both the
# MusicXML and MuseScore importers. Compiles XmlScoreToMidi.cpp directly so
# no other project sources are pulled in.
add_executable(test_xml_score_to_midi
    test_xml_score_to_midi.cpp
    ../src/converter/MusicXml/XmlScoreToMidi.cpp
)
target_include_directories(test_xml_score_to_midi PRIVATE
    ../src/converter/MusicXml
)
target_link_libraries(test_xml_score_to_midi PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME XmlScoreToMidi COMMAND test_xml_score_to_midi)

# -----------------------------------------------------------------------------
# test_musicxml_export
# -----------------------------------------------------------------------------
# Validates the MusicXML export pipeline (Phase 43, 1.8.0): the MIDI->notation
# engraver (score::buildScore in MidiToScore.cpp) - note-value mapping, rest
# filling, ties across barlines, chords, enharmonic spelling - and the
# MusicXmlWriter serialiser. buildScore takes a plain ScoreInput, so this links
# only the two pure .cpp files (not MidiToScoreExtract.cpp, which pulls in the
# MidiFile tree) + Qt6::Core/Test.
add_executable(test_musicxml_export
    test_musicxml_export.cpp
    ../src/converter/Score/MidiToScore.cpp
    ../src/converter/MusicXml/MusicXmlWriter.cpp
)
target_include_directories(test_musicxml_export PRIVATE
    ../src/converter/Score
    ../src/converter/MusicXml
)
target_link_libraries(test_musicxml_export PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME MusicXmlExport COMMAND test_musicxml_export)

# -----------------------------------------------------------------------------
# test_chord_detector
# -----------------------------------------------------------------------------
# Validates the pure pitch-class chord identifier in src/midi/ChordDetector.
# ChordDetector has no dependencies on MidiFile or any other project source,
# so the test executable links only the single .cpp.
add_executable(test_chord_detector
    test_chord_detector.cpp
    ../src/midi/ChordDetector.cpp
)
target_include_directories(test_chord_detector PRIVATE
    ../src/midi
)
target_link_libraries(test_chord_detector PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME ChordDetector COMMAND test_chord_detector)

# -----------------------------------------------------------------------------
# test_srt_parser
# -----------------------------------------------------------------------------
# Validates the SRT subtitle import/export in src/converter/SrtParser.
# SrtParser depends on MidiFile only for tick<->ms conversion (two non-virtual
# methods). To avoid linking the entire MidiFile dep tree, the test provides
# an ODR shim: it defines its own minimal MidiFile class with the same two
# member symbols. SrtParser.cpp is compiled here against the real MidiFile.h
# but never constructs MidiFile, so no vtable / Q_OBJECT machinery is needed.
add_executable(test_srt_parser
    test_srt_parser.cpp
    ../src/converter/SrtParser.cpp
)
target_include_directories(test_srt_parser PRIVATE
    ../src/converter
    ../src/midi
)
target_link_libraries(test_srt_parser PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME SrtParser COMMAND test_srt_parser)

# -----------------------------------------------------------------------------
# test_lrc_exporter
# -----------------------------------------------------------------------------
# Validates the LRC (lyric) import/export converter. LrcExporter, like
# SrtParser, depends on MidiFile only for tick<->ms conversion. Same ODR
# shim trick: the test defines its own minimal MidiFile with the two
# required member symbols. LrcExporter.cpp compiles against the real
# MidiFile.h but never constructs MidiFile.
add_executable(test_lrc_exporter
    test_lrc_exporter.cpp
    ../src/converter/LrcExporter.cpp
)
target_include_directories(test_lrc_exporter PRIVATE
    ../src/converter
    ../src/midi
)
target_link_libraries(test_lrc_exporter PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME LrcExporter COMMAND test_lrc_exporter)

# -----------------------------------------------------------------------------
# test_protocol
# -----------------------------------------------------------------------------
# Validates the undo/redo stack semantics in src/protocol/Protocol.
# Protocol.cpp / ProtocolItem.cpp / ProtocolEntry.cpp / ProtocolStep.cpp
# reference MidiFile (setSaved, protocol) and MidiTrack (typeinfo for a
# dynamic_cast). The test TU provides ODR shims for both — minimal stand-in
# classes whose only job is to satisfy the linker. See the file header for
# details. No real MidiFile / MidiTrack symbols are pulled in.
add_executable(test_protocol
    test_protocol.cpp
    ../src/protocol/Protocol.cpp
    ../src/protocol/ProtocolStep.cpp
    ../src/protocol/ProtocolItem.cpp
    ../src/protocol/ProtocolEntry.cpp
)
target_include_directories(test_protocol PRIVATE
    ../src/protocol
    ../src/midi
)
target_link_libraries(test_protocol PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)
add_test(NAME Protocol COMMAND test_protocol)

# -----------------------------------------------------------------------------
# test_gp_unzip
# -----------------------------------------------------------------------------
# Validates the tiny in-tree ZIP reader used by the GP7/GP8 importers.
# Builds archives in memory (STORE + DEFLATE) using zlib and verifies
# round-trip extraction plus error paths. zlib resolves via the system
# package or, failing that, via Qt6's bundled ZlibPrivate (matches the
# main project's strategy in the top-level CMakeLists).
find_package(ZLIB QUIET)
set(_gpunzip_zlib_target "")
if(ZLIB_FOUND)
    set(_gpunzip_zlib_target ZLIB::ZLIB)
else()
    find_package(Qt6 COMPONENTS ZlibPrivate QUIET)
    if(TARGET Qt6::ZlibPrivate)
        set(_gpunzip_zlib_target Qt6::ZlibPrivate)
    endif()
endif()
if(_gpunzip_zlib_target STREQUAL "")
    message(STATUS "test_gp_unzip skipped: no zlib available")
else()
    add_executable(test_gp_unzip
        test_gp_unzip.cpp
        ../src/converter/GuitarPro/GpUnzip.cpp
    )
    target_include_directories(test_gp_unzip PRIVATE
        ../src/converter/GuitarPro
    )
    target_link_libraries(test_gp_unzip PRIVATE
        Qt6::Core
        Qt6::Test
        ${_gpunzip_zlib_target}
    )
    add_test(NAME GpUnzip COMMAND test_gp_unzip)
endif()

# -----------------------------------------------------------------------------
# test_ffxiv_voice_analyzer
# -----------------------------------------------------------------------------
# Validates the pure voice-load + note-rate logic in
# src/ai/FfxivVoiceLoadCore (Phase 32.4). The core has no MidiFile,
# MidiChannel, or Qt GUI dependencies, so the test compiles only that
# single .cpp.
add_executable(test_ffxiv_voice_analyzer
    test_ffxiv_voice_analyzer.cpp
    ../src/ai/FfxivVoiceLoadCore.cpp
)
target_include_directories(test_ffxiv_voice_analyzer PRIVATE
    ../src/ai
)
target_link_libraries(test_ffxiv_voice_analyzer PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME FfxivVoiceAnalyzer COMMAND test_ffxiv_voice_analyzer)

# -----------------------------------------------------------------------------
# test_ffxiv_equalizer_service
# -----------------------------------------------------------------------------
# Validates the per-instrument volume mixer service used by the FFXIV
# SoundFont Equalizer (Phase 39). The service is intentionally Qt-only
# (no FluidSynth, no MidiFile), so the test compiles a single .cpp.
add_executable(test_ffxiv_equalizer_service
    test_ffxiv_equalizer_service.cpp
    ../src/midi/FfxivEqualizerService.cpp
)
target_include_directories(test_ffxiv_equalizer_service PRIVATE
    ../src/midi
)
target_link_libraries(test_ffxiv_equalizer_service PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME FfxivEqualizerService COMMAND test_ffxiv_equalizer_service)

# -----------------------------------------------------------------------------
# test_selection
# -----------------------------------------------------------------------------
# Validates the global Selection singleton in src/tool/Selection.
# Selection.cpp depends only on ProtocolEntry (light) and EventWidget
# (heavy QWidget header but only setEvents/reload are called, and only
# when the static _eventWidget pointer is non-null). The test TU provides
# ODR shims for both dependencies — it links neither ProtocolEntry.cpp nor
# any GUI .cpp.
add_executable(test_selection
    test_selection.cpp
    ../src/tool/Selection.cpp
)
target_include_directories(test_selection PRIVATE
    ../src/tool
    ../src/protocol
    ../src/gui
)
target_link_libraries(test_selection PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME Selection COMMAND test_selection)

# -----------------------------------------------------------------------------
# test_meta_events
# -----------------------------------------------------------------------------
# Validates SMF byte encoding and BPM<->us-per-quarter conversion for the
# three meta events: KeySignatureEvent, TimeSignatureEvent, TempoChangeEvent.
# Compiles only those three .cpp files; ODR-shims MidiEvent base class,
# GraphicObject, ProtocolEntry, EventWidget, and the two MidiFile methods
# (ticksPerQuarter / calcMaxTime) referenced from the .cpp bodies. No real
# MidiFile / MidiChannel / MidiTrack tree is linked.
add_executable(test_meta_events
    test_meta_events.cpp
    ../src/MidiEvent/KeySignatureEvent.cpp
    ../src/MidiEvent/TimeSignatureEvent.cpp
    ../src/MidiEvent/TempoChangeEvent.cpp
)
target_include_directories(test_meta_events PRIVATE
    ../src/MidiEvent
    ../src/midi
    ../src/protocol
    ../src/gui
)
target_link_libraries(test_meta_events PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME MetaEvents COMMAND test_meta_events)

# -----------------------------------------------------------------------------
# test_conversation_store
# -----------------------------------------------------------------------------
# Validates the JSON-backed conversation persistence in src/ai/ConversationStore.
# The store is pure Qt (no AiClient / EditorContext / MidiFile deps), so the
# test compiles ConversationStore.cpp directly and links nothing else from the
# project. Storage is redirected via QStandardPaths::setTestModeEnabled(true)
# so tests never touch the developer's real MidiPilotHistory folder.
add_executable(test_conversation_store
    test_conversation_store.cpp
    ../src/ai/ConversationStore.cpp
)
target_include_directories(test_conversation_store PRIVATE
    ../src/ai
)
target_link_libraries(test_conversation_store PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME ConversationStore COMMAND test_conversation_store)

# -----------------------------------------------------------------------------
# test_midi_event_serializer  -- DEFERRED
# -----------------------------------------------------------------------------
# An attempt to test the pure helpers (noteName, validateEventJson) by
# compiling MidiEventSerializer.cpp into a narrow test was made and rolled
# back. The TU references the entire MIDI event hierarchy + MidiFile/Channel
# /Track from inside serialize() / deserialize(); /OPT:REF (Release default)
# does not strip those references in this project's link config, leaving 20+
# unresolved externals. Restoring would require either:
#   (a) extracting the validator + noteName into a stand-alone TU, or
#   (b) the broader MidiFile-as-static-lib refactor (Planning/06 §3.1).
# Tracked in Planning/06_TEST_CASES.md §2.6.

# -----------------------------------------------------------------------------
# test_midi_track
# -----------------------------------------------------------------------------
# Validates the narrow accessor / undo-snapshot surface of src/midi/MidiTrack
# (number, name fallback, hidden, muted, assignedChannel, copy/reloadState).
# MidiTrack.cpp references MidiFile / Appearance / TextEvent inside setName /
# color / copyToFile only — those branches are unreachable from this test, so
# /OPT:REF strips them at link time. We still link the protocol/ TUs (same
# pattern as test_protocol) because setNumber / setHidden / setMuted route
# through ProtocolEntry::protocol → new ProtocolItem(...). An ODR-shimmed
# MidiFile returns a null Protocol* so no real Protocol object is needed at
# runtime.
add_executable(test_midi_track
    test_midi_track.cpp
    ../src/midi/MidiTrack.cpp
    ../src/protocol/ProtocolEntry.cpp
    ../src/protocol/ProtocolItem.cpp
    ../src/protocol/Protocol.cpp
    ../src/protocol/ProtocolStep.cpp
)
target_include_directories(test_midi_track PRIVATE
    ../src/midi
    ../src/protocol
)
target_link_libraries(test_midi_track PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME MidiTrack COMMAND test_midi_track)

# -----------------------------------------------------------------------------
# test_tool_definitions
# -----------------------------------------------------------------------------
# Schema-validity tests for src/ai/ToolDefinitions::toolSchemas(). Only the
# JSON-construction surface is exercised; executeTool / exec* paths reference
# MidiFile / MidiPilotWidget / MidiEventSerializer and friends, but they are
# unreferenced from toolSchemas() so /Gy + /OPT:REF (Release default in
# build_tests.bat) strips them at link time. No project source other than
# ToolDefinitions.cpp is linked.
add_executable(test_tool_definitions
    test_tool_definitions.cpp
    ../src/ai/ToolDefinitions.cpp
)
target_include_directories(test_tool_definitions PRIVATE
    ../src/ai
)
target_compile_definitions(test_tool_definitions PRIVATE
    TOOLDEFINITIONS_TEST_STUB_FFXIV
)
target_link_libraries(test_tool_definitions PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME ToolDefinitions COMMAND test_tool_definitions)

# -----------------------------------------------------------------------------
# test_agent_tool_policy
# -----------------------------------------------------------------------------
# Pure unit test for the Phase 31 model-isolation policy table in
# src/ai/AgentToolPolicy. AgentToolPolicy.cpp depends only on Qt Core, so
# this executable is fully self-contained.
add_executable(test_agent_tool_policy
    test_agent_tool_policy.cpp
    ../src/ai/AgentToolPolicy.cpp
)
target_include_directories(test_agent_tool_policy PRIVATE
    ../src/ai
)
target_link_libraries(test_agent_tool_policy PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME AgentToolPolicy COMMAND test_agent_tool_policy)

# -----------------------------------------------------------------------------
# test_model_favorites
# -----------------------------------------------------------------------------
# Pure unit test for the ModelFavorites helper: chat-model heuristic, QSettings
# round-trip, visibleModels() filter behaviour. No network, no GUI.
add_executable(test_model_favorites
    test_model_favorites.cpp
    ../src/ai/ModelFavorites.cpp
)
target_include_directories(test_model_favorites PRIVATE ../src/ai)
target_link_libraries(test_model_favorites PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME ModelFavorites COMMAND test_model_favorites)

# -----------------------------------------------------------------------------
# test_model_list_fetcher
# -----------------------------------------------------------------------------
# Pure unit test for ModelListFetcher::normaliseOllama() — the /api/tags ->
# cache-schema transform (capabilities, embedding filter, size badge, id
# fallback, large-size overflow). No network: the method is static. Needs
# Qt6::Network because ModelListFetcher.cpp uses QNetworkAccessManager.
add_executable(test_model_list_fetcher
    test_model_list_fetcher.cpp
    ../src/ai/ModelListFetcher.cpp
)
target_include_directories(test_model_list_fetcher PRIVATE ../src/ai)
target_link_libraries(test_model_list_fetcher PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)
add_test(NAME ModelListFetcher COMMAND test_model_list_fetcher)
# Needs Qt6Network.dll on PATH at run time (links ModelListFetcher.cpp, which
# uses QNetworkAccessManager). Ship the Qt bin dir like PromptProfiles does, so
# ctest finds it even under a minimal-env runner.
if(WIN32)
    get_target_property(_qt_core_loc Qt6::Core LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_core_loc}" DIRECTORY)
    set_tests_properties(ModelListFetcher PROPERTIES
        ENVIRONMENT "PATH=${_qt_bin_dir};$ENV{PATH}")
endif()

# -----------------------------------------------------------------------------
# test_snap_math
# -----------------------------------------------------------------------------
# Pure unit test for SnapMath::snapToDiv() — the Modern (hard grid) vs Legacy
# (magnetic ±px) snap math used by the Magnet tool. Header-only, no sources to
# link beyond Qt6::Core.
add_executable(test_snap_math
    test_snap_math.cpp
)
target_include_directories(test_snap_math PRIVATE ../src/tool)
target_link_libraries(test_snap_math PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME SnapMath COMMAND test_snap_math)
# Ship the Qt bin dir on PATH so ctest finds Qt6Core/Qt6Test DLLs even under a
# minimal-env runner (same guard as PromptProfiles / ModelListFetcher).
if(WIN32)
    get_target_property(_qt_core_loc Qt6::Core LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_core_loc}" DIRECTORY)
    set_tests_properties(SnapMath PROPERTIES
        ENVIRONMENT "PATH=${_qt_bin_dir};$ENV{PATH}")
endif()

# -----------------------------------------------------------------------------
# test_prompt_profiles
# -----------------------------------------------------------------------------
# Pure unit test for PromptProfileStore: glob match, resolution layering,
# QSettings round-trip, and the read-only "GPT-5.5 Decisive" built-in.
# No GUI, no network. Sandboxes through QStandardPaths::setTestModeEnabled.
add_executable(test_prompt_profiles
    test_prompt_profiles.cpp
    ../src/ai/PromptProfileStore.cpp
)
target_include_directories(test_prompt_profiles PRIVATE ../src/ai)
target_link_libraries(test_prompt_profiles PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME PromptProfiles COMMAND test_prompt_profiles)
# Make sure ctest finds Qt6Cored.dll / Qt6Testd.dll even when invoked from
# a shell that doesn't have Qt's bin on PATH (VS Code's CMake Tools spawns
# ctest with a minimal env). Other tests rely on the developer's outer
# PATH; this one ships its own.
if(WIN32)
    get_target_property(_qt_core_loc Qt6::Core LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_core_loc}" DIRECTORY)
    set_tests_properties(PromptProfiles PROPERTIES
        ENVIRONMENT "PATH=${_qt_bin_dir};$ENV{PATH}")
endif()

# -----------------------------------------------------------------------------
# test_agent_runner_state
# -----------------------------------------------------------------------------
# Pure unit tests for the lightweight AgentRunner conductor helpers: task
# classification, compact working-state updates, pitch-bend rejection steering,
# and request-local state injection. The exercised helpers do not touch network
# or editor state, so no AiClient / MidiPilotWidget source is linked.
add_executable(test_agent_runner_state
    test_agent_runner_state.cpp
    ../src/ai/AiClient.h
    ../src/ai/AgentRunner.cpp
    ../src/ai/AgentToolPolicy.cpp
)
target_include_directories(test_agent_runner_state PRIVATE ../src/ai)
target_link_libraries(test_agent_runner_state PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME AgentRunnerState COMMAND test_agent_runner_state)

# -----------------------------------------------------------------------------
# test_provider_matrix
# -----------------------------------------------------------------------------
# Live-API smoke test that exercises the four AiClient code paths
# (non-stream / stream  ×  no tools / +tools) for OpenAI, OpenRouter and
# Gemini. Each provider is SKIPPED unless the matching env var is set:
#   MIDIPILOT_TEST_OPENAI_KEY / MIDIPILOT_TEST_OPENROUTER_KEY /
#   MIDIPILOT_TEST_GEMINI_KEY
# Uses MidiPilot's QSettings store but snapshots+restores the touched keys.
# Not added to add_test() by default to avoid slow/network-dependent CI runs;
# configure with -DMIDIPILOT_ENABLE_LIVE_API_TESTS=ON, then run manually:
#   ctest -R ProviderMatrix --output-on-failure
option(MIDIPILOT_ENABLE_LIVE_API_TESTS "Register live provider API smoke tests" OFF)
add_executable(test_provider_matrix
    test_provider_matrix.cpp
    ../src/ai/AiClient.cpp
    ../src/ai/ModelListCache.cpp
)
target_include_directories(test_provider_matrix PRIVATE ../src/ai)
target_link_libraries(test_provider_matrix PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)
if(MIDIPILOT_ENABLE_LIVE_API_TESTS)
    add_test(NAME ProviderMatrix COMMAND test_provider_matrix)
    set_tests_properties(ProviderMatrix PROPERTIES
        LABELS "live-api;slow"
        TIMEOUT 600
    )
endif()

# -----------------------------------------------------------------------------
# test_streaming_fallback
# -----------------------------------------------------------------------------
# Unit-tests the per-(provider,model) streaming blocklist that backs the
# auto-fallback to the non-streaming AiClient path (PHASE-27.7). Pure
# QSettings + AiClient public API — no network, no live keys required.
# Sandboxes its own provider/model names with a per-PID suffix and restores
# AI/api_key + AI/provider + AI/model around the run, so the user's
# MidiPilot config is never touched.
add_executable(test_streaming_fallback
    test_streaming_fallback.cpp
    ../src/ai/AiClient.cpp
    ../src/ai/ModelListCache.cpp
)
target_include_directories(test_streaming_fallback PRIVATE ../src/ai)
target_link_libraries(test_streaming_fallback PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)
add_test(NAME StreamingFallback COMMAND test_streaming_fallback)

# -----------------------------------------------------------------------------
# test_channel_visibility_manager
# -----------------------------------------------------------------------------
# Validates the in-memory ChannelVisibilityManager singleton (defaults, the
# channel-16 inheritance for indices > 16, bounds clamping, and reset). The
# manager has no QSettings persistence in the current source; the original
# roadmap entry was updated to reflect that. No other project source linked.
add_executable(test_channel_visibility_manager
    test_channel_visibility_manager.cpp
    ../src/gui/ChannelVisibilityManager.cpp
)
target_include_directories(test_channel_visibility_manager PRIVATE
    ../src/gui
)
target_link_libraries(test_channel_visibility_manager PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME ChannelVisibilityManager COMMAND test_channel_visibility_manager)

# -----------------------------------------------------------------------------
# test_note_pairing
# -----------------------------------------------------------------------------
# Validates the static OffEvent::onEvents pairing map: NoteOnEvent ctor
# registers, dtor (V131-P2-03) drains, OffEvent ctor pairs by line + channel.
# Compiles only NoteOnEvent.cpp + OffEvent.cpp + OnEvent.cpp; the MidiEvent
# base, ProtocolEntry, GraphicObject, EventWidget and MidiFile are ODR-shimmed
# in the test TU (same pattern as test_meta_events). No real MidiFile /
# MidiChannel / MidiTrack tree is linked.
add_executable(test_note_pairing
    test_note_pairing.cpp
    ../src/MidiEvent/NoteOnEvent.cpp
    ../src/MidiEvent/OffEvent.cpp
    ../src/MidiEvent/OnEvent.cpp
)
target_include_directories(test_note_pairing PRIVATE
    ../src/MidiEvent
    ../src/midi
    ../src/protocol
    ../src/gui
)
target_link_libraries(test_note_pairing PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME NotePairing COMMAND test_note_pairing)

# -----------------------------------------------------------------------------
# test_midi_channel
# -----------------------------------------------------------------------------
# Validates src/midi/MidiChannel: insertion / iteration ordering by tick,
# removeEvent semantics inside vs. outside an active Protocol action, the
# channel-17 / channel-18 single-anchor-at-tick-0 guard, OnEvent/OffEvent
# cascading delete, progAtTick walking, deleteAllEvents, number() clamping,
# visible() delegation. Same ODR-shim approach as test_meta_events / test_protocol:
# the four event .cpps + MidiChannel.cpp + ChannelVisibilityManager.cpp + the
# protocol/* TUs are linked, while MidiFile / MidiTrack / Appearance / the
# MidiEvent base are out-of-line shims defined in the test TU itself.
add_executable(test_midi_channel
    test_midi_channel.cpp
    ../src/midi/MidiChannel.cpp
    ../src/gui/ChannelVisibilityManager.cpp
    ../src/MidiEvent/NoteOnEvent.cpp
    ../src/MidiEvent/OffEvent.cpp
    ../src/MidiEvent/OnEvent.cpp
    ../src/MidiEvent/ProgChangeEvent.cpp
    ../src/protocol/Protocol.cpp
    ../src/protocol/ProtocolStep.cpp
    ../src/protocol/ProtocolItem.cpp
    ../src/protocol/ProtocolEntry.cpp
)
target_include_directories(test_midi_channel PRIVATE
    ../src/midi
    ../src/MidiEvent
    ../src/protocol
    ../src/gui
)
target_link_libraries(test_midi_channel PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME MidiChannel COMMAND test_midi_channel)

# -----------------------------------------------------------------------------
# test_event_perf
# -----------------------------------------------------------------------------
# Performance / memory smoke-harness: builds up to 100k NoteOn/Off pairs in
# real MidiChannels and reports build time, select-all/scan time and (on
# Windows, via PSAPI) the working-set delta -> measured bytes-per-note for
# the Phase 28 multi-document memory budget. Same ODR-shim linkage as
# test_midi_channel. Asserts are loose anti-hang guards; the numbers come
# out via qInfo (run: ctest -R EventPerf -V).
add_executable(test_event_perf
    test_event_perf.cpp
    ../src/midi/MidiChannel.cpp
    ../src/gui/ChannelVisibilityManager.cpp
    ../src/MidiEvent/NoteOnEvent.cpp
    ../src/MidiEvent/OffEvent.cpp
    ../src/MidiEvent/OnEvent.cpp
    ../src/MidiEvent/ProgChangeEvent.cpp
    ../src/protocol/Protocol.cpp
    ../src/protocol/ProtocolStep.cpp
    ../src/protocol/ProtocolItem.cpp
    ../src/protocol/ProtocolEntry.cpp
)
target_include_directories(test_event_perf PRIVATE
    ../src/midi
    ../src/MidiEvent
    ../src/protocol
    ../src/gui
)
target_link_libraries(test_event_perf PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
if(WIN32)
    target_link_libraries(test_event_perf PRIVATE psapi)
endif()
add_test(NAME EventPerf COMMAND test_event_perf)
# Qt DLLs must be on PATH for the ctest runner (same fix as ModelListFetcher).
get_target_property(_qt_core_loc Qt6::Core LOCATION)
get_filename_component(_qt_bin_dir "${_qt_core_loc}" DIRECTORY)
set_tests_properties(EventPerf PROPERTIES
    ENVIRONMENT "PATH=${_qt_bin_dir};$ENV{PATH}")

# -----------------------------------------------------------------------------
# test_document_manager
# -----------------------------------------------------------------------------
# Phase 28: pure list/active-index bookkeeping for multi-document tabs.
# DocumentManager never dereferences the MidiFile, so the test links only the
# single .cpp and uses sentinel pointers.
add_executable(test_document_manager
    test_document_manager.cpp
    ../src/gui/DocumentManager.cpp
)
target_include_directories(test_document_manager PRIVATE
    ../src/gui
)
target_link_libraries(test_document_manager PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME DocumentManager COMMAND test_document_manager)
get_target_property(_qt_core_loc Qt6::Core LOCATION)
get_filename_component(_qt_bin_dir "${_qt_core_loc}" DIRECTORY)
set_tests_properties(DocumentManager PROPERTIES
    ENVIRONMENT "PATH=${_qt_bin_dir};$ENV{PATH}")

# -----------------------------------------------------------------------------
# test_tempo_conversion_service
# -----------------------------------------------------------------------------
# Validates src/converter/TempoConversionService — Phase 33's
# "Convert Tempo, Preserve Duration" headless engine. Same ODR-shim
# strategy as test_midi_channel: real MidiChannel + the four event types
# we construct (NoteOnEvent, OffEvent, OnEvent, TempoChangeEvent +
# ProgChangeEvent for the OnEvent dependency) + the protocol/* TUs +
# the real ChannelVisibilityManager (header-only deps). MidiFile,
# MidiTrack, MidiEvent base, GraphicObject, EventWidget, Appearance are
# defined out-of-line in the test TU. The MidiEvent::setMidiTime shim
# is smarter than other tests' — it actually re-keys events in their
# channel's QMultiMap so the service's tick-scaling pass produces the
# expected map state.
add_executable(test_tempo_conversion_service
    test_tempo_conversion_service.cpp
    ../src/converter/TempoConversionService.cpp
    ../src/midi/MidiChannel.cpp
    ../src/gui/ChannelVisibilityManager.cpp
    ../src/MidiEvent/NoteOnEvent.cpp
    ../src/MidiEvent/OffEvent.cpp
    ../src/MidiEvent/OnEvent.cpp
    ../src/MidiEvent/TempoChangeEvent.cpp
    ../src/MidiEvent/ProgChangeEvent.cpp
    ../src/protocol/Protocol.cpp
    ../src/protocol/ProtocolStep.cpp
    ../src/protocol/ProtocolItem.cpp
    ../src/protocol/ProtocolEntry.cpp
)
target_include_directories(test_tempo_conversion_service PRIVATE
    ../src/converter
    ../src/midi
    ../src/MidiEvent
    ../src/protocol
    ../src/gui
)
target_link_libraries(test_tempo_conversion_service PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME TempoConversionService COMMAND test_tempo_conversion_service)

# -----------------------------------------------------------------------------
# test_paste_special_dialog
# -----------------------------------------------------------------------------
# Phase 34 GUI-only coverage of PasteSpecialDialog: default radio
# selection per PasteAssignment, chosenAssignment() follows toggling,
# the "Make this the new default" checkbox cascade, and a smoke check
# that the summary label surfaces event count + source track names.
# Compiles only PasteSpecialDialog.cpp; no MidiFile / MidiEvent / EventTool
# linkage so the test stays headless and self-contained.
add_executable(test_paste_special_dialog
    test_paste_special_dialog.cpp
    ../src/gui/PasteSpecialDialog.cpp
)
target_include_directories(test_paste_special_dialog PRIVATE
    ../src/gui
)
target_link_libraries(test_paste_special_dialog PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME PasteSpecialDialog COMMAND test_paste_special_dialog)

# -----------------------------------------------------------------------------
# test_text_event
# -----------------------------------------------------------------------------
# Validates SMF byte encoding (FF <type> <varlen> <utf8>) and accessors for
# TextEvent. Compiles only TextEvent.cpp; ODR-shims MidiEvent base + GraphicObject
# + ProtocolEntry + EventWidget + MidiTrack + the single MidiFile helper
# writeVariableLengthValue (which TextEvent::save calls). No real MidiFile /
# MidiChannel / MidiTrack tree is linked.
add_executable(test_text_event
    test_text_event.cpp
    ../src/MidiEvent/TextEvent.cpp
)
target_include_directories(test_text_event PRIVATE
    ../src/MidiEvent
    ../src/midi
    ../src/protocol
    ../src/gui
)
target_link_libraries(test_text_event PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME TextEvent COMMAND test_text_event)

# -----------------------------------------------------------------------------
# test_sysex_event
# -----------------------------------------------------------------------------
# Validates the F0/<data>/F7 framing of SysExEvent::save plus the empty-payload
# guard, accessors and setData round-trip. Same ODR-shim approach as
# test_text_event; SysExEvent.cpp does not call any MidiFile helper so even
# the writeVariableLengthValue shim is unnecessary.
add_executable(test_sysex_event
    test_sysex_event.cpp
    ../src/MidiEvent/SysExEvent.cpp
)
target_include_directories(test_sysex_event PRIVATE
    ../src/MidiEvent
    ../src/midi
    ../src/protocol
    ../src/gui
)
target_link_libraries(test_sysex_event PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME SysExEvent COMMAND test_sysex_event)

# -----------------------------------------------------------------------------
# test_midi_event
# -----------------------------------------------------------------------------
# Validates the MidiEvent base class in src/MidiEvent/MidiEvent.cpp: ctor
# field init, copy ctor, channel() clamping to 0..18, track/file/widget
# accessors, knownMetaTypes() content, the piano-roll line-enum constants
# (TEMPO_CHANGE_EVENT_LINE = 128 ... UNKNOWN_LINE), and the temporaryRecordID
# round-trip. Links the real MidiEvent.cpp; ODR-shims the MidiFile /
# MidiChannel / Appearance / EventWidget / GraphicObject / ProtocolEntry
# symbols that the live MidiEvent vtable references (never called at
# runtime). loadMidiEvent's references to every event subclass ctor are
# stripped by /Gy + /OPT:REF (Release default in build_tests.bat).
add_executable(test_midi_event
    test_midi_event.cpp
    ../src/MidiEvent/MidiEvent.cpp
    ../src/MidiEvent/NoteOnEvent.cpp
    ../src/MidiEvent/OffEvent.cpp
    ../src/MidiEvent/OnEvent.cpp
    ../src/MidiEvent/ControlChangeEvent.cpp
    ../src/MidiEvent/ProgChangeEvent.cpp
    ../src/MidiEvent/PitchBendEvent.cpp
    ../src/MidiEvent/ChannelPressureEvent.cpp
    ../src/MidiEvent/KeyPressureEvent.cpp
    ../src/MidiEvent/TempoChangeEvent.cpp
    ../src/MidiEvent/TimeSignatureEvent.cpp
    ../src/MidiEvent/KeySignatureEvent.cpp
    ../src/MidiEvent/TextEvent.cpp
    ../src/MidiEvent/SysExEvent.cpp
    ../src/MidiEvent/UnknownEvent.cpp
    ../src/gui/GraphicObject.cpp
)
target_include_directories(test_midi_event PRIVATE
    ../src/MidiEvent
    ../src/midi
    ../src/protocol
    ../src/gui
)
target_link_libraries(test_midi_event PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME MidiEventBase COMMAND test_midi_event)

# -----------------------------------------------------------------------------
# test_channel_events
# -----------------------------------------------------------------------------
# Single happy-path encode-and-verify per channel-voice event class
# (ControlChangeEvent, PitchBendEvent, ProgChangeEvent, ChannelPressureEvent,
# KeyPressureEvent) plus a few high-value edge cases (PitchBend 14-bit split
# at centre/min/max; ControlChange channel-nibble mask). Same ODR-shim
# approach as test_text_event / test_sysex_event.
add_executable(test_channel_events
    test_channel_events.cpp
    ../src/MidiEvent/ControlChangeEvent.cpp
    ../src/MidiEvent/PitchBendEvent.cpp
    ../src/MidiEvent/ProgChangeEvent.cpp
    ../src/MidiEvent/ChannelPressureEvent.cpp
    ../src/MidiEvent/KeyPressureEvent.cpp
)
target_include_directories(test_channel_events PRIVATE
    ../src/MidiEvent
    ../src/midi
    ../src/protocol
    ../src/gui
)
target_link_libraries(test_channel_events PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME ChannelEvents COMMAND test_channel_events)

# -----------------------------------------------------------------------------
# test_mml_converter
# -----------------------------------------------------------------------------
# Validates the MML (Music Macro Language) parser pipeline:
# MmlConverter::convert -> MmlLexer::tokenize -> MmlParser::parse -> MmlSong.
# All three .cpp files are pure value transforms with no MidiFile / Qt GUI
# deps, so the test links nothing else from the project.
add_executable(test_mml_converter
    test_mml_converter.cpp
    ../src/converter/MML/MmlConverter.cpp
    ../src/converter/MML/MmlLexer.cpp
    ../src/converter/MML/MmlParser.cpp
)
target_include_directories(test_mml_converter PRIVATE
    ../src/converter/MML
)
target_link_libraries(test_mml_converter PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME MmlConverter COMMAND test_mml_converter)

# -----------------------------------------------------------------------------
# test_gp_binary_reader
# -----------------------------------------------------------------------------
# Validates the byte-stream extractor used by every Guitar Pro 3/4/5 binary
# parser (src/converter/GuitarPro/GpBinaryReader). The class is pure STL —
# no Qt, no MidiFile, no project deps — so the test compiles only the single
# .cpp and links nothing else from the project.
add_executable(test_gp_binary_reader
    test_gp_binary_reader.cpp
    ../src/converter/GuitarPro/GpBinaryReader.cpp
)
target_include_directories(test_gp_binary_reader PRIVATE
    ../src/converter/GuitarPro
)
target_link_libraries(test_gp_binary_reader PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME GpBinaryReader COMMAND test_gp_binary_reader)

# -----------------------------------------------------------------------------
# test_mml_midi_writer
# -----------------------------------------------------------------------------
# Validates the SMF byte serializer that turns an MmlSong into a Standard
# MIDI File byte stream. MmlMidiWriter has zero MidiFile / GUI deps; it walks
# the header-only POD structs in MmlModels.h and emits raw bytes via
# QByteArray. The test compiles only that single .cpp.
add_executable(test_mml_midi_writer
    test_mml_midi_writer.cpp
    ../src/converter/MML/MmlMidiWriter.cpp
)
target_include_directories(test_mml_midi_writer PRIVATE
    ../src/converter/MML
)
target_link_libraries(test_mml_midi_writer PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME MmlMidiWriter COMMAND test_mml_midi_writer)

# -----------------------------------------------------------------------------
# test_three_mle_parser
# -----------------------------------------------------------------------------
# Validates the 3MLE INI-format parser that converts FFXIV bard scores
# (multi-channel MML in [Settings] / [ChannelN] sections) into an MmlSong.
# ThreeMleParser delegates per-channel MML parsing to MmlLexer + MmlParser
# (already covered by test_mml_converter); the harness pulls all three .cpps
# in so the lexer/parser symbols resolve at link time.
add_executable(test_three_mle_parser
    test_three_mle_parser.cpp
    ../src/converter/MML/ThreeMleParser.cpp
    ../src/converter/MML/MmlLexer.cpp
    ../src/converter/MML/MmlParser.cpp
)
target_include_directories(test_three_mle_parser PRIVATE
    ../src/converter/MML
)
target_link_libraries(test_three_mle_parser PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME ThreeMleParser COMMAND test_three_mle_parser)

# -----------------------------------------------------------------------------
# test_collab_history_file
# -----------------------------------------------------------------------------
# Validates the sidecar-JSON I/O of src/collab/CollabHistoryFile (Phase 9.1b):
# sidecarPathFor() path translation, tolerant field parsing in loadFromJson,
# QSaveFile-based atomic save/load round-trip, appendCommit / ensureSessionId,
# and compactHistory(N) hunk-stripping while preserving audit metadata.
# Pure Qt (QJson + QSaveFile + QUuid) — no MidiFile dep, no network, no
# QSettings. The test compiles only the single .cpp; QTemporaryDir scopes
# all on-disk fixtures.
add_executable(test_collab_history_file
    test_collab_history_file.cpp
    ../src/collab/CollabHistoryFile.cpp
)
target_include_directories(test_collab_history_file PRIVATE
    ../src/collab
)
target_link_libraries(test_collab_history_file PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME CollabHistoryFile COMMAND test_collab_history_file)

# -----------------------------------------------------------------------------
# test_logging_config
# -----------------------------------------------------------------------------
# Validates src/LoggingConfig (Phase 9.6f): coarse log-level mapping to Qt
# filter-rules, per-category override layering + ordering, the collab-verbose
# flag, and the loadLevel / loadPerCategory / applyAndPersist persistence
# round-trip. Sandboxes QSettings via QStandardPaths::setTestModeEnabled(true)
# so Logging/* and Collab/verboseLogging keys never touch the dev config.
# Pure Qt — no MidiFile, no network deps.
add_executable(test_logging_config
    test_logging_config.cpp
    ../src/LoggingConfig.cpp
)
target_include_directories(test_logging_config PRIVATE
    ../src
)
target_link_libraries(test_logging_config PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME LoggingConfig COMMAND test_logging_config)

# -----------------------------------------------------------------------------
# test_drum_kit_preset
# -----------------------------------------------------------------------------
# Validates the DrumKitPreset static factory (GM / Rock / Jazz percussion
# groupings used by the drum-roll lane mapper). Pure data factory — no Qt
# parent / signal deps — the test compiles only that one .cpp.
add_executable(test_drum_kit_preset
    test_drum_kit_preset.cpp
    ../src/gui/DrumKitPreset.cpp
)
target_include_directories(test_drum_kit_preset PRIVATE
    ../src/gui
)
target_link_libraries(test_drum_kit_preset PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME DrumKitPreset COMMAND test_drum_kit_preset)

# Make Qt DLLs visible to the tests at runtime on Windows.
if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    set_tests_properties(
        XmlScoreToMidi
        MusicXmlExport
        ChordDetector
        SrtParser
        LrcExporter
        Protocol
        Selection
        MetaEvents
        ConversationStore
        MidiTrack
        ToolDefinitions
        ChannelVisibilityManager
        NotePairing
        MidiChannel
        TextEvent
        SysExEvent
        ChannelEvents
        MidiEventBase
        MmlConverter
        ModelFavorites
        AgentRunnerState
        AgentToolPolicy
        StreamingFallback
        FfxivVoiceAnalyzer
        FfxivEqualizerService
        TempoConversionService
        PromptProfiles
        PasteSpecialDialog
        GpBinaryReader
        MmlMidiWriter
        ThreeMleParser
        DrumKitPreset
        MidiDiff
        PrBundle
        MidiHash
        CollabIdentity
        HistoryReconciliation
        CollabHistoryFile
        LoggingConfig
        IceConfig
        RtcSignalingToken
        SessionMode
        TimeDisplayFormat
        ImportOnlyFormats
        SidFile
        SidCpu
        SidCapture
        SidReconstruct
        SidMidiWriter
        PROPERTIES
            ENVIRONMENT "PATH=${_qt_bin_dir};$ENV{PATH}"
    )
    if(MIDIPILOT_ENABLE_LIVE_API_TESTS)
        set_tests_properties(ProviderMatrix PROPERTIES
            ENVIRONMENT "PATH=${_qt_bin_dir};$ENV{PATH}")
    endif()
    if(TARGET test_gp_unzip)
        set_tests_properties(GpUnzip PROPERTIES
            ENVIRONMENT "PATH=${_qt_bin_dir};$ENV{PATH}")
    endif()
endif()
