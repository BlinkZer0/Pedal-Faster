<#
.SYNOPSIS
    Runs the Pedal Faster unit, preset and integration tests (including acoustic regression checks).
.EXAMPLE
    ./scripts/test-windows.ps1                 # all tests
    ./scripts/test-windows.ps1 -Category DSP   # DSP | Presets | Integration
    ./scripts/test-windows.ps1 -UpdateBaseline # re-record tests/regression/baseline.json
#>
param(
    [ValidateSet('Release', 'Debug')] [string] $Config = 'Release',
    [string] $Category = '',
    [switch] $UpdateBaseline,
    [string] $WorkDir = (Join-Path $env:LOCALAPPDATA 'PedalFaster')
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $WorkDir "build\PedalFasterTests_artefacts\$Config\PedalFasterTests.exe"
if (-not (Test-Path $exe)) { throw "Test runner not built: $exe (run scripts/build-windows.ps1)" }

$baseline = Join-Path $repo 'tests\regression\baseline.json'
if ($UpdateBaseline -and (Test-Path $baseline)) { Remove-Item $baseline }

$runArgs = @('--regression-dir', (Join-Path $repo 'tests\regression'))
if ($Category) { $runArgs += @('--category', $Category) }
Push-Location $repo
try {
    & $exe @runArgs
    $code = $LASTEXITCODE
} finally { Pop-Location }
if ($code -ne 0) { Write-Host 'TESTS FAILED' -ForegroundColor Red; exit $code }
Write-Host 'All tests passed.' -ForegroundColor Green
