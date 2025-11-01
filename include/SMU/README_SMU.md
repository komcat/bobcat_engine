# SMU (Source Measure Unit) System - Design Documentation

## Overview

The SMU system provides a unified interface for controlling Source Measure Units (SMU) and related measurement devices. It follows the same architecture pattern as the Power Supply and TEC systems in the bobcat_engine project.

---

## Architecture Pattern

### Component Structure

```
ISMUDevice (Interface)
    ↑
    | implements
    |
Keithley6482Adapter (Concrete Implementation)
    ↑
    | managed by
    |
SMUManager
    ↑
    | configured by
    |
SMUJsonLoader → reads → config/smu_devices.json
```

### File Organization

```
include/SMU/
├── ISMUDevice.h              - Pure virtual interface for all SMU devices
├── SMUManager.h/cpp          - Manager for multiple SMU devices (QObject)
├── SMUTypes.h                - Common types, enums, and structures
├── SMUJsonLoader.h/cpp       - Configuration loader (factory pattern)
├── Keithley6482Adapter.h/cpp - Keithley 6482 Picoammeter driver
└── README_SMU.md             - This file

config/
└── smu_devices.json          - Device configuration file
```

---

## Design Pattern Details

### 1. Interface Layer (ISMUDevice)

**Purpose**: Define common operations for all SMU devices

**Key Methods**:
- Connection: `Connect()`, `Disconnect()`, `IsConnected()`
- Device Info: `GetDeviceName()`, `GetDeviceID()`, `GetSupportedMode()`
- Output Control: `SetOutputOn()`, `SetOutputOff()`, `IsOutputOn()`
- Source Control: `SetVoltage()`, `SetCurrent()`, `SetCompliance()`
- Measurement: `ReadVoltage()`, `ReadCurrent()`, `MeasureCurrent()`, `MeasureVoltage()`
- Configuration: `SetMeasurementRange()`, `SetIntegrationTime()`, `SetZeroCheck()`
- Error Handling: `GetLastError()`, `ClearError()`

**Features**:
- Pure virtual interface (no implementation)
- Supports different SMU modes (voltage source, current source, measure-only)
- Thread-safe design (implementations use mutex)

---

### 2. Manager Layer (SMUManager)

**Purpose**: Manage multiple SMU devices, handle lifecycle and routing

**Key Features**:
- Device registration and lookup (`AddDevice()`, `GetDevice()`)
- Batch operations (`ConnectAll()`, `DisconnectAll()`, `OutputOffAll()`)
- Qt signals for UI integration (`deviceAdded`, `deviceConnectionChanged`)
- Thread-safe access to device map

**Typical Usage**:
```cpp
SMUManager* manager = new SMUManager();
SMUJsonLoader::LoadConfiguration("config/smu_devices.json", manager);

SMUDevicePtr smu = manager->GetDevice("Keithley_6482");
if (smu && smu->Connect()) {
  double current = smu->ReadCurrent();
  std::cout << "Current: " << current << " A" << std::endl;
}
```

---

### 3. Adapter Layer (Device-Specific Implementations)

**Purpose**: Implement ISMUDevice interface for specific hardware

**Current Adapters**:
- **Keithley6482Adapter**: Keithley 6482 Picoammeter (dual-channel current measurement)

**Adapter Responsibilities**:
- VISA communication (GPIB/USB/Ethernet)
- SCPI command translation
- Error handling and reporting
- Thread safety (using mutex)
- Device-specific initialization

**Example: Keithley 6482**
```cpp
Keithley6482Adapter adapter("GPIB0::1::INSTR", config);

if (adapter.Connect()) {
  adapter.SetIntegrationTime(5.0);         // 5 NPLC (slower but more accurate)
  adapter.SetMeasurementRange(2e-9, true); // 2nA range, auto-range enabled
  adapter.PerformZeroCorrection();         // Auto-zero

  double current = adapter.ReadCurrent();  // Read current
  std::cout << "Measured: " << current << " A" << std::endl;
}
```

---

### 4. Configuration Layer (SMUJsonLoader)

**Purpose**: Factory pattern - create devices from JSON configuration

