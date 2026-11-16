# Makes a test PC clean again before trying a new release zip, so nothing installed on it can hide a
# file missing from the package.
#
#   clean_test_pc.ps1           list what is there: changes nothing
#   clean_test_pc.ps1 -Clean    the same list, then asks before removing each item
#
# A release installs nothing into Windows: its build tools, Python and FFmpeg stay in its own folder.
# What this finds is everything else that could stand in for a missing file: Visual Studio and its
# Build Tools, the Visual C++ runtime, Windows SDKs, Python, Git, CMake, LLVM, MinGW, Qt, and old
# extracted release folders (which hold that copy's saves). Run it on the test PC, never on a PC
# you build on. Removing needs administrator rights; with -Clean it asks Windows for them.
#
# Windows PowerShell 5.1, nothing else needed. clean_test_pc.bat runs it with a double-click.
param(
    [switch]$Clean,
    # More folders to search for extracted releases, besides Downloads, Desktop and Documents.
    [string[]]$SearchFolder = @()
)

$ErrorActionPreference = "Stop"

# 32-bit PowerShell on 64-bit Windows sees a redirected registry and misses most 64-bit programs, so
# run again in the 64-bit one (SysNative is how a 32-bit process reaches it).
if ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) {
    $ps64 = Join-Path $env:WINDIR "SysNative\WindowsPowerShell\v1.0\powershell.exe"
    $argList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $PSCommandPath)
    if ($Clean) { $argList += "-Clean" }
    foreach ($f in $SearchFolder) { $argList += @("-SearchFolder", $f) }
    & $ps64 @argList
    exit $LASTEXITCODE
}

