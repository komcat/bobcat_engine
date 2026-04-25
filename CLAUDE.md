# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

This is a **CMake-based C++17 Qt6 application** for industrial motion control and automation. Builds with **MSVC (cl.exe)** and **Ninja** on Windows.

### Build Commands
```bash
# Configure build (x64 Debug with Simulation - most commonly used for development)
cmake --preset x64-debug

# Build the project
cmake --build out/build/x64-debug

# Run the executable
./out/build/x64-debug/bobcat_engine.exe
```

### Version System
The build version auto-increments via `build_number.txt` on every configure. Override with:
```bash
cmake --preset x64-debug -DBOBCAT_VERSION_BUILD_OVERRIDE=123
```

### Available Build Presets
| Preset | Description |
|--------|-------------|
| `x64-debug` | 64-bit Debug, **Simulation mode** (default for development) |
| `x64-debug-hardware` | 64-bit Debug, connects to real hardware |
| `x64-debug-hardware-no-halcon` | 64-bit Debug, hardware mode without Halcon SDK |
| `x64-release` | 64-bit Release, hardware mode |
| `x64-release-no-halcon` | 64-bit Release, hardware without Halcon |
| `x86-debug` | 32-bit Debug, simulation mode |
| `x86-debug-hardware` | 32-bit Debug, hardware mode |
| `x86-release` | 32-bit Release, hardware mode |

### Simulation vs Hardware Mode
- **Simulation mode** (`BOBCAT_SIMULATION=ON`): Skips hardware connections, useful for UI development and testing
- **Hardware mode** (`BOBCAT_SIMULATION=OFF`): Connects to physical devices

### Build Output Location
Built executables and runtime files are located in: `out/build/<preset-name>/`

Config files are automatically copied from `config/` to the build output directory. `*.local.json` files in the build output are preserved (not overwritten).

### Utility Targets
The build also produces these standalone tools:
- **`config_editor`** - Standalone JSON configuration file editor (`tools/config_editor/`)
- **`data_explorer`** - Browse and filter serial number result data (`tools/data_explorer/`)
- **`find_visa_devices`** - Scan for connected VISA instruments (only built when VISA SDK found)
- **`vision_profile_tester`** - Vision algorithm profiling tool (`tools/vision_profile_tester/`)

## Project Layout (Non-Standard)

**Critical:** This project stores `.cpp` implementation files alongside `.h` headers in `include/`. The `src/` directory contains only a few files (`src/dut/`, `src/lighting/`, `src/motions/`). When adding new files, follow the existing convention and place both `.h` and `.cpp` in the appropriate `include/` subdirectory unless the subsystem already uses `src/`.

```
bobcat_engine/
├── Application.cpp, bobcat_engine.cpp    # Entry points (root level)
├── include/                               # Headers AND implementations
│   ├── AppUI/                            # Qt UI (pages, forms, dialogs)
│   │   ├── AccuracyTrainer/             # Accuracy trainer module (teach + run alignment)
│   │   ├── PlacementCorrectionTrainer/  # Placement angle correction module
│   │   ├── EdgeTransferTrainer/         # Edge-transfer teach-then-run module
│   │   ├── MotionPages/                 # Motion control UI (jog, interlock widget)
│   │   └── RunProgramPage/             # Recipe execution UI + RollCorrectionWidget
│   ├── AuxWindow/                        # Auxiliary display windows
│   ├── camera/                           # Camera management
│   ├── context/ApplicationContext.h      # DI container (raw pointer struct)
│   ├── graph/                            # Motion graph navigation
│   ├── IO/                               # Digital I/O (EziIO, WAGO)
│   ├── motions/                          # Motion control (ACS, PI, scanning, interlocks)
│   ├── pneumatics/                       # Pneumatic slides
│   ├── ProgramManager/                   # Recipe execution state machine
│   ├── recipe/                           # Recipe system + plugin processes
│   ├── sequence/                         # SequenceEngine + command handlers
│   ├── vision/                           # Vision processing (OpenCV/Halcon)
│   └── [PSU, SMU, TEC, lighting, safety, settings, ...]
├── src/                                   # Sparse - only dut, lighting, motions
├── config/                                # JSON hardware configuration
├── tools/                                 # Standalone utility apps
└── docs/                                  # 40+ documentation files
```

