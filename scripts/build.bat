@echo off
REM Configure and build.
REM
REM   scripts\build.bat            Debug
REM   scripts\build.bat Release
setlocal
cd /d "%~dp0.."

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Debug

where cmake >nul 2>nul
if errorlevel 1 (
  echo cmake is not on PATH. Install it, or use the copy bundled with Visual Studio
  echo under Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
  exit /b 1
)

cmake --preset default || exit /b 1
cmake --build build\default --config %CONFIG% || exit /b 1

echo.
echo Built into build\default\bin\%CONFIG%
endlocal
