# Fast Mode Analysis - Python vs C++ Implementation

## Python Implementation Analysis

The Python code achieves ~80 Hz in COARSE mode using these optimizations:

### Configuration for COARSE Mode (lines 293-347)

```python
def configure_for_alignment(self, initial_range: str = "AUTO"):
    # For AUTO mode (COARSE):
    self.multimeter.write(":SENS1:CURR:RANG:AUTO ON")
    self.multimeter.write(":SENS2:CURR:RANG:AUTO ON")
    self.multimeter.write(":SENS1:CURR:NPLC 0.1")      # ← KEY: 0.1 NPLC (10x faster)
    self.multimeter.write(":SENS2:CURR:NPLC 0.1")

    # Averaging with repeating filter
    self.multimeter.write(":SENS1:CURR:AVER:STAT ON")
    self.multimeter.write(":SENS1:CURR:AVER:COUN 3")
    self.multimeter.write(":SENS1:CURR:AVER:TCON REP")

    # Auto-zero ON for accuracy
    self.multimeter.write(":SYST:AZER ON")

    # Read both channels at once
    self.multimeter.write(":FORM:ELEM CURR1,CURR2")
```

**Polling Loop** (line 231):
```python
time.sleep(0.01)  # 10ms delay = theoretical 100Hz
```

### Ultra-Fast Mode (lines 370-421)

For MONITOR mode (maximum speed):

```python
def configure_ultra_fast_mode(self):
    # Minimum NPLC
    self.multimeter.write(":SENS1:CURR:NPLC 0.01")     # ← CRITICAL: 0.01 NPLC
    self.multimeter.write(":SENS2:CURR:NPLC 0.01")

    # Disable accuracy features for speed
    self.multimeter.write(":SYST:AZER OFF")            # ← Auto-zero OFF
    self.multimeter.write(":SENS1:CURR:AVER:STAT OFF") # ← Averaging OFF
    self.multimeter.write(":SENS2:CURR:AVER:STAT OFF")

    # Fixed range (no auto-ranging overhead)
    self.multimeter.write(":SENS1:CURR:RANG:AUTO OFF")
    self.multimeter.write(":SENS1:CURR:RANG 0.02")     # ← 20mA fixed range
    self.multimeter.write(":SENS2:CURR:RANG:AUTO OFF")
    self.multimeter.write(":SENS2:CURR:RANG 0.02")

    # Display OFF for speed
    self.multimeter.write(":DISP:ENAB OFF")            # ← Display OFF

    # Continuous measurement mode
    self.multimeter.write(":INIT:CONT ON")

    # Read both channels simultaneously
    self.multimeter.write(":FORM:ELEM CURR1,CURR2")
    self.multimeter.write(":FORM:DATA ASC")
```

**Ultra-Fast Read** (lines 576-601):
```python
def read_ultra_fast(self):
    # Use PyVISA's optimized ASCII values reader
    response = self.multimeter.query_ascii_values(":READ?")
    values = {1: response[0], 2: response[1]}
    return values
```

---

## Our Current C++ Implementation (9.74 Hz)

### Current Settings:
```cpp
Connect() {
    SetIntegrationTimeInternal(m_config.MeasurementNPLC);  // = 1.0 NPLC
    SetMeasurementRangeInternal(m_config.FixedRange, false);  // Auto-range
}

ReadCurrent() {
    QueryCommandInternal(":READ?", response);  // Reads single channel
    return std::stod(response);
}
```

**Polling Test**:
```cpp
keithley->StartPolling(10);  // 10ms interval
```

**Result**: 9.74 Hz (102.63 ms per measurement)

---

## Why We're Slower

| Setting | Python (COARSE) | Python (ULTRA-FAST) | Our C++ | Impact |
|---------|----------------|---------------------|---------|--------|
| **NPLC** | 0.1 | 0.01 | **1.0** | **10-100x slower** |
| **Auto-zero** | ON | OFF | ON (default) | 2x slower |
| **Averaging** | ON (3 count) | OFF | ON (default) | 1.5x slower |
| **Display** | ON | OFF | ON (default) | Minor |
| **Range** | Auto | Fixed (20mA) | Auto | Minor overhead |
| **Channels** | Both at once | Both at once | **Single** | 2x slower for dual |

**Key Bottleneck**: NPLC = 1.0

- **NPLC = 1.0**: ~16.7 ms integration + GPIB overhead (~80-100ms total)
- **NPLC = 0.1**: ~1.67 ms integration + GPIB overhead (~10-15ms total)
- **NPLC = 0.01**: ~0.167 ms integration + GPIB overhead (~1-5ms total)