## High-Level Architecture

### Application Lifecycle
`Application::run()` initializes the system in this order:
1. Create QApplication, apply dark theme (RobotoMono font)
2. Check `BOBCAT_SIMULATION` environment variable
3. Show `ConnectionSplashScreen` (hardware mode only)
4. `initializeManagers()` - creates all managers sequentially, wiring them into `ApplicationContext`
5. `createMainWindow()` - builds the Qt UI
6. `connectHardwareAsync()` - deferred hardware connections via `ConnectionWorkerPool` (background threads with timeout)

### ApplicationContext
A simple struct (`include/context/ApplicationContext.h`) holding raw pointers to all managers. Ownership lives in `Application.cpp`. All managers receive this context to access sibling managers.

### Manager Tree
```
ApplicationContext (dependency injection container)
├── UnifiedMotionManager    - Multi-brand motion control (ACS gantry + PI hexapod)
├── InterlockManager        - Position-based motion interlock rules
├── CameraManager           - Multi-camera (Pylon/IDS uEye) via CameraHardwareFactory
├── CameraConfigManager     - Per-camera calibration settings
├── EziIOManager            - Digital I/O (EziIO + WAGO Modbus TCP)
├── PneumaticManager        - Pneumatic slide control
├── TECManager              - Thermoelectric cooler control (CLD101x, TED4015)
├── PowerSupplyManager      - Power supplies (Keysight E36103B, E36441A)
├── SMUManager / SMUPollingManager - Source measure units (Keithley)
├── LightingManager         - LED controllers (CCS PD3)
├── TcpClientManager        - Network communication
├── DataStore               - Key-value data storage
├── LogManager              - spdlog-based logging
├── SequenceEngine          - Command execution (dispatcher + handlers)
├── RecipeManager           - Process recipe management
├── VisionManager           - Image processing plugins (OpenCV/Halcon)
├── SafetyWatchManager      - I/O safety monitoring → emergency stop
├── AdaptiveScanController  - Coarse-to-fine scanning
├── CoordinateSystemManager - Coordinate frame transformations
├── DeviceUnderTest         - Current DUT instance
├── DeviceLinkManager       - Linked multi-device motion
├── AuxWindowManager        - Auxiliary display windows
├── GdiCameraWindowManager  - GDI-based camera monitor (x2 instances)
├── QtCameraWindow          - Qt-based camera monitor (x2 instances)
├── SwirBeamWindowManager   - SWIR beam processing window
├── BeamServerClient        - TCP server client for beam detection
├── BeamResultBroker        - Beam detection pub-sub broker
├── BeamOptimizerManager    - Beam optimization coordinator
├── ApplicationSettings     - INI-based app settings (config/system/applicationSettings.ini)
└── SafetyEventLog          - Structured JSON-lines audit trail
```

### Core Execution Pipeline

**Recipe** (JSON) → **ProcessSequenceFactory** (converts steps) → **SequenceEngine** (executes) → **CommandDispatcher** → **CommandHandler** (device-specific)

Key flow:
1. **RecipeManager** loads/saves recipe JSON files containing ordered process steps
2. **ProcessPluginRegistry** maps process type names to `IProcessPlugin` implementations
3. Each plugin's `BuildSequence()` uses `SequenceBuilder` to create command sequences
4. **ProgramManager** (`include/ProgramManager/`) orchestrates recipe execution with a state machine (`RecipeExecutionStateMachine`), handling loops, step dependencies, and conditional execution
5. **SequenceEngine** executes steps (Actions, Loops, Conditionals) with stop/pause/step-by-step/breakpoint support
6. **CommandDispatcher** routes each action to the appropriate handler based on device type

### Command Handlers
All in `include/sequence/*CommandHandler.cpp`. Each handles a specific hardware domain:

