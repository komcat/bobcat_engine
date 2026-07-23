<#
.SYNOPSIS
    Prepare a TARGET machine to run a deployed bobcat_engine package.
    Run this from INSIDE the unzipped package (next to bobcat_engine.exe).

.DESCRIPTION
    Steps, in order:
      1. Install the VC++ 2015-2022 x64 redistributable (redist\vc_redist.x64.exe).
      2. Open camera firewall ports (config\bobcat_firewall.ps1), unless -SkipFirewall.
      3. Probe the vendor runtimes that are NOT bundled with the app and report
         whether the app will be able to load them:
           * VC++ CRT  (msvcp140.dll)      - from step 1
           * ACS       (ACSC.dll)          - ACS SPiiPlus user-mode driver
           * NI-VISA   (visa64.dll)        - NI-VISA runtime (TEC/SMU/PSU)
         Bundled DLLs (Qt, PI GCS2, EziIO, IDS ueye, Pylon, Thorlabs, OpenCV) are
         shipped app-local and need no install.

    Requires an elevated (Administrator) PowerShell for steps 1 and 2.
    Use -CheckOnly to run the probes without installing or touching the firewall.

.EXAMPLE
    # On the target, elevated, from the unzipped package folder:
    .\prep-target.ps1

.EXAMPLE
    .\prep-target.ps1 -CheckOnly        # just report, change nothing
#>
[CmdletBinding()]
param(
    [string] $PackageRoot = $PSScriptRoot,   # defaults to the folder this script sits in
    [switch] $SkipFirewall,
    [switch] $CheckOnly
)

$ErrorActionPreference = 'Stop'
$warn = 0

# LoadLibrary probe: reflects the actual DLL search path the app would use.
Add-Type -Namespace Win32 -Name Dll -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32", SetLastError=true, CharSet=System.Runtime.InteropServices.CharSet.Unicode)]
public static extern System.IntPtr LoadLibrary(string name);
'@
function Test-DllLoadable([string]$dll) {
    $h = [Win32.Dll]::LoadLibrary($dll)
    return ($h -ne [System.IntPtr]::Zero)
}

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

Write-Host "bobcat_engine target prep  ($PackageRoot)`n" -ForegroundColor Cyan

$isAdmin = Test-Admin
if (-not $isAdmin -and -not $CheckOnly) {
    Write-Warning "Not elevated. Steps 1-2 need Administrator. Re-run in an elevated PowerShell, or use -CheckOnly."
}

# --- 1) VC++ redistributable -----------------------------------------------------
if (-not $CheckOnly) {
    $redist = Join-Path $PackageRoot "redist\vc_redist.x64.exe"
    if (Test-Path $redist) {
        if ($isAdmin) {
            Write-Host "--> installing VC++ redist (quiet)..."
            $p = Start-Process -FilePath $redist -ArgumentList "/install","/quiet","/norestart" -Wait -PassThru
            # 0 = installed, 1638/3010 = already present / reboot required (all benign)
            if ($p.ExitCode -in 0,1638,3010) { Write-Host "    redist ok (exit $($p.ExitCode))" }
            else { Write-Warning "    redist installer returned $($p.ExitCode)"; $warn++ }
        } else { Write-Warning "skipping redist install (needs admin)"; $warn++ }
    } else {
        Write-Warning "redist\vc_redist.x64.exe not in package. Install VC++ 2015-2022 x64 manually."; $warn++
    }
}

# --- 2) camera firewall ----------------------------------------------------------
if (-not $CheckOnly -and -not $SkipFirewall) {
    $fw = Join-Path $PackageRoot "config\bobcat_firewall.ps1"
    if (Test-Path $fw) {
        if ($isAdmin) {
            Write-Host "--> opening camera firewall ports..."
            & $fw
        } else { Write-Warning "skipping firewall (needs admin)"; $warn++ }
    } else { Write-Warning "config\bobcat_firewall.ps1 not found (skip if no GigE cameras)" }
}

# --- 3) probe non-bundled vendor runtimes ---------------------------------------
Write-Host "`nRuntime probes (app will load these from the system search path):"
function Probe([string]$name, [string]$dll, [string]$fix, [switch]$Optional) {
    if (Test-DllLoadable $dll) {
        Write-Host ("  [PASS] {0} ({1})" -f $name, $dll) -ForegroundColor Green
    } elseif ($Optional) {
        Write-Host ("  [WARN] {0} ({1}) -> {2}" -f $name, $dll, $fix) -ForegroundColor Yellow
        $script:warn++
    } else {
        Write-Host ("  [MISS] {0} ({1}) -> {2}" -f $name, $dll, $fix) -ForegroundColor Red
        $script:warn++
    }
}
Probe "VC++ CRT"  "msvcp140.dll" "install redist\vc_redist.x64.exe (step 1)"
Probe "ACS motion" "ACSC.dll"    "install the ACS SPiiPlus user-mode driver / SDK runtime"
Probe "NI-VISA"    "visa64.dll"  "install the NI-VISA runtime" -Optional

# --- reminders the script can't verify automatically ----------------------------
Write-Host "`nAlso confirm (vendor drivers, not auto-checked):" -ForegroundColor Cyan
Write-Host "  * Basler Pylon GigE: cameras reachable (firewall done above); optional GigE filter driver"
Write-Host "  * IDS uEye: uEye driver/service installed so cameras enumerate"
Write-Host "  * EziIO over USB: USB (FTDI) driver installed"
Write-Host "  * Thorlabs Kinesis: TEC device driver installed if used"

Write-Host ""
if ($warn -eq 0) {
    Write-Host "Target looks ready. Launch bobcat_engine.exe, load the product, re-verify positions/calibration." -ForegroundColor Green
} else {
    Write-Host ("$warn item(s) need attention above before running against hardware.") -ForegroundColor Yellow
}
