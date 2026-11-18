@echo off
rem Removes the installed Pedal Faster VST3, standalone app and Start Menu shortcut (asks for admin).
rem User presets in %APPDATA%\Pedal Faster are kept.
setlocal
set "PS1=%TEMP%\PedalFasterUninstall.ps1"
> "%PS1%" echo $vst3 = Join-Path $env:CommonProgramFiles 'VST3\Pedal Faster.vst3'
>>"%PS1%" echo if (Test-Path $vst3) { Remove-Item -Recurse -Force $vst3 }
>>"%PS1%" echo $app = Join-Path $env:ProgramFiles 'Pedal Faster'
>>"%PS1%" echo if (Test-Path $app) { Remove-Item -Recurse -Force $app }
>>"%PS1%" echo $lnk = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'Pedal Faster.lnk'
>>"%PS1%" echo if (Test-Path $lnk) { Remove-Item -Force $lnk }
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { Start-Process powershell -Verb RunAs -Wait -WindowStyle Hidden -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','\"%PS1%\"' } catch { exit 1 }"
del "%PS1%" >nul 2>&1
if exist "%CommonProgramFiles%\VST3\Pedal Faster.vst3" (echo Uninstall failed or was cancelled.) else (echo Pedal Faster removed. Your presets in %APPDATA%\Pedal Faster were kept.)
pause
