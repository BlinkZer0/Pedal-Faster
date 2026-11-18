@echo off
rem ==========================================================================
rem  Installs Pedal Faster (asks for administrator rights):
rem    VST3 bundle -> C:\Program Files\Common Files\VST3\Pedal Faster.vst3
rem    Standalone  -> C:\Program Files\Pedal Faster\Pedal Faster.exe  (+ Start Menu shortcut)
rem  Works from the source tree (after build.bat) or from an unzipped release package.
rem  Usage: install.bat [Debug|Release]      Close your DAW first.
rem
rem  The files are first staged in %TEMP% without admin rights, then only a small
rem  PowerShell copy script runs elevated. (Elevated processes often cannot read
rem  OneDrive placeholder files, so nothing elevated touches this folder.)
rem ==========================================================================
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

rem --- a release package keeps the files next to this script; otherwise use the build tree
if exist "%~dp0Pedal Faster.vst3\Contents\x86_64-win\Pedal Faster.vst3" (
  set "SRC_VST3=%~dp0Pedal Faster.vst3"
  set "SRC_EXE=%~dp0Pedal Faster.exe"
) else (
  set "SRC_VST3=%LOCALAPPDATA%\PedalFaster\build\PedalFaster_artefacts\%CONFIG%\VST3\Pedal Faster.vst3"
  set "SRC_EXE=%LOCALAPPDATA%\PedalFaster\build\PedalFaster_artefacts\%CONFIG%\Standalone\Pedal Faster.exe"
)

if not exist "%SRC_VST3%\Contents\x86_64-win\Pedal Faster.vst3" (
  echo Not built yet: %SRC_VST3%
  echo Run build.bat first.
  pause
  exit /b 1
)

rem --- stage everything in %TEMP% (no admin needed)
set "STAGE=%TEMP%\PedalFasterInstall"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%" || goto :fail
xcopy "%SRC_VST3%" "%STAGE%\Pedal Faster.vst3\" /e /i /q /y >nul || goto :fail
if exist "%SRC_EXE%" copy /y "%SRC_EXE%" "%STAGE%\Pedal Faster.exe" >nul

set "PS1=%STAGE%\install.ps1"
> "%PS1%" echo $ErrorActionPreference = 'Stop'
>>"%PS1%" echo $stage = '%STAGE%'
>>"%PS1%" echo $log = Join-Path $stage 'install.log'
>>"%PS1%" echo try {
>>"%PS1%" echo   $vst3Dir = Join-Path $env:CommonProgramFiles 'VST3'
>>"%PS1%" echo   New-Item -ItemType Directory -Force $vst3Dir ^| Out-Null
>>"%PS1%" echo   $dst = Join-Path $vst3Dir 'Pedal Faster.vst3'
>>"%PS1%" echo   if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
>>"%PS1%" echo   Copy-Item -Recurse -Force (Join-Path $stage 'Pedal Faster.vst3') $dst
>>"%PS1%" echo   $exe = Join-Path $stage 'Pedal Faster.exe'
>>"%PS1%" echo   if (Test-Path $exe) {
>>"%PS1%" echo     $app = Join-Path $env:ProgramFiles 'Pedal Faster'
>>"%PS1%" echo     New-Item -ItemType Directory -Force $app ^| Out-Null
>>"%PS1%" echo     Copy-Item -Force $exe (Join-Path $app 'Pedal Faster.exe')
>>"%PS1%" echo     $lnk = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'Pedal Faster.lnk'
>>"%PS1%" echo     $s = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
>>"%PS1%" echo     $s.TargetPath = (Join-Path $app 'Pedal Faster.exe'); $s.WorkingDirectory = $app; $s.Save()
>>"%PS1%" echo   }
>>"%PS1%" echo   'OK' ^| Set-Content $log
>>"%PS1%" echo } catch { ('FAILED: ' + $_.Exception.Message) ^| Set-Content $log }

echo Installing (Windows will ask for administrator permission)...
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { Start-Process powershell -Verb RunAs -Wait -WindowStyle Hidden -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','\"%PS1%\"' } catch { exit 1 }"
if errorlevel 1 (
  echo Install cancelled - administrator permission was not granted.
  goto :fail
)

set "RESULT="
if exist "%STAGE%\install.log" set /p RESULT=<"%STAGE%\install.log"
if not "%RESULT%"=="OK" (
  echo Install failed: %RESULT%
  goto :fail
)
if not exist "%CommonProgramFiles%\VST3\Pedal Faster.vst3\Contents\x86_64-win\Pedal Faster.vst3" goto :fail

rmdir /s /q "%STAGE%" >nul 2>&1
echo.
echo Pedal Faster installed.
echo   VST3       : %CommonProgramFiles%\VST3\Pedal Faster.vst3
echo   Standalone : %ProgramFiles%\Pedal Faster\Pedal Faster.exe  (Start Menu: Pedal Faster)
echo   User presets are stored in %APPDATA%\Pedal Faster\Presets
echo Rescan plugins in your DAW.
pause
exit /b 0

:fail
echo.
echo Install FAILED. Nothing was changed in Program Files.
echo If it keeps failing, copy this folder manually (as administrator):
echo   %SRC_VST3%
echo   to  %CommonProgramFiles%\VST3\
pause
exit /b 1
