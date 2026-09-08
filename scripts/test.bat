@echo off
REM Build and run the tests.
REM
REM Three of them: the engine's own cases, which need no device, and the two binding
REM tests, which start a real engine -- so a window appears for a few seconds and
REM closes itself. Those two skip, with a reason, when Python, node or koffi is not
REM installed; see bindings/tests/README.md.
setlocal
cd /d "%~dp0.."
cmake --preset default || exit /b 1
cmake --build build\default --config Debug || exit /b 1
ctest --preset debug
endlocal
