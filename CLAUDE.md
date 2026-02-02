# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

This is a **CMake-based C++ Qt6 application** for industrial motion control and automation.

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
The build version auto-increments via `build_number.txt`. Override with:
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
    │   ├── SMUManager (source measure units)
    │   ├── SMUPollingManager (SMU data polling)
    │   ├── LightingManager (lighting controllers)
    │   ├── TcpClientManager (network communication)
    │   ├── DataStore (application-wide data storage)
    │   ├── LogManager (logging system)
    │   ├── SequenceEngine (command sequencing)
    │   ├── RecipeManager (process recipes)
    │   ├── VisionManager (image processing)
    │   ├── SafetyWatchManager (I/O safety monitoring)
    │   ├── AdaptiveScanController (scanning operations)
    │   ├── CoordinateSystemManager (coordinate transformations)
    │   ├── DeviceUnderTest (current DUT instance)
    │   ├── AuxWindowManager (auxiliary window controls)
    │   ├── DeviceLinkManager (linked device motion)
    │   └── GdiCameraWindowManager (GDI camera monitor)
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
  - `PSUCommandHandler` - power supply commands
  - `SMUCommandHandler` - source measure unit commands
  - `LightingCommandHandler` - lighting control commands
  - `VisionCommandHandler` - vision processing commands
  - `CalibrationCommandHandler` - calibration procedures
  - `DUTCommandHandler` - device under test operations
  - `AuxWindowCommandHandler` - auxiliary window controls
  - `CoordinateAlignCommandHandler` - coordinate alignment
  - `RelativePositionCommandHandler` - relative position calculations
  - `SequenceContextCommandHandler` - context variable operations
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

#### 7. Vision System (OpenCV/Halcon)
- **VisionManager** - Plugin-based image processing
- **IVisionPlugin** - Interface for vision algorithms (circle detection, edge detection, pattern matching)
- OpenCV plugins in `include/vision/plugins/`
- Halcon plugins available when SDK present
- Location: `include/vision/VisionManager.h`

#### 8. Safety System
- **SafetyWatchManager** - Monitors I/O states for safety conditions
- Halts motion/scanning when configured safety signals trigger
- Configuration: `config/safety_watch.json` (or `.local.json`)
- Location: `include/safety/SafetyWatchManager.h`

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
- `lighting_config.json` - Lighting controller settings
- `safety_watch.json` - Safety I/O monitoring configuration

### Local Config Override Pattern
The application uses a `*.local.json` pattern for user-specific configuration:
- Application first tries `config/xxx.local.json`, then falls back to `config/xxx.json`
- Local configs are gitignored - safe for machine-specific settings
- Example: `motor_devices.local.json` overrides `motor_devices.json`

Named positions for motion devices are stored in the motor_devices.json file under the `named_positions` section.

## Adding New Process Types to Recipe System

The recipe system uses a **plugin-based architecture** for process types. See `docs/creating_recipe_plugins.md` for the comprehensive guide.

### Quick Plugin Creation
1. Create plugin class implementing `IProcessPlugin` interface in `include/recipe/processes/`
2. Define metadata: `GetName()`, `GetCategory()`, `GetDescription()`
3. Implement `GetDefaultParameters()`, `CreateConfigUI()`, `BuildSequence()`
4. Register in auto-registration section at file bottom
5. If using a NEW category, add it to `RecipeManager::getProcessCategories()`
6. Add source file to CMakeLists.txt

Plugin files are organized by category: `MotionProcesses.cpp`, `IOProcesses.cpp`, `SystemProcesses.cpp`, etc.

### Legacy Approach (non-plugin)
See `include/recipe/new process addition.md` for the older manual approach requiring changes to multiple files.

## External Dependencies

### Required SDKs (in `external/` directory)
- **Pylon 8** - Basler camera SDK
- **IDS uEye** - IDS camera SDK
- **ACSC** - ACS motion controller library
- **PI GCS2** - PI motion controller library
- **EziIO** - Fastech I/O controller library
- **NI-VISA** - VISA instrument communication (for TEC, SMU devices)
- **OpenCV 4.12** - Computer vision (optional)
- **Halcon** - Machine vision library (optional, delay-loaded)
- **nlohmann/json** - JSON parsing (header-only)

### Qt6 Dependencies
- Qt6::Widgets
- Qt6::Charts

CMake automatically detects which SDKs are present and enables/disables features accordingly. Compile definitions `HAS_OPENCV` and `HAS_HALCON` indicate availability.

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
- `CMakePresets.json` - Build preset definitions
- `include/motions/unified_motion_manager.h` - Motion control interface
- `include/sequence/SequenceEngine.h` - Command execution engine
- `include/sequence/SequenceBuilder.h` - Sequence construction API
- `include/recipe/RecipeManager.h` - Recipe data management
- `include/recipe/IProcessPlugin.h` - Recipe plugin interface
- `include/recipe/ProcessPluginRegistry.h` - Plugin registration
- `config/*.json` - Hardware configuration files

## Adding New Commands

See `docs/ADDING_NEW_COMMANDS_GUIDE.md` for adding commands to the sequence system.

Command handlers are in `include/sequence/*CommandHandler.cpp`:
- `MotionCommandHandler` - Motion device commands (move, home, scan)
- `IOCommandHandler` - Digital I/O commands
- `PneumaticCommandHandler` - Pneumatic slide commands
- `CameraCommandHandler` - Camera capture commands
- `TECCommandHandler` - Temperature controller commands
- `SystemCommandHandler` - Wait, prompts, logging
- `DataStoreCommandHandler` - Data storage operations
- `PSUCommandHandler` - Power supply control
- `SMUCommandHandler` - Source measure unit control
- `LightingCommandHandler` - Lighting controller commands
- `VisionCommandHandler` - Vision processing commands (requires OpenCV)
- `CalibrationCommandHandler` - Calibration procedures
- `DUTCommandHandler` - Device under test operations
- `AuxWindowCommandHandler` - Auxiliary window commands
- `CoordinateAlignCommandHandler` - Coordinate alignment operations
- `RelativePositionCommandHandler` - Relative position calculations
- `SequenceContextCommandHandler` - Context variable manipulation

## Camera Configuration

Camera connection types in `camera_config.json`:
- `ip` - GigE cameras via IP address (e.g., `"ip_address": "192.168.0.68"`)
- `serial` - Any camera by serial number (works for USB cameras)
- `index` - Any camera by device index (works for USB cameras)
- `auto` - First available camera (works for USB cameras)

Example USB Basler camera config:
```json
{
  "id": "usb_camera",
  "camera_type": "PYLON",
  "connection_type": "serial",
  "serial_number": "YOUR_SERIAL"
}
```

## Sequence Context System

Commands can store and retrieve values at runtime using `SequenceContext`:
- Use `captureAs` parameter to store command results in context
- Reference context values with `context:variable_name` syntax
- See `docs/sequence_context_usage_guide.md` for details

Example:
```cpp
// In command handler - store result
context->Set("measured_value", std::to_string(result));

// In plugin - reference context value
sequence->AddAction(device, "MoveToAbs", {
  {"x", "context:calculated_x"}  // Uses value from context
});
```

## User Preferences

- User will compile the application themselves - skip `cmake --build` commands
- `#include <iostream>` has been added to SystemCommandHandler