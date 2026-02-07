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

## Project Layout (Non-Standard)

**Critical:** This project stores `.cpp` implementation files alongside `.h` headers in `include/`. The `src/` directory contains only a few files (`src/dut/`, `src/lighting/`, `src/motions/`). When adding new files, follow the existing convention and place both `.h` and `.cpp` in the appropriate `include/` subdirectory unless the subsystem already uses `src/`.

```
bobcat_engine/
├── Application.cpp, bobcat_engine.cpp    # Entry points (root level)
├── include/                               # Headers AND implementations
│   ├── AppUI/                            # Qt UI (pages, forms, dialogs)
│   ├── AuxWindow/                        # Auxiliary display windows
│   ├── camera/                           # Camera management
│   ├── context/ApplicationContext.h      # DI container (raw pointer struct)
│   ├── graph/                            # Motion graph navigation
│   ├── IO/                               # Digital I/O (EziIO, WAGO)
│   ├── motions/                          # Motion control (ACS, PI, scanning)
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
├── CameraManager           - Multi-camera (Pylon/IDS uEye) via CameraHardwareFactory
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
├── GdiCameraWindowManager  - GDI-based camera monitor
└── ApplicationSettings     - INI-based app settings (config/system/applicationSettings.ini)
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
| MotionCommandHandler | Motion control | MoveToAbs, MoveToNamedPosition, HomeAxis, SetVelocity |
| IOCommandHandler | Digital I/O | SetOutput, ReadInput, WaitForInput |
| PneumaticCommandHandler | Pneumatic slides | Extend, Retract, WaitForState |
| CameraCommandHandler | Camera ops | Capture, StartLive, StopLive |
| TECCommandHandler | TEC devices | SetTemperature, EnableOutput |
| SystemCommandHandler | System | Wait, Prompt, Log, UserInput |
| DataStoreCommandHandler | Data storage | Set, Get, Export |
| PSUCommandHandler | Power supply | SetVoltage, SetCurrent, EnableOutput |
| SMUCommandHandler | SMU | Configure, Measure, EnableOutput |
| LightingCommandHandler | Lighting | SetIntensity, EnableChannel |
| VisionCommandHandler | Vision (OpenCV) | RunPlugin, DetectCircle, PatternMatch |
| CalibrationCommandHandler | Calibration | Various calibration procedures |
| DUTCommandHandler | Device under test | CreateDUT, SetSerialNumber |
| AuxWindowCommandHandler | Aux windows | ShowChart, ShowCamera |
| CoordinateAlignCommandHandler | Coord alignment | AlignToPoints, TransformCoordinates |
| RelativePositionCommandHandler | Relative positions | CalculateOffset, MoveRelative |
| SequenceContextCommandHandler | Context vars | SetVariable, GetVariable, Evaluate |

### Motion Control System
- **UnifiedMotionManager** (`include/motions/unified_motion_manager.h`) - requires both ACS and PI SDKs
- Routes commands to correct controller based on device type from `config/motor_devices.json`
- **Named positions** - saved positions referenced by name (e.g., "home", "lensgrip")
- **Waypoint system** - automatic safe path routing with exit/entry waypoints (state machine)
- **Calculated positions** - offset notation: `"lensgrip+Y1"`, `"safe+X-2+Z5"`
- **Graph navigation** - `include/graph/MotionGraph*.cpp` provides graph-based pathfinding between positions
- **6-axis support** - XYZUVW (position + rotation)

### Recipe Plugin System
Plugin-based architecture for process types. See `docs/creating_recipe_plugins.md`.

**To add a new process type:**
1. Create plugin class implementing `IProcessPlugin` in `include/recipe/processes/`
2. Implement: `GetName()`, `GetCategory()`, `GetDescription()`, `GetDefaultParameters()`, `CreateConfigUI()`, `BuildSequence()`
3. Register via auto-registration macro at file bottom
4. If new category: add to `RecipeManager::getProcessCategories()`
5. Add source file to CMakeLists.txt `SOURCES` list

Plugin files are organized by category: `MotionProcesses.cpp`, `IOProcesses.cpp`, `SystemProcesses.cpp`, `TECProcesses.cpp`, `PSUProcesses.cpp`, `SMUProcesses.cpp`, `DataStoreProcesses.cpp`, `DUTProcesses.cpp`, `ConditionalProcesses.cpp`, `CalibrationProcesses.cpp`, `VisionProcesses.cpp`, `CameraProcesses.cpp`, `LightingProcesses.cpp`, `AuxWindowProcesses.cpp`, `DispenseProcesses.cpp`, etc.

### Adding New Commands
See `docs/ADDING_NEW_COMMANDS_GUIDE.md` for the full guide on extending the sequence system with new command handlers.

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
- **nlohmann/json** - JSON parsing (header-only)
- **spdlog** - Logging (header-only)

### Qt6 (auto-detected at C:/Qt/6.10.x or C:/Qt/6.9.3)
- Qt6::Widgets, Qt6::Charts, Qt6::OpenGLWidgets

### Conditional Compilation
- UnifiedMotionManager requires **both** ACS and PI SDKs
- Vision plugins require OpenCV; Halcon plugins require **both** OpenCV and Halcon
- VISA-based adapters (CLD101x, TED4015, Keithley) require NI-VISA

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
- **`include/safety/SafetyEventLog.h`** — Structured JSON-lines audit trail (`logs/safety_events.jsonl`), wired into ApplicationContext
- **`include/safety/SafetyWatchManager.h`** — I/O safety monitoring with E-stop broadcast to all hardware managers
- **`include/common/ThreadPriority.h`** — Cross-platform thread priority utility

### E-Stop Cascade
When `SafetyWatchManager` triggers, it broadcasts to: TECManager → `DisableAllOutputs()`, PowerSupplyManager → `disableAllOutputs()`, SMUManager → `OutputOffAll()`, PneumaticManager → `StopAll()`, LightingManager → `AllOff()`. All actions are logged to SafetyEventLog with CRITICAL severity.

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

## Important Files

- `Application.cpp` - App initialization, manager creation, hardware connection
- `include/context/ApplicationContext.h` - Central dependency container (struct)
- `include/motions/unified_motion_manager.h` - Motion control unified interface
- `include/sequence/SequenceEngine.h` - Command execution engine
- `include/sequence/SequenceBuilder.h` - Sequence construction API
- `include/sequence/CommandDispatcher.h` - Routes commands to handlers
- `include/recipe/IProcessPlugin.h` - Recipe plugin interface
- `include/recipe/ProcessPluginRegistry.h` - Plugin auto-registration
- `include/recipe/RecipeManager.h` - Recipe data management
- `include/ProgramManager/ProgramManager.h` - Recipe execution orchestration
- `include/ProgramManager/RecipeExecutionStateMachine.cpp` - Execution state machine
- `include/vision/IVisionPlugin.h` - Vision plugin interface
- `include/common/ErrorCode.h` - Unified error codes for the entire application
- `include/safety/SafetyEventLog.h` - Structured safety audit logging
- `include/IO/SocketGuard.h` - RAII wrapper for socket lifetime
- `CMakeLists.txt` - Build configuration (source lists, SDK detection, library linking)

## User Preferences

- User will compile the application themselves - skip `cmake --build` commands
- `#include <iostream>` has been added to SystemCommandHandler
