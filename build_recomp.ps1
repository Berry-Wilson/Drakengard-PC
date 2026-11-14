param(
    [switch]$SkipRecomp,
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$CMake = "C:\Program Files\CMake\bin\cmake.exe"
$BuildDir = Join-Path $Root "external\PS2Recomp\out\build"
$RecompExe = Join-Path $BuildDir "ps2xRecomp\$Config\ps2_recomp.exe"
$RunnerExe = Join-Path $BuildDir "ps2xRuntime\$Config\ps2EntryRunner.exe"

if (-not $SkipRecomp) {
    Write-Host "[1/3] Analyzing the ELF, writing the function map & TOML config..." -ForegroundColor Cyan
    & $CMake --build $BuildDir --config $Config --target ps2_analyzer --parallel 8
    python (Join-Path $Root "tools\generate_recomp_config.py")

    Write-Host "[2/3] Running PS2Recomp on SLUS_207.32..." -ForegroundColor Cyan
    # drakengard.toml uses repo-relative paths
    Push-Location $Root
    try { & $RecompExe "recomp\drakengard.toml" } finally { Pop-Location }

    Write-Host "[2.5/3] Packing generated C++ sources into unity batches..." -ForegroundColor Cyan
    python (Join-Path $Root "tools\pack_recomp_sources.py")
}

Write-Host "[3/3] Compiling native PC executable ($Config)..." -ForegroundColor Cyan
& $CMake --build $BuildDir --config $Config --target ps2EntryRunner --parallel 8

Write-Host "Build complete: $RunnerExe" -ForegroundColor Green
