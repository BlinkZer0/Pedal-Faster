<#
.SYNOPSIS
    Checks the web preview's JavaScript DSP against the C++ engine: renders the factory presets with the
    C++ reference DI in headless Chrome/Edge and compares levels + spectra with tests/regression/baseline.json.
.EXAMPLE
    ./scripts/test-web.ps1            # all 250 presets
    ./scripts/test-web.ps1 -Count 20  # quick run
    ./scripts/test-web.ps1 -LiveAudio # also run the real AudioWorklet path in a (briefly visible) browser window
#>
param(
    [int] $Count = 0,
    [switch] $LiveAudio,
    [double] $MaxMedianLevelDiffDb = 1.0,
    [double] $MaxLevelDiffDb = 3.0,
    [string] $WorkDir = (Join-Path $env:LOCALAPPDATA 'PedalFaster')
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$tests = Join-Path $WorkDir 'build\PedalFasterTests_artefacts\Release\PedalFasterTests.exe'
$browser = @("$env:ProgramFiles\Google\Chrome\Application\chrome.exe", "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $browser) { throw 'Chrome or Edge is required' }

$tmp = Join-Path $env:TEMP 'PedalFasterWebParity'
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory $tmp | Out-Null
& $tests --write-reference-di (Join-Path $tmp 'ref-di.f32') | Out-Null
Copy-Item (Join-Path $repo 'tests\regression\baseline.json') $tmp
Copy-Item (Join-Path $repo 'web\test\parity.js') $tmp
$html = [IO.File]::ReadAllText((Join-Path $repo 'web\index.html'))
$html = $html.Replace('</body>', '<script src="parity.js"></script></body>')
[IO.File]::WriteAllText((Join-Path $tmp 'parity.html'), $html)

$url = 'file:///' + ((Join-Path $tmp 'parity.html') -replace '\\', '/') + $(if ($Count) { "?n=$Count" } else { '' })
$profile = Join-Path $tmp 'profile'
$ErrorActionPreference = 'Continue'   # the browser logs harmless noise to stderr
$dom = & $browser --headless=new --disable-gpu --no-first-run --user-data-dir="$profile" --allow-file-access-from-files `
    --virtual-time-budget=900000 --timeout=900000 --dump-dom $url 2>$null | Out-String
$ErrorActionPreference = 'Stop'
$m = [regex]::Match($dom, '<pre id="parity-result">([\s\S]*?)</pre>')
if (-not $m.Success) { throw "No parity result (browser output length $($dom.Length))" }
$text = [Net.WebUtility]::HtmlDecode($m.Groups[1].Value)
Write-Host $text
if ($text.StartsWith('ERROR')) { exit 1 }
$r = $text | ConvertFrom-Json
$ok = $r.nonFinite -eq 0 -and $r.levelDiffMedian -le $MaxMedianLevelDiffDb -and $r.levelDiffMax -le $MaxLevelDiffDb
if ($ok) { Write-Host "Web DSP parity OK ($($r.presets) presets match the C++ engine)" -ForegroundColor Green }
else {
    Write-Host 'Web DSP parity FAILED' -ForegroundColor Red; exit 1
}

# ---- optional: real Web Audio path (AudioWorklet + OfflineAudioContext) in a visible browser window.
# Headless Chrome/Edge cannot run AudioWorklets, so this opens a normal window for ~10 s (no sound is played).
if ($LiveAudio) {
    $udd = Join-Path $tmp 'live-profile'
    $page = 'file:///' + ((Join-Path $repo 'web\index.html') -replace '\\', '/') + '?selftest&browse'
    $p = Start-Process $browser -ArgumentList @('--no-first-run', '--no-default-browser-check', "--user-data-dir=`"$udd`"", '--remote-debugging-port=9333', '--window-size=900,600', "`"$page`"") -PassThru
    $title = ''
    for ($i = 0; $i -lt 30 -and -not $title.StartsWith('SELFTEST'); $i++) {
        Start-Sleep -Milliseconds 500
        try { $title = ((Invoke-RestMethod 'http://localhost:9333/json') | Where-Object { $_.title -like 'SELFTEST*' } | Select-Object -First 1).title } catch { }
        if (-not $title) { $title = '' }
    }
    Get-CimInstance Win32_Process -Filter "Name='$([IO.Path]::GetFileName($browser))'" | Where-Object { $_.CommandLine -like "*live-profile*" } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    $json = [Net.WebUtility]::HtmlDecode($title.Substring([Math]::Min(9, $title.Length)))
    Write-Host "Live Web Audio self-test: $json"
    $live = try { $json | ConvertFrom-Json } catch { $null }
    if (-not $live -or -not $live.workletLoaded -or ($live.results | Where-Object { -not $_.finite -or $_.rmsDb -lt -30 -or $_.rmsDb -gt -10 })) {
        Write-Host 'Live Web Audio self-test FAILED' -ForegroundColor Red; exit 1
    }
    Write-Host 'Live Web Audio self-test OK' -ForegroundColor Green
}
exit 0
