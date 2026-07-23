# Deploying bobcat_engine to another machine

Scenario: **same hardware, different target PC.** You want to carry the process
logic (recipes + programs) and a starting-point config, then re-verify the
machine-specific calibration on the target.

## TL;DR

> **PowerShell execution policy:** Windows blocks unsigned `.ps1` by default
> ("running scripts is disabled on this system"). Either run each script as
> `powershell -ExecutionPolicy Bypass -File .\path\script.ps1`, or once per user
> run `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`. On the **target**,
> the unzipped `prep-target.ps1` carries the Mark-of-the-Web, so use the
> `-ExecutionPolicy Bypass -File` form (or `Unblock-File` it first).

```powershell
# 0. (dev box) confirm the build environment is ready
powershell -ExecutionPolicy Bypass -File .\tools\deploy\prep-devbox.ps1

# 1. Build a redistributable release (hardware mode is the release default)
cmake --preset x64-release
cmake --build out/build/x64-release        # (the user builds this themselves)

# 2. Assemble the package (binaries from release, config/seed from your operating build).
#    The VC++ redist is auto-found (tools\deploy\vc_redist.x64.exe or the VS install);
#    pass -VcRedist only to override.
.\tools\deploy\package.ps1 -Products SIPHOG888 -Zip

# 3. Copy dist\bobcat_engine_<stamp>.zip to the target, unzip, then (elevated):
#      powershell -ExecutionPolicy Bypass -File .\prep-target.ps1
#    ...and follow DEPLOY_README.txt
```

## Machine prep scripts

Two helper scripts bracket the packaging step:

### Dev box — `tools/deploy/prep-devbox.ps1`
Read-only readiness check before you build/package. Verifies the toolchain
(CMake/Ninja on PATH **or** VS-bundled, MSVC, git), Qt under `C:\Qt`, and the
`external/` SDK folders. Exits non-zero if a required item is missing.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\deploy\prep-devbox.ps1   # PASS/MISS; exit 0 = ready
```

Prep steps it can't do for you (manual, one-time):
- Install **Visual Studio 2022** with *Desktop development with C++* (brings MSVC + the C++ CMake tools = CMake + Ninja).
- Install **Qt 6.9.3** (or 6.10.x) via the Qt online installer to `C:\Qt`.
- Restore the vendor SDKs into `external/` (ACSC, PI, eziio, `pylon 8`, uEye, visa, opencv, halcon, kinesis, …).

### Target box — `tools/deploy/prep-target.ps1`
Shipped **inside every package** (next to `bobcat_engine.exe`). Run it elevated
from the unzipped folder. It:
1. installs `redist\vc_redist.x64.exe` (quiet),
2. opens camera firewall ports via `config\bobcat_firewall.ps1`,
3. probes the non-bundled vendor runtimes and reports PASS/MISS:
   - `msvcp140.dll` (VC++ CRT — from step 1)
   - `ACSC.dll` (ACS SPiiPlus user-mode driver — **required for ACS motion**)
   - `visa64.dll` (NI-VISA runtime — TEC/SMU/PSU instruments)

```powershell
# elevated, from the unzipped package folder:
powershell -ExecutionPolicy Bypass -File .\prep-target.ps1              # install + firewall + probe
powershell -ExecutionPolicy Bypass -File .\prep-target.ps1 -CheckOnly   # probe only, change nothing
```

Prep steps it flags but can't install (vendor installers, one-time):
- **ACS SPiiPlus** user-mode driver/SDK runtime (provides `ACSC.dll`).
- **NI-VISA** runtime, if TEC/SMU/PSU VISA instruments are used.
- **Camera drivers**: Basler Pylon (GigE filter driver optional), IDS uEye driver/service.
- **USB drivers**: EziIO (FTDI), Thorlabs Kinesis TEC.

Everything else (Qt, PI GCS2, EziIO lib, IDS `ueye_api`, Pylon, Thorlabs Kinesis
DLLs, OpenCV) is bundled app-local and needs no install.

## Why two source trees

The packaging script pulls from **two** build outputs because, on the dev
machine, the redistributable binaries and the real operating config live apart:

| Piece | Source (`package.ps1` param) | Why |
|-------|------------------------------|-----|
| exe, Qt release DLLs, SDK runtime DLLs, Qt plugin dirs, scripts, assets | `-BinSource` (default `out/build/x64-release`) | The clean, redistributable build |
| `config/` — root `*.json` + `*.local.json`, product folders with taught data | `-DataSource` (default `out/build/x64-debug-hardware`) | The build you actually operate; has the real positions/calibration/recipes |

Build junk (`CMakeFiles/`, `*_autogen/`, `*.pdb`, `build.ninja`, …) and, per
product, `backups/` and `instances_data/` are excluded.

## Two traps that make a naive folder-copy fail

1. **Debug builds don't run on a clean PC.** `x64-debug-hardware` links the
   *debug* CRT (`ucrtbased.dll`, `vcruntime140d.dll`), which ships only with
   Visual Studio. Deploy the **`x64-release`** build. The script warns if it sees
   `Qt6Cored.dll` in `-BinSource`.
2. **Release CRT is not bundled.** No `msvcp140.dll` sits next to the exe. The
   target needs the **VC++ 2015–2022 x64 redistributable**. Pass `-VcRedist` to
   fold `vc_redist.x64.exe` into the package (`redist/`), and run it once on the
   target.

## Portable vs. machine-specific

Even on identical hardware, the mechanical zero differs, so treat carried
calibration as a **starting point**, not final:

| Portable — ship as-is | Re-verify per machine |
|-----------------------|-----------------------|
| `config/<PRODUCT>/recipes/`, `programs/` | `config/<PRODUCT>/positions.<PRODUCT>.json` |
| `scripts/`, vision profile definitions | `config/<PRODUCT>/calibration/`, `transformation_matrix.json` |
| root config templates | `config/SYSTEM/needlexyoffset.json` |
| | `config/*.local.json` (IPs/ports if wiring differs) |

The seed scope in use ("everything except `backups`/`instances_data`") carries
positions and calibration so bring-up is faster — just confirm/re-teach them
before production.

## On the target machine

The generated `DEPLOY_README.txt` in each package restates this:

1. Install `redist\vc_redist.x64.exe` (if not already present).
2. Install/confirm hardware SDK drivers: ACS, PI GCS2, EziIO, Pylon, IDS uEye, NI-VISA.
3. For GigE cameras, run `config\bobcat_firewall.ps1` (as admin) to open ports.
4. Launch `bobcat_engine.exe`, load the product, and re-verify positions/calibration.

## Script reference — `tools/deploy/package.ps1`

| Param | Default | Meaning |
|-------|---------|---------|
| `-BinSource` | `out/build/x64-release` | Redistributable binaries source |
| `-DataSource` | `out/build/x64-debug-hardware` | Authoritative `config/` source |
| `-Products` | *(all)* | Product folder(s) to seed; others dropped |
| `-OutDir` | `dist/bobcat_engine_<stamp>` | Package destination |
| `-VcRedist` | *(auto)* | Override path to `vc_redist.x64.exe`. Auto-resolves to `tools\deploy\vc_redist.x64.exe` then the newest one under the VS 2022 install |
| `-IncludeUtilities` | off | Also include `config_editor`/`data_explorer`/`find_visa_devices` |
| `-Zip` | off | Also produce `<OutDir>.zip` |