---

## Recommended C++ Implementation

### Option 1: COARSE Mode (~50-80 Hz)

Match Python's alignment configuration:

```cpp
bool Keithley6482Adapter::ConfigureFastMode()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_connected) return false;

    // NPLC = 0.1 (10x faster than current)
    WriteCommandInternal(":SENS:CURR:NPLC 0.1");

    // Auto-range ON (for wide dynamic range during alignment)
    WriteCommandInternal(":SENS:CURR:RANG:AUTO ON");

    // Averaging: 3 count, repeating (noise reduction)
    WriteCommandInternal(":SENS:CURR:AVER:STAT ON");
    WriteCommandInternal(":SENS:CURR:AVER:COUN 3");
    WriteCommandInternal(":SENS:CURR:AVER:TCON REP");

    // Auto-zero ON (maintain accuracy)
    WriteCommandInternal(":SYST:AZER ON");

    // Read both channels simultaneously
    WriteCommandInternal(":FORM:ELEM CURR1,CURR2");

    return true;
}

// Read both channels at once
std::pair<double, double> ReadBothChannels()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string response;
    if (!QueryCommandInternal(":READ?", response)) {
        return {-999.0, -999.0};
    }

    // Parse "1.234e-6,5.678e-7"
    size_t comma = response.find(',');
    if (comma != std::string::npos) {
        double ch1 = std::stod(response.substr(0, comma));
        double ch2 = std::stod(response.substr(comma + 1));
        return {ch1, ch2};
    }

    return {-999.0, -999.0};
}
```

**Expected Rate**: ~50-80 Hz (12-20 ms per measurement)

### Option 2: ULTRA-FAST Mode (~100+ Hz)

Maximum speed (sacrifice some accuracy):

```cpp
bool Keithley6482Adapter::ConfigureUltraFastMode()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_connected) return false;

    // Minimum NPLC
    WriteCommandInternal(":SENS:CURR:NPLC 0.01");

    // Disable accuracy features for speed
    WriteCommandInternal(":SYST:AZER OFF");           // Auto-zero OFF
    WriteCommandInternal(":SENS:CURR:AVER:STAT OFF"); // Averaging OFF

    // Fixed range (20mA)
    WriteCommandInternal(":SENS:CURR:RANG:AUTO OFF");
    WriteCommandInternal(":SENS:CURR:RANG 0.02");     // 20mA

    // Display OFF (if supported)
    try {
        WriteCommandInternal(":DISP:ENAB OFF");
    } catch (...) {
        // Some units don't support this
    }

    // Continuous measurement mode
    WriteCommandInternal(":INIT:CONT ON");

    // Read both channels
    WriteCommandInternal(":FORM:ELEM CURR1,CURR2");
    WriteCommandInternal(":FORM:DATA ASC");

    return true;
}
```

**Expected Rate**: ~100-150 Hz (7-10 ms per measurement)

---

## Comparison Summary

| Mode | NPLC | Auto-Zero | Averaging | Range | Expected Hz | Use Case |
|------|------|-----------|-----------|-------|-------------|----------|
| **Current (Accurate)** | 1.0 | ON | Default | Auto | **9.74 Hz** | Precision measurement |
| **COARSE (Python)** | 0.1 | ON | 3x REP | Auto | **~80 Hz** | **Alignment (recommended)** |
| **ULTRA-FAST** | 0.01 | OFF | OFF | Fixed | **~150 Hz** | Real-time monitoring |

---

## Implementation Plan

1. Add configuration modes to `Keithley6482Adapter`:
   - `ConfigureAccurateMode()` - Current (NPLC=1.0)
   - `ConfigureFastMode()` - COARSE (NPLC=0.1)
   - `ConfigureUltraFastMode()` - MONITOR (NPLC=0.01)

2. Add dual-channel reading:
   - `ReadBothChannels()` - Read Ch1 & Ch2 simultaneously
   - Update polling to optionally read both channels

3. Update polling:
   - Add mode selection parameter
   - Emit separate signals for Ch1 and Ch2

4. Test each mode and measure actual Hz rate

---

**Recommendation**: Implement **COARSE mode** (NPLC=0.1) as default for polling to match Python's ~80 Hz performance while maintaining reasonable accuracy.

**Date**: 2025-10-31
**Analysis**: Python vs C++ speed comparison for Keithley 6482
