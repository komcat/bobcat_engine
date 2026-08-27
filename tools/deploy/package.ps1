<#
.SYNOPSIS
    Assemble a clean, redistributable bobcat_engine deployment package for a
    fresh target machine (same hardware, different PC).

.DESCRIPTION
    The package is drawn from TWO source trees, because the redistributable
    binaries and the real (taught) operating config normally live in different
    build outputs on the dev machine:

      * -BinSource   Release build output. Supplies the exe, Qt release DLLs,
                     all SDK runtime DLLs, Qt plugin dirs, scripts, assets,
                     translations. Build junk is excluded.
                     Default: out/build/x64-release

      * -DataSource  The build output you actually OPERATE from. Supplies the
                     authoritative config/ tree: root *.json + *.local.json
                     (machine params) and the seed product folder(s) with real
                     taught positions / calibration / recipes / programs.
                     Default: out/build/x64-debug-hardware

    Per-product, 'backups/', 'instances_data/' and 'images/' are excluded
    (runtime cruft), matching the chosen "everything except backups /
    instances_data / images" seed scope. 'images/' holds timestamped trainer
    frame captures and is typically the largest thing under config/.

    Because the release exe links the *release* CRT (not bundled anywhere), the
    target needs the VC++ redistributable. Point -VcRedist at vc_redist.x64.exe
    and it is folded into the package under redist/.

.EXAMPLE
    # Default: seed ALL products, package to dist\, then zip
    .\tools\deploy\package.ps1 -Zip

.EXAMPLE
    # Seed a single product and include the VC++ redist installer
    .\tools\deploy\package.ps1 -Products SIPHOG888 `
        -VcRedist C:\redist\vc_redist.x64.exe -Zip

.NOTES
    Run from the repo root. Requires robocopy (built into Windows).
#>
[CmdletBinding()]
param(
    [string]   $BinSource  = "out/build/x64-release",
    [string]   $DataSource = "out/build/x64-debug-hardware",
    [string[]] $Products,                       # default: all non-system product folders
    [string]   $OutDir,                         # default: dist/bobcat_engine_<date>
    [string]   $VcRedist,                       # path to vc_redist.x64.exe (optional)
    [switch]   $IncludeUtilities,               # include config_editor / data_explorer / find_visa_devices
    [switch]   $Zip
)

$ErrorActionPreference = 'Stop'

# --- resolve paths relative to repo root (script lives in tools/deploy) ---------
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $RepoRoot

# Join against repo root only for relative paths; honor absolute paths as given.
function Resolve-Under([string]$p) {
    if ([System.IO.Path]::IsPathRooted($p)) { $p } else { Join-Path $RepoRoot $p }
}

$BinSrc  = Resolve-Under $BinSource
$DataSrc = Resolve-Under $DataSource

if (-not (Test-Path (Join-Path $BinSrc  "bobcat_engine.exe"))) {
    throw "BinSource has no bobcat_engine.exe: $BinSrc  (build x64-release first)"
}
if (-not (Test-Path (Join-Path $DataSrc "config"))) {
    throw "DataSource has no config/ tree: $DataSrc"
}

# Guard against shipping a debug build unnoticed (debug CRT is not redistributable)
if (Test-Path (Join-Path $BinSrc "Qt6Cored.dll")) {
    Write-Warning "BinSource looks like a DEBUG build (Qt6Cored.dll present)."
    Write-Warning "Debug builds need Visual Studio's debug CRT on the target and will NOT run on a clean PC."
    Write-Warning "Use -BinSource out/build/x64-release for a real deployment."
}

if (-not $OutDir) {
    $stamp  = Get-Date -Format "yyyyMMdd_HHmmss"
    $OutDir = Join-Path $RepoRoot "dist/bobcat_engine_$stamp"
}
$Pkg = Join-Path $OutDir "bobcat_engine"     # the runnable tree the operator copies

Write-Host "==> Packaging bobcat_engine" -ForegroundColor Cyan
Write-Host "    binaries : $BinSrc"
Write-Host "    config   : $DataSrc"
Write-Host "    output   : $Pkg`n"

New-Item -ItemType Directory -Force -Path $Pkg | Out-Null

# robocopy success codes are 0-7; >=8 is a real failure.
function Invoke-Robocopy([string[]]$RoboArgs) {
    $out = & robocopy @RoboArgs
    if ($LASTEXITCODE -ge 8) {
        $out | Write-Host
        throw "robocopy failed (exit $LASTEXITCODE): $($RoboArgs -join ' ')"
    }
    $global:LASTEXITCODE = 0
}

# --- 1) runtime tree from BinSource, minus build junk and config -----------------
# config/ is taken from DataSource in step 2, so exclude it here.
$excludeDirs = @(
    "config", "logs", "data", "Testing", "CMakeFiles", "_deps",
    "bobcat_engine_autogen", "config_editor_autogen", "data_explorer_autogen",
    "find_visa_devices_autogen", "lua54_autogen", "include"
)
$excludeFiles = @(
    "*.obj", "*.ilk", "*.pdb", "*.lib", "*.exp", "*.ninja", "*.ninja_deps",
    ".ninja_deps", ".ninja_log", "CMakeCache.txt", "cmake_install.cmake",
    "build.ninja", "VSInheritEnvironments.txt", "*.session.json",
    "temp_*.json", "CHANGELOG.md"
)
if (-not $IncludeUtilities) {
    $excludeFiles += @("config_editor.exe", "data_explorer.exe",
                       "find_visa_devices.exe", "PylonFirmwareUpdater.exe")
}

