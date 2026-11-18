<#
.SYNOPSIS
    Configures and builds Pedal Faster (VST3, Standalone, tests) with CMake + MSVC.
.EXAMPLE
    ./scripts/build-windows.ps1 -Config Release
#>
param(
    [ValidateSet('Release', 'Debug')] [string] $Config = 'Release',
    [string] $WorkDir = (Join-Path $env:LOCALAPPDATA 'PedalFaster'),
    [string] $JuceTag = '9.0.5'
)
$ErrorActionPreference = 'Stop'
$repo  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $WorkDir 'build'
$juce  = Join-Path $WorkDir 'deps\juce-src'

function Find-CMake {
    $c = Get-Command cmake -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -property installationPath
        $p = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path $p) { return $p }
    }
    throw 'CMake not found. Install Visual Studio 2022+ with the "Desktop development with C++" workload.'
}

$cmake = Find-CMake
if (-not (Test-Path (Join-Path $juce 'CMakeLists.txt'))) {
    Write-Host "[Pedal Faster] fetching JUCE $JuceTag..."
    git clone --depth 1 --branch $JuceTag https://github.com/juce-framework/JUCE.git $juce
    if ($LASTEXITCODE -ne 0) { throw 'git clone of JUCE failed' }
}

Write-Host '[Pedal Faster] configuring...'
& $cmake -S $repo -B $build "-DPF_JUCE_DIR=$juce"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

Write-Host "[Pedal Faster] building $Config..."
& $cmake --build $build --config $Config --target PedalFaster_VST3 PedalFaster_Standalone PedalFasterTests --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

$art = Join-Path $build "PedalFaster_artefacts\$Config"
Write-Host ''
Write-Host "VST3       : $(Join-Path $art 'VST3\Pedal Faster.vst3')"
Write-Host "Standalone : $(Join-Path $art 'Standalone\Pedal Faster.exe')"
Write-Host "Tests      : $(Join-Path $build "PedalFasterTests_artefacts\$Config\PedalFasterTests.exe")"
