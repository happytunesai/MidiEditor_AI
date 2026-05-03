@echo off
REM MidiEditor_AI - Reset Settings Only
REM
REM Wipes the persisted QSettings store so the next launch behaves like a
REM fresh install (default theme, default toolbar, no recent files, no AI
REM keys, no FFXIV presets, no window geometry, ...) WITHOUT rebuilding.
REM
REM Use this when you want to test "first launch" UX against an existing
REM build\bin\MidiEditorAI.exe.
REM
REM The app uses QSettings("MidiEditor","NONE") which on Windows lands at
REM HKEY_CURRENT_USER\Software\MidiEditor\NONE. We delete the whole
REM HKCU\Software\MidiEditor key to also catch any legacy default-ctor
REM writes that may have landed one level up.
REM
REM This script is path-agnostic — it touches only the registry, no relative
REM filesystem paths, so it works identically from any cwd.

setlocal

echo === MidiEditor_AI: Reset persisted settings ===
echo.
echo This will delete:
echo   HKCU\Software\MidiEditor   (and everything underneath, incl. \NONE)
echo.
echo Make sure MidiEditorAI.exe is CLOSED before continuing,
echo otherwise Qt will rewrite the keys on shutdown.
echo.
choice /C YN /N /M "Proceed? [Y/N] "
if errorlevel 2 (
    echo Aborted.
    exit /b 1
)

echo.
reg delete "HKCU\Software\MidiEditor" /f >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo   - HKCU\Software\MidiEditor removed.
) else (
    echo   - HKCU\Software\MidiEditor not present, nothing to clear.
)

echo.
echo === Done. Next launch of MidiEditorAI.exe will behave as fresh install. ===
endlocal
