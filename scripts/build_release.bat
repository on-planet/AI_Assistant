@echo off
setlocal

REM AI_assistant Release build script (Windows / VS 2022)
set BUILD_DIR=cmake-build-release

echo [1/3] Configure Release...
cmake -S . -B %BUILD_DIR% -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
  echo Configure failed.
  exit /b 1
)

echo [2/3] Build Release...
cmake --build %BUILD_DIR% --config Release --target AI_assistant
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

echo [3/3] Done.
echo Output: %BUILD_DIR%\Release\AI_assistant.exe
exit /b 0
