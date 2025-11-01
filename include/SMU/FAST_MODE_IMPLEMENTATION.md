# Fast Mode Implementation - Complete

## Overview

Implemented fast measurement modes to match Python server's ~80 Hz performance in COARSE mode.

## Features Added

### 1. Measurement Modes (Enum)

Three performance/accuracy modes:

```cpp
enum class MeasurementMode {
  ACCURATE,    // NPLC=1.0, auto-zero ON, averaging ON (~10 Hz, high accuracy)
  FAST,        // NPLC=0.1, auto-zero ON, averaging ON (~80 Hz, good accuracy) - COARSE
  ULTRA_FAST   // NPLC=0.01, auto-zero OFF, averaging OFF (~150 Hz, lower accuracy) - MONITOR
};
```

### 2. Configuration Method

```cpp
bool ConfigureMeasurementMode(MeasurementMode mode);
```

Configures the Keithley 6482 with optimized settings for each mode.

### 3. Dual-Channel Reading

```cpp
std::pair<double, double> ReadBothChannels();
```

Reads both channels simultaneously (faster than reading separately).

---

## Mode Details

### ACCURATE Mode (Default)

**Target**: ~10 Hz, High Accuracy

**Settings**:
- NPLC = 1.0 (one power line cycle)
- Auto-zero: ON (accuracy)
- Averaging: ON (default)
- Auto-range: ON
- Display: ON
- Format: Single channel (READ)

**Use Case**: Precision measurements, calibration, final verification

**SCPI Commands**:
```
:SENS:CURR:NPLC 1.0
:SENS:CURR:RANG:AUTO ON
:SYST:AZER ON
:SENS:CURR:AVER:STAT ON
:DISP:ENAB ON
:FORM:ELEM READ
```

---

### FAST Mode (COARSE - Recommended for Alignment)

**Target**: ~80 Hz, Good Accuracy

**Settings**:
- NPLC = 0.1 (10x faster)
- Auto-zero: ON (maintain accuracy)
- Averaging: ON, 3 count, repeating filter
- Auto-range: ON (wide dynamic range)
- Display: ON
- Format: Dual-channel (CURR1,CURR2)

**Use Case**: **Optical/electrical alignment, real-time positioning**

**SCPI Commands**:
```
:SENS:CURR:NPLC 0.1
:SENS:CURR:RANG:AUTO ON
:SENS:CURR:AVER:STAT ON
:SENS:CURR:AVER:COUN 3
:SENS:CURR:AVER:TCON REP
:SYST:AZER ON
:DISP:ENAB ON
:FORM:ELEM CURR1,CURR2
```

**Python Equivalent**: `configure_for_alignment("AUTO")` in COARSE mode

---

### ULTRA_FAST Mode (MONITOR)

**Target**: ~150 Hz, Lower Accuracy

**Settings**:
- NPLC = 0.01 (minimum, 100x faster than ACCURATE)
- Auto-zero: OFF (speed over accuracy)
- Averaging: OFF (no filtering)
- Auto-range: OFF, Fixed 20mA range
- Display: OFF (reduce overhead)
- Continuous mode: ON
- Format: Dual-channel (CURR1,CURR2), ASCII

**Use Case**: Real-time monitoring, high-speed scanning

**SCPI Commands**:
```
:SENS:CURR:NPLC 0.01
:SYST:AZER OFF
:SENS:CURR:AVER:STAT OFF
:SENS:CURR:RANG:AUTO OFF
:SENS:CURR:RANG 0.02
:DISP:ENAB OFF
:INIT:CONT ON
:FORM:ELEM CURR1,CURR2
:FORM:DATA ASC
```

**Python Equivalent**: `configure_ultra_fast_mode()` in MONITOR mode

---

## Usage Examples

### Example 1: Switch to FAST mode for alignment

```cpp
auto keithley = GetDevice("Keithley_6482");

// Configure FAST mode (COARSE - like Python alignment mode)
keithley->ConfigureMeasurementMode(Keithley6482Adapter::MeasurementMode::FAST);

// Start polling at high speed
keithley->StartPolling(10);  // 10ms interval

// ... perform alignment ...

keithley->StopPolling();
```

### Example 2: Read both channels at once

```cpp
auto keithley = GetDevice("Keithley_6482");

// Configure FAST mode (enables dual-channel format)
keithley->ConfigureMeasurementMode(Keithley6482Adapter::MeasurementMode::FAST);

// Read both channels simultaneously
auto [ch1, ch2] = keithley->ReadBothChannels();

std::cout << "Channel 1: " << ch1 << " A" << std::endl;
std::cout << "Channel 2: " << ch2 << " A" << std::endl;
```