| Handler | Domain | Notable Commands |
|---------|--------|-----------------|
| MotionCommandHandler | Motion control | MoveToAbs, MoveToNamedPosition, HomeAxis, SetVelocity, GridScanDualChannel |
| IOCommandHandler | Digital I/O | SetOutput, ReadInput, WaitForInput |
| PneumaticCommandHandler | Pneumatic slides | Extend, Retract, WaitForState |
| CameraCommandHandler | Camera ops | Capture, StartLive, StopLive |
| TECCommandHandler | TEC devices | SetTemperature, EnableOutput |
| SystemCommandHandler | System | Wait, Prompt, Log, UserInput |
| DataStoreCommandHandler | Data storage | Set, Get, Export |
| PSUCommandHandler | Power supply | SetVoltage, SetCurrent, EnableOutput |
| SMUCommandHandler | SMU | Configure, Measure, EnableOutput |
| LightingCommandHandler | Lighting | SetIntensity, EnableChannel |
| VisionCommandHandler | Vision (OpenCV) | RunPlugin, DetectCircle, PatternMatch, execute_profile |
| CalibrationCommandHandler | Calibration | Various calibration procedures |
| DUTCommandHandler | Device under test | CreateDUT, SetSerialNumber |
| AuxWindowCommandHandler | Aux windows | ShowChart, ShowCamera |
| CoordinateAlignCommandHandler | Coord alignment | AlignToPoints, TransformCoordinates |
| RelativePositionCommandHandler | Relative positions | CalculateOffset, MoveRelative |
| SequenceContextCommandHandler | Context vars | SetVariable, GetVariable, Evaluate |
| AccuracyTrainerCommandHandler | Accuracy training | RunAlignment (headless, no Qt dependency) |

### Motion Control System
- **UnifiedMotionManager** (`include/motions/unified_motion_manager.h`) - requires both ACS and PI SDKs
- Routes commands to correct controller based on device type from `config/motor_devices.json`
- **Named positions** - saved positions referenced by name (e.g., "home", "lensgrip")
- **Waypoint system** - automatic safe path routing with exit/entry waypoints (state machine)
- **Calculated positions** - offset notation: `"lensgrip+Y1"`, `"safe+X-2+Z5"`
- **Graph navigation** - `include/graph/MotionGraph*.cpp` provides graph-based pathfinding between positions
- **6-axis support** - XYZUVW (position + rotation)
- **Interlock enforcement** - `InterlockManager::CheckMove()` called in all 5 move methods; blocks moves when monitored device axis meets a condition

### Motion Interlock System
`InterlockManager` (`include/motions/InterlockManager.h`) provides position-based motion safety rules:
- Up to `MAX_INTERLOCK_RULES` (3) rules, each monitoring one device/axis and blocking another device
- Rule structure: "if `monitorDevice.monitorAxis` `op` `threshold`, block all moves on `blockedDevice`"
- Operators: `<`, `>`, `<=`, `>=`
- Config persisted to `config/interlocks.json` (local-first pattern)
- UI widget: `InterlockWidget` in `include/AppUI/MotionPages/`

### Recipe Plugin System
Plugin-based architecture for process types. See `docs/creating_recipe_plugins.md`.

**To add a new process type:**
1. Create plugin class implementing `IProcessPlugin` in `include/recipe/processes/`
2. Implement: `GetName()`, `GetCategory()`, `GetDescription()`, `GetDefaultParameters()`, `CreateConfigUI()`, `BuildSequence()`
3. Register via auto-registration macro at file bottom
4. If new category: add to `RecipeManager::getProcessCategories()`
5. Add source file to CMakeLists.txt `SOURCES` list

Plugin files are organized by category: `MotionProcesses.cpp`, `IOProcesses.cpp`, `SystemProcesses.cpp`, `TECProcesses.cpp`, `PSUProcesses.cpp`, `SMUProcesses.cpp`, `DataStoreProcesses.cpp`, `DUTProcesses.cpp`, `ConditionalProcesses.cpp`, `CalibrationProcesses.cpp`, `VisionProcesses.cpp`, `CameraProcesses.cpp`, `LightingProcesses.cpp`, `AuxWindowProcesses.cpp`, `DispenseProcesses.cpp`, `AccuracyTrainerProcesses.cpp`, etc.

### Adding New Commands
See `docs/ADDING_NEW_COMMANDS_GUIDE.md` for the full guide on extending the sequence system with new command handlers.

## Accuracy Trainer Module

Located in `include/AppUI/AccuracyTrainer/` (10 files). Teaches and corrects bonding position accuracy using a local coordinate frame. Activated via F7 shortcut in MainWindow.

