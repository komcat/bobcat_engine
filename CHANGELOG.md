# Changelog

All notable changes to bobcat_engine, tracked by git commit.

---

## v1.14.0 — 2026-06-11

### Highlights

- **SWIR poynt beam-tracking mode** — New swappable detection backend faithful to the poynt `server_v2.py` live-tracking path: 4-corner background subtraction + `find_roi` + per-ROI d4sigma with no rejection filters. Slot assignment is a deterministic per-frame `cy`-sort (top → B0/RX, bottom → B1/TX) so labels stay stable. Selected via `usePoyntDetection` in `config/beam_detection.json` (nested `poynt` block); dispatch priority is Halcon > Poynt > Blob > d4sigma. Scope is live per-frame tracking only — the z-scan FIT path keeps using the Python beam_server over TCP. Overlay was reworked to match: per-beam label reads `B0 (RX) / B1 (TX)`, AR/EE rows dropped, `PK` kept, new `SAT` row (saturated pixel count) and history charts now track SAT instead of peak intensity. Stacked-label collision avoidance added so co-quadrant labels no longer overlap. Lone single-beam frames pick their slot from the centroid's vertical half (top → B0, bottom → B1) instead of nearest-prior, keeping overlay magnifiers, crop tiles, history charts, and the broker label consistent.
- **Dual-Hex Global Maximize** — New "2-device maximize" routine on the Scanning tab of RunProgramPage that coordinates both hexapods to maximize one shared signal. A collimating device steps `-Z` while a search device runs a centered NxN XY grid and moves to peak; iterates while the session-peak signal improves >1%, then returns both devices to the session peak. Stop/error always restores both devices to the session peak and the hexapods' original system velocity. Qt-free algorithm runs on a worker thread with halt flag, RAII velocity guard (1 mm/s during run), centered grid search, and averaged DataStore measurement.
- **Bulk DUT/PSU/SMU "all off" recipe commands** — Three new manager-level safety/cleanup steps: `DUT Insert All from DataStore` (snapshots every DataStore channel into one grouped DUT entry; soft no-op when no DUT is active so it doesn't fail the sequence), `PSU All Output OFF` (auto-connects every PSU before disabling — fixes silent skip when running fresh), `SMU All Output OFF` (per-channel `SelectChannel + SetOutputOff` loop on TSP adapters — fixes 2602B leaving `smub` on when only `smua` was killed), and `SMU All Stop Polling`. Routed through new sentinel device names `"psu"` / `"smu"` (mirrors the existing `"pneumatic"` pattern for `RetractAll`).
- **Keithley TSP per-channel routing fix** — `SmuHelpers::SelectChannelIfSupported` was only dynamic-casting to `Keithley6482Adapter`, so on `KeithleySMUAdapter` (2400/2600 series including the 2602B) every per-channel command (`SetVoltage`, `SetCurrent`, `SetCompliance`, `OutputOn`, `OutputOff`) silently ignored the requested channel and operated on whichever TSP channel was last selected — including across recipe runs. Now selects the channel on both adapter families.
- **RunProgramPage Snapshot quick-action** — New green "Snap shot" button next to the Idle badge prompts for a label and dispatches `system/CaptureSnapshot` through a stack-local `SystemCommandHandler`, matching the Init DUT pattern. Recipe page's `SequenceEngine` is also now wired with `StateSnapshotManager` so recipe runs can capture snapshots via the same path.
- **ACS positioning repeatability fix** — ACS gantry moves were intermittently not landing exactly/repeatably because the background position-polling thread was making concurrent synchronous `acsc_*` calls on the *same* controller handle as the move thread — unsupported by the SPiiPlus C library. All SDK access is now serialized behind a per-controller mutex (held only across a single short call, never across a sleep/wait, so the live-position UI stays responsive), and the move path was simplified to mirror the proven Horizon C# adapter: `acsc_ToPoint(flags=0)` immediate-start moves, relative-as-absolute (dropped `ACSC_AMF_RELATIVE`), and poll-`MST_MOVE` completion instead of the long blocking `acsc_WaitMotionEnd`. Confined to `ACSController`; its public API, the motion interlocks, and the PI hexapod path are unchanged. Confirmed on `gantry-main`.

### All Changes (by date)

## 2026-06-13

- **ACS motion: serialize SDK access + mirror proven C# move path** (13333da)
  - Root cause of non-repeatable ACS positioning: the 200 ms background polling thread (`CommunicationThreadFunc`) issued synchronous `acsc_*` calls (`GetFPosition` / `GetMotorState`) on the **same** controller handle concurrently with the move thread's `acsc_ToPointM` / `acsc_GoM` / `acsc_WaitMotionEnd`. The ACS SPiiPlus C library does not support unsynchronized concurrent synchronous calls on one handle, so moves intermittently failed to land exactly. Added `ACSController::m_sdkMutex` guarding **every** `acsc_*` call — held only for the duration of a single call (or the coupled `ToPointM`+`GoM` pair), never across a sleep or blocking wait — so the poll thread and any move/jog thread can never have two SDK calls in flight at once. `m_sdkMutex` and the existing cache `m_mutex` are never held simultaneously (no lock-ordering risk).
  - Move mechanism now mirrors the working Horizon C# adapter (`komcat/Horizon/.../ACSMotionController.cs`): single-axis `MoveToPosition` / `MoveRelative` use `acsc_ToPoint(flags=0)` for immediate start (dropped the `ACSC_AMF_WAIT` + `GoM` pairing); relative moves read `acsc_GetFPosition` and command an **absolute** target (dropped controller-side `ACSC_AMF_RELATIVE`), for both single- and multi-axis.
  - `WaitForMotionCompletion` / `WaitForMultiAxisCompletion` drop the long blocking `acsc_WaitMotionEnd` in favor of a bounded poll of `MST_MOVE` (10 ms) with each read mutex-guarded and the sleep outside the lock — keeps the live-position UI responsive during a move and keeps all SDK access short and serializable. Post-move diagnostics (`CheckInPosition`, `VerifyFinalPosition`, `[MoveDiag]`) preserved.
  - Synchronized multi-axis moves keep the correct `acsc_ToPointM(ACSC_AMF_WAIT)` + `acsc_GoM` start, now issued under one lock scope. `Connect`/`Disconnect` guarded; `Disconnect` marks disconnected before closing the handle. Public `ACSController` API unchanged, so `UnifiedMotionManager`, the 6 `InterlockManager::CheckMove()` enforcement sites, and the PI controller are untouched. Confined to `include/motions/ACS/acs_controller.{h,cpp}`.

## 2026-06-11

- **Recipes: bulk DUT/PSU/SMU off + Keithley TSP channel routing fix** (e4fd660)
  - `DUT.InsertAllFromDataStore` — new command in `DutInsertCommands`. Snapshots every DataStore channel into a single grouped `nlohmann::json` entry on the active DUT (optional `prefix` filter strips the prefix from field names). Soft no-op when no DUT is active — logs Info and returns true rather than failing the sequence, so recipes can capture readings without forcing every run to create a DUT first. New `DUTInsertAllFromDataStorePlugin` recipe step (`entry_name` default `"datastore"`, optional `prefix`).
  - `PSU.DisableAllOutputs` — manager-level broadcast routed via new sentinel device name `"psu"`. Handler intercepts before per-device lookup, iterates `getDeviceNames()`, calls `psu->connect()` on any disconnected device, then disables every channel via `psuManager->getDeviceChannelCount` + `setOutputOff`. Fixes the silent no-op where `PowerSupplyManager::disableAllOutputs()` (written originally for the E-stop cascade where everything is already connected) skipped any PSU whose `isConnected()` was false. New `PSUAllOutputOffPlugin`.
  - `SMU.OutputOffAll` — manager-level broadcast routed via new sentinel `"smu"`. Handler dynamic-casts to `KeithleySMUAdapter` and loops `GetEnabledChannels()` calling `SelectChannel + SetOutputOff` per channel; single-channel devices (e.g., Keithley 6482, where `:OUTP OFF` is global) keep the single-call path. Fixes the 2602B leaving `smub` energized when `SMUManager::OutputOffAll()`'s single `device->SetOutputOff()` call only killed `smua` (whichever was selected last). New `SMUAllOutputOffPlugin`.
  - `SMU.StopPollingAll` — manager-level broadcast to `SMUPollingManager::StopAllPolling()`. New `SMUAllStopPollingPlugin`.
  - `SmuHelpers::SelectChannelIfSupported` — now dynamic-casts to **both** `Keithley6482Adapter` and `KeithleySMUAdapter`. Previously only the 6482 path worked; on the 2602B the channel parameter was silently ignored, so SMU Configure with channel 1 selected would operate on whichever TSP channel was last left active (typically channel 2 after a prior run). This makes `SetVoltage`, `SetCurrent`, `SetCompliance`, `OutputOn`, and `OutputOff` honor the requested channel on both adapter families.
  - `SequenceEngine` registers sentinel `"psu"` and `"smu"` devices in `m_deviceToManagerMap` (mirrors the existing `"pneumatic"` sentinel for `RetractAll`); `IsDeviceConnected` returns true for the sentinel when any device of that type is registered.

- **SWIR poynt: mirror server tracking, live crop refresh, SAT charts** (11b0b50)
  - `PoyntBeamDetector` — dropped all rejection filters and intensity ranking; select beams purely by centroid `Y` (topmost → B0/RX, bottommost → B1/TX), mirroring poynt `server_v2_ui.py sorted_b[0]/sorted_b[-1]` exactly.
  - `SwirBeamWindow` — bottom beam-crop strip now included in the per-frame invalidate and the detection-thread repaint so the crops refresh at the live feed rate. `BeginPaint` was previously clipping the present to the image rect, leaving the strip stale.
  - Top-right history charts track saturated-pixel count (count of `==255` pixels) instead of peak intensity; relabeled `B0 SAT` / `B1 SAT`.
  - Silenced per-100-frame `[BeamDetect]` detect-time mean/p95 console spam (rolling histogram is still kept internally).

- **SWIR: add poynt beam-tracking mode (server_v2 port) + overlay/jog tweaks** (a5cc5fa)
  - New swappable `poynt` detection mode reusing shared `SwirBeamDetector` helpers — 4-corner background subtraction + `find_roi` + per-ROI d4sigma with no rejection filters. Slots assigned by deterministic per-frame `cy`-sort (no nearest-neighbour tracking). Selected via `usePoyntDetection` in `config/beam_detection.json` (nested `poynt` block); dispatch priority Halcon > Poynt > Blob > d4sigma.
  - Overlay — per-beam label now `B0 (RX)` / `B1 (TX)`, dropped AR/EE rows, kept PK, added SAT (count of saturated `==255` pixels). Stack labels in the same visual quadrant so they no longer overlap. d4sigma `RunBeamDetection` now also computes `saturatedPixelCount` and uses `>=` for the saturated flag (was always false).
  - UI — added Z-/Z+ 10µm jog buttons for `gantry-main` on the SWIR left panel.
  - Scope: live per-frame tracking only; the z-scan FIT path keeps using the Python beam_server over TCP.

## 2026-06-09

- **SwirBeam: route single-beam frames to B0/B1 by image region** (ba3f6bd)
  - Lone candidates now pick their slot from the centroid's vertical half (top → B0, bottom → B1) instead of nearest-prior, keeping overlay magnifiers, crop tiles, history charts, and the broker label consistent with "B0 is always the top beam". Swapped GA1/GA2 quadrant labels in `beam_detection.json` to match the new orientation.

- **RunProgramPage: add Dual-Hex Global Maximize routine to Scanning tab** (c463f56)
  - New "2-device maximize" feature coordinates both hexapods to maximize one shared signal. Collimating device steps `-Z` while a search device runs a centered NxN XY grid and moves to peak; iterates while session-peak signal improves >1%, then returns both devices to the session peak. Stop/error always restores both devices to the session peak and the hexapods' original system velocity.
  - `DualHexMaximizer` (`include/motions/scanning`) — Qt-free algorithm on a worker thread with halt flag, RAII velocity guard (1 mm/s during run), centered grid search, and averaged DataStore measurement.
  - `GlobalMaximizeExecutor` / `GlobalMaximizeController` / `GlobalMaximizeWidget` (`RunProgramPage`) — Qt UI section below "Hexapod Scanning" with channel, beam direction, Z step, grid step, grid size, and settle-time selectors; mirrors the existing scan stack.
  - Includes an in-progress `PlacementConfigEditWidget` for the Placement Correction Trainer and a CLAUDE.md docs refresh (trainer module list, F-key table).

## 2026-06-08

- **Placement Correction: fix U-angle correction** (01b757d)
  - Bug fix in the placement-angle math for the U rotation axis.

## 2026-05-31

- **QtCameraChartOverlay: gate per-overlay throughput diagnostics behind a runtime flag** (bedc91e)
  - Disables the per-overlay `std::cout` stats by default to keep the console quiet. Flag is intentionally non-const so the `std::cout` chain still compiles under `/W4` without C4127 and the stats locals stay referenced.

- **RunProgramPage: Snapshot quick-action button right of status badge** (bc8772d)
  - New green "Snap shot" button next to the Idle badge prompts for a label and dispatches `system/CaptureSnapshot` through a stack-local `SystemCommandHandler`, matching the Init DUT pattern. Also wires `StateSnapshotManager` into the Recipe page's `SequenceEngine` so recipe runs can capture snapshots via the same path.

---

## v1.13.0 — 2026-05-28

### Highlights

- **State Snapshot capture** — New `StateSnapshotManager` walks every reachable manager and writes a labeled JSON dump of motion positions, TEC/PSU/SMU/lighting readings, IO bits, pneumatic states, DataStore values, and the active DUT, plus a PNG from every grabbing camera. Three triggers share one capture path: a "Capture Snapshot" recipe step (`system/CaptureSnapshot`), `bobcat.snapshot(label, opts)` in Lua, and **Ctrl+Shift+S** in MainWindow. Output lives at `data/<product>/snapshots/<ts>_<label>.{json,png}` with an append-only `index.jsonl` summary. Subsystem failures fall into `skipped`/`errors` arrays rather than aborting.
- **RunProgramPage DUT controls** — New stacked Init/× Close button pair pinned left of the Idle status badge. Operator can `dut/LoadOrCreate` or `dut/Close` directly from the Run page; the active serial number is overlaid on the badge. Both actions go through `DUTCommandHandler` (same code path a recipe step takes).
- **Dispense traceability snapshot** — After camera nav back over the dispensed location, optionally captures `data/<product>/dispense_image/<UID>_dispense_<timestamp>.png` for visual review. Controlled by a new `capture_after_dispense` plugin checkbox (default on).
- **siphog-saa-uploader** — New standalone Python service uploads per-PCBA SAA data to a Google Sheet; OAuth refresh-token revocation self-heals automatically.
- **Thorlabs TTC-001 T-Cube TEC adapter** — Via Kinesis SDK. Adds sensor-mode-aware unit conversion (transducer / 20 kΩ / 200 kΩ thermistor) using `ThermistorBeta`/`R0`/`T0` fields on `TECDevice` to convert Kinesis device units to °C.
- **TEC recipe plugins** — `TEC Turn On` / `TEC Turn Off` first-class recipe steps.
- **Halcon LineDetect live Canny edge overlay** — Real-time Canny edge preview in the vision tuning wizard for line-detection profile authoring.
- **Camera / vision UX polish** — Per-row text fields + Apply buttons in the vision wizard, exposure cap raised 100ms → 1s in vision wizard and MoveToPosition editor, imprint-image capture during camera-to-needle calibration, replace blind `+1mm` with a taught final Z touch.
- **SWIR & trainer responsiveness** — `B DETECT` toggle no longer blocks the UI thread (async stop); OpenCV/Halcon thread pools clamped to 1 to cap SwirBeamWindow memory growth; trainer retrace widgets read cached positions instead of synchronously querying the controller on the UI thread.

### All Changes (by date)

## 2026-05-28

- **RunProgramPage: Init / Close DUT buttons stacked left of status badge**
  - `ExecutionStatusWidget` — `m_programStatusLabel` shrunk 250×150 → 250×75; new `m_initDutButton` (53 px, blue) + `m_closeDutButton` ("×", 20 px, red, hidden by default) stacked in a 70-wide column inside `headerRow`. `setDutSerial(QString)`/`dutSerial()` pair lets the page overlay the active SN on the badge.
  - `RunProgramPage::onInitDutClicked` — pops `QInputDialog` (pre-seeded with current SN), instantiates a stack-local `DUTCommandHandler(m_context)`, dispatches `dut/LoadOrCreate` with the `serialNumber` param (productName auto-resolves from `ProductPositionManager` inside the handler). On success, stores the SN on the widget, reveals the close button, and repaints the badge.
  - `RunProgramPage::onCloseDutClicked` — same single-command pattern but dispatches `dut/Close`; clears the SN overlay and re-hides the close button.
  - `applyProgramStatusLabel` now appends `\nSN: <serial>` to the badge text when a SN is set; font shrunk 18 → 16 px to fit two lines in the new 75 px-tall badge.

- **StateSnapshotManager — labeled machine-state snapshots** (b291eac)
  - New `include/snapshot/StateSnapshotManager.{h,cpp}` — `Capture(SnapshotOptions)` walks every manager in `ApplicationContext` and writes one JSON file + per-camera PNGs + appends a one-line summary to `index.jsonl`. Subsystem dumpers are all best-effort with try/catch; failures land in `root["skipped"]`/`root["errors"]` without aborting.
  - Per-subsystem dumpers — motion (cached positions when available), TEC (`TECStatus`), PSU (per-channel voltage/current/setpoint/output), SMU (`ReadAllChannels` for enabled channels), lighting (per-channel intensity/percent/mode/on), IO (raw masks + named pin maps), pneumatic (position + sensor states), DataStore (cap 500 keys), DUT (serial/product/created/entry count).
  - Camera frames — instantiates a temporary `CameraCommandHandler` and dispatches `CaptureToFile` per camera id; failures land in `skipped.cameras` rather than aborting the snapshot.
  - **System command** (`system/CaptureSnapshot`) — new `SysSnapshotCommands` family wired into `SystemCommandHandler`'s dispatch ladder. Auto-populates recipe metadata (`GetSequenceName`/`GetCurrentStepIndex`/`GetCurrentStepName`) from the live `SequenceContext`; writes result back as `snapshot.path`/`snapshot.label`/`snapshot.timestamp`/`snapshot.images_count`.
  - **Recipe plugin** — `CaptureSnapshotPlugin` (category "System") with label/tags/note/capture_image fields, auto-registered.
  - **Lua wrapper** — `bobcat.snapshot(label, opts)` (opts accepts `tags` as table or string, `note`, `capture_image`).
  - **UI hotkey** — `MainWindow` Ctrl+Shift+S pops `QInputDialog::getText` for a label and calls `StateSnapshotManager::Capture` directly (no recipe in the loop). Success/failure shown in a message box with the saved path.
  - **Wiring** — `StateSnapshotManager*` pointer added to `ApplicationContext`; new `SequenceEngine::SetStateSnapshotManager` forwarder mirrors the SWIR callback pattern.

- **Dispense: auto-capture traceability snapshot after dispense** (8ac13ba)
  - New `capture_after_dispense` plugin checkbox (default on). After camera navigation back over the dispensed location, the plugin emits a `CaptureToFile` for `main_camera` writing to `data/<product>/dispense_image/<UID>_dispense_<timestamp>.png` (flat layout — no date subfolder). `{timestamp}` placeholder substituted at execution time by `CameraCommandHandler`.

## 2026-05-27

- **siphog-saa-uploader: per-PCBA SAA data → Google Sheet** (a605293, f2aaacb)
  - New standalone Python service that watches per-PCBA SAA files and uploads results to a Google Sheet. OAuth refresh-token revocation/expiry now self-heals automatically — the uploader detects the failed token, deletes the stale credentials file, and re-prompts the user once instead of crashing on every poll.

- **DataBackup: change-detection off the UI thread** (fce1da5)
  - The auto-backup change detector (SHA-256 over product folders) now runs in a worker thread and only signals the UI when it finds a real delta. Previously the hash walk happened on the timer callback on the UI thread and could stutter the Run page during long-running backups.

- **TEC recipe plugins: Turn On / Turn Off** (0547a5e)
  - New first-class `TEC Turn On` / `TEC Turn Off` process plugins. Use these instead of raw `TECCommandHandler.TEC_On`/`TEC_Off` actions for visibility in the recipe browser.

- **LineDetect (Halcon): live Canny edge overlay for tuning** (d15d3a5)
  - Vision profile editor for Halcon line-detect now renders a real-time Canny edge preview overlay while you adjust the threshold/min-length/max-gap sliders. Makes parameter tuning visual instead of trial-and-error.

- **QtCameraVisionController: stop double-applying ROI** (c0a760f)
  - The vision controller was applying the wizard's ROI both before and after the plugin transform, producing offset detections for any profile that returned image-space coordinates.

- **Vision wizard: per-row text fields + Apply buttons for camera settings** (82bdf4c, 57b8360, df1b528)
  - Each tunable (exposure, gain, gamma, …) now has a discrete `QLineEdit` + `Apply` pair instead of a single shared editor. Exposure max raised from 100 ms → 1 s in both the vision wizard and the MoveToPosition editor's per-step exposure override.

- **CalibrateCameraToNeedleOffset: imprint capture + taught Z touch** (1c53594, 2e12fb3)
  - The calibration recipe now captures the needle imprint photo to a traceability folder for QA review. The final Z touch step replaces the previous blind `+1mm` jog with a manually taught Z value, eliminating the "wrong Z destroys the needle" failure mode when product height changes.

## 2026-05-22

- **Thorlabs TTC-001 T-Cube TEC adapter via Kinesis SDK** (df02018, 374f351)
  - New `TTC001Adapter` implementation of `ITECDevice` using Thorlabs Kinesis USB. Adapter is *sensor-mode-aware*: Kinesis `TC_SetTemperature` / `TC_GetTemperatureReading` return device units that depend on the configured sensor mode (transducer = °C, but 20 kΩ / 200 kΩ thermistor modes return raw counts). The adapter consults `TECDevice.SensorMode` + `ThermistorBeta` / `R0Ohms` / `T0Celsius` and converts via the beta equation so the public contract stays in °C.
  - Force-pushes sensor mode via `TC_SetSensorType` on `Connect()` because the Kinesis GUI's "Persist Settings to the Device" checkbox is rarely ticked — without this, devices boot in factory-default transducer mode and the adapter would misread raw thermistor counts.
  - No native 10 kΩ thermistor option in TTC-001 hardware; project standard is the 20 kΩ variant.

## 2026-05-20

- **Clamp OpenCV/Halcon thread pools to 1** (05e583e)
  - Both libraries default to one thread per logical core, and SwirBeamWindow's high-rate beam-detection pipeline was spinning up large pools per call. Memory growth from per-thread arena caches now bounded to a single worker each.

- **SwirBeamWindow: async-stop beam detection** (1e05d3e)
  - The `B DETECT` toggle no longer waits on the in-flight Halcon detection to finish before returning; the stop request is posted and the UI returns immediately. Previously the toggle could freeze the SWIR window for several seconds when D4σ + saturation guard were both enabled.

- **Trainer retrace widgets: cached position reads on the UI thread** (c3fdbca)
  - `PlacementRetraceWidget` and friends now call `GetCachedPosition` (populated by the background poller) instead of `GetCurrentPosition` (synchronous controller round-trip). Eliminates 100–300 ms UI hitches during retrace workflows when motion controllers are busy.

---

## v1.8.0 — 2026-04-14

### Highlights

- **Placement Correction v3 — Normal-Placement Model** — Redesigned retrace to re-measure a new *Normal Placement* line (where the lens physically lands) instead of the ideal Target line, using a cached *design offset* rotated by the reference-frame Δθ. Adds a 3rd teach widget, new JSON field `normalPlacementLine`, cached `designOffsetX/Y`, and file schema v3 (backward-compat with v1/v2 — retrace stays disabled until normal line is taught).
- **Retrace Audit Log** — Every retrace run (manual / autoRunAll / recipe) appends a JSONL entry to `{productDir}/placement_correction/{setName}.retrace_log.jsonl` with retraced points, frame Δθ, design offset, correction vectors, live hex, corrected pose, tune offsets, per-step metadata, and outcome (saved/skipped/ended/failed). Rotates at 10 MB.
- **Retrace Log Viewer** — New read-only workflow tab with color-coded outcome table, JSON detail pane, Refresh/Open-in-Explorer buttons.
- **Auto-Disable U Correction at Large Δθ** — If retraced frame rotation exceeds 3.0°, applyUCorrection is auto-unchecked with a visible ⚠ warning in the review text. Operator must manually re-enable after verifying reference points.
- **Recipe Interlock** — `SequenceEngine::IsExecuting()` exposes thread-safe execution state. Trainer Move buttons refuse to start when a recipe is running; also prevents double-click races.
- **Async Safe-Move Workers** — All trainer-initiated moves (hex Move buttons on Positions tab; gantry Auto-Nav in teach widgets) now run off the UI thread with SetSystemVelocity → MoveToAbs → restore velocity. Indeterminate progress bar + red Stop (EmergencyStop) while moving.
- **UI Tabs for Left Panel** — Device / Positions / Camera grouped into a QTabWidget with fixed-width, top-left-aligned fields.
- **Teach Widget Enhancements** — Per-point vision profile selector + "Detect & Center" (single-shot world-position centering); Auto-Nav to previously-captured P1/P2 (disabled until first capture); instruction label sized to fit wrapped text.
- **Save vs Save As** — Save overwrites the loaded set silently; Save As opens the rename dialog. Labels "Target Position" (tooltip: *first light position*) and "Position after correction".

### All Changes (by date)

## 2026-05-19

- **Add UNI-T UDP3305S-E 3-channel PSU adapter (USB / NI-VISA)**
  - **New driver** (`include/PSU/UnitTUDP3305S.{h,cpp}`): `IPowerSupply` implementation for the UNI-T UDP3305S-E 3-channel programmable DC PSU over USB/USBTMC + NI-VISA. Mutex-guarded VISA session, RAII session close in destructor, force-off of all 3 channels on `disconnect()`, `[[nodiscard]]` on bool setters, channel validation (1..3). Public `query()` escape hatch for raw SCPI used by the control page.
  - **Verified SCPI on hardware** (per `docs/UDP3305S/test_channels.py`): `*IDN?`, `APPLy CH<n>,<v>,<i>`, `SOURce<n>:VOLTage?`, `SOURce<n>:CURRent?`, `OUTPut CH<n>,ON/OFF`, `OUTPut? CH<n>` (parses both `ON`/`OFF` string form and `1`/`0` numeric form per device quirk), `MEASure:VOLTage? CH<n>`, `MEASure:CURRent? CH<n>`.
  - **Unverified SCPI** (logged with `(unverified SCPI)` marker for grep): individual `setVoltage` / `setCurrent` via `SOURce<n>:VOLTage <v>` / `SOURce<n>:CURRent <i>`; `setOVP` / `setOCP` via `SOURce<n>:VOLTage:PROTection` / `SOURce<n>:CURRent:PROTection`. Set-point readbacks for OVP/OCP use the same family `:PROTection?` form.
  - **Loader** (`include/PSU/PowerSupplyJsonLoader.{h,cpp}`): factory branch for `type: "unit_t_udp3305s"`; widened `createPowerSupply()` to accept a `visaResource` parameter; loader now accepts either `ip` (TCP-shaped types) or `visa_resource` (USB/VISA types) as device addressing. Header included unconditionally, matching the `SMUJsonLoader` pattern (project assumes VISA is always present at build time).
  - **Control page** (`include/AppUI/PowerSupplyControlPage/PowerSupplyControlPage.cpp`): third `dynamic_cast` branch in `onReadStatusClicked` so the "Read Status" button queries `SOURce<n>:VOLTage:PROTection?` / `SOURce<n>:CURRent:PROTection?` on the UDP3305S (Keysight branches unchanged).
  - **Build** (`CMakeLists.txt`): new `if(VISA_FOUND)` block for VISA-based PSU adapters (right after the SMU block at L1435) listing `UnitTUDP3305S.{cpp,h}`. Reuses the existing NI-VISA link from L1724; no new SDK detection.
  - **Config docs** (`config/power_supply_config_examples.md`): documented the new `unit_t_udp3305s` type, USB/VISA addressing scheme (resource string format, STMicro VID `0x0483` quirk), `visa_resource` parameter, and recommendation to put per-machine entries in `power_supply_config.local.json` (gitignored).
  - **Reference docs** (`docs/UDP3305S/`): handoff brief with verified SCPI table + acceptance criteria, Python repro scripts (`check_connection.py`, `test_channels.py`) that exercise all 3 channels at safe 1 V / 0.1 A test points, and the device user manual PDF.
  - **Manager / handler / plugins**: no code changes — `PowerSupplyManager`, `PSUCommandHandler`, and the recipe `PSU*` plugins were already device-agnostic and route by name through `IPowerSupply`. Existing E-stop cascade (`PowerSupplyManager::disableAllOutputs()`) covers the new device automatically.

- **Roll correction: power-guarded micro-steps + extract `RollCorrectionController`**
  - **Power-loss fix** (`RollCorrectionController::onCorrectWRequested`): the W rotation is now split into micro-steps of ≤ 0.02° (was ≤ 0.085° sub-steps). Around each micro-step the worker samples the tracking channel's optical power before and after the rotation; a > 10% drop vs. the pre-step rolling baseline triggers a 0.5 µm XY `XCANExtended` regain scan. Previously a single large W step dropped power into the background-noise floor, leaving the re-peak scan no gradient to climb. If a regain scan runs but power stays below threshold, it logs a warning and continues; a hard scan-command failure still aborts.
  - **Refactor**: roll-correction business logic (8 slots, worker threads, `SequenceContext` stop flags, running state) moved out of `RunProgramPage` into new `RollCorrectionController.{h,cpp}` — mirrors the `ScanController` + `ScanControlWidget` split. `RunProgramPage` loses ~340 lines; it now constructs the controller with a `Deps` struct (`context`, `widget`, `appendLog` callback) and keeps only the `RollCorrectionWidget` pointer (tab body + DataStore channel feed). `RollCorrectionWidget` stays pure UI.
  - **Warnings**: fixed C4099 `class`/`struct` mismatch on `ApplicationContext` forward declarations in `ScanController.h`, `ExecutionController.h`, `ProgramEditPanel.h`, `UserInteractionOverlay.h`, and the new `RollCorrectionController.h`.
  - **Docs**: `docs/architecture/run_program_page.md` — helper table updated, "Roll Correction" section rewritten to document the widget/controller split and the micro-step algorithm.

## 2026-04-14

- **placement correction v3: normal-placement retrace model + retrace log + UI polish**
  - **Data model** (`PlacementCorrectionData.h`): added `PlacementLinePoints normalPlacementLine`, cached scalars `designOffsetX/Y/designOffsetValid`, new `normalAngleDeg`; `Recalculate()` now handles all three lines and computes `designOffset = target_midpoint − normal_midpoint`; new math helper `Rotate2D(dx, dy, angleDeg, outDx, outDy)`
  - **Store** (`PlacementCorrectionStore.cpp`): serialize/load `normalPlacementLine`; bumped `CURRENT_FILE_VERSION` → **3** (v1/v2 files load with `normalPlacementLine.valid=false`; retrace guard short-circuits until taught); load log prints `nrm=` and `designOffset=` status
  - **Teach widget integration** (`PlacementCorrectionPage.{h,cpp}`): new 3rd `TeachPlacementLineWidget("Normal", …)` at stack index 3, wired to `m_currentSet.normalPlacementLine` via `lineCaptured`; workflow nav renumbered 1..9 with **"3. Teach Normal Placement Line"** inserted; `SetLine()` routing restores normal line on load
  - **Retrace widget v3 rewrite** (`PlacementRetraceWidget.{h,cpp}`): mass-rename `Tgt/tgt` → `Nrm/nrm` across states/slots/members/labels; steps 3/4 re-measure `normalPlacementLine` (not `targetLine`); `onStartRetrace` caches `designOffsetX/Y`; **`computeRetraceResults` replaced**: `Δθ = retracedRefAngle − taughtRefAngle`, `rotatedOffset = Rotate2D(designOffset, Δθ)`, `correctionGlobal = rotatedOffset`, transform to device-local via matrix, apply to live hex with `correctedU += Δθ` (if applyU); Idle summary shows per-point profile status (RefP1/P2, NrmP1/P2); if ready=false, enumerates which exact field is missing
  - **AutoStart preflight guard**: if `designOffsetValid/referenceLine.valid/normalPlacementLine.valid` is false, immediately emit `retraceEnded` so recipe `QEventLoop` exits instead of hanging indefinitely on pre-v3 sets
  - **Auto-disable U correction at |Δθ| > 3°**: `computeRetraceResults` unchecks the UI checkbox (signal-blocked to avoid recompute) and sets `applyUCorrection=false`; review text shows ⚠ warning with threshold value and guidance to verify retraced reference
  - **`PlacementRetraceLogger` (new)**: JSONL append writer at `{productDir}/placement_correction/{setName}.retrace_log.jsonl`; `RetraceLogEntry` + `RetracePointMeta` structs capture timestamp, set identity, mode (`manual`/`autoRunAll`/`recipe`), outcome (`saved`/`skipped`/`ended`/`failed`), retraced XYZ pts + angles, taught/retraced ref angles, `frameDeltaTheta`, taught+rotated design offsets, `idealTarget`, correction global+local, live hex XYZUVW, tune offsets, corrected pose, per-step meta (`autoDetected`/`visionIters`/`finalResidualMm`/`reason`), optional `failureStep`/`failureDetail`; 10 MB rotation → `.jsonl.1`
  - **Logger wiring in retrace widget**: `SetMode(mode)` sets the log-line mode; `onAutoRunAll` auto-upgrades to `"autoRunAll"` unless already `"recipe"`; per-step meta stamped in `runAutoDetect` success branch and in each `onConfirm*` slot for manual fallback; `computeRetraceResults` stashes intermediates (`m_logFrameDeltaTheta`, `m_logRotatedOffsetX/Y`, `m_logIdealTargetX/Y`, `m_logCorrGlobal*`, `m_logCorrLocal*`) for the log entry; `writeLogEntry(outcome, failureStep, failureDetail)` builds + appends; call sites: `onSaveToTarget("saved")`, `onSkip("skipped")`, `onEnd("ended")`, AutoStart-refused `("failed", "preflight", …)`; `m_loggedThisRun` flag suppresses duplicate on save→continue
  - **`RetraceLogViewer` (new)**: read-only widget at stack index 9 ("9. View Retrace Log" nav); color-coded QTableWidget (most recent first) with Timestamp/Outcome/Mode/Δθ/Corr dX/Corr dY/Corrected X/Corrected Y; detail pane shows selected entry's pretty JSON; Refresh + Open-in-Explorer buttons; `SetData(set)` resolves log path via `PlacementRetraceLogger::ResolvePathForSet` and reloads
  - **Recipe interlock**: `SequenceEngine::IsExecuting()` — new thread-safe `std::atomic<bool>` set via RAII `ExecGuard` in `ExecuteSequence` so it clears on any return path; `RunProgramPage::SetMode("recipe")` called before `SetData` so recipe-run retraces log with the correct mode
  - **Tune-offset mid-recipe guard** (`RunProgramPage.cpp`): `offsetsPersistRequested` lambda now skips the disk save if `m_isExecuting`; logs "applied in-memory; disk save deferred" to the recipe log pane. Prevents a live-edited tune offset from overwriting the set JSON that a later recipe step might load.
  - **Async safe-move workers**: `PlacementCorrectionPage::startSafeMoveAsync` (hex Move buttons) lowers velocity → `MoveToAbs6` (coordinated 6-axis, was XYZ-only single-axis `MoveToAbs`) → restore velocity; disables all 3 Move buttons during; red **Stop** triggers `EmergencyStop(device)`; indeterminate `QProgressBar` during move. `TeachPlacementLineWidget::startAutoNavAsync` (gantry Auto-Nav to taught P1/P2) mirrors the pattern with 10 mm/s safe velocity and 50 mm/s restore; Auto-Nav buttons default-disabled until `m_line.valid`; interlocked against `SequenceEngine::IsExecuting()`
  - **Left panel tabs**: `QTabWidget` with **Device** / **Positions** / **Camera** tabs replaces the stacked group boxes; fixed-width combos/spinboxes + `addStretch` for top-left alignment; "Master Position (baseline)" relabeled **"Target Position"** (tooltip: *first light position*); "Target Position (corrected)" relabeled **"Position after correction"**; new **Normal Placement Position** combo (third Move row) backed by `PlacementCorrectionConfig::normalPositionName`
  - **Save / Save As split**: Save overwrites the currently-loaded set silently (falls through to Save As if no `sourceFilename`); Save As always opens the rename dialog; Save As button added next to Save
  - **Teach widget polish**: per-point `QComboBox` + **Refresh** for vision profile (written to `pNVisionProfilePath` on selection change, emits `lineCaptured` if line valid); **Detect & Center** (purple, single-shot — runs profile, MoveToAbs to detected world XY keeping Z); **Auto-Nav to P1 / P2** (green, disabled until first capture); instruction label min-height + `MinimumExpanding` vertical policy so wrapped text isn't clipped
  - **Vision-no-match guard in detect paths** (`TeachPlacementLineWidget::runDetectAndCenter` and `PlacementRetraceWidget::runAutoDetect`): check `SequenceContext::HasValue("vision.detected.world_x_mm"/"_y_mm")` before trusting world coords — prevents `MoveToAbs(0, 0, Z)` when Halcon pattern match returns `success=true` with no anchor/corner/center
  - **Stop button on retrace wait pages**: red Stop beside each Confirm button on Ref P1/P2 and Nrm P1/P2 pages; calls `onEnd` → `retraceEnded`
  - **Retrace Idle page buttons**: Start Retrace fixed 180×44 + Auto-Run All 4 (currently hidden via `setVisible(false)` until feature matures); both left-aligned via `addStretch(1)`
  - **Docs**: new `docs/placement_correction.md` — full v3 model explanation (three-line design, master-pose correlation, 6-state wizard, computeRetraceResults math, edge cases, JSON schema, source-file map) with a worked example using `VISION LEFT FIRST LIGHT.json`



### Highlights

- **Sub-Recipe System** — Recipes can now inline other recipes as sub-steps, with folder-based categorization, drag-drop organization, and usage-aware deletion
- **Config Backup & Restore** — One-click backup of all config/SYSTEM/product folders with timestamped restore points
- **Lighting System Overhaul** — Grouped channels, two-column UI, improved sequence plugins
- **Motion Page Redesign** — Isometric 3D viewport with CCW/CW rotation, graph edge visualization
- **PSU & SMU Page Redesign** — Per-channel UI template, multi-channel SCPI commands, external connection detection
- **Placement Correction Trainer** — Teach reference/target line angles, compute hex U-axis correction, retrace validation, recipe-executable step
- **Roll Correction Widget** — Dual-channel peak measurement, arctan-based W-axis correction, fine grid scan with background stop control
- **Dual-Channel Grid Scan** — GridScanDualChannel plugin with data logging, SI formatting, and delta tracking
- **Motion Interlock System** — Position-based safety rules (up to 3), configurable operators, persisted to interlocks.json
- **Beam Detection Pipeline** — SWIR beam processing, encircled energy (EE) metric, D4sigma detection (Halcon), beam focus optimizer, CE/EE charts
- **Qt Camera Monitor** — Qt-based camera window with side panel, window persistence, dual instances, click-to-move calibration
- **Accuracy Trainer Enhancements** — SQLite corner logging, unique point UIDs, reference image crop+crosshair, vision profile detect & center, Z offset trainer, yaw trainer, 2-3 corner hex teach
- **Safety Infrastructure (MISRA Phases 1-5)** — Bounded loops, [[nodiscard]], /W4, SocketGuard RAII, exception removal, unified ErrorCode enum, SafetyEventLog, thread priority, E-stop cascade, command timeout watchdog, concurrency hardening, health monitor, file rotation

### All Changes (by date)

## 2026-04-14

- **fix VisionCommandHandler reporting success on no-match**
  - Halcon pattern-match plugins (Gray, NCC, Edge, Descriptor) intentionally return `num_matches=0` with `success=true` on no-match; this caused `execute_profile` to leave `vision.result.success=true` while skipping `world_x_mm`/`world_y_mm`, so callers reading defaults saw `(0, 0)` and downstream consumers (e.g. `PlacementRetraceWidget::runAutoDetect`) could issue `MoveToAbs(0, 0, currentZ)` — a dangerous garbage move
  - Added `ApplyNoMatchGuardToContext()` helper in `VisionCommandHandler.cpp` (anonymous namespace) — flips `vision.result.success=false` and sets `vision.result.error` when either `vision.result.num_matches == 0` or none of `match_0_anchor_{x,y}`/`corner_{x,y}`/`center_{x,y}` keys are present
  - Guard wired into `Execute()` right after `StoreResultInContext`, covering all three command paths (`execute`, `capture_and_execute`, `execute_profile`) via the common `CaptureAndExecute` → `Execute` chain
  - `Execute()` now returns `false` when the guard flips success, aligning Halcon behavior with the OpenCV plugin convention where no-match is a dispatch-level failure; `execute_profile` still returns `true` from `ExecuteCommand` per the existing "dispatch OK" pattern, but context state is now correct
  - `capture_as` prefix copies propagate the corrected `success=false`/`error` automatically via the existing `vision.*` → `<prefix>.*` copy loop
  - Removed the now-redundant inline success-flip in the `calculate_world_position` / `!foundReference` branch; kept the diagnostic `⚠️ No reference point found` logs

## 2026-04-11

- **runtime theme switcher with CC-29 palette and live reload**
  - New `Theme::ThemeId` enum (`Default`, `CC29`) and `Theme::applyPalette()` in `Theme.h` — bulk-reassigns all `Color::*` pointers to the chosen palette at runtime (constexpr dropped to mutable `inline const char*`)
  - Added CC-29 palette (`Palette::CC29`) — 23 colors mapped to semantic roles: navy/purple backgrounds, cream text, cyan primary, sage green success, tan warning, coral danger, pink/mint accents; background `#212123`
  - New `ThemeManager` QObject singleton (`include/AppUI/ThemeManager.h/.cpp`) — `setTheme()` reassigns palette pointers, re-applies global QSS via `qApp->setStyleSheet(DarkTheme::getStyleSheet())`, emits `themeChanged()` signal
  - `RecipePageStyles::Colors::*` converted from `const QString` to `inline QString()` functions so they return fresh palette values after a theme switch (3 consumer files updated)
  - New `UI/Theme` combo setting in Application Settings page (`Default (Dark)` / `CC-29 (Retro)`) — uses existing combo infrastructure, no new UI code
  - Boot-time palette read in `Application.cpp` — reads `QSettings("UI/Theme")` before first `setStyleSheet()` so the initial render uses the saved theme
  - Live reload wiring in `MainWindow.cpp` — `settingsChanged` → lambda reads QSettings → `ThemeManager::setTheme()` → `themeChanged()` → widget `applyTheme()` slots
  - `applyTheme()` public slots added to `TopBar` (10 styled elements), `RunProgramPage` (status badge, buttons, progress bar), `RecipePage` (tabs, tree, bezel buttons, vision group, recipe name edit) — re-apply local QSS from current `Theme::Color::*` values

- **compact settings page property editor**
  - Value editor field resized from full-width/tall to compact 420px-wide fixed-height with top-left alignment
  - Section badge (`m_propSectionLabel`) now `Maximum/Fixed` size policy — shrinks to text content
  - `QStackedWidget` vertical policy set to `Fixed`, wrapped in `QHBoxLayout` with trailing stretch for flush-left alignment
  - All 4 editor widgets (LineEdit, SpinBox, CheckBox, ComboBox) constrained to `kEditorMaxWidth`
  - Property panel root layout set to `AlignTop | AlignLeft`

- **Theme palette system and Phase 2 migration**
  - New `Theme.h` — header-only `Theme::Palette::*` (raw hex constants) + `Theme::Color::*` (semantic role aliases) single source of truth for the entire app's color scheme
  - New `ThemeButtonStyles.h` — `Theme::Buttons::bezel()` builder + 5 convenience variants (`bezelPrimary/Success/Warning/Danger/Neutral`) + `Theme::Boxes::groupBoxDark()`; eliminates ~30-line duplicated bezel QSS at every call site
  - `DarkTheme.cpp` rewritten to use `__PLACEHOLDER__` template substituted from `Theme::Color::*` at runtime
  - Phase 2 migration of TopBar (5 buttons + labels), RunProgramPage execution view + Edit Program subpage, RollCorrectionWidget (all buttons + labels), RecipePage + RecipePageStyles.h — ~550 lines of duplicated QSS removed
  - `RecipePageStyles.h` Colors:: layer rebound to `Theme::Color::*` references

- **scan-busy indicator mirrored to RunProgramPage status badge**
  - Wired `ScanActivityTracker::activeChanged` to RunProgramPage execution badge via `QueuedConnection`; shows amber `SCANNING` overlay while any scan is active, reverts to base state (Idle/Running/Stopped/Loop) when scan ends
  - Status badge resized to 250x150; `setProgramStatus()`/`applyProgramStatusLabel()` helpers centralize the 5 status set-sites

- **scan-busy indicator in top bar, Scanning tab cleanup, grid-scan log demotion**
  - New `ScanActivityTracker` QObject (`include/common/`) — thread-safe, reference-counted broadcaster with `activeChanged(bool)` signal; scan operations register via RAII `ScanActivityGuard` so every return path (success, early failure, user stop) correctly pairs Begin/End
  - `⚡ SCANNING` amber badge added to TopBar next to the safety LED; hidden when idle, visible whenever any scan is in progress (tab-initiated OR recipe-initiated), driven via `Qt::QueuedConnection` for cross-thread safety
  - `MotionCommandHandler` — RAII guards at all 5 scan command entry points: `XCAN`, `XCANExtended`, `GridScan`, `GridScanExtended`, `GridScanDualChannel`; covers recipe-driven grid scans
  - `ScanExecutor` — flag-guarded Begin/End around `executeScan()` + `executeRotationScan()` with pairing in `cleanup()`/`cleanupRotation()` so async completion and stop paths clear correctly
  - `ApplicationContext.scanActivityTracker` field; instance owned by `Application`, created after settings load but before SequenceEngine init so MotionCommandHandler can be wired at registration time
  - Scanning tab (`ScanControlWidget`): removed the "Rotation Optimization" group entirely (~200 LOC — device/axis/step/threshold/Start Rotation Scan button); `RotationScanController` and `ScanExecutor::executeRotationScan` left in place but no longer reachable from this UI
  - Scanning tab compact layout: Hexapod Scanning group top-left anchored via `QSizePolicy::Fixed` + trailing horizontal/vertical stretches; all 6 Coarse/Fine/Ultra Fine buttons `setFixedSize(140, 26)`; STOP button `setFixedSize(140, 90)`; Data Channel combo `setFixedWidth(320)`; big value label `setFixedWidth(500)`
  - Grid-scan log noise reduction (19 sites): demoted per-point "New peak", per-20-points "Progress", context-capture echoes, and redundant "Scan completed successfully" trailers across XCAN/XCANExtended/GridScan/GridScanExtended/GridScanDualChannel; KEPT at INFO: scan start, peak-value callbacks, returning-to-peak, final summary, exit threshold reached, stop requested

- `8022a4a` — **add Logging.DebugEnabled application setting to suppress verbose developer logs**
  - New `[Logging] DebugEnabled` bool in `config/system/applicationSettings.ini`, default `false` (quiet operator mode); editable via Application Settings → Logging section with a bool toggle
  - `LogManager::SetMinimumLevel` moved out of header to sync both `m_minimumLevel` and spdlog's own level — fixes latent bug where spdlog's hard-coded info level silently dropped DEBUG messages even when `m_minimumLevel` allowed them
  - `LogManager` constructor default minimum level changed from `DEBUG` to `INFO`
  - Fixed 3 `MotionCommandHandler` messages literally prefixed `"[DEBUG]"` but emitted via `m_logger->Info` — stripped the prefix and demoted the calls
  - Demoted ~80 call sites from INFO to DEBUG across `SequenceEngine` (22 "Registered X handler" startup lines + `⚙️ Executing`, `Custom timeout`, `Building device registry`), `MotionCommandHandler` (param-resolution echoes, `WaitForMotion` diagnostics, `ParallelMove` per-device progress), `ACSController`/`PIController`/`UnifiedMotionManager` (init/shutdown, thread start/stop, per-axis velocity echoes, `MoveToAbs`/`MoveRelative` routing), `SystemCommandHandler` (`Waiting for Xms` / `Wait completed` pairs), `IOCommandHandler` (pin-name fallback, `Delaying`), `DUTCommandHandler` (product-name loads), `CameraManager` (exposure setting echoes), `AdaptiveScanController` (ctor/dtor), `JogPanelWindow` (device-not-connected, panel-refreshed, applied-exposure)
  - Outcome: trivial 8-step recipe log dropped from ~60 lines to ~10 operator-meaningful lines when the setting is off; scans no longer flood the log with 100+ per-iteration move traces

- **placement correction manual offset tuner on review and view-results pages**
  - Added Manual Offset Tuner group box to PlacementRetraceWidget (Review state): nudge the corrected hex target position by XYZ (mm) and UVW (°) before saving; offsets are applied on top of the computed retrace correction and written to `motor_devices.local.json`
  - Added "Re-save Target Position" tuner workflow to PlacementResultWidget (View Results state): adjust an already-saved target position by XYZ/UVW offsets and re-persist without requiring a full retrace cycle
  - New persisted config fields in `PlacementCorrectionConfig`: `tuneOffsetX/Y/Z` (mm) and `tuneOffsetU/V/W` (°), serialized in PlacementCorrectionStore so offsets survive session restarts
  - New signals `PlacementRetraceWidget::offsetsPersistRequested` and `PlacementResultWidget::targetResaved` wired in both PlacementCorrectionPage (F8 trainer flow) and RunProgramPage (recipe execution flow)
  - `RunProgramPage` gains `m_activePlacementStoreBasePath` member so offsets tuned during a recipe run are saved to the correct per-product placement_correction directory
  - UI convention: tuner group boxes use `setFixedWidth` + `Qt::AlignLeft | Qt::AlignTop` layout; spinboxes are fixed width; range ±1, step 0.001, 3 decimals

## 2026-04-09

- **stepwise roll W correction with channel tracking and per-substep XCANExtended re-peak**
  - Added "Tracking Channel" dropdown to RollCorrectionWidget; populated from DataStore via existing periodic refresh + one-shot populate at construction
  - Correct W button now requires both CH1/CH2 registration AND a selected tracking channel
  - Replaced single-shot W rotation with stepwise procedure: split total W into N sub-steps no larger than 0.085 deg each (hard cap 0.1 deg), e.g. 0.3 deg → 4 × 0.075 deg
  - Velocity reduced from 1.0 to 0.1 mm/s for all correction moves
  - After every W sub-step, runs XCANExtended (axes=XY, step=0.5 µm, settling=300 ms) on the selected channel to re-peak optical power before applying the next sub-step
  - Background-threaded with SequenceContext stop control; STOP Scan button reused to abort mid-correction (halts before next W sub-step or scan)
  - Original system velocity restored on every exit path including failure and stop

## 2026-04-07

- **uprev to v1.7: main menu redesign, changelog viewer, embedded icons, product image**
  - Version bumped from 1.6 to 1.7 with build number reset
  - Added in-app Changelog button next to version title, opens scrollable dialog with CHANGELOG.md
  - Main menu buttons reorganized to 4-column x 2-row grid layout with F1-F7 shortcut tooltips
  - Embedded menu icons via Qt resource system (.qrc) — no runtime file dependency
  - New icons: RunProgramPage.png (Process), accuracy_trainer.png, visionmanager.png, logo.png (app icon)
  - Application window icon set to logo.png via QApplication::setWindowIcon
  - Product image placeholder (250x250): auto-loads product.png from active product folder
  - Removed Delete Product button from main menu

## 2026-04-05

- `fb97882` — **add sub-recipe plugin, recipe folder categories, and recipe management UI**
  - Sub-Recipe plugin: inlines another recipe's steps at build time via ProcessSequenceFactory
  - Recipe list refactored from flat QListWidget to categorized QTreeWidget with physical subfolders
  - Drag-drop moves recipe files with confirmation warning about program references
  - Action buttons: create/remove folder, copy recipe, delete with usage checking (scans programs and sub-recipe references)
  - Recipe selector widget (addRecipeSelector) with recursive FileSelectorDialog for subfolder support
  - Edit Program table: added File column showing recipe filename alongside display name
  - Recipe name field now shows filename for consistency

- `9538f0e` — **add config backup & restore feature to settings page**
  - ConfigBackupManager module for timestamped backups of config root, SYSTEM, and product folders
  - Backup destination user-configurable via QFileDialog, persisted in QSettings across restarts
  - Restore dialog lists available backups with scope selection and delete option
  - New Backup section in application settings tree

- `f5f40dd` — **overhaul lighting system: grouped channels, two-column UI, improved sequence plugins**

## 2026-04-04

- `239c3ef` — **fix CCW/CW buttons to rotate viewport around screen center instead of camera azimuth**

## 2026-04-03

- `d60923a` — **redesign motion page: isometric 3D view, UI layout improvements, graph edge visualization**
- `033b135` — **redesign PSU and SMU control pages with per-channel UI template**
- `0c2dc4f` — add documentation comments to placement correction trainer and sequence plugin
- `8b3d333` — add test server v08
- `8aec14f` — **add axis scan plugin, recipe filter, edit-recipe navigation, and multiple bug fixes**

## 2026-04-02

- `a518c90` — fix shutdown hang caused by AuxViewWindow refusing to close
- `9426f76` — add placement correction retrace as recipe-executable step in RunProgramPage
- `6526269` — add shutdown confirm dialog, app icon update, camera monitor geometry persistence, and aux window close fix
- `85a87ee` — add fine grid scan buttons and stop scan to roll correction tab
- `692bd8a` — add grid scan file logging helper, dual-channel delta tracking, and user stop handling
- `f20a93f` — add roll correction tab with move-to-peak and W correction

## 2026-04-01

- `f87a359` — **add dual-channel grid scan plugin, grid scan data logging, SI formatting, and log level tuning**
- `2916ed6` — **add placement correction trainer, recipe file rename, scan timeout increase, and quick move grid expansion**

## 2026-03-31

- `1e5899b` — **add motion interlock system, device-colored UI, and velocity presets**
- `47ecf71` — add jog step sizes JSON config with local-first loading and per-device colors
- `4123fde` — update jog panel 0.2u, 0.3u and UI 3-column button layout
- `d73a54c` — add relative move shortcut in Qt window
- `d5db4ef` — fix bug dispense autosave navigation file

## 2026-03-30

- `db32b9a` — update plugin and trainer for auto navigate
- `4973d76` — fix XYZ reteach

## 2026-03-28

- `05f9f19` — add window persistence and second Qt camera monitor instance
- `d601dc3` — add Qt-based camera monitor window with side panel controls
- `245c6f9` — extract GDI camera window overlays into standalone files
- `9ed811e` — add FPS overlay to GDI camera window, set default camera settings (10fps, auto gain OFF)
- `318df7f` — add blob detection mode

## 2026-03-25

- `264a073` — add Ultra Fine (0.5/0.2/0.1 um) scan mode to hexapod scanning
- `10e0723` — fix 60s timeout issue in scanning

## 2026-03-20

- `b0e7d42` — change total slot limit to 20 from 5

## 2026-03-19

- `1db3624` — **add SQLite logging for accuracy trainer corners, unique point UIDs, and reference image crop+crosshair**

## 2026-03-17

- `b34b1b2` — split detection into single-frame scoring and EMA-smoothed display
- `ac9efd8` — refactor beam optimizer, add TEC controls, improve beam detection
- `725f00a` — add beam focus optimizer, CE/EE charts, and 3-column tools layout
- `797c109` — add pneumatic RetractAll command and Slide Retract All recipe plugin
- `f462b7d` — improve beam detection stability and PSU current tracking

## 2026-03-15

- `c2069f9` — fix splash screen hang
- `a876850` — add encircled energy (EE) metric to beam detection pipeline
- `24593ad` — create placeholder for beam detection, implement beam detection

## 2026-03-14

- `241251c` — add camera calibration fields to camera_config.json for config_editor

## 2026-03-13

- `cb8c51d` — add auto focus in jog
- `9a75c35` — **add and test Yaw trainer**

## 2026-03-12

- `1b2cc4b` — add trainer documents
- `391c23c` — **add Z Offset Trainer with global-to-local Z correction**
- `dc7e646` — add channel selection to SMU plugins, fix Save As duplicate IDs, remove copy suffix

## 2026-03-10

- `dea77b8` — add accuracy mode
- `ebb17ff` — fix bug 60 sec timeout for plugins waiting user interaction

## 2026-03-06

- `a056f4b` — fix bug 60 sec wait timeout

## 2026-03-05

- `2326d04` — apply 2-3 corner teach for hex devices for positioning instead of single point
- `f627106` — add planar adjustment

## 2026-03-04

- `eaefc7c` — add open config button
- `a1e1991` — adjust safety guard polling and update jog UI

## 2026-03-03

- `9ae0e17` — fix custom server for filtering noise beams parameters
- `f6420ec` — add pneumatic camera offset

## 2026-03-02

- `762453b` — fix GDI camera window menu responsiveness and fix UI error on SMU page

## 2026-02-25

- `66292fe` — **add calibration cam offset trainer and plugin for move to high/low mag positions**

## 2026-02-24

- `13885d0` — update UI trainer, fix issue closing main app but GDI window still active, update real-world measurement

## 2026-02-23

- `cdb970b` — update click-to-move to correct mm/pixel value automatically with different image sizes

## 2026-02-21

- `49ed466` — add 2nd GDI and inject BS2 beam stream into sequence context
- `a997616` — enable 2 realtime camera monitor windows to support beam stream
- `1018dd8` — fix inpos logic for ACS controller

## 2026-02-20

- `07132be` — fix save template to autosave when working PR matching, add accuracy save file UID
- `0320ad7` — merge Accuracy Trainer

## 2026-02-19

- `3332577` — camera settle time 500ms, remove excess delay 500ms per axis in ACS wait for motion complete
- `45b6bcb` — **add accuracy trainer: taught position of reference, taught bonding position, apply manual or vision alignment**

## 2026-02-18

- `fcf835c` — update UI and GDI camera beam streaming optimize

## 2026-02-14

- `af4baee` — add beam server analyzer

## 2026-02-13

- `62c9bd4` — create vision tester

## 2026-02-12

- `4f0b943` — tested the loop for d4sigma, update RunProgramPage UI, move progress bar
- `3ade49d` — add d4sigma beam detection and update documentation
- `9836bbc` — add Halcon d4sigma beam vision profile
- `b874456` — add custom beam server

## 2026-02-10

- `a37f6dd` — add "Refresh CV" button to Context Tab
- `91c2ad9` — fix parallel move error after security code update

## 2026-02-09

- `45e0d38` — add UserPromptWidget alignment enum (Center, TopLeft) and setAlignment methods

## 2026-02-07

- `d62debb` — enable DoorOpen safety input
- `e897da3` — **add SafetyWatchManager health monitor (heartbeat, stall detection, watchdog timeout)**
- `f8a2b0f` — add motion E-stop cascade
- `3cb417a` — **Phase 4 MISRA compliance: E-stop broadcast, command timeout watchdog, concurrency hardening, connection pre-checks**
- `ed7c2b0` — **Phase 3 MISRA compliance: unified ErrorCode enum, SafetyEventLog, thread priority, clang-tidy**

## 2026-02-06

- `1d96425` — **Phase 2 MISRA compliance: zero-warning /W4 build, SocketGuard RAII, exception removal**
- `5d50081` — **Phase 1 MISRA compliance: bounded loops, [[nodiscard]] attributes**