**Configuration Format**:
```json
{
  "devices": [
    {
      "name": "Keithley_6482",
      "type": "keithley_6482",
      "visa_resource": "GPIB0::1::INSTR",
      "enabled": true,
      "measurement_nplc": 5,
      "auto_range": true,
      "fixed_range": 0.0
    }
  ]
}
```

**Supported Fields**:
- `name` (required): Device name for lookup
- `type` (required): Device type identifier
- `visa_resource` (required): VISA resource string
- `enabled`: Enable/disable device (default: true)
- `default_voltage`: Default source voltage (V)
- `default_current`: Default source current (A)
- `default_compliance`: Default compliance limit
- `measurement_nplc`: Integration time (Number of Power Line Cycles)
- `auto_range`: Enable auto-ranging
- `fixed_range`: Fixed measurement range (if auto_range is false)

---

## Device Support

### Current Devices

#### Keithley 6482 Picoammeter

**Model**: Keithley 6482 (Dual-channel picoammeter)

**Capabilities**:
- ✅ Dual-channel current measurement
- ✅ Range: 2nA to 21mA full scale
- ✅ Resolution: 10fA (femtoamp)
- ❌ NO voltage/current sourcing (measurement-only)

**VISA Resource**: `GPIB0::1::INSTR`

**SCPI Commands**:
```
*IDN?                          - Get device identification
:SENS:CURR:RANG <range>        - Set current range (e.g., 2e-9 for 2nA)
:SENS:CURR:RANG:AUTO ON/OFF    - Auto-range on/off
:SENS:CURR:NPLC <value>        - Set integration time (0.01 to 10)
:READ?                         - Read current measurement
:SYST:ZCH ON/OFF               - Zero check on/off
:SYST:ZCOR:ACQ                 - Perform zero correction
```

**Example Configuration**:
```json
{
  "name": "Keithley_6482",
  "type": "keithley_6482",
  "visa_resource": "GPIB0::1::INSTR",
  "enabled": true,
  "measurement_nplc": 5,
  "auto_range": true
}
```

---

### Future Devices (To Be Implemented)

#### Keithley 2400 SourceMeter
- Full SMU: Source voltage/current, measure both
- 4-wire sensing
- ±210V, ±1.05A

#### Keysight B2900 Series SMU
- Precision SMU
- Wide voltage/current ranges
- Fast measurement speed

---

## Integration with ApplicationContext

To integrate SMU system into the main application:

### 1. Add to ApplicationContext.h
```cpp
#include "SMUManager.h"

struct ApplicationContext {
  // ... existing managers ...
  SMUManager* smuManager;
};
```

### 2. Initialize in Application.cpp
```cpp
#include "SMUJsonLoader.h"

// Create SMU Manager
m_context->smuManager = new SMUManager(this);

// Load configuration
std::string smuConfigPath = "config/smu_devices.json";
if (QFile::exists(QString::fromStdString(smuConfigPath))) {
  SMUJsonLoader::LoadConfiguration(smuConfigPath, m_context->smuManager);
  m_context->smuManager->ConnectAll();
} else {
  std::cout << "[Application] SMU config not found, skipping..." << std::endl;
}
```

### 3. Add to CMakeLists.txt
```cmake
# SMU System (VISA-based)
if(VISA_FOUND)
  list(APPEND SOURCES
    include/SMU/SMUManager.cpp
    include/SMU/SMUJsonLoader.cpp
    include/SMU/Keithley6482Adapter.cpp
  )
  list(APPEND HEADERS
    include/SMU/ISMUDevice.h
    include/SMU/SMUManager.h
    include/SMU/SMUTypes.h
    include/SMU/SMUJsonLoader.h
    include/SMU/Keithley6482Adapter.h
  )
  message(STATUS "SMU system enabled")
endif()
```

---

## Usage Examples

### Basic Current Measurement
```cpp
// Get SMU from manager
SMUDevicePtr smu = context->smuManager->GetDevice("Keithley_6482");

if (smu && smu->Connect()) {
  // Set high accuracy mode
  smu->SetIntegrationTime(10.0);  // 10 NPLC (~167ms @ 60Hz)

  // Perform zero correction
  smu->PerformZeroCorrection();

  // Measure current
  SMUMeasurement result = smu->MeasureCurrent();
  if (result.success) {
    std::cout << "Current: " << result.value << " A" << std::endl;
    std::cout << "Timestamp: " << result.timestamp << std::endl;
  } else {
    std::cerr << "Error: " << result.errorMessage << std::endl;
  }
}
```