### Architecture
```
AccuracyTrainerPage (main container, 30% left nav + 70% right content)
├── TeachBondingWidget      - Teach 2-3 corner positions → compute bonding center
├── TeachReferenceWidget    - Teach 2-3 reference landmarks → compute local coordinate frame
├── TeachMultiDeviceWidget  - Teach hex device 6-axis + gantry viewing positions + corners + Z offsets
├── EditZOffsetWidget       - Edit Z offset parameters (component/bondline thickness)
├── RunAlignmentWidget      - Single-device: capture refs → rigid transform → corrected bonding moves
└── MultiDeviceRunAlignmentWidget - Multi-device: refs + hex registration + coordinate transforms

AccuracyTrainerStore       - JSON persistence (v2 format) to {productDir}/accuracy_trainer/
AccuracyTrainerData.h      - Data structs (ReferencePoint, BondingPoint, HexDeviceTeachData, TrainingSet) + math
AccuracyTrainerLogger      - SQLite3 logging of corner confirmations with taught vs confirmed deltas
```

### Data Flow
1. **Teach Phase**: User teaches reference points (Ref1/Ref2/Ref3) to define a local coordinate frame. Origin = Ref1, X-axis = Ref1→Ref2, Y-axis = perpendicular (CCW 90deg). Bonding points are taught by 2-3 corners, center computed automatically. All stored as `TrainingSet`.
2. **Run Phase**: Move to taught reference positions, user adjusts to actual positions (or vision auto-detects). Compute rigid 2D transform (rotation + translation) from taught→actual. Apply transform to all bonding points, move to corrected positions.
3. **Multi-Device Run**: Additionally registers hex devices by 3-corner capture in gantry frame, uses `TransformationMatrixLoader` to transform bonding positions between gantry and hex coordinate systems.

### Headless Execution
`AccuracyTrainerCommandHandler` (device type `"accuracytrainer"`) runs alignment without Qt widgets. Parameters: `training_set_name`, `register_hex_left`, `register_hex_right`, `move_to_bonding`. Results stored in SequenceContext as `accuracy_trainer.result.*` (success, corrected positions, deltas).

### Key Design Decisions
- **Re-entrancy guard**: `TeachMultiDeviceWidget` is NOT refreshed in `updateCurrentTrainingSet()` to prevent data loss during teaching
- **Vision integration** (conditional `HAS_OPENCV`): `VisionCommandHandler::execute_profile` + `calculate_world_position` for auto-detect in reference and hex corner teaching
- **File format v2**: Includes UUID, Z parameters, hex corner data. v1 files auto-upgraded on load with sensible defaults
- **SQLite logging**: Corner confirmations logged to `logs/accuracy_trainer/accuracy_trainer_log.db` with unique point UIDs (`ref_{name}`, `hex_{device}_{corner}`)

## Placement Correction Trainer Module

Located in `include/AppUI/PlacementCorrectionTrainer/` (9 files). Corrects hexapod placement angle by comparing reference and target line angles measured on the gantry.

### Architecture
```
PlacementCorrectionPage (main container)
├── TeachPlacementLineWidget  - Teach 2 points each for reference and target lines
├── PlacementResultWidget     - Show angle delta and corrected U value
├── PlacementRetraceWidget    - Re-measure and validate correction
├── ExposureAdjustWidget      - Camera exposure controls during measurement
├── ImageGalleryWidget        - Captured placement images
└── SummaryDiagramWidget      - Visual summary of corrections

PlacementCorrectionStore     - JSON persistence to {productDir}/placement_correction/
PlacementCorrectionData.h    - Structs (PlacementLinePoints, PlacementCorrectionConfig, PlacementCorrectionSet) + math
```

### Data Flow
1. **Teach**: Capture 2 gantry XY points each for a reference line and target line
2. **Compute**: `PlacementCorrectionMath::ComputeLineAngle()` for each line, delta angle = target - reference
3. **Correct**: Apply delta to hex device U axis: `correctedU = masterU + deltaAngle` (with optional sign reversal)
4. **Config**: `PlacementCorrectionConfig` maps a master position (golden reference, never modified) to a target position that receives the corrected values

## SWIR Beam Detection (`include/SwirBeamWindow/`)

Real-time beam profiling/tracking shown in a Win32+GDI+ window (not Qt). Three detection modes coexist in `SwirBeamDetector.cpp` and are selected at runtime via `config/beam_detection.json`:

