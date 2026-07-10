cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# Propagate CMake's IPO/LTO policy to FetchContent-fetched subprojects
# (libdatachannel, usrsctp, libjuice). Without this, those projects'
# older `cmake_minimum_required` leaves CMP0069 at OLD, so any
# `set_target_properties(... INTERPROCEDURAL_OPTIMIZATION ON)` they do
# is silently ignored AND CMake emits a "policy CMP0069 is not set"
# warning per affected target. Setting the default to NEW honours their
# IPO request and silences the warnings.
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

set(MIDIEDITOR_RELEASE_VERSION_STRING "2.0.1")
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

# Collaboration features (Phase 9.1+, async PR-style editing).
# Default ON because the runtime is fully gated by an opt-in toggle (Settings ->
# Collaboration -> Enable collaboration features) and the on-disk footprint is
# zero until the user explicitly initialises a file. Builders who want a slim
# build can disable this option to exclude src/collab and src/gui/collab from
# the source list entirely.
option(MIDIEDITOR_ENABLE_COLLAB "Enable collaboration features (PR-style async editing)" ON)
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
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Network OpenGL OpenGLWidgets Multimedia)

if(NOT Qt6_FOUND)
    # Try with Qt6_DIR environment variable
    if(DEFINED ENV{Qt6_DIR})
        set(Qt6_DIR $ENV{Qt6_DIR})
        find_package(Qt6 QUIET COMPONENTS Core Widgets Gui Network OpenGL OpenGLWidgets Multimedia)
    endif()
endif()

if(NOT Qt6_FOUND)
    # Try with QT_ROOT_DIR environment variable
    if(DEFINED ENV{QT_ROOT_DIR})
        set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} $ENV{QT_ROOT_DIR})
        find_package(Qt6 QUIET COMPONENTS Core Widgets Gui Network OpenGL OpenGLWidgets Multimedia)
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

# Vendored libsidplayfp (SID emulation engine for accurate RSID import +
# authentic playback). Builds the 'sidplayfp' static lib target.
#
# The source is committed ONCE as third_party/libsidplayfp-vendored.zip (a single
# blob instead of ~140 loose third-party files); the extracted tree is gitignored.
# On a fresh checkout / CI the tree is absent, so unpack it here before use -
# file(ARCHIVE_EXTRACT) runs at configure time (libarchive, same as the zip was
# made with), so the directory exists by the time add_subdirectory() runs below.
# Locally the tree already exists, so this is a no-op. Re-zip if the lib is patched.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/libsidplayfp/CMakeLists.txt")
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/libsidplayfp-vendored.zip")
        message(FATAL_ERROR
            "libsidplayfp is missing: neither third_party/libsidplayfp/ nor "
            "third_party/libsidplayfp-vendored.zip is present.")
    endif()
    message(STATUS "Unpacking vendored libsidplayfp from libsidplayfp-vendored.zip")
    file(ARCHIVE_EXTRACT
        INPUT "${CMAKE_SOURCE_DIR}/third_party/libsidplayfp-vendored.zip"
        DESTINATION "${CMAKE_SOURCE_DIR}/third_party")
endif()
add_subdirectory(third_party/libsidplayfp)

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
        Qt6::Multimedia   # QAudioSink for authentic SID (libsidplayfp) playback
        sidplayfp   # vendored libsidplayfp (SID->MIDI capture; RSID accuracy)
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
    elseif(FLUIDSYNTH_LIBRARY)
        target_include_directories(MidiEditorAI PRIVATE ${FLUIDSYNTH_INCLUDE_DIR})
        target_link_libraries(MidiEditorAI PRIVATE ${FLUIDSYNTH_LIBRARY})
    elseif(FLUIDSYNTH_LIBRARIES)
        # pkg_check_modules may set vars without creating an IMPORTED target.
        target_include_directories(MidiEditorAI PRIVATE ${FLUIDSYNTH_INCLUDE_DIRS})
        target_link_directories(MidiEditorAI PRIVATE ${FLUIDSYNTH_LIBRARY_DIRS})
        target_link_libraries(MidiEditorAI PRIVATE ${FLUIDSYNTH_LIBRARIES})
    else()
        message(FATAL_ERROR "FluidSynth was detected but no link target/library was resolved")
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
    src/converter/Sid
    src/converter/Score
)
message(STATUS "Added project include directories")

