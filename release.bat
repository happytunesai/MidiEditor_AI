@echo off
REM ============================================================
REM  MidiEditor AI — Release Build & Package Script
REM  Builds Release binary, deploys Qt, copies assets, creates zip
REM  Requires: VS 2019 Build Tools + Qt 6.5.3 (MSVC 2019 x64)
REM ============================================================

setlocal enabledelayedexpansion

REM --- Configuration ---
set QT_DIR=C:\Qt\6.5.3\msvc2019_64
set CMAKE_EXE="C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

REM --- Read version from CMakeLists.txt ---
for /f "tokens=2 delims=()" %%v in ('findstr /C:"MIDIEDITOR_RELEASE_VERSION_STRING" CMakeLists.txt ^| findstr /C:"set"') do (
    for /f "tokens=2 delims= " %%a in ("%%v") do set RAW=%%a
)
set VERSION=%RAW:"=%
if "%VERSION%"=="" set VERSION=1.0.0
set RELEASE_DIR=MidiEditorAI-v%VERSION%-win64
set RELEASE_ZIP=%RELEASE_DIR%.zip

echo.
echo ============================================
echo   MidiEditor AI — Release Build v%VERSION%
echo ============================================
echo.

REM --- Setup MSVC environment ---
echo [1/6] Setting up MSVC 2019 x64 environment...
call %VCVARS% >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to initialize MSVC environment!
    pause
    exit /b 1
)

REM --- CMake Configure ---
echo [2/6] CMake Configure...
%CMAKE_EXE% -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%QT_DIR%
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed!
    pause
    exit /b 1
)

REM --- CMake Build ---
echo [3/6] Building Release...
%CMAKE_EXE% --build build --config Release
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)

REM --- Deploy Qt ---
echo [4/6] Deploying Qt libraries...
%QT_DIR%\bin\windeployqt.exe build\bin\MidiEditorAI.exe --release --compiler-runtime --no-translations --no-system-d3d-compiler --no-opengl-sw
if %ERRORLEVEL% neq 0 (
    echo WARNING: windeployqt had issues, continuing...
)

REM --- Prepare release directory ---
echo [5/6] Preparing release directory...
if exist "%RELEASE_DIR%" rmdir /s /q "%RELEASE_DIR%"
mkdir "%RELEASE_DIR%"

REM Copy build output
xcopy /s /e /y "build\bin\*" "%RELEASE_DIR%\" >nul

REM Copy runtime assets
if exist "run_environment\metronome" xcopy /s /e /y "run_environment\metronome" "%RELEASE_DIR%\metronome\" >nul
if exist "run_environment\graphics" xcopy /s /e /y "run_environment\graphics" "%RELEASE_DIR%\graphics\" >nul
if exist "run_environment\midieditor.ico" copy /y "run_environment\midieditor.ico" "%RELEASE_DIR%\" >nul
if exist "run_environment\mozart_turkish_march.mid" copy /y "run_environment\mozart_turkish_march.mid" "%RELEASE_DIR%\" >nul
if exist "run_environment\updater.bat" copy /y "run_environment\updater.bat" "%RELEASE_DIR%\" >nul

REM --- Create zip ---
echo [6/6] Creating %RELEASE_ZIP%...
if exist "%RELEASE_ZIP%" del "%RELEASE_ZIP%"
powershell -Command "Compress-Archive -Path '%RELEASE_DIR%\*' -DestinationPath '%RELEASE_ZIP%'"
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to create zip!
    pause
    exit /b 1
)

echo.
echo ============================================
echo   BUILD COMPLETE!
echo   Zip: %RELEASE_ZIP%
echo   Dir: %RELEASE_DIR%\MidiEditorAI.exe
echo ============================================
echo.
pause
