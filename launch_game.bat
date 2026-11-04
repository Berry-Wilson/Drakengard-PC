@echo off
setlocal
cd /d "%~dp0\iso_extracted"

rem Records pad input by default so a session can be replayed later with
rem PS2X_INPUT_REPLAY=<file> (see ps2xRuntime/src/lib/ps2_pad.cpp).
rem Run "launch_game.bat norecord" to play without recording.
if /i "%~1"=="norecord" goto launch

if not exist "%~dp0input_recordings" mkdir "%~dp0input_recordings"
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set STAMP=%%t
set "PS2X_INPUT_RECORD=%~dp0input_recordings\input_%STAMP%.txt"

:launch
echo Starting Drakengard PC Port (Vulkan parallel-gs backend)...
if defined PS2X_INPUT_RECORD (echo Recording pad input to %PS2X_INPUT_RECORD%) else (echo Input recording off)
echo Console output will be mirrored to launch_console.log
echo.

powershell -NoProfile -ExecutionPolicy Bypass -Command "& '%~dp0external\PS2Recomp\out\build\ps2xRuntime\Release\ps2EntryRunner.exe' '%~dp0iso_extracted\SLUS_207.32' *>&1 | Tee-Object -FilePath '%~dp0launch_console.log'"

pause
