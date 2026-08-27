@echo off
REM Build and run the tests.
setlocal
cd /d "%~dp0.."
cmake --preset default || exit /b 1
cmake --build build\default --config Debug || exit /b 1
ctest --preset debug
endlocal
