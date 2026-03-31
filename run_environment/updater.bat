@echo off
REM ============================================================
REM  MidiEditor AI — Auto-Updater Script
REM  Called by the app after download. Waits for app exit, then
REM  extracts the update ZIP and restarts the application.
REM
REM  Arguments:
REM    %1 = Full path to downloaded ZIP file
REM    %2 = Application directory (where MidiEditorAI.exe lives)
REM    %3 = Executable name (e.g. MidiEditorAI.exe)
REM    %4 = (Optional) MIDI file path to reopen after restart
REM ============================================================

setlocal enabledelayedexpansion

set ZIP_PATH=%~1
set APP_DIR=%~2
set EXE_NAME=%~3
set MIDI_PATH=%~4

REM --- Validate arguments ---
if "%ZIP_PATH%"=="" goto :usage
if "%APP_DIR%"=="" goto :usage
if "%EXE_NAME%"=="" goto :usage

if not exist "%ZIP_PATH%" (
    echo ERROR: Update ZIP not found: %ZIP_PATH%
    pause
    exit /b 1
)

echo.
echo ============================================
echo   MidiEditor AI — Auto-Updater
echo ============================================
echo.

REM --- Wait for the application to exit ---
echo Waiting for %EXE_NAME% to close...
:wait_loop
tasklist /FI "IMAGENAME eq %EXE_NAME%" 2>NUL | find /I "%EXE_NAME%" >NUL
if %ERRORLEVEL%==0 (
    timeout /t 1 /nobreak >NUL
    goto :wait_loop
)
echo Application closed.

REM --- Create backup of current exe ---
echo Creating backup...
if exist "%APP_DIR%\%EXE_NAME%" (
    copy /y "%APP_DIR%\%EXE_NAME%" "%APP_DIR%\%EXE_NAME%.bak" >NUL 2>&1
)

REM --- Extract update ZIP ---
echo Extracting update...
powershell -Command "Expand-Archive -Path '%ZIP_PATH%' -DestinationPath '%APP_DIR%' -Force" 2>NUL
if %ERRORLEVEL% neq 0 (
    echo ERROR: Extraction failed!
    echo Restoring backup...
    if exist "%APP_DIR%\%EXE_NAME%.bak" (
        copy /y "%APP_DIR%\%EXE_NAME%.bak" "%APP_DIR%\%EXE_NAME%" >NUL 2>&1
    )
    pause
    exit /b 1
)

REM --- Handle nested subfolder from ZIP ---
REM release.bat creates MidiEditorAI-v{ver}-win64/ subfolder inside the ZIP
for /d %%D in ("%APP_DIR%\MidiEditorAI-*") do (
    echo Moving files from subfolder: %%~nxD
    xcopy /s /e /y "%%D\*" "%APP_DIR%\" >NUL 2>&1
    rmdir /s /q "%%D" >NUL 2>&1
)

REM --- Cleanup ---
echo Cleaning up...
if exist "%ZIP_PATH%" del "%ZIP_PATH%" >NUL 2>&1
if exist "%APP_DIR%\%EXE_NAME%.bak" del "%APP_DIR%\%EXE_NAME%.bak" >NUL 2>&1

REM --- Restart application ---
echo Restarting MidiEditor AI...
if "%MIDI_PATH%"=="" (
    start "" "%APP_DIR%\%EXE_NAME%"
) else (
    start "" "%APP_DIR%\%EXE_NAME%" --open "%MIDI_PATH%"
)

echo Update complete!
exit /b 0

:usage
echo Usage: updater.bat ^<zipPath^> ^<appDir^> ^<exeName^> [midiFilePath]
pause
exit /b 1
