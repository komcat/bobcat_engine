# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> **Detail docs**: per-subsystem file layouts and helper contracts live under `docs/architecture/`. Each section below links to the relevant file when one exists.

## Build System

This is a **CMake-based C++17 Qt6 application** for industrial motion control and automation. Builds with **MSVC (cl.exe)** and **Ninja** on **Windows only** (no cross-platform support).

### Build Commands
```bash
# Configure build (x64 Debug with Simulation - most commonly used for development)
cmake --preset x64-debug

# Build the project
cmake --build out/build/x64-debug

# Run the executable (bash)
./out/build/x64-debug/bobcat_engine.exe
# Run the executable (PowerShell)
.\out\build\x64-debug\bobcat_engine.exe
```

**Agent note:** the user compiles themselves — do not invoke `cmake --build` from an agent session. Configure (`cmake --preset ...`) is fine; trigger an actual build only on explicit request.

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
└── docs/                                  # 40+ documentation files (incl. docs/architecture/)
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

The four biggest handlers are split into a thin orchestrator + per-family helpers — touch the helper, not the orchestrator:

- **Motion** — 9 helpers under `include/sequence/motion/`. See [docs/architecture/motion_command_handler.md](docs/architecture/motion_command_handler.md).
- **Vision** — 4 helpers under `include/sequence/vision/`. See [docs/architecture/vision_command_handler.md](docs/architecture/vision_command_handler.md).
- **CustomServer** — 4 helpers under `include/sequence/customserver/`. See [docs/architecture/customserver_command_handler.md](docs/architecture/customserver_command_handler.md).
- **Calibration** — 6 helpers under `include/sequence/calibration/`. See [docs/architecture/calibration_command_handler.md](docs/architecture/calibration_command_handler.md).

### Motion Control System
- **UnifiedMotionManager** (`include/motions/unified_motion_manager.h`) - requires both ACS and PI SDKs
- Routes commands to correct controller based on device type from `config/motor_devices.json`
- **Named positions** - saved positions referenced by name (e.g., "home", "lensgrip")
- **Waypoint system** - automatic safe path routing with exit/entry waypoints (state machine)
- **Calculated positions** - offset notation: `"lensgrip+Y1"`, `"safe+X-2+Z5"`
- **Graph navigation** - `include/graph/MotionGraph*.cpp` provides graph-based pathfinding between positions
- **6-axis support** - XYZUVW (position + rotation)
- **Interlock enforcement** - `InterlockManager::CheckMove()` called inside the 5 move methods of `UnifiedMotionManager` itself (not the command handler); blocks moves when monitored device axis meets a condition. Anything that goes through `UnifiedMotionManager::Move*()` is automatically protected — split helper command files do not need their own interlock checks.

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

## Trainer Modules

Three trainer modules share a teach-then-run pattern (data structs + JSON store + teach widgets + run widget + optional headless `*CommandHandler`):

- **Accuracy Trainer** — `include/AppUI/AccuracyTrainer/`. Bonding-position correction via local coordinate frame. F7 in MainWindow. See [docs/architecture/accuracy_trainer_module.md](docs/architecture/accuracy_trainer_module.md).
- **Placement Correction Trainer** — `include/AppUI/PlacementCorrectionTrainer/`. Hexapod placement angle correction by gantry-measured line angles. See [docs/architecture/placement_correction_trainer.md](docs/architecture/placement_correction_trainer.md).
- **Edge Transfer Trainer** — `include/AppUI/EdgeTransferTrainer/`. Same teach-then-run shape as the other two (8 widgets + `EdgeTransferStore` + `EdgeTransferData`); treat as a peer.

## UI Pages and Windows

Each of these is composed from focused helpers — touch the helper for the area you're working in, **not** the orchestrator. Linked docs list the helper file table and any construction-order constraints.

| Subsystem | Path | Detail doc |
|-----------|------|-----------|
| F1 Process page | `include/AppUI/RunProgramPage/` (incl. `RollCorrectionWidget`) | [docs/architecture/run_program_page.md](docs/architecture/run_program_page.md) |
| F2 Recipe page | `include/AppUI/RecipePage/` | [docs/architecture/recipe_page.md](docs/architecture/recipe_page.md) |
| Qt camera monitor (x2) | `include/QtCameraWindow/` | [docs/architecture/qt_camera_window.md](docs/architecture/qt_camera_window.md) |
| GDI camera monitor (x2) | `include/GdiWindow/` | [docs/architecture/gdi_camera_window.md](docs/architecture/gdi_camera_window.md) |
| SWIR beam window | `include/SwirBeamWindow/` (Win32+GDI+) | [docs/architecture/swir_beam_window.md](docs/architecture/swir_beam_window.md) |
| F8 Data Backup | `include/AppUI/DataBackupPage.{h,cpp}` | [docs/architecture/data_backup.md](docs/architecture/data_backup.md) |

### UI Page Navigation

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

This codebase follows MISRA-inspired safety conventions. The rules below are **enforced invariants**, not aspirational — Phases 1–5 (completed Feb 2026) swept the codebase to comply, so new code that re-introduces `while(true)`, `throw`, raw socket lifetime management, or generic error returns is a regression. When modifying code, follow these patterns:

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

## Testing

This is an industrial automation application without a formal automated test suite — there are no unit/integration tests to run, and no `ctest` target. The verification loop is:
- **Build cleanly** under `/W4` (zero warnings is the bar — see Phase 2 Part A)
- **Simulation mode** (`x64-debug` preset) - runs without hardware for UI/logic testing
- **Manual hardware testing** - connects to physical devices via a hardware preset

Do not claim "tests pass" or fabricate a test command — when asked to verify a change, build + (where possible) run in simulation, and report exactly what was exercised.

## Quick Reference

### Common Workflows

**Add a new recipe process type:**
1. Create plugin in `include/recipe/processes/` implementing `IProcessPlugin`
2. Register via auto-registration macro
3. If new category: add to `RecipeManager::getProcessCategories()`
4. Add source to CMakeLists.txt

**Add a new command to existing handler:**
1. For Motion / Vision / CustomServer / Calibration: add the `if (methodName == "...")` branch inside the appropriate per-family helper (see linked docs in the Command Handlers section). Do NOT modify the orchestrator unless starting a new family.
2. For other handlers: add `else if (methodName == "YourCommand")` block in `*CommandHandler.cpp`, validate parameters, execute, return success/failure.

**Add machine-specific config:**
1. Copy `config/xxx.json` to `config/xxx.local.json`
2. Edit local file (gitignored, won't be overwritten by builds)

**Add a new trainer module:**
1. Create data structures in `*Data.h` with pure math functions (no dependencies)
2. Create store class for JSON persistence (product-relative path, local-first)
3. Create teach widgets and run widgets as separate `QWidget` subclasses
4. Create container page that manages widget switching and training set selection
5. Optionally add a `*CommandHandler` for headless recipe execution