| Mode | Function | Notes |
|------|----------|-------|
| `halcon` (plan of record) | `RunHalconBeamDetection` | HDevelop port: local-mean BG (51×51) + threshold + connected components + per-region first-moment D4σ. Requires `HAS_HALCON`. Toggle: `useHalconDetection: true`. |
| `blob` | `RunBlobDetection` | OpenCV threshold + contour. Toggle: `useBlobDetection: true`. |
| `d4sigma` (legacy) | `RunBeamDetection` | OpenCV X/Y projection peak finding + corner-patch BG. Default if neither flag is set. Has known robustness issues vs Halcon. |

For new tuning/fixes prefer the Halcon path. Don't propose tuning d4sigma thresholds (`minPeakValue`, `aspectRatioMax`, etc.) unless the miss log shows `mode=d4sigma`.

### Frame pipeline (single-slot, latest-wins)
- `CameraManager` rate-limits broadcasts to subscribers (`GetMinFrameIntervalMs()` returns 33 ms — ~30 fps cap).
- `SwirBeamWindow::OnNewFrame` writes two single-slot buffers under separate mutexes:
  - `m_detectionBuffer` (gray) — overwritten if detection thread hasn't consumed previous frame; tracked by `m_detectionInputDrops`.
  - `m_frameBuffer` (BGR) — write skipped entirely (`m_hasNewFrame` flag) when UI repaint hasn't completed.
- Detection runs on its own `std::thread` at `THREAD_PRIORITY_BELOW_NORMAL`, sleeping on `m_detectionCV` between frames.
- No queue. Frame drops at the detection input are silent except for the `[BeamDetect] detection-input drops total=...` console line.

### Beam ID stability
After detection, the caller in `SwirBeamWindow.cpp` (the detection thread lambda) re-orders the result vector via nearest-neighbour association to `m_prevBeams[2]` so that slot 0 / slot 1 keep following the same physical beam across frames. CE/EE history, broker publishing, and overlay rendering all depend on this slot identity. Single-beam frames are slot-aware: the lone candidate is matched to the closer prior slot rather than always overwriting slot 0.

### GA quadrant labels
`gaQuadrantLabels` in `beam_detection.json` maps each visual quadrant to a GA channel string. With the current Y-flip convention used by the labeling code, **set top-quadrants (`q1`, `q2`) to the GA channel that is physically at the bottom of the image** and vice versa — verify against the live overlay before committing changes.

## Roll Correction Widget

Located in `include/AppUI/RunProgramPage/RollCorrectionWidget.h/.cpp`. Corrects hexapod roll (W axis) using dual-channel peak position measurement.

### Workflow
1. Register CH1/CH2 peak X positions per hex device (hex-left, hex-right)
2. Compute roll angle: `arctan(deltaX / spacing)`
3. Apply 3-step correction: move to CH1 peak, shift X halfway, rotate W per direction table
4. All moves use 1mm/s safe velocity with automatic restore
5. "Fine Grid Scan" button runs `GridScanDualChannel` on background thread; "STOP Scan" uses SequenceContext-based stop control

## Data Backup System

Located in `include/AppUI/DataBackupPage.h/.cpp`. Product-scoped file backup with SHA-256 integrity verification. Accessed via "Data Backup" button on main menu (F8).

### Backup Storage
Backups are stored per-product in `config/{productName}/backups/{timestamp}_{type}[_{name}]/`. Each backup copies the entire product directory (positions, recipes, programs, trainer datasets) except the `backups/` subfolder itself.

### Backup Types
| Type | Trigger | Retention |
|------|---------|-----------|
| `save_point` | User clicks "Create Save Point" | Never auto-pruned |
| `auto` | QTimer at configurable interval (default 30 min) | Keep last 10 |
| `pre_revert` | Automatically before any restore | Keep last 3 |

### Key Features
- **SHA-256 manifest** (`manifest.json`) stores per-file hash and size for integrity verification
- **Smart auto-backup** skips if no files changed since last backup (hash comparison)
- **Show Changes** compares current product files against any backup: reports `[M]` modified, `[+]` added, `[-]` deleted files
- **Pre-revert safety** always snapshots current state before restoring
- Auto-backup settings persist in QSettings (`Backup/AutoEnabled`, `Backup/AutoIntervalMin`)

## UI Page Navigation

MainWindow uses a `QStackedWidget` with F-key shortcuts for page switching:

