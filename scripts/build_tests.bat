@echo off
REM Build & run unit tests. Reuses the existing build/ directory and just
REM enables BUILD_TESTING. Safe to run after a normal build.bat.
REM
REM Lives under scripts/ — pushd into the project root so relative paths
REM (build/, CMakeLists.txt, ...) work unchanged.

setlocal
pushd "%~dp0.."

call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul

set CMAKE_EXE="C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set QT_DIR=C:\Qt\6.5.3\msvc2019_64
set CMAKE_PREFIX_PATH=%QT_DIR%
set FLUIDSYNTH_DIR=%CD%\fluidsynth\fluidsynth-v2.5.2-win10-x64-cpp11

echo === Reconfigure with BUILD_TESTING=ON ===
%CMAKE_EXE% -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH=%QT_DIR% -DFLUIDSYNTH_DIR=%FLUIDSYNTH_DIR% ^
    -DBUILD_TESTING=ON
if %ERRORLEVEL% neq 0 ( popd & exit /b 1 )

echo.
echo === Build all test targets ===
%CMAKE_EXE% --build build
if %ERRORLEVEL% neq 0 ( popd & exit /b 1 )

echo.
echo === Run ctest ===
ctest --test-dir build -V
set EC=%ERRORLEVEL%
popd
exit /b %EC%
