@echo off
REM Generate the Visual Studio solution and open it.
REM
REM The solution is generated rather than committed: the CMake files are the build
REM description, and a checked-in .sln would be a second one to keep in step.
setlocal
cd /d "%~dp0.."

cmake --preset default || exit /b 1

for %%f in (build\default\*.sln) do (
  echo Opening %%f
  start "" "%%f"
  goto :done
)
echo No solution was generated in build\default.
:done
endlocal
