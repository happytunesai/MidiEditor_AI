cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

set(MIDIEDITOR_RELEASE_VERSION_STRING "1.5.3")
project(MidiEditorAI VERSION ${MIDIEDITOR_RELEASE_VERSION_STRING} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Performance optimizations
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Enable interprocedural optimization (LTO) for release builds
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message(STATUS "Building optimized ${CMAKE_BUILD_TYPE} version")

    # Aggressive optimization flags (MIDI-safe: no -ffast-math due to timing precision requirements)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -fomit-frame-pointer -funroll-loops -fprefetch-loop-arrays")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -DNDEBUG -fomit-frame-pointer")

        # CPU-specific optimizations (x86-64-v3 for modern CPUs with AVX2)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=x86-64-v3 -mtune=generic")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -march=x86-64-v3 -mtune=generic")

        # Advanced optimization flags
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fvectorize -fslp-vectorize")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffunction-sections -fdata-sections")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -falign-functions=32 -falign-loops=32")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fmerge-all-constants")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fmodulo-sched -fmodulo-sched-allow-regmoves")

        # Safe floating-point optimizations (no -ffast-math)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-math-errno -ffinite-math-only")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-signed-zeros -fno-trapping-math")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -fno-math-errno -ffinite-math-only")

        # Link-time optimizations with advanced flags
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-Wl,-O3 -Wl,--as-needed -Wl,--gc-sections -Wl,--icf=all -s")

    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(CMAKE_CXX_FLAGS_RELEASE "/Ox /Ob2 /Oi /Ot /GL /DNDEBUG /arch:AVX2")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/Ox /Ob2 /Oi /Ot /GL /Zi /DNDEBUG /arch:AVX2")
        # Advanced MSVC optimizations (optimal for both Intel and AMD)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Gy /Gw /GA")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Gy /Gw /GA")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /OPT:REF /OPT:ICF")
        set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/LTCG /OPT:REF /OPT:ICF /DEBUG")
    endif()

    # Enable LTO if supported
    if(ipo_supported)
        message(STATUS "IPO/LTO enabled")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "IPO/LTO not supported: ${ipo_error}")
    endif()
else()
    message(STATUS "Building debug version with optimizations disabled")
endif()

# Platform detection
if(WIN32)
    set(PLAT_WINDOWS TRUE)
elseif(UNIX AND NOT APPLE)
    set(PLAT_LINUX TRUE)
elseif(APPLE)
    set(PLAT_MACOSX TRUE)
endif()

# Options
option(GENERATE_REPOSITORY "generate repository to update online repositories" OFF)
if(PLAT_LINUX)
    execute_process(COMMAND lsb_release -si OUTPUT_VARIABLE LINUX_DISTRO OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(LINUX_DISTRO STREQUAL "Ubuntu")
        option(LIBRARIES_FROM_APT "use libraries from apt rather than package manager" OFF)
    endif()
endif()

# Qt6 detection with fallbacks
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} $ENV{QT_ROOT_DIR} $ENV{Qt6_DIR})

# Add common Qt installation paths
if(WIN32)
    # Common Qt installation paths on Windows
    # Prioritize MinGW builds if using GCC compiler
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        file(GLOB QT_MINGW "C:/Qt/6.*/mingw*")
        if(QT_MINGW)
            list(GET QT_MINGW -1 QT_MINGW_LATEST)
            set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "${QT_MINGW_LATEST}")
            message(STATUS "Found Qt MinGW installation: ${QT_MINGW_LATEST}")
        endif()
    endif()

    # Fallback to MSVC builds
    file(GLOB QT_VERSIONS "C:/Qt/6.*")
    if(QT_VERSIONS)
        list(GET QT_VERSIONS -1 QT_LATEST_VERSION)  # Get the latest version
        set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "${QT_LATEST_VERSION}/msvc2022_64")
        message(STATUS "Found Qt installation: ${QT_LATEST_VERSION}/msvc2022_64")
    endif()

    # Also try other common compiler versions
    file(GLOB QT_MSVC2019 "C:/Qt/6.*/msvc2019_64")
    if(QT_MSVC2019)
        list(GET QT_MSVC2019 -1 QT_MSVC2019_LATEST)
        set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "${QT_MSVC2019_LATEST}")
        message(STATUS "Found Qt MSVC2019 installation: ${QT_MSVC2019_LATEST}")
    endif()
