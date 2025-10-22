# TEC Manager System

A multi-device TEC (Thermoelectric Cooler) management system following the same architectural pattern as UnifiedMotionManager.

## Architecture

```
TECManager (Main Manager)
    ├── ITECDevice (Interface)
    │   └── CLD101xAdapter (Hardware Implementation)
    ├── TECTypes.h (Data Structures)
    └── TECJsonLoader (Configuration Loader)
```

## File Structure

### Core Files
- **TECTypes.h** - Data structures (TECDevice, TECStatus, TECCommandResult)
- **ITECDevice.h** - Interface for TEC devices
- **TECManager.h/.cpp** - Main manager class
- **CLD101xAdapter.h/.cpp** - CLD101x hardware adapter
- **TECJsonLoader.h/.cpp** - JSON configuration loader

### Configuration
- **tec_config.json** - Device configuration file

### Example
- **tec_example.cpp** - Usage example

## Features

### Device Management
- Add/Remove devices dynamically
- Multi-device support
- Automatic routing to correct hardware adapter
- Enable/disable individual devices

### Connection Management
- Connect/Disconnect all devices
- Connect only enabled devices
- Per-device connection control
- Automatic reconnection

### TEC Control
- Set temperature setpoint (°C)
- Turn TEC on/off
- Read current temperature

### Laser Control
- Set laser current (mA)
- Turn laser on/off
- Read current laser current

### Status Queries
- Get device status (all parameters)
- Get connection status
- Get device lists
- Device count statistics

## Configuration Format

```json
{
  "tec_devices": [
    {
      "name": "laser-main",
      "controller_type": "CLD101x",
      "ip_address": "127.0.0.88",
      "port": 65432,
      "enabled": true,
      "default_temperature": 25.0,
      "default_laser_current": 50.0
    }
  ]
}
```

### Configuration Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| name | string | Yes | Unique device identifier |
| controller_type | string | Yes | Controller type (e.g., "CLD101x") |
| ip_address | string | Yes | Server IP address |
| port | int | No | Server port (default: 65432) |
| enabled | bool | No | Device enabled flag (default: true) |
| default_temperature | double | No | Default temperature setpoint (°C) |
| default_laser_current | double | No | Default laser current (mA) |

## Usage Example

```cpp
#include "TECManager.h"
#include "TECJsonLoader.h"
#include "WinSockGuard.h"

int main() {
    // Initialize WinSock
    WinSockGuard wsGuard;
    
    // Create manager
    TECManager tecManager;
    
    // Load configuration
    TECJsonLoader::LoadFromFile(tecManager, "tec_config.json");
    
    // Connect to enabled devices
    tecManager.ConnectEnabledDevices();
    
    // Use device
    std::string deviceName = "laser-main";
    
    // Set temperature and turn TEC on
    tecManager.SetTemperature(deviceName, 25.0);
    tecManager.TEC_On(deviceName);
    
    // Set laser current and turn laser on
    tecManager.SetLaserCurrent(deviceName, 50.0);
    tecManager.Laser_On(deviceName);
    
    // Read values
    auto tempResult = tecManager.ReadTemperature(deviceName);
    auto currentResult = tecManager.ReadLaserCurrent(deviceName);
    
    // Get status
    TECStatus status = tecManager.GetDeviceStatus(deviceName);
    
    // Turn off
    tecManager.Laser_Off(deviceName);
    tecManager.TEC_Off(deviceName);
    
    // Disconnect
    tecManager.DisconnectAll();
    
    return 0;
}
```

## Server Commands

The CLD101x adapter communicates with the server using these commands:

| Command | Parameter | Description |
|---------|-----------|-------------|
| SET_TEC_TEMPERATURE | temperature (°C) | Set TEC temperature setpoint |
| TEC_ON | - | Turn TEC on |
| TEC_OFF | - | Turn TEC off |
| READ_TEC_TEMPERATURE | - | Read current temperature |
| SET_LASER_CURRENT | current (A) | Set laser current |
| LASER_ON | - | Turn laser on |
| LASER_OFF | - | Turn laser off |
| READ_LASER_CURRENT | - | Read current laser current |
| OUTPUT1:STATE? | - | Query laser state |
| OUTPUT2:STATE? | - | Query TEC state |

**Note:** Laser current is in mA in the API but converted to A for the server.

## Adding New Device Types

To add support for new TEC controller types:

1. Create new adapter class implementing `ITECDevice`
2. Add case in `TECManager::CreateDeviceAdapter()`
3. Update JSON loader if needed

Example:

```cpp
// NewTECAdapter.h
class NewTECAdapter : public ITECDevice {
    // Implement all ITECDevice methods
};

// In TECManager.cpp
std::unique_ptr<ITECDevice> TECManager::CreateDeviceAdapter(const TECDevice& config) {
    std::string normalizedType = NormalizeType(config.TypeController);
    
    if (normalizedType == "CLD101X") {
        return std::make_unique<CLD101xAdapter>(config);
    }
    else if (normalizedType == "NEWTEC") {
        return std::make_unique<NewTECAdapter>(config);
    }
    
    return nullptr;
}
```

## Integration with Existing System

### Adding to ApplicationContext

```cpp
// ApplicationContext.h
struct ApplicationContext {
    UnifiedMotionManager* motionManager;
    EziIOManager* ioManager;
    PneumaticManager* pneumaticManager;
    CameraManager* cameraManager;
    TECManager* tecManager;  // ADD THIS
};
```

### Loading in Application

```cpp
// In Application.cpp initialization
m_context.tecManager = new TECManager();
TECJsonLoader::LoadFromFile(*m_context.tecManager, "tec_config.json");
m_context.tecManager->ConnectEnabledDevices();
```

## Dependencies

- **nlohmann/json** - JSON parsing
- **WinSock2** - TCP socket communication (Windows)
- **WinSockGuard** - RAII wrapper for WSAStartup/WSACleanup

## Notes

- All temperatures are in Celsius (°C)
- All laser currents are in milliamps (mA) in the API
- Server expects laser current in Amps (A) - conversion is automatic
- Connection timeout: 5000 ms
- Thread-safe: No (use mutex if accessing from multiple threads)

## Future Enhancements

- [ ] Add async/non-blocking operations
- [ ] Add command queue system
- [ ] Add temperature monitoring thread
- [ ] Add safety interlocks
- [ ] Add data logging
- [ ] Add command handler for sequence engine