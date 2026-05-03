@echo off
REM MidiEditor_AI Build Script
REM Requires: VS 2019 Build Tools + Qt 6.5.3 (MSVC 2019 x64)
REM
REM Lives under scripts/ but operates on the project root above it. We
REM pushd into the parent so every relative path (build/, run_environment/,
REM CMakeLists.txt, ...) keeps working unchanged.

setlocal
pushd "%~dp0.."

echo === Setting up MSVC 2019 x64 environment ===
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

set CMAKE_EXE="C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set QT_DIR=C:\Qt\6.5.3\msvc2019_64
set CMAKE_PREFIX_PATH=%QT_DIR%
set FLUIDSYNTH_DIR=%CD%\fluidsynth\fluidsynth-v2.5.2-win10-x64-cpp11

echo.
echo === CMake Configure ===
%CMAKE_EXE% -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%QT_DIR% -DFLUIDSYNTH_DIR=%FLUIDSYNTH_DIR%
if %ERRORLEVEL% neq 0 (
    echo CMake configuration FAILED!
    popd
    pause
    exit /b 1
)

echo.
echo === CMake Build ===
%CMAKE_EXE% --build build --config Release
if %ERRORLEVEL% neq 0 (
    echo Build FAILED!
    popd
    pause
    exit /b 1
)

echo.
echo === Build SUCCESS ===
echo Executable: build\bin\MidiEditorAI.exe
echo.
echo To run, you need Qt DLLs in PATH. Running windeployqt...
%QT_DIR%\bin\windeployqt.exe build\bin\MidiEditorAI.exe
echo.
echo === Ready to run! ===
echo Run: build\bin\MidiEditorAI.exe
popd
pause