| Key | Page | Class |
|-----|------|-------|
| F1 | Process (Run Program) | `RunProgramPage` |
| F2 | Recipe | `RecipePage` |
| F3 | Motion | `MotionPage` |
| F4 | Hardware & Instrument | `HardwareInstrumentPage` |
| F5 | Settings | `ApplicationSettingsPage` |
| F6 | Vision Manager | `VisionManagerPage` |
| F7 | Accuracy Trainer | `TrainerContainerPage` |
| F8 | Data Backup | `DataBackupPage` |

Window toggles: Ctrl+J (Jog Panel), Ctrl+K (Aux Window), Ctrl+R / Ctrl+T (Camera Monitors).

All pages receive `ApplicationContext*` in constructor and emit `backToMainMenu()` signal. Pages that need product awareness connect to `MainMenuPage::productChanged()` signal.

## Product Directory Structure

Each product folder lives under `config/{productName}/` and contains:
```
config/SIPHOG101/
├── positions.SIPHOG101.json     # Named positions (loaded by ProductPositionManager)
├── recipes/                      # Recipe JSON files
├── programs/                     # Run program JSON files
├── accuracy_trainer/             # Accuracy trainer datasets
├── placement_correction/         # Placement correction datasets
└── backups/                      # Data backup snapshots (auto-managed)
```

Active product is managed by `ProductPositionManager` (accessible via `ApplicationContext`). Use `HasLoadedProduct()` / `GetCurrentProductName()` / `GetCurrentProductPath()`.

## Configuration System

All hardware configuration is JSON-based in `config/`:

| File | Purpose |
|------|---------|
| `motor_devices.json` | Motion controllers, axes, and named positions |
| `camera_config.json` | Camera devices and connection settings |
| `io_devices.json` | Digital I/O devices |
| `pneumatic_slides.json` | Pneumatic slide configurations |
| `tec_devices.json` | TEC device settings |
| `power_supply_config.json` | Power supply settings |
| `smu_devices.json` | Source measure unit settings |
| `tcp_clients.json` | TCP client connections |
| `transformation_matrix.json` | Coordinate transformation matrices |
| `lighting_config.json` | Lighting controller settings |
| `safety_watch.json` | Safety I/O monitoring |
| `pi_pivot_config.json` | PI hexapod pivot point config |
| `interlocks.json` | Motion interlock rules (up to 3) |
| `jog_stepsize.json` | Jog step sizes (17 presets, 0.1um to 5000um) |
| `beam_detection.json` | SWIR/beam detection settings |
| `system/applicationSettings.ini` | Application-wide settings |
| `SYSTEM/needlexyoffset.json` | Needle XY offset data |

### Local Config Override Pattern
- Application tries `config/xxx.local.json` first, falls back to `config/xxx.json`
- Local configs are gitignored - safe for machine-specific settings

### Camera Connection Types
- `ip` - GigE cameras via IP address
- `serial` - By serial number (USB cameras)
- `index` - By device index
- `auto` - First available camera

## Sequence Context System

Commands can store and retrieve values at runtime using `SequenceContext`:
- Use `captureAs` parameter to store command results in context
- Reference context values with `context:variable_name` syntax
- See `docs/sequence_context_usage_guide.md` for details

```cpp
// In command handler - store result
context->Set("measured_value", std::to_string(result));

// In plugin - reference context value
sequence->AddAction(device, "MoveToAbs", {
  {"x", "context:calculated_x"}
});
```

## Lua Scripting (`include/scripting/`)

`ScriptEngine` (registered closure `_dispatch`) and the Lua wrapper modules in `ScriptEngine.cpp` expose the same command surface as the recipe handlers, but for procedural use. Scripts live under `scripts/` (global) or `config/{product}/scripts/` (per-product).

- High-level wrappers: `motion.*`, `psu.*`, `tec.*`, `camera.*`, `vision.*`, `recipe.run(...)`, `dut.*`, `ctx.*`, `file.*`, `bobcat.*`, etc.
- Low-level escape hatch: `_dispatch(deviceName, methodName, paramsTable)` calls any `*CommandHandler` method directly. Use this when a wrapper is missing or doesn't pass the parameter you need (e.g. `camera.captureToFile` doesn't forward `product_name`, so call `_dispatch(camera, "CaptureToFile", {product_name=..., filename=...})` instead).
- `recipe.run(name, opts)` runs a recipe end-to-end and blocks until completion; respects `fail_on_error` flag.
- Files written via `file.appendCsv` etc. are relative to the executable's working directory (`out/build/<preset>/`), not the source tree.

