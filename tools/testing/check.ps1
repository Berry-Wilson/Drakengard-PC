# The project's regression check: options table, build, then a replay compared with a baseline.
#
#   tools\testing\check.ps1 -SaveBaseline     run on a known-good build: keeps its frames as the baseline
#   tools\testing\check.ps1                   after a change: builds, replays, reports frames that differ
#
# Baselines are frame dumps of the game, so they stay on this PC (out\test\baseline), never in git.
# A replay can still leave its path from boot now and then (see README.md here); if nearly every
# frame differs, run the check again before believing it.
param(
    [switch]$SaveBaseline,
    [string]$Recording = "input_recordings\input_20261109_161215.txt",
    [int]$Seconds = 132,
    [string]$Aspect = "16:9",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $root
$name = [IO.Path]::GetFileNameWithoutExtension($Recording) + $(if ($Aspect) { "_" + $Aspect.Replace(":", "x") } else { "" })
$baseline = "out\test\baseline\$name"
$run = "out\test\check\$name"

python tools\check_settings.py
if ($LASTEXITCODE -ne 0) { throw "The options table and the code disagree." }

if (-not $NoBuild) {
    cmake --build external\PS2Recomp\out\build --config Release --target ps2EntryRunner --parallel 8 | Select-String " error |ps2EntryRunner.exe" | ForEach-Object Line
    if ($LASTEXITCODE -ne 0) { throw "The build failed." }
}

# Fixed renderer settings, so a baseline does not depend on the caller's environment.
$env:PS2X_PCSX2_UPSCALE = "2"; $env:PS2X_PROGRESSIVE = "1"
$target = if ($SaveBaseline) { $baseline } else { $run }
& "$PSScriptRoot\replay.ps1" -Recording $Recording -Seconds $Seconds -OutDir $target -DumpEvery 120 -Aspect $Aspect | Out-Null
Remove-Item Env:PS2X_PCSX2_UPSCALE, Env:PS2X_PROGRESSIVE, Env:PS2X_ASPECT -ErrorAction SilentlyContinue

if ($SaveBaseline) { "Baseline saved in $baseline"; exit 0 }
if (-not (Test-Path "$baseline\frames")) { throw "No baseline yet: run with -SaveBaseline on a known-good build." }
$report = python tools\testing\frames.py diff "$run\frames" "$baseline\frames"
$report
if ($report | Select-String "differs|sizes differ") { "CHECK FAILED: frames differ from the baseline"; exit 1 }
"CHECK PASSED"