if ($Clean) {
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $admin) {
        $argList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-Clean")
        foreach ($f in $SearchFolder) { $argList += @("-SearchFolder", "`"$f`"") }
        Start-Process powershell -Verb RunAs -ArgumentList $argList
        return
    }
}

# ---- What counts --------------------------------------------------------------------------------

# Installed programs, matched on their name in Apps & features, in the order they are removed:
# Visual Studio first, because it holds on to the SDKs and the runtime.
$Products = @(
    @{ Kind = "Visual Studio";        Pattern = '^Visual Studio (Build Tools|Community|Professional|Enterprise)' },
    @{ Kind = "Visual Studio";        Pattern = '^Microsoft Visual Studio Installer$' },
    @{ Kind = "Windows SDK";          Pattern = '^Windows Software Development Kit' },
    # Only the v14 runtime (2015 and later) is what the release's renderer uses; older ones are left alone.
    @{ Kind = "Visual C++ runtime";   Pattern = '^Microsoft Visual C\+\+ (v14|2015|2017|2019|2022).*Redistributable' },
    @{ Kind = "Python";               Pattern = '^Python \d+\.\d+\.\d+ \((32|64)-bit\)$|^Python Launcher$' },
    @{ Kind = "Git";                  Pattern = '^Git( version [\d.]+)?$' },
    @{ Kind = "CMake";                Pattern = '^CMake$' },
    @{ Kind = "Ninja";                Pattern = '^Ninja' },
    @{ Kind = "LLVM";                 Pattern = '^LLVM' },
    @{ Kind = "MSYS2 / MinGW";        Pattern = '^MSYS2|MinGW' },
    @{ Kind = "Qt";                   Pattern = '^Qt( \d|$)|^Qt Maintenance' }
)

# Programs a build would look for on PATH.
$Commands = @("cl", "link", "python", "python3", "py", "pip", "git", "cmake", "ninja", "clang", "clang++", "gcc", "g++",
              "mingw32-make", "qmake", "windeployqt")

# Folders tools leave without an entry in Apps & features (aqtinstall's Qt, unpacked toolchains).
$LooseFolders = @("C:\Qt", "C:\msys64", "C:\mingw64", "C:\llvm-mingw", "C:\Program Files\LLVM", "C:\Program Files\CMake",
                  "$env:LOCALAPPDATA\Programs\Python", "C:\Python312", "C:\Python311", "C:\Python310")

# ---- Looking ------------------------------------------------------------------------------------

function Get-InstalledPrograms {
    $keys = @("HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*",
              "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*",
              "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*")
    foreach ($key in $keys) {
        Get-ItemProperty $key -ErrorAction SilentlyContinue |
            Where-Object { $_.DisplayName -and $_.UninstallString -and -not $_.SystemComponent -and -not $_.ParentKeyName }
    }
}

function Find-Products {
    # An empty pattern matches every program on the PC, so refuse to run with one.
    foreach ($product in $Products) {
        if (-not ($product -is [hashtable]) -or [string]::IsNullOrWhiteSpace($product.Pattern)) {
            throw "A product pattern is missing; nothing was searched or removed."
        }
    }
    $installed = @(Get-InstalledPrograms)
    $found = @()
    $seen = @{}
    foreach ($product in $Products) {
        foreach ($p in $installed | Where-Object { $_.DisplayName -match $product.Pattern } | Sort-Object DisplayName) {
            if ($seen.ContainsKey($p.DisplayName)) { continue }
            $seen[$p.DisplayName] = $true
            $found += [pscustomobject]@{ Kind = $product.Kind; Name = $p.DisplayName; Version = $p.DisplayVersion; Entry = $p }
        }
    }
    $found
}

function Find-VisualStudioInstances {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return @() }
    $json = & $vswhere -all -products * -format json -utf8
    if (-not $json) { return @() }
    @($json | Out-String | ConvertFrom-Json)
}

function Find-Commands {
    foreach ($name in $Commands) {
        foreach ($c in @(Get-Command $name -CommandType Application -All -ErrorAction SilentlyContinue)) {
            # Windows' "App execution alias" python.exe only opens the Store; it is no Python.
            if ($c.Source -like "*\Microsoft\WindowsApps\*") { continue }
            [pscustomobject]@{ Name = $name; Path = $c.Source }
        }
    }
}

function Find-ReleaseFolders {
    $roots = @([Environment]::GetFolderPath("UserProfile") + "\Downloads", [Environment]::GetFolderPath("Desktop"),
               [Environment]::GetFolderPath("MyDocuments")) + $SearchFolder
    foreach ($root in $roots | Where-Object { $_ -and (Test-Path $_) }) {
        # A release is a folder with the launcher in it, at most two levels down.
        Get-ChildItem $root -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            $_
            Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue
        } | Where-Object { Test-Path (Join-Path $_.FullName "drakengard-launcher.exe") } | ForEach-Object { $_.FullName }
    }
}

function Find-Variables {
    foreach ($scope in "User", "Machine") {
        $vars = [Environment]::GetEnvironmentVariables($scope)
        foreach ($name in $vars.Keys | Where-Object { $_ -like "PS2X_*" -or $_ -in @("QTDIR", "Qt6_DIR", "VCINSTALLDIR", "VSINSTALLDIR") }) {
            [pscustomobject]@{ Scope = $scope; Name = $name; Value = $vars[$name] }
        }
    }
}

# ---- Removing -----------------------------------------------------------------------------------

# The command that removes a program quietly, from what its installer registered.
function Get-UninstallCommand($entry) {
    if ($entry.QuietUninstallString) { return $entry.QuietUninstallString }
    $u = $entry.UninstallString.Trim()
    # The Visual Studio Installer takes its own flags; its uninstaller runs as registered.
    if ($entry.DisplayName -eq "Microsoft Visual Studio Installer") { return $u }
    if ($u -match '(?i)msiexec(\.exe)?\s+/[IX]\s*(\{[0-9A-F-]+\})') {
        return "msiexec.exe /x $($Matches[2]) /passive /norestart"
    }
    if ($u -match '(?i)\\unins\d+\.exe"?\s*$') { return "$u /SILENT /NORESTART" }   # Inno Setup (Git)
    if ($u -match '(?i)\s/uninstall\b') { return "$u /passive /norestart" }          # WiX bundles (VC++ runtime, Python)
    $u
}

function Invoke-CommandLine($line) {
    # Split "C:\path\to.exe" args, or C:\path\to.exe args, into the program and its arguments.
    if ($line -match '^\s*"([^"]+)"\s*(.*)$' -or $line -match '^\s*(\S+)\s*(.*)$') {
        $p = Start-Process -FilePath $Matches[1] -ArgumentList $Matches[2] -Wait -PassThru
        return $p.ExitCode
    }
    throw "Can't run: $line"
}

$script:YesToAll = $false
function Confirm-Step($question) {
    if ($script:YesToAll) { return $true }
    while ($true) {
        $a = (Read-Host "$question [y = yes, n = no, a = yes to all, q = quit]").Trim().ToLower()
        switch ($a) {
            "y" { return $true }
            "n" { return $false }
            "a" { $script:YesToAll = $true; return $true }
            "q" { exit 0 }
        }
    }
}

function Remove-ToRecycleBin($path) {
    Add-Type -AssemblyName Microsoft.VisualBasic
    [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory($path, "OnlyErrorDialogs", "SendToRecycleBin")
}

# ---- Report -------------------------------------------------------------------------------------

Write-Host "Looking for build tools and old releases on $env:COMPUTERNAME..." -ForegroundColor Cyan
# PowerShell names ignore case: none of these may reuse a name from "What counts" ($Products and so on).
$foundVs = @(Find-VisualStudioInstances)
$foundPrograms = @(Find-Products)
$foundOnPath = @(Find-Commands)
$foundFolders = @($LooseFolders | Where-Object { Test-Path $_ })
$foundReleases = @(Find-ReleaseFolders | Sort-Object -Unique)
$foundVariables = @(Find-Variables)

Write-Host ""
Write-Host "Installed programs:" -ForegroundColor Yellow
if ($foundVs.Count -eq 0 -and $foundPrograms.Count -eq 0) { Write-Host "  none" }
foreach ($vs in $foundVs) { Write-Host ("  Visual Studio instance  {0} {1}  ({2})" -f $vs.displayName, $vs.installationVersion, $vs.installationPath) }
foreach ($p in $foundPrograms) { Write-Host ("  {0,-22} {1} {2}" -f $p.Kind, $p.Name, $p.Version) }

Write-Host "On PATH:" -ForegroundColor Yellow
if ($foundOnPath.Count -eq 0) { Write-Host "  none" }
foreach ($c in $foundOnPath) { Write-Host ("  {0,-12} {1}" -f $c.Name, $c.Path) }

Write-Host "Loose folders:" -ForegroundColor Yellow
if ($foundFolders.Count -eq 0) { Write-Host "  none" }
foreach ($f in $foundFolders) { Write-Host "  $f" }

Write-Host "Extracted releases (each holds that copy's saves in mc0):" -ForegroundColor Yellow
if ($foundReleases.Count -eq 0) { Write-Host "  none" }
foreach ($r in $foundReleases) { Write-Host "  $r" }

Write-Host "Environment variables:" -ForegroundColor Yellow
if ($foundVariables.Count -eq 0) { Write-Host "  none" }
foreach ($v in $foundVariables) { Write-Host ("  {0,-8} {1} = {2}" -f $v.Scope, $v.Name, $v.Value) }

$total = $foundVs.Count + $foundPrograms.Count + $foundFolders.Count + $foundReleases.Count + $foundVariables.Count
Write-Host ""
if ($total -eq 0 -and $foundOnPath.Count -eq 0) {
    Write-Host "This PC is clean: nothing here can stand in for a file the release needs." -ForegroundColor Green
    if ($Clean) { Read-Host "Press Enter to close" | Out-Null }
    return
}
if (-not $Clean) {
    Write-Host "Nothing was changed. Run with -Clean (or clean_test_pc.bat) to remove these, one question each." -ForegroundColor Cyan
    return
}

# ---- Clean --------------------------------------------------------------------------------------

Write-Host "Removing, one question each. Other programs may need the Visual C++ runtime; most games" -ForegroundColor Cyan
Write-Host "reinstall it themselves, but say no if this PC is used for more than testing." -ForegroundColor Cyan
$failed = @()

$installer = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\setup.exe"
foreach ($vs in $foundVs) {
    if (-not (Confirm-Step "Uninstall $($vs.displayName) from $($vs.installationPath)?")) { continue }
    $code = (Start-Process $installer -ArgumentList @("uninstall", "--installPath", "`"$($vs.installationPath)`"", "--passive", "--norestart") -Wait -PassThru).ExitCode
    if ($code -ne 0 -and $code -ne 3010) { $failed += "$($vs.displayName) (exit code $code)" }
}

# Visual Studio's own entries go with its instances; read the list again before the rest.
foreach ($p in @(Find-Products)) {
    if ($p.Name -match '^Visual Studio (Build Tools|Community|Professional|Enterprise)') { continue }
    if (-not (Confirm-Step "Uninstall $($p.Name) $($p.Version)?")) { continue }
    $line = Get-UninstallCommand $p.Entry
    Write-Host "  $line"
    $code = Invoke-CommandLine $line
    if ($code -ne 0 -and $code -ne 3010 -and $code -ne 1605) { $failed += "$($p.Name) (exit code $code)" }
}

foreach ($f in $foundFolders + $foundReleases) {
    if (-not (Test-Path $f)) { continue }
    if (-not (Confirm-Step "Move $f to the Recycle Bin?")) { continue }
    try { Remove-ToRecycleBin $f } catch { $failed += "$f ($($_.Exception.Message))" }
}

foreach ($v in $foundVariables) {
    if (-not (Confirm-Step "Remove the $($v.Scope) environment variable $($v.Name)?")) { continue }
    [Environment]::SetEnvironmentVariable($v.Name, $null, $v.Scope)
}

Write-Host ""
if ($failed.Count -gt 0) {
    Write-Host "These were not removed:" -ForegroundColor Red
    foreach ($f in $failed) { Write-Host "  $f" }
}
Write-Host "Done. Restart Windows, then run clean_test_pc.ps1 again to check: PATH entries left by" -ForegroundColor Green
Write-Host "removed programs are only gone once Windows has restarted." -ForegroundColor Green
Read-Host "Press Enter to close" | Out-Null