## Edge Transfer Trainer (`include/AppUI/EdgeTransferTrainer/`)

Same teach-then-run pattern as Accuracy / Placement trainers (8 widgets + `EdgeTransferStore` + `EdgeTransferData`). When working on trainer features, treat this as a peer of the other two — the data layout, JSON store, and run-time correction flow follow the same conventions.

## External Dependencies

### SDKs in `external/` (CMake auto-detects presence)
- **Pylon 8** - Basler camera SDK
- **IDS uEye** - IDS camera SDK
- **ACSC** - ACS motion controller (x64/x86 libs)
- **PI GCS2** - PI motion controller (x64/x86 libs)
- **EziIO** - Fastech I/O controller (x64/x86 libs)
- **NI-VISA** - VISA instrument communication (TEC, SMU devices)
- **OpenCV 4.12** - Computer vision (optional, compile def `HAS_OPENCV`)
- **Halcon** - Machine vision (optional, delay-loaded, compile def `HAS_HALCON`)
- **SQLite3** - Database for accuracy trainer logging (bundled in `external/sqlite3/`)
- **nlohmann/json** - JSON parsing (header-only)
- **spdlog** - Logging (header-only)

### Qt6 (auto-detected at C:/Qt/6.10.x or C:/Qt/6.9.3)
- Qt6::Widgets, Qt6::Charts, Qt6::OpenGLWidgets

### Conditional Compilation
- UnifiedMotionManager requires **both** ACS and PI SDKs
- Vision plugins require OpenCV; Halcon plugins require **both** OpenCV and Halcon
- VISA-based adapters (CLD101x, TED4015, Keithley) require NI-VISA
- Accuracy Trainer vision features require `HAS_OPENCV`

## Coding Conventions (MISRA-Aligned)

This codebase follows MISRA-inspired safety conventions. When modifying code, follow these patterns:

### Error Handling — No Exceptions
- **Do NOT use `throw`** — the codebase has been purged of exceptions
- Return error codes (`ErrorCode` enum from `include/common/ErrorCode.h`) or `std::optional` for fallible operations
- Use the `IsValid()` pattern: construct object, then check `IsValid()` before use
- Example: `DeviceUnderTest::create()` returns `std::optional<DeviceUnderTest>`

### Loop Safety
- **Do NOT use `while(true)`** — use bounded `for` loops with a maximum iteration count
- Pattern: `for (int i = 0; i < MAX_ITERATIONS; ++i)` with a break condition inside

### RAII for Sockets
- Use `SocketGuard` (`include/IO/SocketGuard.h`) for all socket lifetime management
- Already applied in: WagoIODevice, TcpDataClient, CLD101xAdapter, Keysight adapters

### Attributes and Warnings
- Build uses `/W4` (MSVC warning level 4) — code must compile warning-free
- Use `[[nodiscard]]` on safety-critical return values (error codes, status results)
- Comment out unused parameter names rather than using `(void)param`: `void foo(int /*param*/)`

### Thread Safety
- Use `include/common/ThreadPriority.h` for setting thread priorities (safety-critical threads use `AboveNormal`)
- Prefer `std::future` over `.detach()` for thread lifetime management
- Avoid `static` mutable state — use member variables instead

## Safety Infrastructure

- **`include/common/ErrorCode.h`** — Unified error code enum (40+ codes, categorized 0x0000-0x0F00)
- **`include/safety/SafetyEventLog.h`** — Structured JSON-lines audit trail (`logs/safety_events.jsonl`), wired into ApplicationContext, with 10MB file rotation
- **`include/safety/SafetyWatchManager.h`** — I/O safety monitoring with E-stop broadcast, heartbeat health monitor (2s QTimer), `pollingThreadStalled` signal
- **`include/common/ThreadPriority.h`** — Cross-platform thread priority utility

