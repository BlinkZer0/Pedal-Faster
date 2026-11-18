<#
.SYNOPSIS
    Validates the built VST3 with pluginval (strictness 10, editor tests included) and, when available,
    Steinberg's VST3 SDK validator. Downloads pluginval into %LOCALAPPDATA%\PedalFaster\tools on first use.
.EXAMPLE
    ./scripts/validate-windows.ps1
    ./scripts/validate-windows.ps1 -BuildSteinbergValidator   # also fetch + build the VST3 SDK validator
#>
param(
    [ValidateSet('Release', 'Debug')] [string] $Config = 'Release',
    [int] $Strictness = 10,
    [switch] $SkipGui,
    [switch] $BuildSteinbergValidator,
    [string] $WorkDir = (Join-Path $env:LOCALAPPDATA 'PedalFaster')
)
$ErrorActionPreference = 'Stop'
$vst3  = Join-Path $WorkDir "build\PedalFaster_artefacts\$Config\VST3\Pedal Faster.vst3"
$tools = Join-Path $WorkDir 'tools'
if (-not (Test-Path $vst3)) { throw "VST3 not built: $vst3" }

# ---- pluginval
$pvDir = Join-Path $tools 'pluginval'
$pv = Join-Path $pvDir 'pluginval.exe'
if (-not (Test-Path $pv)) {
    New-Item -ItemType Directory -Force $pvDir | Out-Null
    $zip = Join-Path $pvDir 'pluginval.zip'
    Invoke-WebRequest 'https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip' -OutFile $zip
    Expand-Archive $zip -DestinationPath $pvDir -Force
}

# ---- Steinberg validator (optional)
$validator = Get-ChildItem -Path (Join-Path $tools 'vst3sdk-build') -Recurse -Filter validator.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $validator -and $BuildSteinbergValidator) {
    $src = Join-Path $tools 'vst3sdk-src'
    if (-not (Test-Path $src)) { git clone --depth 1 --recurse-submodules --shallow-submodules https://github.com/steinbergmedia/vst3sdk.git $src }
    $cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if (-not $cmake) {
        $vs = & (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe') -latest -products * -property installationPath
        $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    }
    & $cmake -S $src -B (Join-Path $tools 'vst3sdk-build') -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF -DSMTG_RUN_VST_VALIDATOR=OFF
    & $cmake --build (Join-Path $tools 'vst3sdk-build') --config Release --target validator --parallel
    $validator = Get-ChildItem -Path (Join-Path $tools 'vst3sdk-build') -Recurse -Filter validator.exe | Select-Object -First 1
}

$failed = $false
if ($validator) {
    Write-Host "== Steinberg VST3 validator: $($validator.FullName)"
    & $validator.FullName $vst3
    if ($LASTEXITCODE -ne 0) { $failed = $true; Write-Host 'VST3 validator FAILED' -ForegroundColor Red }
} else {
    Write-Host '== Steinberg VST3 validator not available (use -BuildSteinbergValidator to build it)'
}

Write-Host "== pluginval (strictness $Strictness)"
$pvArgs = @('--strictness-level', $Strictness, '--output-dir', (Join-Path $tools 'pluginval-logs'))
if ($SkipGui) { $pvArgs += '--skip-gui-tests' }
if ($validator) { $pvArgs += @('--vst3validator', $validator.FullName) }
$pvArgs += @('--validate', $vst3)
# pluginval is a GUI-subsystem executable: start it explicitly and wait, otherwise PowerShell
# returns immediately and the exit code is meaningless.
$quoted = $pvArgs | ForEach-Object { if ("$_" -match '\s') { '"' + $_ + '"' } else { "$_" } }
$pvLog = Join-Path $tools 'pluginval-console.txt'
$proc = Start-Process -FilePath $pv -ArgumentList ($quoted -join ' ') -NoNewWindow -Wait -PassThru -RedirectStandardOutput $pvLog
Get-Content $pvLog | Where-Object { $_ -match 'Starting tests in|FAILED|!!!|SUCCESS|Strictness' } | ForEach-Object { Write-Host $_ }
if ($proc.ExitCode -ne 0) { $failed = $true; Write-Host "pluginval FAILED (exit $($proc.ExitCode)), see $pvLog" -ForegroundColor Red }

if ($failed) { exit 1 }
Write-Host 'Validation passed.' -ForegroundColor Green