endif()

# Try to find Qt6 with different approaches
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Network OpenGL OpenGLWidgets)

if(NOT Qt6_FOUND)
    # Try with Qt6_DIR environment variable
    if(DEFINED ENV{Qt6_DIR})
        set(Qt6_DIR $ENV{Qt6_DIR})
        find_package(Qt6 QUIET COMPONENTS Core Widgets Gui Network OpenGL OpenGLWidgets)
    endif()
endif()

if(NOT Qt6_FOUND)
    # Try with QT_ROOT_DIR environment variable
    if(DEFINED ENV{QT_ROOT_DIR})
        set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} $ENV{QT_ROOT_DIR})
        find_package(Qt6 QUIET COMPONENTS Core Widgets Gui Network OpenGL OpenGLWidgets)
    endif()
endif()

if(NOT Qt6_FOUND)
    message(FATAL_ERROR "Qt6 not found. Please set Qt6_DIR or QT_ROOT_DIR environment variable to your Qt installation directory, or install Qt6 to C:/Qt/")
endif()

# Debug Qt6 information
message(STATUS "Qt6 found: ${Qt6_FOUND}")
message(STATUS "Qt6 version: ${Qt6_VERSION}")
message(STATUS "Qt6 directory: ${Qt6_DIR}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
get_target_property(QT6_CORE_LOCATION Qt6::Core LOCATION)
message(STATUS "Qt6::Core location: ${QT6_CORE_LOCATION}")

# Check for compiler/Qt compatibility
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARNING "Using MinGW compiler with Qt6. Make sure Qt6 was compiled with MinGW, not MSVC.")
    message(STATUS "If you encounter linking errors, try using Qt6 compiled for MinGW or switch to MSVC compiler.")
endif()

# Enable Qt6 MOC, UIC, and RCC processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Ensure proper Qt6 setup
if(Qt6_FOUND)
    qt6_standard_project_setup()
endif()

# Main target
add_executable(MidiEditorAI)

