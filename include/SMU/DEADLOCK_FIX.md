# SMU Deadlock Fix - Summary

## Problem

**Error**: `resource deadlock would occur` when calling `SMUManager->ConnectAll()`

**Root Cause**: Mutex deadlock in `Keithley6482Adapter::Connect()`

### Deadlock Scenarios

1. **Connect() → QueryCommand() chain**:
   - `Connect()` acquires lock
   - Calls `QueryCommand()` which checks `m_connected` (false)
   - Fails because device not marked as connected yet

2. **Connect() → Disconnect() chain**:
   - `Connect()` acquires lock
   - On error, calls `Disconnect()`
   - `Disconnect()` tries to acquire same lock → **DEADLOCK**

3. **Connect() → SetIntegrationTime() chain**:
   - `Connect()` acquires lock
   - Calls `SetIntegrationTime()`
   - `SetIntegrationTime()` tries to acquire lock → **DEADLOCK**

4. **Connect() → SetMeasurementRange() chain**:
   - `Connect()` acquires lock
   - Calls `SetMeasurementRange()`
   - `SetMeasurementRange()` tries to acquire lock → **DEADLOCK**

---

## Solution

### 1. Use Recursive Mutex

**File**: `include/SMU/Keithley6482Adapter.h`

**Change**:
```cpp
// BEFORE:
mutable std::mutex m_mutex;

// AFTER:
mutable std::recursive_mutex m_mutex;  // Allow nested locking
```

**Benefit**: Same thread can acquire lock multiple times without deadlock

---

### 2. Refactor Connect() Method

**File**: `include/SMU/Keithley6482Adapter.cpp`

#### Change 1: Direct VISA Calls for *IDN?

Instead of calling `QueryCommand()` before `m_connected` is set:

```cpp
// BEFORE:
if (!QueryCommand("*IDN?", response)) {  // Fails because m_connected is false
  Disconnect();  // DEADLOCK!
  return false;
}

// AFTER:
char buffer[BUFFER_SIZE];
status = viPrintf(m_instrument, "*IDN?\n");  // Direct VISA call
status = viScanf(m_instrument, "%t", buffer);
m_deviceID = TrimString(std::string(buffer));
```

#### Change 2: Set m_connected BEFORE Calling Other Methods

```cpp
// BEFORE:
QueryCommand("*IDN?", response);
m_deviceID = response;
m_connected = true;  // Set AFTER initialization

WriteCommand("*RST");  // These would fail because m_connected is false

// AFTER:
// Query *IDN? using direct VISA
m_deviceID = TrimString(buffer);
m_connected = true;  // Set BEFORE calling other methods

WriteCommand("*RST");  // Now works because m_connected is true
SetIntegrationTime(...);  // Now works
SetMeasurementRange(...);  // Now works
```

#### Change 3: Manual Cleanup on Error

Instead of calling `Disconnect()` which tries to lock:

```cpp
// BEFORE:
if (error) {
  Disconnect();  // DEADLOCK - tries to lock again
  return false;
}

// AFTER:
if (error) {
  // Clean up manually without locking
  viClose(m_instrument);
  viClose(m_defaultRM);
  m_instrument = VI_NULL;
  m_defaultRM = VI_NULL;
  return false;
}
```

---

## Testing

After fix, expected output:

```
[Application] Initializing SMU system...
[SMUManager] Initialized
[Keithley6482] Connecting to: GPIB0::1::INSTR
[Keithley6482] Connected to: KEITHLEY INSTRUMENTS INC.,MODEL 6482,4599955,A02   Jan 18 2017 10:20:01/B01  /F
[Application] ✅ Connected to SMU: Keithley_6482
[Application] Device ID: KEITHLEY INSTRUMENTS INC.,MODEL 6482,4599955,A02   Jan 18 2017 10:20:01/B01  /F
[Application] Supported Mode: MEASURE_ONLY
[Application] Performing test measurement...
[Application] ✅ Test measurement successful!
[Application] Current reading: [value] A
```

---

## Files Modified

1. ✅ `include/SMU/Keithley6482Adapter.h` (line 125)
   - Changed `std::mutex` to `std::recursive_mutex`

2. ✅ `include/SMU/Keithley6482Adapter.cpp` (lines 31-114)
   - Refactored `Connect()` method
   - Use direct VISA calls for *IDN? query
   - Set `m_connected = true` before calling other methods
   - Manual cleanup instead of calling `Disconnect()`

---

## Status

✅ **FIXED** - Ready for testing with Keithley 6482 hardware

---

**Date**: 2025-10-31
**Issue**: Resource deadlock in SMU connection
**Resolution**: Recursive mutex + refactored Connect() method
