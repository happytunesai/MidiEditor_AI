# MidiEditorAI unit tests
#
# Each test is a small self-contained Qt Test executable so the test binary
# can be kept narrow (no need to link the entire MidiEditorAI codebase just
# to exercise one module).

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Widgets)

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

# Make Qt DLLs visible to the tests at runtime on Windows.
if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    set_tests_properties(
        XmlScoreToMidi
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
