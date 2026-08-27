@echo off
REM Build, then run the Sandbox from the directory its assets were copied into.
REM Assets resolve relative to the working directory, so starting it anywhere else
REM fails to find the font and the engine stops with a message about it.
setlocal
cd /d "%~dp0.."

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Debug

call scripts\build.bat %CONFIG% || exit /b 1

pushd build\default\bin\%CONFIG% || exit /b 1
sandbox.exe
popd
endlocal
