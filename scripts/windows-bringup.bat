@echo off
REM First run of RendeerAlpha on a Windows machine. The work is in windows-bringup.ps1;
REM this exists so it can be double-clicked or run from cmd without touching the
REM execution policy.
REM
REM   scripts\windows-bringup.bat              check, build, test, stage
REM   scripts\windows-bringup.bat --release    stage a Release engine instead of Debug
REM   scripts\windows-bringup.bat --jobs 4     limit the parallel compile
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-bringup.ps1" %*
exit /b %errorlevel%