$roboArgs = @($BinSrc, $Pkg, "/E", "/NFL", "/NDL", "/NP", "/NJH", "/NJS")
$roboArgs += "/XD"; $roboArgs += $excludeDirs
$roboArgs += "/XF"; $roboArgs += $excludeFiles
Write-Host "--> copying runtime binaries..."
Invoke-Robocopy $roboArgs

# --- 2) config tree from DataSource ---------------------------------------------
# Copy root config/ files first (non-recursive at top level handled by /E, but we
# prune product sub-trees below). Simplest: mirror config, then remove excluded
# per-product subdirs.
$cfgSrc = Join-Path $DataSrc "config"
$cfgDst = Join-Path $Pkg     "config"
Write-Host "--> copying config (root + product seed)..."
# Exclude runtime cruft everywhere under config: backups, instances_data, images.
Invoke-Robocopy @($cfgSrc, $cfgDst, "/E", "/NFL", "/NDL", "/NP", "/NJH", "/NJS",
                  "/XD", "backups", "instances_data", "images")

# --- 3) narrow to requested products (optional) ---------------------------------
# System/support folders that are NOT products and must always stay.
$systemDirs = @("SYSTEM", "system", "calibration", "overlays",
                "accuracy_trainer", "coordalign")
if ($Products) {
    Get-ChildItem $cfgDst -Directory | ForEach-Object {
        $name = $_.Name
        if ($systemDirs -notcontains $name -and $Products -notcontains $name) {
            Write-Host "    dropping product folder: $name"
            Remove-Item $_.FullName -Recurse -Force
        }
    }
}

# --- 4) VC++ redistributable ----------------------------------------------------
# Resolve order: explicit -VcRedist  ->  tools\deploy\vc_redist.x64.exe  ->  newest
# vc_redist.x64.exe under the VS 2022 install (matches the build toolset).
if (-not $VcRedist) {
    $localRedist = Join-Path $PSScriptRoot "vc_redist.x64.exe"
    if (Test-Path $localRedist) {
        $VcRedist = $localRedist
    } else {
        $vsHit = Get-ChildItem "$env:ProgramFiles\Microsoft Visual Studio\2022\*\VC\Redist\MSVC" `
                    -Recurse -Filter vc_redist.x64.exe -ErrorAction SilentlyContinue |
                 Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($vsHit) { $VcRedist = $vsHit.FullName }
    }
}
if ($VcRedist) {
    if (-not (Test-Path $VcRedist)) { throw "VcRedist not found: $VcRedist" }
    $redistDir = Join-Path $Pkg "redist"
    New-Item -ItemType Directory -Force -Path $redistDir | Out-Null
    Copy-Item $VcRedist (Join-Path $redistDir "vc_redist.x64.exe") -Force
    Write-Host "--> bundled VC++ redist  ($VcRedist)"
} else {
    Write-Warning "No VC++ redist found (pass -VcRedist). Target must have VC++ 2015-2022 x64 installed."
}

# --- 5) ship the target-prep script inside the package --------------------------
$prep = Join-Path $PSScriptRoot "prep-target.ps1"
if (Test-Path $prep) {
    Copy-Item $prep (Join-Path $Pkg "prep-target.ps1") -Force
    Write-Host "--> bundled prep-target.ps1"
}

# --- 6) drop a first-run readme into the package --------------------------------
$readme = @"
bobcat_engine deployment package
================================
Built from : $BinSource  (binaries)  +  $DataSource  (config)
Seeded     : $(if ($Products) { $Products -join ', ' } else { 'all products' })
Packaged   : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')

FIRST-RUN ON TARGET MACHINE
1. Open an ELEVATED PowerShell in this folder and run (the -ExecutionPolicy
   Bypass avoids the "running scripts is disabled" block on zipped files):
       powershell -ExecutionPolicy Bypass -File .\prep-target.ps1
   (installs the VC++ redist, opens camera firewall ports, and probes the
    vendor runtimes that aren't bundled: ACS ACSC.dll, NI-VISA visa64.dll.)
2. Install/confirm any hardware SDK drivers it flags (ACS, NI-VISA, camera drivers).
3. Launch bobcat_engine.exe.

RE-TEACH PER MACHINE (carried data is only a STARTING POINT)
Even on identical hardware the mechanical zero differs, so re-verify:
  * Named positions        config\<PRODUCT>\positions.<PRODUCT>.json
  * Calibration            config\<PRODUCT>\calibration\, transformation_matrix.json
  * Needle offset          config\SYSTEM\needlexyoffset.json
Recipes and programs (config\<PRODUCT>\recipes, \programs) are portable as-is.

Machine-specific device params live in config\*.local.json — adjust IPs/ports if
this target's wiring differs.
"@
Set-Content -Path (Join-Path $Pkg "DEPLOY_README.txt") -Value $readme -Encoding UTF8

# --- 7) optional zip ------------------------------------------------------------
if ($Zip) {
    $zipPath = "$OutDir.zip"
    Write-Host "--> zipping to $zipPath"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path $Pkg -DestinationPath $zipPath
    Write-Host "`nDONE. Package: $zipPath" -ForegroundColor Green
} else {
    Write-Host "`nDONE. Package tree: $Pkg" -ForegroundColor Green
}
