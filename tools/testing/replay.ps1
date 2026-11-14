# Runs the game replaying a pad recording, for a fixed time, with its output in log files.
#
#   tools\testing\replay.ps1 -Recording input_recordings\input_20261109_161215.txt -Seconds 132 `
#       -OutDir out\test\sky -DumpEvery 120 -Aspect 16:9
#
# Frame dumps (PNG) go to <OutDir>\frames when -DumpEvery is given. Any other PS2X_* variable set
# by the caller passes through to the game. Replays are pixel-deterministic on the GPU renderer, so
# two runs can be compared frame by frame (tools\testing\frames.py diff); a run that starts on a
# different logo has drifted from boot and should be repeated.
param(
    [Parameter(Mandatory = $true)][string]$Recording,
    [int]$Seconds = 120,
    [string]$OutDir = "out\test\replay",
    [int]$DumpEvery = 0,       # vsyncs between frame dumps; 0 = none
    [double]$DumpAfter = 10,   # seconds before the first dump
    [int]$DumpCount = 0,       # 0 = no limit
    [string]$Aspect = ""       # "16:9" or empty for 4:3
)

$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$exe = "$root\external\PS2Recomp\out\build\ps2xRuntime\Release\ps2EntryRunner.exe"
$game = "$root\iso_extracted"
if (-not (Test-Path $exe)) { throw "Runner not built: $exe" }

$out = New-Item -ItemType Directory -Force $OutDir
$env:PS2X_INPUT_REPLAY = (Resolve-Path $Recording).Path
Remove-Item Env:PS2X_INPUT_RECORD -ErrorAction SilentlyContinue
if ($Aspect) { $env:PS2X_ASPECT = $Aspect } else { Remove-Item Env:PS2X_ASPECT -ErrorAction SilentlyContinue }
if ($DumpEvery -gt 0) {
    $frames = New-Item -ItemType Directory -Force "$($out.FullName)\frames"
    Get-ChildItem $frames -Filter "frame_*.png" | Remove-Item
    $env:PS2X_DUMP_FRAMES_DIR = $frames.FullName
    $env:PS2X_DUMP_FRAMES_EVERY = "$DumpEvery"
    $env:PS2X_DUMP_FRAMES_AFTER = "$DumpAfter"
    if ($DumpCount -gt 0) { $env:PS2X_DUMP_FRAMES_COUNT = "$DumpCount" }
    else { Remove-Item Env:PS2X_DUMP_FRAMES_COUNT -ErrorAction SilentlyContinue }
}

$p = Start-Process -FilePath $exe -ArgumentList "$game\SLUS_207.32" -WorkingDirectory $game `
    -RedirectStandardOutput "$($out.FullName)\stdout.log" -RedirectStandardError "$($out.FullName)\stderr.log" -PassThru
$start = Get-Date
while (((Get-Date) - $start).TotalSeconds -lt $Seconds) {
    if ($p.HasExited) { "The game exited after $([int]((Get-Date) - $start).TotalSeconds) s with code $($p.ExitCode)"; break }
    Start-Sleep -Milliseconds 500
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
"Logs and frames are in $($out.FullName)"
