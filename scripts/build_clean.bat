@echo off
REM MidiEditor_AI Clean Build Script
REM Builds a fresh "first-install" version for testing default behaviour.
REM
REM What this does (in order):
REM   1. Wipes the build/ folder so CMake reconfigures from scratch.
REM   2. Deletes the persisted QSettings registry key for MidiEditor so the
REM      next launch behaves like an absolutely fresh installation
REM      (default theme, default toolbar, no FFXIV presets, no recent files,
REM      no AI keys, no window geometry, etc.).
REM   3. Runs the normal build.bat configure + build + windeployqt flow.
REM
REM Use this when you need to verify "first launch" / "settings reset" UX
REM such as the default toolbar load, default theme, FFXIV equalizer
REM defaults, etc. After running, just launch build\bin\MidiEditorAI.exe.
REM
REM Lives under scripts/ — pushd into the project root so the build/ folder
REM and any other relative paths resolve from the repo root, not scripts/.

setlocal
pushd "%~dp0.."

echo === MidiEditor_AI CLEAN build ===
echo.

REM --- 1. Wipe the build directory -------------------------------------------
if exist "build" (
    echo Removing existing build\ folder ...
    rmdir /S /Q "build"
    if exist "build" (
        echo ERROR: could not remove build\ folder. Close any open exe / explorer windows and retry.
        popd
        pause
        exit /b 1
    )
) else (
    echo No existing build\ folder, skipping wipe.
)

REM --- 2. Wipe persisted QSettings -------------------------------------------
REM The app uses QSettings("MidiEditor","NONE") which on Windows lands at
REM HKEY_CURRENT_USER\Software\MidiEditor\NONE. Some legacy / default-ctor
REM writes may also exist under HKCU\Software\MidiEditor without a sub-key.
echo.
echo Clearing persisted user settings (HKCU\Software\MidiEditor) ...
reg delete "HKCU\Software\MidiEditor" /f >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo   - HKCU\Software\MidiEditor removed.
) else (
    echo   - HKCU\Software\MidiEditor not present, nothing to clear.
)

REM --- 3. Delegate to the normal build script --------------------------------
echo.
echo === Delegating to build.bat ===
popd
call "%~dp0build.bat"
exit /b %ERRORLEVEL%