### E-Stop Cascade
When `SafetyWatchManager` triggers, `Application::performEStopCascade()` broadcasts to: TECManager → `DisableAllOutputs()`, PowerSupplyManager → `disableAllOutputs()`, SMUManager → `OutputOffAll()`, PneumaticManager → `StopAll()`, LightingManager → `AllOff()`. All actions are logged to SafetyEventLog with CRITICAL severity. Results are consolidated into a single `ESTOP_CASCADE_COMPLETE` log entry.

## Static Analysis

A `.clang-tidy` configuration exists at project root. Enabled check families: `bugprone-*`, `modernize-*`, `performance-*`, `readability-*`, plus select `cppcoreguidelines` and `misc` checks. Header filter: `include/.*\.h$`.

## Key Design Patterns

1. **Manager Pattern** - Each subsystem has a dedicated manager class
2. **Dependency Injection** - `ApplicationContext` struct provides manager access (raw pointers, no interfaces)
3. **Factory Pattern** - `CameraHardwareFactory`, hardware adapter factories
4. **Command/Handler/Dispatcher** - SequenceEngine → CommandDispatcher → *CommandHandler
5. **Builder Pattern** - `SequenceBuilder` for constructing command sequences
6. **Plugin Architecture** - `IProcessPlugin` for recipe processes, `IVisionPlugin` for vision algorithms
7. **State Machine** - Waypoint navigation, recipe execution (`RecipeExecutionStateMachine`)
8. **Trainer Pattern** - Teach-then-run workflow: `AccuracyTrainer` and `PlacementCorrectionTrainer` both use a teach phase (store reference geometry) followed by a run phase (measure actual, compute transform, apply correction)

## Important Files

- `Application.cpp` - App initialization, manager creation, hardware connection, E-stop cascade
- `include/context/ApplicationContext.h` - Central dependency container (struct)
- `include/motions/unified_motion_manager.h` - Motion control unified interface
- `include/motions/InterlockManager.h` - Position-based motion interlock rules
- `include/sequence/SequenceEngine.h` - Command execution engine
- `include/sequence/SequenceBuilder.h` - Sequence construction API
- `include/sequence/CommandDispatcher.h` - Routes commands to handlers
- `include/sequence/AccuracyTrainerCommandHandler.h` - Headless alignment execution
- `include/recipe/IProcessPlugin.h` - Recipe plugin interface
- `include/recipe/ProcessPluginRegistry.h` - Plugin auto-registration
- `include/recipe/RecipeManager.h` - Recipe data management
- `include/ProgramManager/ProgramManager.h` - Recipe execution orchestration
- `include/ProgramManager/RecipeExecutionStateMachine.cpp` - Execution state machine
- `include/vision/IVisionPlugin.h` - Vision plugin interface
- `include/common/ErrorCode.h` - Unified error codes for the entire application
- `include/safety/SafetyEventLog.h` - Structured safety audit logging
- `include/IO/SocketGuard.h` - RAII wrapper for socket lifetime
- `include/AppUI/AccuracyTrainer/AccuracyTrainerData.h` - Trainer data structures + transform math
- `include/AppUI/PlacementCorrectionTrainer/PlacementCorrectionData.h` - Placement correction data + angle math
- `include/AppUI/DataBackupPage.h` - Product data backup/restore with SHA-256 integrity
- `include/AppUI/SettingsPage/ConfigBackupManager.h` - Root/system config backup (dialog-based, in Settings)
- `CMakeLists.txt` - Build configuration (source lists, SDK detection, library linking)

## Quick Reference

### Common Workflows

**Add a new recipe process type:**
1. Create plugin in `include/recipe/processes/` implementing `IProcessPlugin`
2. Register via auto-registration macro
3. If new category: add to `RecipeManager::getProcessCategories()`
4. Add source to CMakeLists.txt

**Add a new command to existing handler:**
1. Add `else if (methodName == "YourCommand")` block in `*CommandHandler.cpp`
2. Validate parameters, execute logic, return success/failure

**Add machine-specific config:**
1. Copy `config/xxx.json` to `config/xxx.local.json`
2. Edit local file (gitignored, won't be overwritten by builds)

**Add a new trainer module:**
1. Create data structures in `*Data.h` with pure math functions (no dependencies)
2. Create store class for JSON persistence (product-relative path, local-first)
3. Create teach widgets and run widgets as separate `QWidget` subclasses
4. Create container page that manages widget switching and training set selection
5. Optionally add a `*CommandHandler` for headless recipe execution
