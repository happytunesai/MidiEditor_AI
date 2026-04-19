# MidiEditorAI unit tests
#
# Each test is a small self-contained Qt Test executable so the test binary
# can be kept narrow (no need to link the entire MidiEditorAI codebase just
# to exercise one module).

find_package(Qt6 REQUIRED COMPONENTS Core Test)

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

# Make Qt DLLs visible to the tests at runtime on Windows.
if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    set_tests_properties(XmlScoreToMidi ChordDetector PROPERTIES
        ENVIRONMENT "PATH=${_qt_bin_dir};$ENV{PATH}"
    )
endif()
