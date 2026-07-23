<#
.SYNOPSIS
    Verify a DEV machine has everything needed to BUILD and PACKAGE bobcat_engine.
    Read-only: it checks and reports, it does not install anything.

.DESCRIPTION
    Checks the toolchain (CMake, Ninja, MSVC), Qt, the external/ SDK folders, and
    git. Prints PASS / MISSING per item and exits non-zero if a REQUIRED item is
    missing so it can gate CI or a pre-package step.

.EXAMPLE
    .\tools\deploy\prep-devbox.ps1
#>
[CmdletBinding()]
param()

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$fail = 0

function Test-Item {
    param([string]$Name, [scriptblock]$Check, [string]$Fix, [switch]$Optional)
    $ok = $false
    try { $ok = [bool](& $Check) } catch { $ok = $false }
    if ($ok) {
        Write-Host ("  [PASS] {0}" -f $Name) -ForegroundColor Green
    } elseif ($Optional) {
        Write-Host ("  [WARN] {0} (optional) -> {1}" -f $Name, $Fix) -ForegroundColor Yellow
    } else {
        Write-Host ("  [MISS] {0} -> {1}" -f $Name, $Fix) -ForegroundColor Red
        $script:fail++
    }
}

function Have-Cmd([string]$c) { $null -ne (Get-Command $c -ErrorAction SilentlyContinue) }

# Locate a VS 2022 install root (this project builds via VS-integrated CMake, so
# cmake/ninja usually live under the VS install rather than the system PATH).
$vsRoot = $null
foreach ($ed in "Enterprise","Professional","Community","BuildTools") {
    foreach ($pf in "$env:ProgramFiles","${env:ProgramFiles(x86)}") {
        $c = Join-Path $pf "Microsoft Visual Studio\2022\$ed"
        if (Test-Path $c) { $vsRoot = $c; break }
    }
    if ($vsRoot) { break }
}
$vsCMake = if ($vsRoot) { Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }
$vsNinja = if ($vsRoot) { Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" }

Write-Host "bobcat_engine dev-box readiness ($RepoRoot)`n" -ForegroundColor Cyan

Write-Host "Toolchain"
Test-Item "CMake (PATH or VS-bundled)"  { (Have-Cmd cmake) -or ($vsCMake -and (Test-Path $vsCMake)) }  "install CMake, or the VS 'C++ CMake tools' component"
Test-Item "Ninja (PATH or VS-bundled)"  { (Have-Cmd ninja) -or ($vsNinja -and (Test-Path $vsNinja)) }  "install Ninja (ships with VS 'C++ CMake tools')"
Test-Item "MSVC (cl.exe or Visual Studio)" {
    (Have-Cmd cl) -or
    (Test-Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe") -or
    (Test-Path "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe")
} "install Visual Studio 2022 with the 'Desktop development with C++' workload"
Test-Item "git on PATH"    { Have-Cmd git }   "install Git (needed for the MICRO version derive)"

Write-Host "`nQt"
Test-Item "Qt 6.9.x / 6.10.x under C:\Qt" {
    (Get-ChildItem "C:\Qt" -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^6\.(9|10)\.' }).Count -gt 0
} "install Qt 6.9.3 (or 6.10.x) via the Qt online installer to C:\Qt"

Write-Host "`nExternal SDKs (external/)"
$ext = Join-Path $RepoRoot "external"
# Required for a hardware build; CMake auto-detects but hardware presets expect these.
$reqSdks = @{
    "ACSC" = "ACS motion SDK";  "PI" = "PI GCS2 SDK"; "eziio" = "Fastech EziIO SDK"
    "pylon" = "Basler Pylon 8"; "spdlog" = "spdlog (header-only)"; "nlohmann" = "nlohmann/json"
    "sqlite3" = "SQLite3 (bundled)"
}
$optSdks = @{
    "uEye" = "IDS uEye SDK"; "visa" = "NI-VISA (TEC/SMU/PSU)"; "opencv" = "OpenCV 4.12 (HAS_OPENCV)"
    "halcon" = "Halcon (HAS_HALCON)"; "kinesis" = "Thorlabs Kinesis"
}
# Match by prefix so versioned folder names (e.g. "pylon 8") are found.
function Test-SdkDir([string]$name) {
    [bool](Get-ChildItem $ext -Directory -Filter "$name*" -ErrorAction SilentlyContinue)
}
foreach ($k in $reqSdks.Keys | Sort-Object) {
    Test-Item ("external\{0}  ({1})" -f $k, $reqSdks[$k]) { Test-SdkDir $k } "restore external\$k from the SDK archive"
}
foreach ($k in $optSdks.Keys | Sort-Object) {
    Test-Item ("external\{0}  ({1})" -f $k, $optSdks[$k]) { Test-SdkDir $k } "restore external\$k if this hardware is used" -Optional
}

Write-Host ""
if ($fail -eq 0) {
    Write-Host "READY. Build + package with:" -ForegroundColor Green
    Write-Host "  cmake --preset x64-release"
    Write-Host "  cmake --build out/build/x64-release"
    Write-Host "  .\tools\deploy\package.ps1 -Products <PRODUCT> -VcRedist <path\vc_redist.x64.exe> -Zip"
    exit 0
} else {
    Write-Host ("NOT READY: {0} required item(s) missing (see above)." -f $fail) -ForegroundColor Red
    exit 1
}
