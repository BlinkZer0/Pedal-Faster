<#
.SYNOPSIS
    Creates the portable release package dist\PedalFaster-<version>-win64.zip containing the complete
    VST3 bundle, the standalone app, install/uninstall scripts, the factory presets as JSON and the docs.
.EXAMPLE
    ./scripts/package-windows.ps1
#>
param(
    [ValidateSet('Release', 'Debug')] [string] $Config = 'Release',
    [string] $WorkDir = (Join-Path $env:LOCALAPPDATA 'PedalFaster')
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$version = (Select-String -Path (Join-Path $repo 'CMakeLists.txt') -Pattern 'project\(PedalFaster VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$art = Join-Path $WorkDir "build\PedalFaster_artefacts\$Config"
$vst3 = Join-Path $art 'VST3\Pedal Faster.vst3'
$exe = Join-Path $art 'Standalone\Pedal Faster.exe'
$tests = Join-Path $WorkDir "build\PedalFasterTests_artefacts\$Config\PedalFasterTests.exe"
foreach ($p in @($vst3, $exe, $tests)) { if (-not (Test-Path $p)) { throw "Missing build output: $p (run scripts/build-windows.ps1)" } }

$name = "PedalFaster-$version-win64"
$dist = Join-Path $repo 'dist'
$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item -Recurse $vst3 (Join-Path $stage 'Pedal Faster.vst3')
Copy-Item $exe $stage
Copy-Item (Join-Path $repo 'install.bat') $stage
Copy-Item (Join-Path $repo 'uninstall.bat') $stage
Copy-Item (Join-Path $repo 'LICENSES.md') $stage
Copy-Item (Join-Path $repo 'docs\USER_MANUAL.md') $stage
& $tests --export-factory (Join-Path $stage 'Factory Presets (JSON)') | Out-Null

@"
Pedal Faster $version for Windows x64

Install:   double-click install.bat (asks for administrator rights)
             VST3       -> C:\Program Files\Common Files\VST3\Pedal Faster.vst3
             Standalone -> C:\Program Files\Pedal Faster\Pedal Faster.exe
Manual:    copy the "Pedal Faster.vst3" folder into C:\Program Files\Common Files\VST3\
Portable:  "Pedal Faster.exe" runs from any folder (choose your audio interface in Options > Audio Settings).
Uninstall: uninstall.bat (your presets in %APPDATA%\Pedal Faster are kept)

The factory presets are built into the plugin; the JSON copies are for reference / sharing.
No development tools are needed to run this release.
"@ | Set-Content -Encoding utf8 (Join-Path $stage 'README.txt')

$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Write-Host "Package: $zip"