# Apply performance optimizations to target
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    if(ipo_supported)
        set_property(TARGET MidiEditorAI PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

target_link_libraries(MidiEditorAI PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::OpenGL
        Qt6::OpenGLWidgets
)

# Link system OpenGL library for cross-platform compatibility
find_package(OpenGL REQUIRED)
target_link_libraries(MidiEditorAI PRIVATE ${OPENGL_LIBRARIES})

# Optional FluidSynth support for built-in synthesizer
# Accept pre-built install dir via -DFLUIDSYNTH_DIR=... or FLUIDSYNTH_DIR env var
if(NOT FLUIDSYNTH_DIR)
    set(FLUIDSYNTH_DIR "$ENV{FLUIDSYNTH_DIR}")
endif()

if(FLUIDSYNTH_DIR)
    # Pre-built path provided — search there first
    find_library(FLUIDSYNTH_LIBRARY
        NAMES fluidsynth libfluidsynth libfluidsynth-3
        PATHS "${FLUIDSYNTH_DIR}/lib"
        NO_DEFAULT_PATH
    )
    find_path(FLUIDSYNTH_INCLUDE_DIR
        NAMES fluidsynth.h
        PATHS "${FLUIDSYNTH_DIR}/include"
        NO_DEFAULT_PATH
    )
    if(FLUIDSYNTH_LIBRARY AND FLUIDSYNTH_INCLUDE_DIR)
        set(FLUIDSYNTH_FOUND TRUE)
    else()
        message(WARNING "FLUIDSYNTH_DIR set to '${FLUIDSYNTH_DIR}' but fluidsynth not found there")
    endif()
endif()

if(NOT FLUIDSYNTH_FOUND)
    # Fallback: pkg-config (Linux/macOS system install)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FLUIDSYNTH IMPORTED_TARGET fluidsynth)
    endif()
endif()

if(NOT FLUIDSYNTH_FOUND)
    # Last resort: generic system search
    find_library(FLUIDSYNTH_LIBRARY NAMES fluidsynth libfluidsynth libfluidsynth-3)
    find_path(FLUIDSYNTH_INCLUDE_DIR NAMES fluidsynth.h)
    if(FLUIDSYNTH_LIBRARY AND FLUIDSYNTH_INCLUDE_DIR)
        set(FLUIDSYNTH_FOUND TRUE)
    endif()
endif()

if(FLUIDSYNTH_FOUND)
    message(STATUS "FluidSynth found - enabling built-in synthesizer support")
    target_compile_definitions(MidiEditorAI PRIVATE FLUIDSYNTH_SUPPORT)
    # Pass define to MOC so it processes headers inside #ifdef FLUIDSYNTH_SUPPORT
    set_property(TARGET MidiEditorAI APPEND PROPERTY AUTOMOC_MOC_OPTIONS "-DFLUIDSYNTH_SUPPORT")
    if(TARGET PkgConfig::FLUIDSYNTH)
        target_link_libraries(MidiEditorAI PRIVATE PkgConfig::FLUIDSYNTH)
    else()
        target_include_directories(MidiEditorAI PRIVATE ${FLUIDSYNTH_INCLUDE_DIR})
        target_link_libraries(MidiEditorAI PRIVATE ${FLUIDSYNTH_LIBRARY})
    endif()
else()
    message(STATUS "FluidSynth not found - built-in synthesizer disabled")
endif()

# Built-in LAME MP3 encoder (compiled from source, always available when FluidSynth is)
if(FLUIDSYNTH_FOUND)
    set(LAME_SRC_DIR "${CMAKE_SOURCE_DIR}/lame/lame-3.100")
    if(EXISTS "${LAME_SRC_DIR}/libmp3lame/lame.c")
        message(STATUS "LAME source found - building built-in MP3 encoder")
        
        # Collect LAME libmp3lame source files
        file(GLOB LAME_SOURCES "${LAME_SRC_DIR}/libmp3lame/*.c")
        
        # Add SSE optimizations on x64
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            file(GLOB LAME_VECTOR_SOURCES "${LAME_SRC_DIR}/libmp3lame/vector/*.c")
            list(APPEND LAME_SOURCES ${LAME_VECTOR_SOURCES})
        endif()

        # Build as static library (C language)
        add_library(mp3lame STATIC ${LAME_SOURCES})
        set_target_properties(mp3lame PROPERTIES LINKER_LANGUAGE C)
        target_include_directories(mp3lame PRIVATE
            "${CMAKE_SOURCE_DIR}/lame"
            "${LAME_SRC_DIR}/include"
            "${LAME_SRC_DIR}/libmp3lame"
        )
        target_compile_definitions(mp3lame PRIVATE
            HAVE_CONFIG_H
            LAME_LIBRARY_BUILD
            _CRT_SECURE_NO_WARNINGS
        )
        # Suppress LAME warnings (it's third-party code)
        if(MSVC)
            target_compile_options(mp3lame PRIVATE /W0)
        else()
            target_compile_options(mp3lame PRIVATE -w)
        endif()

        # Link LAME into MidiEditorAI
        target_link_libraries(MidiEditorAI PRIVATE mp3lame)
        target_include_directories(MidiEditorAI PRIVATE "${LAME_SRC_DIR}/include")
        target_compile_definitions(MidiEditorAI PRIVATE LAME_SUPPORT)
        set_property(TARGET MidiEditorAI APPEND PROPERTY AUTOMOC_MOC_OPTIONS "-DLAME_SUPPORT")
        set(LAME_FOUND TRUE)
    else()
        message(STATUS "LAME source not found at ${LAME_SRC_DIR} - MP3 export disabled")
    endif()
endif()

# Additional linking for MinGW compatibility
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(MidiEditorAI PRIVATE -Wl,--allow-multiple-definition)
endif()

# Include directories
target_include_directories(MidiEditorAI PRIVATE
    src
    src/gui
    src/midi
    src/MidiEvent
    src/protocol
    src/tool
    src/ai
    src/converter
    src/converter/GuitarPro
    src/converter/MML
    src/converter/MusicXml
)
message(STATUS "Added project include directories")

# Optional zlib for Guitar Pro 6/7/8 support (ZIP-based formats)
set(GP678_ENABLED OFF)
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    message(STATUS "zlib found (system) - enabling GP6/7/8 Guitar Pro import")
    target_compile_definitions(MidiEditorAI PRIVATE GP678_SUPPORT)
    target_link_libraries(MidiEditorAI PRIVATE ZLIB::ZLIB)
    set(GP678_ENABLED ON)
else()
    # Fallback: use Qt6's bundled zlib (shipped with Qt6::Core)
    find_package(Qt6 COMPONENTS ZlibPrivate QUIET)
    if(TARGET Qt6::ZlibPrivate)
        message(STATUS "Using Qt6 bundled zlib - enabling GP6/7/8 Guitar Pro import")
        target_compile_definitions(MidiEditorAI PRIVATE GP678_SUPPORT)
        target_link_libraries(MidiEditorAI PRIVATE Qt6::ZlibPrivate)
        set(GP678_ENABLED ON)
    else()
        message(STATUS "zlib not found - GP6/7/8 disabled, GP3/4/5 still available")
    endif()
endif()

# Source files - be more specific to avoid test/doc/example files
file(GLOB SOURCES_MAIN "src/*.cpp")
file(GLOB SOURCES_MIDIEVENT "src/MidiEvent/*.cpp")
file(GLOB SOURCES_GUI "src/gui/*.cpp")
file(GLOB SOURCES_PROTOCOL "src/protocol/*.cpp")
file(GLOB SOURCES_TOOL "src/tool/*.cpp")
file(GLOB SOURCES_AI "src/ai/*.cpp")
file(GLOB SOURCES_CONVERTER_BASE "src/converter/*.cpp")
file(GLOB SOURCES_CONVERTER "src/converter/GuitarPro/*.cpp")
file(GLOB SOURCES_CONVERTER_MML "src/converter/MML/*.cpp")
file(GLOB SOURCES_CONVERTER_MUSICXML "src/converter/MusicXml/*.cpp")

# If GP678_SUPPORT is not available, exclude GP6/7/8 parser files that need zlib
if(NOT GP678_ENABLED)
    list(FILTER SOURCES_CONVERTER EXCLUDE REGEX "Gp678Parser\\.cpp")
    list(FILTER SOURCES_CONVERTER EXCLUDE REGEX "GpUnzip\\.cpp")
endif()

# For midi directory, be selective to avoid test files
file(GLOB SOURCES_MIDI "src/midi/*.cpp")

# Combine all sources
set(SOURCES
    ${SOURCES_MAIN}
    ${SOURCES_MIDIEVENT}
    ${SOURCES_GUI}
    ${SOURCES_MIDI}
    ${SOURCES_PROTOCOL}
    ${SOURCES_TOOL}
    ${SOURCES_AI}
    ${SOURCES_CONVERTER_BASE}
    ${SOURCES_CONVERTER}
    ${SOURCES_CONVERTER_MML}
    ${SOURCES_CONVERTER_MUSICXML}
)

# Add only main rtmidi files if they exist (not tests/docs/examples)
if(EXISTS "${CMAKE_SOURCE_DIR}/src/midi/rtmidi/RtMidi.cpp")
    list(APPEND SOURCES "src/midi/rtmidi/RtMidi.cpp")
    # Add rtmidi include directory so RtMidi.h can be found
    target_include_directories(MidiEditorAI PRIVATE "src/midi/rtmidi")
    message(STATUS "Added RtMidi.cpp and include directory")
else()
    message(WARNING "RtMidi.cpp not found at src/midi/rtmidi/RtMidi.cpp")
endif()

# Remove any test, example, or doc files that might have been included
list(FILTER SOURCES EXCLUDE REGEX ".*/tests/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*/test/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*/examples/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*/example/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*/doc/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*/docs/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*/android/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*/samples/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*/old/.*")

# Headers - be more specific to avoid test/doc/example files
file(GLOB HEADERS_MAIN "src/*.h")
file(GLOB HEADERS_MIDIEVENT "src/MidiEvent/*.h")
file(GLOB HEADERS_GUI "src/gui/*.h")
file(GLOB HEADERS_PROTOCOL "src/protocol/*.h")
file(GLOB HEADERS_TOOL "src/tool/*.h")
file(GLOB HEADERS_AI "src/ai/*.h")
file(GLOB HEADERS_CONVERTER "src/converter/GuitarPro/*.h")
file(GLOB HEADERS_CONVERTER_MML "src/converter/MML/*.h")
file(GLOB HEADERS_CONVERTER_MUSICXML "src/converter/MusicXml/*.h")

# For midi directory, be selective to avoid test files
file(GLOB HEADERS_MIDI "src/midi/*.h")

# Combine all headers
set(HEADERS
    ${HEADERS_MAIN}
    ${HEADERS_MIDIEVENT}
    ${HEADERS_GUI}
    ${HEADERS_MIDI}
    ${HEADERS_PROTOCOL}
    ${HEADERS_TOOL}
    ${HEADERS_AI}
    ${HEADERS_CONVERTER}
    ${HEADERS_CONVERTER_MML}
    ${HEADERS_CONVERTER_MUSICXML}
)

if(EXISTS "${CMAKE_SOURCE_DIR}/src/midi/rtmidi/RtMidi.h")
    list(APPEND HEADERS "src/midi/rtmidi/RtMidi.h")
    message(STATUS "Added RtMidi.h")
else()
    message(WARNING "RtMidi.h not found at src/midi/rtmidi/RtMidi.h")
endif()

# Remove any test, example, or doc files that might have been included
list(FILTER HEADERS EXCLUDE REGEX ".*/tests/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/test/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/examples/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/example/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/doc/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/docs/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/android/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/samples/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/old/.*")

# Check if we have any source files
list(LENGTH SOURCES SOURCE_COUNT)
if(SOURCE_COUNT EQUAL 0)
    message(FATAL_ERROR "No source files found! Please check that the src directory exists and contains .cpp files.")
endif()

message(STATUS "Found ${SOURCE_COUNT} source files")

target_sources(MidiEditorAI PRIVATE ${SOURCES} ${HEADERS})

# Qt resources
if(EXISTS "${CMAKE_SOURCE_DIR}/resources.qrc")
    target_sources(MidiEditorAI PRIVATE resources.qrc)
    message(STATUS "Added Qt resources from resources.qrc")
else()
    message(WARNING "resources.qrc not found - some resources may be missing")
endif()

# Architecture defines
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(MidiEditorAI PRIVATE __ARCH64__)
endif()

# Version defines
string(TIMESTAMP CURRENT_DATE "%x")
target_compile_definitions(MidiEditorAI PRIVATE
        MIDIEDITOR_RELEASE_VERSION_ID_DEF=0
        MIDIEDITOR_RELEASE_DATE_DEF="${CURRENT_DATE}"
        MIDIEDITOR_RELEASE_VERSION_STRING_DEF="${MIDIEDITOR_RELEASE_VERSION_STRING}"
)

# Platform-specific settings
if(PLAT_LINUX)
    target_compile_definitions(MidiEditorAI PRIVATE
            __LINUX_ALSASEQ__
            __LINUX_ALSA__
    )
    target_link_libraries(MidiEditorAI PRIVATE asound)
elseif(PLAT_WINDOWS)
    target_compile_definitions(MidiEditorAI PRIVATE __WINDOWS_MM__)
    target_link_libraries(MidiEditorAI PRIVATE winmm dwmapi)

    # Add Windows resource file if it exists
    if(EXISTS "${CMAKE_SOURCE_DIR}/midieditor.rc")
        target_sources(MidiEditorAI PRIVATE midieditor.rc)
        message(STATUS "Added Windows resource file: midieditor.rc")
    else()
        message(WARNING "midieditor.rc not found - Windows resources may be missing")
    endif()

    set_target_properties(MidiEditorAI PROPERTIES
            WIN32_EXECUTABLE TRUE
    )
elseif(PLAT_MACOSX)
    target_compile_definitions(MidiEditorAI PRIVATE __MACOSX_CORE__)
    target_link_libraries(MidiEditorAI PRIVATE
            "-framework CoreMidi"
            "-framework CoreAudio"
            "-framework CoreFoundation"
    )
endif()

# Output directory
set_target_properties(MidiEditorAI PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Qt deployment for development builds (Windows only)
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(WINDEPLOYQT_EXECUTABLE)
        # Deploy Qt libraries to build directory for debugging
        add_custom_command(TARGET MidiEditorAI POST_BUILD
            COMMAND ${WINDEPLOYQT_EXECUTABLE}
                --release
                --compiler-runtime
                --force
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --plugindir $<TARGET_FILE_DIR:MidiEditorAI>/plugins
                $<TARGET_FILE:MidiEditorAI>
            COMMENT "Deploying Qt libraries for release"
        )

        # Ensure platforms plugin is deployed
        add_custom_command(TARGET MidiEditorAI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:MidiEditorAI>/plugins/platforms
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${Qt6_DIR}/../../../plugins/platforms/qwindows$<$<CONFIG:Debug>:d>.dll
                $<TARGET_FILE_DIR:MidiEditorAI>/plugins/platforms/
            COMMENT "Ensuring Qt platform plugins are available"
        )

        # Copy FluidSynth DLLs to build directory if found
        if(FLUIDSYNTH_FOUND AND FLUIDSYNTH_DIR)
            file(GLOB FLUIDSYNTH_DLLS "${FLUIDSYNTH_DIR}/bin/*.dll")
            if(FLUIDSYNTH_DLLS)
                add_custom_command(TARGET MidiEditorAI POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${FLUIDSYNTH_DLLS}
                        $<TARGET_FILE_DIR:MidiEditorAI>/
                    COMMENT "Copying FluidSynth DLLs to build directory"
                )
            endif()
        endif()

        # Copy bundled SoundFont files to build directory so clean builds
        # don't lose them (user-downloaded fonts will still need re-download)
        file(GLOB BUNDLED_SOUNDFONTS "${CMAKE_SOURCE_DIR}/soundfont/*.sf2")
        if(BUNDLED_SOUNDFONTS)
            add_custom_command(TARGET MidiEditorAI POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    $<TARGET_FILE_DIR:MidiEditorAI>/soundfonts
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${BUNDLED_SOUNDFONTS}
                    $<TARGET_FILE_DIR:MidiEditorAI>/soundfonts/
                COMMENT "Copying bundled SoundFont files to build directory"
            )
        endif()
    else()
        message(WARNING "windeployqt not found - attempting manual Qt deployment")

        # Manual deployment of essential Qt DLLs
        set(QT_BIN_DIR ${Qt6_DIR}/../../../bin)
        set(QT_PLUGINS_DIR ${Qt6_DIR}/../../../plugins)

        # Copy essential Qt DLLs
        add_custom_command(TARGET MidiEditorAI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${QT_BIN_DIR}/Qt6Core$<$<CONFIG:Debug>:d>.dll
                ${QT_BIN_DIR}/Qt6Gui$<$<CONFIG:Debug>:d>.dll
                ${QT_BIN_DIR}/Qt6Widgets$<$<CONFIG:Debug>:d>.dll
                ${QT_BIN_DIR}/Qt6Network$<$<CONFIG:Debug>:d>.dll
                $<TARGET_FILE_DIR:MidiEditorAI>/
            COMMENT "Manually copying Qt DLLs"
        )

        # Copy platform plugins
        add_custom_command(TARGET MidiEditorAI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:MidiEditorAI>/plugins/platforms
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${QT_PLUGINS_DIR}/platforms/qwindows$<$<CONFIG:Debug>:d>.dll
                $<TARGET_FILE_DIR:MidiEditorAI>/plugins/platforms/
            COMMENT "Manually copying Qt platform plugins"
        )
    endif()

    # Copy updater.bat to build directory
    if(EXISTS "${CMAKE_SOURCE_DIR}/run_environment/updater.bat")
        add_custom_command(TARGET MidiEditorAI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/run_environment/updater.bat
                $<TARGET_FILE_DIR:MidiEditorAI>/
            COMMENT "Copying updater.bat"
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Optional unit tests (Qt Test based) — opt-in via -DBUILD_TESTING=ON
# ---------------------------------------------------------------------------
option(BUILD_TESTING "Build MidiEditorAI unit tests" OFF)
if(BUILD_TESTING)
    include(CTest)  # calls enable_testing() and generates DartConfiguration.tcl
    add_subdirectory(tests)
endif()

# Installation
set(INSTALL_DIR "packaging/org.midieditor.midieditor/data/")
set(BIN_DIR "${INSTALL_DIR}/bin")
set(PLUGIN_DIR "${BIN_DIR}/plugins")

install(TARGETS MidiEditorAI DESTINATION "${BIN_DIR}")

if(PLAT_WINDOWS)
    # Qt deployment
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(WINDEPLOYQT_EXECUTABLE)
        install(CODE "
            execute_process(COMMAND ${WINDEPLOYQT_EXECUTABLE}
                --plugindir ${PLUGIN_DIR}
                --libdir ${BIN_DIR}
                \${CMAKE_INSTALL_PREFIX}/${BIN_DIR}/MidiEditorAI.exe
            )
            file(GLOB DSENGINE_FILES \"\${CMAKE_INSTALL_PREFIX}/${BIN_DIR}/**/dsengine.dll\")
            foreach(FILE \${DSENGINE_FILES})
                file(REMOVE \${FILE})
            endforeach()
            if(NOT \"${FLUIDSYNTH_DIR}\" STREQUAL \"\")
                file(GLOB FS_DLLS \"${FLUIDSYNTH_DIR}/bin/*.dll\")
                if(FS_DLLS)
                    file(COPY \${FS_DLLS} DESTINATION \${CMAKE_INSTALL_PREFIX}/${BIN_DIR})
                endif()
            endif()
        ")
    endif()
endif()

# Manual target (Windows only)
if(PLAT_WINDOWS)
    add_custom_target(manual ALL)
    install(DIRECTORY "manual/" DESTINATION "packaging/org.midieditor.manual/data/manual")
endif()

# Installer target
if(PLAT_WINDOWS OR PLAT_LINUX)
    add_custom_target(installer DEPENDS MidiEditorAI)

    if(PLAT_WINDOWS)
        add_dependencies(installer manual)

        find_program(BINARYCREATOR_EXECUTABLE binarycreator)
        find_program(REPOGEN_EXECUTABLE repogen)

        if(BINARYCREATOR_EXECUTABLE AND REPOGEN_EXECUTABLE)
            if(GENERATE_REPOSITORY)
                add_custom_command(TARGET installer POST_BUILD
                        COMMAND ${REPOGEN_EXECUTABLE} --update-new-components --packages packaging ${CMAKE_BINARY_DIR}/website/repository
                        COMMAND ${BINARYCREATOR_EXECUTABLE} --config scripts/packaging/windows/config.xml --packages packaging ${CMAKE_BINARY_DIR}/website/MidiEditor.exe
                        COMMAND ${CMAKE_COMMAND} -E copy_directory manual ${CMAKE_BINARY_DIR}/website
                        COMMENT "Generating repository and online installer"
                )
            else()
                add_custom_command(TARGET installer POST_BUILD
                        COMMAND ${BINARYCREATOR_EXECUTABLE} --config scripts/packaging/windows/config.xml --packages packaging packaging/Install.exe
                        COMMENT "Generating offline installer"
                )
            endif()
        endif()

    elseif(PLAT_LINUX AND LINUX_DISTRO STREQUAL "Ubuntu")
        install(FILES "scripts/packaging/debian/MidiEditor.desktop" DESTINATION "usr/share/applications")
        install(FILES "scripts/packaging/debian/logo48.png" DESTINATION "usr/share/pixmaps")
        install(FILES "scripts/packaging/debian/copyright" DESTINATION "usr/share/doc/midieditor")

        configure_file("scripts/packaging/debian/control" "${CMAKE_BINARY_DIR}/control" @ONLY)
        install(FILES "${CMAKE_BINARY_DIR}/control" DESTINATION "DEBIAN")

        add_custom_command(TARGET installer POST_BUILD
                COMMAND chmod 755 ${CMAKE_INSTALL_PREFIX}
                COMMAND find ${CMAKE_INSTALL_PREFIX} -type d -exec chmod 755 {} +
                COMMAND find ${CMAKE_INSTALL_PREFIX} -type f -exec chmod 644 {} +
                COMMAND chmod +x ${CMAKE_INSTALL_PREFIX}/${BIN_DIR}/MidiEditorAI
                COMMAND fakeroot dpkg-deb --build ${CMAKE_INSTALL_PREFIX}
                COMMENT "Building Debian package"
        )
    endif()
endif()