$frameDir = "$PSScriptRoot\scratch_frames"
if (Test-Path $frameDir) {
    Remove-Item -Path "$frameDir\*.bmp" -Force -ErrorAction SilentlyContinue
} else {
    New-Item -ItemType Directory -Force -Path $frameDir | Out-Null
}
$env:PS2X_DUMP_FRAMES_DIR = $frameDir
$env:PS2X_DUMP_FRAMES_EVERY = "20"
$env:PS2X_AUTOPAD = "4:0.2:4000;6:0.2:0008;10:0.2:0008;15:0.2:4000"
$env:PS2X_MAX_TICKS = "800"

$exe = "$PSScriptRoot\external\PS2Recomp\out\build\ps2xRuntime\Release\ps2EntryRunner.exe"
$outLog = "$PSScriptRoot\boot_stdout.log"
$errLog = "$PSScriptRoot\boot_stderr.log"

$p = Start-Process -FilePath $exe -ArgumentList "$PSScriptRoot\iso_extracted\SLUS_207.32" -WorkingDirectory "$PSScriptRoot\iso_extracted" -RedirectStandardOutput $outLog -RedirectStandardError $errLog -PassThru

Write-Host "Process started with PID $($p.Id)"
$exited = $p.WaitForExit(60000)
if (-not $exited) {
    Stop-Process -Id $p.Id -Force
    Write-Host "Killed process after timeout."
} else {
    Write-Host "Process exited cleanly with code $($p.ExitCode)"
}
