@echo off
rem ==========================================================================
rem  Builds Pedal Faster: VST3 + Standalone + test runner.
rem  Usage:  build.bat [Debug|Release]
rem  Build output and the JUCE checkout go to %LOCALAPPDATA%\PedalFaster so
rem  OneDrive doesn't try to sync ~1 GB of intermediate files.
rem ==========================================================================
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "WORK=%LOCALAPPDATA%\PedalFaster"
set "BUILD=%WORK%\build"
set "JUCE=%WORK%\deps\juce-src"

rem --- find CMake (PATH first, then the copy bundled with Visual Studio) ---
set "CMAKE=cmake"
where cmake >nul 2>nul
if errorlevel 1 (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VSDIR=%%i"
)
if defined VSDIR set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

rem --- git is needed to fetch JUCE ---
where git >nul 2>nul
if errorlevel 1 if exist "%ProgramFiles%\Git\cmd\git.exe" set "PATH=%ProgramFiles%\Git\cmd;%PATH%"

if not exist "%JUCE%\CMakeLists.txt" (
  echo [Pedal Faster] fetching JUCE 9.0.5...
  git clone --depth 1 --branch 9.0.5 https://github.com/juce-framework/JUCE.git "%JUCE%" || goto :fail
)

echo [Pedal Faster] configuring...
"%CMAKE%" -S "%~dp0." -B "%BUILD%" -DPF_JUCE_DIR="%JUCE%" || goto :fail

echo [Pedal Faster] building %CONFIG%...
"%CMAKE%" --build "%BUILD%" --config %CONFIG% --target PedalFaster_VST3 PedalFaster_Standalone PedalFasterTests --parallel || goto :fail

echo.
echo [Pedal Faster] Done.
echo   VST3       : %BUILD%\PedalFaster_artefacts\%CONFIG%\VST3\Pedal Faster.vst3
echo   Standalone : %BUILD%\PedalFaster_artefacts\%CONFIG%\Standalone\Pedal Faster.exe
echo   Tests      : %BUILD%\PedalFasterTests_artefacts\%CONFIG%\PedalFasterTests.exe
echo   Run install.bat to copy the VST3 into C:\Program Files\Common Files\VST3
exit /b 0

:fail
echo [Pedal Faster] BUILD FAILED
exit /b 1