### Continuous Monitoring
```cpp
SMUDevicePtr smu = context->smuManager->GetDevice("Keithley_6482");

if (smu && smu->Connect()) {
  smu->SetIntegrationTime(1.0);  // 1 NPLC (fast)
  smu->SetMeasurementRange(2e-9, true);  // 2nA range, auto

  for (int i = 0; i < 100; i++) {
    double current = smu->ReadCurrent();
    std::cout << "Sample " << i << ": " << current << " A" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
```

### Recipe Integration (Sequence Commands)
```cpp
// In SequenceEngine, add SMUCommandHandler

class SMUCommandHandler : public ISequenceCommandHandler {
  bool Handle(const SequenceCommand& cmd, ApplicationContext* ctx) override {
    if (cmd.DeviceType != SequenceDeviceType::SMU) return false;

    SMUDevicePtr smu = ctx->smuManager->GetDevice(cmd.DeviceName);
    if (!smu) return false;

    if (cmd.Action == "measure_current") {
      double current = smu->ReadCurrent();
      ctx->dataStore->SetValue("smu_current", current);
      return true;
    }

    return false;
  }
};
```

---

## Testing Checklist

### Unit Tests
- [ ] SMUManager device add/remove
- [ ] SMUManager batch operations
- [ ] JSON loader parsing
- [ ] JSON loader device creation
- [ ] Keithley6482 connection
- [ ] Keithley6482 measurement
- [ ] Error handling

### Integration Tests
- [ ] Load from config file
- [ ] Connect all devices
- [ ] Perform measurements
- [ ] Handle disconnection
- [ ] Recipe integration

### Hardware Tests (with real Keithley 6482)
- [ ] GPIB connection
- [ ] Device identification (*IDN?)
- [ ] Current measurement
- [ ] Range settings
- [ ] Zero correction
- [ ] Channel selection
- [ ] NPLC settings

---

## Development Status

### ✅ Completed
- [x] Interface design (ISMUDevice)
- [x] Manager implementation (SMUManager)
- [x] Types and enums (SMUTypes)
- [x] JSON loader (SMUJsonLoader)
- [x] Keithley 6482 adapter (header + implementation)
- [x] Configuration file template
- [x] Documentation

### ⏳ Pending
- [ ] CMakeLists.txt integration
- [ ] ApplicationContext integration
- [ ] UI page for SMU control
- [ ] Sequence command handler
- [ ] Additional device adapters (Keithley 2400, Keysight B2900)
- [ ] Unit tests
- [ ] Hardware validation

---

## Related Files

### Power Supply System (Reference Implementation)
- `include/PSU/IPowerSupply.h`
- `include/PSU/PowerSupplyManager.h`
- `include/PSU/KeysightE36103B.h`

### TEC System (Reference Implementation)
- `include/TEC/ITECDevice.h`
- `include/TEC/TECManager.h`
- `include/TEC/CLD101xDirectAdapter.h`

### VISA Integration
- `external/visa/` - VISA SDK
- `docs/Working_with_VISA_Devices.md` - VISA setup guide
- `find_visa_devices.cpp` - VISA device scanner utility

---

## Notes

1. **Thread Safety**: All adapters use `std::mutex` to ensure thread-safe operations
2. **VISA Runtime Required**: NI-VISA Runtime must be installed on the system
3. **Error Handling**: All methods return bool/value with error stored in `GetLastError()`
4. **Not All Methods Apply**: Picoammeter (6482) doesn't support voltage/current sourcing
5. **Qt Integration**: Manager uses Qt signals for UI updates

---

## Support

For questions or issues:
1. Check VISA Quick Reference: `docs/VISA_Quick_Reference.md`
2. Review Power Supply implementation (similar pattern)
3. Use find_visa_devices tool to verify connections
4. Check device manual for SCPI commands

---

**Status**: ✅ Design Complete - Ready for CMake Integration and Testing
**Last Updated**: 2025-10-31
**Version**: 1.0