# Collaboration sources (gated by option). When enabled, both the headless
# service code (src/collab) and the GUI surface (src/gui/collab) are added.
# When disabled, the MIDIEDITOR_COLLAB_ENABLED preprocessor define is absent
# so SettingsDialog.cpp and any future call sites compile without referencing
# any Collab* symbols.
if(MIDIEDITOR_ENABLE_COLLAB)
    target_include_directories(MidiEditorAI PRIVATE src/collab)
    target_compile_definitions(MidiEditorAI PRIVATE MIDIEDITOR_COLLAB_ENABLED=1)
    message(STATUS "Collaboration features: ENABLED (Phase 9.1)")
else()
    message(STATUS "Collaboration features: DISABLED")
endif()

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

# ----------------------------------------------------------------------
# WebRTC support via libdatachannel (Phase 9.6, Plan §11.10)
#
# Pulls libdatachannel via FetchContent and links it as a static lib.
# We disable everything we don't need (audio/video media, websocket,
# examples, tests) to keep the binary footprint small — typical add
# is ~6-8 MB statically linked. Source: github.com/paullouisageneau,
# MPL-2.0; not affiliated with Google. We separately use Google's
# free STUN server pool as a runtime config — that's just URLs in
# Settings, no Google code in the binary.
#
# Crypto: we use MbedTLS for DTLS rather than OpenSSL because it's
# much smaller (~1-2 MB vs ~5-8 MB), builds cleanly under CMake on
# MSVC without external installation, and is a vetted open-source
# implementation. libdatachannel calls find_package(MbedTLS 3
# REQUIRED); OVERRIDE_FIND_PACKAGE (CMake 3.24+) routes that through
# our FetchContent-built copy so no system install is needed.
# ----------------------------------------------------------------------
option(MIDIEDITOR_ENABLE_WEBRTC "Enable WebRTC live-mode transport via libdatachannel" ON)
if(MIDIEDITOR_ENABLE_WEBRTC)
    include(FetchContent)

    # OpenSSL pre-build + install via janbar/openssl-cmake (a CMake
    # wrapper around the upstream OpenSSL source tree). libdatachannel
    # calls find_package(OpenSSL REQUIRED), which respects
    # CMAKE_PREFIX_PATH — so we install OpenSSL to a build-local
    # prefix and prepend it before libdatachannel's find_package fires.
    #
    # Why OpenSSL instead of MbedTLS: MbedTLS 3.6.2's PSA-crypto
    # headers have known C++ inclusion bugs on MSVC (struct return
    # types break under C linkage), which would block libdatachannel's
    # build. OpenSSL is the more battle-tested path for MSVC C++
    # consumers — adds ~5 MB to the final binary (vs ~1-2 MB for
    # MbedTLS), worth the reliability.
    FetchContent_Declare(
        openssl_src
        GIT_REPOSITORY https://github.com/janbar/openssl-cmake.git
        GIT_TAG        1.1.1w-20231130
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(openssl_src)
    if(NOT openssl_src_POPULATED)
        message(STATUS "Fetching OpenSSL source (CMake wrapper)...")
        FetchContent_Populate(openssl_src)
    endif()
    set(OPENSSL_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/openssl-install")
    if(NOT EXISTS "${OPENSSL_INSTALL_DIR}/include/openssl/ssl.h")
        message(STATUS "Building OpenSSL (one-time, takes a few minutes)...")
        execute_process(
            COMMAND ${CMAKE_COMMAND}
                -S "${openssl_src_SOURCE_DIR}"
                -B "${openssl_src_BINARY_DIR}"
                -G "${CMAKE_GENERATOR}"
                # janbar/openssl-cmake 1.1.1w-20231130 declares
                # cmake_minimum_required(VERSION 3.0) which CMake 4.x
                # refuses outright (compat with < 3.5 was dropped). Pin
                # the policy floor to 3.5 just for the OpenSSL sub-build
                # so its old policy semantics (POLICY CMP0xxx defaults)
                # stay intact without us editing the upstream tree.
                -DCMAKE_POLICY_VERSION_MINIMUM=3.5
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=${OPENSSL_INSTALL_DIR}
                -DBUILD_SHARED_LIBS=OFF
                -DWITH_APPS=OFF
            RESULT_VARIABLE _ossl_cfg
            OUTPUT_QUIET
        )
        if(NOT _ossl_cfg EQUAL 0)
            message(FATAL_ERROR "OpenSSL configure failed (rc=${_ossl_cfg})")
        endif()
        execute_process(
            COMMAND ${CMAKE_COMMAND}
                --build "${openssl_src_BINARY_DIR}"
                --target install
                --config Release
            RESULT_VARIABLE _ossl_build
            OUTPUT_QUIET
        )
        if(NOT _ossl_build EQUAL 0)
            message(FATAL_ERROR "OpenSSL build/install failed (rc=${_ossl_build})")
        endif()
        message(STATUS "OpenSSL installed to ${OPENSSL_INSTALL_DIR}")
    endif()
    set(OPENSSL_ROOT_DIR "${OPENSSL_INSTALL_DIR}" CACHE PATH "" FORCE)
    set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
    # janbar/openssl-cmake names libs with platform/version suffixes
    # (e.g. libcrypto-1_1-x64.lib). FindOpenSSL.cmake's default name
    # search doesn't pick those up, AND it overrides any cache pinning
    # we attempt before calling it. Workaround: copy the produced libs
    # to the standard names FindOpenSSL expects (libcrypto.lib,
    # libssl.lib). This is a one-time per-build-dir setup; subsequent
    # configures hit the if(NOT EXISTS) guard.
    if(WIN32 AND NOT EXISTS "${OPENSSL_INSTALL_DIR}/lib/libcrypto.lib")
        file(GLOB _ossl_crypto "${OPENSSL_INSTALL_DIR}/lib/libcrypto*.lib")
        file(GLOB _ossl_ssl    "${OPENSSL_INSTALL_DIR}/lib/libssl*.lib")
        list(GET _ossl_crypto 0 _crypto_src)
        list(GET _ossl_ssl    0 _ssl_src)
        configure_file("${_crypto_src}" "${OPENSSL_INSTALL_DIR}/lib/libcrypto.lib" COPYONLY)
        configure_file("${_ssl_src}"    "${OPENSSL_INSTALL_DIR}/lib/libssl.lib"    COPYONLY)
        message(STATUS "Aliased OpenSSL libs to libcrypto.lib / libssl.lib")
    endif()
    list(APPEND CMAKE_PREFIX_PATH "${OPENSSL_INSTALL_DIR}")

    # libdatachannel — now its find_package(OpenSSL REQUIRED) succeeds
    # via OPENSSL_ROOT_DIR + the prefix path above.
    set(NO_MEDIA      ON CACHE BOOL "" FORCE)
    set(NO_WEBSOCKET  ON CACHE BOOL "" FORCE)
    set(NO_EXAMPLES   ON CACHE BOOL "" FORCE)
    set(NO_TESTS      ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG        v0.21.2
        GIT_SHALLOW    TRUE
    )
    # libdatachannel's transitive deps (deps/plog/CMakeLists.txt:1 declares
    # cmake_minimum_required(VERSION 3.0)) trip CMake 4.x's hard cutoff at
    # 3.5. Pin the policy floor as a non-cached directory-scope variable
    # right before FetchContent_MakeAvailable so the add_subdirectory()
    # chain inherits it without us having to patch the upstream tree.
    # `unset(... PARENT_SCOPE)` doesn't apply here because we ARE in the
    # top-level scope; instead we manually unset() right after the
    # MakeAvailable to keep any downstream add_subdirectory (like tests/
    # at the bottom of this file) from inheriting the loosened policy
    # floor. Cache state is unaffected throughout (BUG-COLLAB-039).
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(libdatachannel)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
    # Use the static target (datachannel-static) so we don't depend on
    # datachannel.dll at runtime — keeps the deployment a single .exe
    # plus Qt/FluidSynth DLLs we already ship. The shared lib is still
    # built but unused; we just don't ship it.
    target_link_libraries(MidiEditorAI PRIVATE LibDataChannel::LibDataChannelStatic)
    target_compile_definitions(MidiEditorAI PRIVATE MIDIEDITOR_WEBRTC_ENABLED RTC_STATIC)
    message(STATUS "libdatachannel + OpenSSL WebRTC enabled (Phase 9.6, static link)")
else()
    message(STATUS "WebRTC disabled — set MIDIEDITOR_ENABLE_WEBRTC=ON to enable")
endif()

# Source files - be more specific to avoid test/doc/example files
file(GLOB SOURCES_MAIN "src/*.cpp")
file(GLOB SOURCES_MIDIEVENT "src/MidiEvent/*.cpp")
file(GLOB SOURCES_GUI "src/gui/*.cpp")
file(GLOB SOURCES_PROTOCOL "src/protocol/*.cpp")
file(GLOB SOURCES_TOOL "src/tool/*.cpp")
file(GLOB SOURCES_AI "src/ai/*.cpp")
if(MIDIEDITOR_ENABLE_COLLAB)
    # CONFIGURE_DEPENDS: re-glob at build time when new files appear in
    # src/collab/ or src/gui/collab/ so multi-config generators (Visual
    # Studio) don't need a manual `cmake configure` after each new file.
    # Supported since CMake 3.12 (Ninja/Make) and 3.20 (VS) — we already
    # require 3.20 at the top of this file.
    file(GLOB SOURCES_COLLAB CONFIGURE_DEPENDS "src/collab/*.cpp")
    file(GLOB SOURCES_GUI_COLLAB CONFIGURE_DEPENDS "src/gui/collab/*.cpp")
endif()
file(GLOB SOURCES_CONVERTER_BASE "src/converter/*.cpp")
file(GLOB SOURCES_CONVERTER "src/converter/GuitarPro/*.cpp")
file(GLOB SOURCES_CONVERTER_MML "src/converter/MML/*.cpp")
file(GLOB SOURCES_CONVERTER_MUSICXML "src/converter/MusicXml/*.cpp")
file(GLOB SOURCES_CONVERTER_SID "src/converter/Sid/*.cpp")
file(GLOB SOURCES_CONVERTER_SCORE "src/converter/Score/*.cpp")

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
    ${SOURCES_COLLAB}
    ${SOURCES_GUI_COLLAB}
    ${SOURCES_CONVERTER_BASE}
    ${SOURCES_CONVERTER}
    ${SOURCES_CONVERTER_MML}
    ${SOURCES_CONVERTER_MUSICXML}
    ${SOURCES_CONVERTER_SID}
    ${SOURCES_CONVERTER_SCORE}
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
if(MIDIEDITOR_ENABLE_COLLAB)
    file(GLOB HEADERS_COLLAB CONFIGURE_DEPENDS "src/collab/*.h")
    file(GLOB HEADERS_GUI_COLLAB CONFIGURE_DEPENDS "src/gui/collab/*.h")
endif()
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
    ${HEADERS_COLLAB}
    ${HEADERS_GUI_COLLAB}
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

# Deployment / packaging
# The application is deployed by the windeployqt POST_BUILD step above and packaged
# into a portable ZIP by CI (.github/workflows/ci.yml). The legacy Qt Installer
# Framework "installer" target and the packaging/ component layout (plus the old
# qmake/xmake build files) were removed in 1.9.2 - the release ships as a ZIP.