### Example 3: Maximum speed monitoring

```cpp
auto keithley = GetDevice("Keithley_6482");

// Configure ULTRA_FAST mode (maximum speed)
keithley->ConfigureMeasurementMode(Keithley6482Adapter::MeasurementMode::ULTRA_FAST);

// Start high-speed polling
keithley->StartPolling(5);  // 5ms interval (theoretical 200Hz)

// ... monitor in real-time ...

keithley->StopPolling();
```

---

## Files Modified

### 1. `Keithley6482Adapter.h`

**Added**:
- `enum class MeasurementMode` (ACCURATE, FAST, ULTRA_FAST)
- `bool ConfigureMeasurementMode(MeasurementMode mode)` - public method
- `std::pair<double, double> ReadBothChannels()` - dual-channel reading
- `MeasurementMode m_currentMode` - state tracking
- `bool ConfigureMeasurementModeInternal(MeasurementMode mode)` - internal method

### 2. `Keithley6482Adapter.cpp`

**Added** (lines 322-475):
- `ConfigureMeasurementMode()` - public wrapper with mutex lock
- `ConfigureMeasurementModeInternal()` - mode configuration implementation
  - ACCURATE mode setup
  - FAST mode setup (matches Python COARSE)
  - ULTRA_FAST mode setup (matches Python MONITOR)
- `ReadBothChannels()` - dual-channel reading implementation

**Modified**:
- Constructor: Initialize `m_currentMode` to `ACCURATE`
- Added `#include <utility>` for `std::pair`

### 3. `Application.cpp`

**Modified** (lines 1442-1508):
- Updated polling test to use FAST mode
- Added FAST mode configuration before polling
- Added dual-channel reading test
- Updated test output to show mode-specific results

---

## Performance Comparison

| Mode | NPLC | Auto-Zero | Averaging | Expected Hz | Previous Hz | Improvement |
|------|------|-----------|-----------|-------------|-------------|-------------|
| **ACCURATE** | 1.0 | ON | ON | ~10 Hz | 9.74 Hz | Baseline |
| **FAST** | 0.1 | ON | 3x REP | **~80 Hz** | 9.74 Hz | **8x faster** |
| **ULTRA_FAST** | 0.01 | OFF | OFF | **~150 Hz** | 9.74 Hz | **15x faster** |

### Expected Test Results

**ACCURATE Mode** (previous result):
```
Samples acquired: 49
Elapsed time: 5.029 seconds
Actual sampling rate: 9.74 Hz
Average period: 102.63 ms
```

**FAST Mode** (expected with NPLC=0.1):
```
Samples acquired: ~400
Elapsed time: 5.0 seconds
Actual sampling rate: ~80 Hz
Average period: ~12.5 ms
```

**ULTRA_FAST Mode** (expected with NPLC=0.01):
```
Samples acquired: ~750
Elapsed time: 5.0 seconds
Actual sampling rate: ~150 Hz
Average period: ~6.7 ms
```

---

## Testing Checklist

- [ ] Build succeeds without errors
- [ ] ACCURATE mode configures successfully
- [ ] FAST mode configures successfully
- [ ] ULTRA_FAST mode configures successfully
- [ ] FAST mode achieves ~80 Hz (matches Python)
- [ ] Dual-channel reading works correctly
- [ ] Both channel values are valid
- [ ] Mode switching works (ACCURATE → FAST → ULTRA_FAST)
- [ ] No crashes during mode changes
- [ ] Application shutdown is clean

---

## Next Steps (Optional)

1. **Add mode to polling**:
   - Allow StartPolling() to specify mode
   - Auto-configure mode when starting polling

2. **Dual-channel polling**:
   - Emit measurements for both channels
   - Add signals: `ch1MeasurementReady`, `ch2MeasurementReady`

3. **Smart mode switching**:
   - Auto-switch to FAST during alignment
   - Auto-switch to ACCURATE for final measurement

4. **Configuration persistence**:
   - Save preferred mode to config file
   - Auto-restore mode on connect

---

**Date**: 2025-10-31
**Feature**: Fast measurement modes matching Python COARSE/MONITOR performance
**Status**: ✅ Implementation complete - Ready for testing
**Expected Performance**: ~80 Hz in FAST mode (8x improvement)
