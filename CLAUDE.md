# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

This is a **CMake-based C++ Qt6 application** for industrial motion control and automation.

### Build Configuration
```bash
# Configure build (x64 Debug - most commonly used)
cmake --preset x64-debug

# Build the project
cmake --build out/build/x64-debug

# Run the executable
./out/build/x64-debug/bobcat_engine.exe
```

### Available Build Presets
- `x64-debug` - 64-bit Debug build (default for development)
- `x64-release` - 64-bit Release build
- `x86-debug` - 32-bit Debug build
- `x86-release` - 32-bit Release build

### Build Output Location
Built executables and runtime files are located in: `out/build/<preset-name>/`

Config files are automatically copied from `config/` to the build output directory.

## High-Level Architecture

### Application Structure
The application follows a **Manager-based architecture** with centralized access through `ApplicationContext`:

```
Application (main entry point)
    ├── ApplicationContext (dependency injection container)
    │   ├── UnifiedMotionManager (motion control)
    │   ├── CameraManager (camera operations)
    │   ├── EziIOManager (digital I/O control)
    │   ├── PneumaticManager (pneumatic slides)
    │   ├── TECManager (thermoelectric cooler control)
    │   ├── PowerSupplyManager (power supply control)
    │   ├── TcpClientManager (network communication)
    │   ├── DataStore (application-wide data storage)
    │   ├── LogManager (logging system)
    │   ├── SequenceEngine (command sequencing)
    │   ├── RecipeManager (process recipes)
    │   ├── ProgramManager (program execution)
    │   └── AdaptiveScanController (scanning operations)
    │
    └── MainWindow (Qt UI)
        ├── MotionPages (motion control UI)
        ├── RecipePage (recipe editing)
        ├── RunProgramPage (program execution)
        └── [other UI pages]
```

### Core System Components

#### 1. Motion Control System
- **UnifiedMotionManager** - Unified interface for multiple motion controller types
  - Supports **ACS** controllers (gantry systems)
  - Supports **PI** controllers (hexapods)
  - Automatic routing to correct controller based on device type
  - Named positions, waypoint system, coordinate transformation
  - Location: `include/motions/unified_motion_manager.h`

Configuration: `config/motor_devices.json`

#### 2. Sequence Engine
- Command-based execution system with dispatcher pattern
- Supports Actions, Loops, and Conditionals
- Handler registration for different device types:
  - `MotionCommandHandler` - motion commands
  - `IOCommandHandler` - I/O operations
  - `PneumaticCommandHandler` - pneumatic controls
  - `CameraCommandHandler` - camera operations
  - `TECCommandHandler` - TEC device commands
  - `SystemCommandHandler` - system operations (prompts, delays)
  - `DataStoreCommandHandler` - data storage operations
- Stop/pause capability with atomic flags
- Location: `include/sequence/SequenceEngine.h`

#### 3. Recipe System
- **RecipeManager** - Manages process recipes (collections of steps)
- **ProcessSequenceFactory** - Converts recipe steps to sequences
- **ProcessConfigWidget** - UI for editing process parameters
- Recipes stored as JSON with:
  - Process type (Pick, Place, Scan, etc.)
  - Parameters (device names, positions, settings)
  - Enable/disable flags
- Location: `include/recipe/RecipeManager.h`

#### 4. Camera System
- **CameraManager** - Multi-camera support
- **CameraHardwareFactory** - Creates camera adapters
- Supported camera types:
  - Pylon (Basler cameras)
  - IDS uEye
- Live view, multi-camera view, frame capture
- Location: `include/camera/CameraManager.h`

Configuration: `config/camera_config.json`

#### 5. I/O and Pneumatics
- **EziIOManager** - Digital I/O control (EziIO devices)
- **PneumaticManager** - Pneumatic slide control
- Configuration-driven device setup
- Locations:
  - `include/IO/EziIOManager.h`
  - `include/pneumatics/PneumaticManager.h`

#### 6. Scanning System
- **AdaptiveScanController** - Automated scanning operations
- Coarse-to-fine scanning strategy
- Data collection and result storage
- Location: `include/motions/scanning/AdaptiveScanController.h`

### Key Design Patterns

1. **Manager Pattern** - Each subsystem has a dedicated manager
2. **Dependency Injection** - ApplicationContext provides manager access
3. **Factory Pattern** - Hardware adapters created via factories
4. **Command/Handler Pattern** - SequenceEngine uses command dispatching
5. **Builder Pattern** - SequenceBuilder for constructing command sequences

## Configuration System

All hardware configuration is JSON-based in the `config/` directory:
- `camera_config.json` - Camera devices
- `motor_devices.json` - Motion controllers and named positions
- `io_devices.json` - Digital I/O devices
- `pneumatic_slides.json` - Pneumatic slide configurations
- `tec_devices.json` - TEC device settings
- `power_supply_config.json` - Power supply settings
- `tcp_clients.json` - TCP client connections
- `transformation_matrix.json` - Coordinate transformation matrices

Named positions for motion devices are stored in the motor_devices.json file under the `named_positions` section.

## Adding New Process Types to Recipe System

See `include/recipe/new process addition.md` for detailed guide.

Quick checklist:
1. Add to `RecipeManager::getAvailableProcessTypes()`
2. Add default parameters in `RecipeManager::getDefaultParameters()`
3. Register builder in `ProcessSequenceFactory` constructor
4. Create widget class in `ProcessConfigWidget.h`
5. Implement widget in `ProcessConfigWidget.cpp`
6. Register widget creation in `RecipePage::updateConfigurationForm()`
7. (Optional) Create new command handler if new device type

## External Dependencies

### Required SDKs (in `external/` directory)
- **Pylon 8** - Basler camera SDK
- **IDS uEye** - IDS camera SDK
- **ACSC** - ACS motion controller library
- **PI GCS2** - PI motion controller library
- **EziIO** - Fastech I/O controller library
- **nlohmann/json** - JSON parsing (header-only)

### Qt6 Dependencies
- Qt6::Widgets
- Qt6::Charts

CMake automatically detects which SDKs are present and enables/disables features accordingly.

## Key Concepts

### Named Positions
Motion devices can have saved positions with names (e.g., "home", "lensgrip", "safe"). These are stored in `motor_devices.json` and can be referenced in sequences and recipes.

### Waypoint System
The motion system includes automatic waypoint routing - devices can have exit/entry waypoints to ensure safe paths between positions.

### Coordinate Transformation
The system supports coordinate transformation matrices for converting between different reference frames (e.g., camera coordinates to motion coordinates).

### Calculated Positions
Positions can use offset notation (e.g., "lensgrip+Y1" or "safe+X-2+Z5") for runtime-calculated positions relative to saved positions.

## Important Files

- `Application.cpp` - Application initialization and manager setup
- `ApplicationContext.h` - Central dependency container
- `CMakeLists.txt` - Build configuration and library linking
- `include/motions/unified_motion_manager.h` - Motion control interface
- `include/sequence/SequenceEngine.h` - Command execution engine
- `include/recipe/RecipeManager.h` - Recipe data management
- `config/*.json` - Hardware configuration files
- i'll cmake build myself