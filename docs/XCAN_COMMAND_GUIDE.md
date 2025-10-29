# XCAN Command - Hexapod Adaptive Scanning

## Overview

The `XCAN` command provides adaptive scanning functionality for PI hexapod devices within sequence execution. It performs multi-axis scanning to find optimal positions by maximizing a specified data channel value.

**Category:** Motion
**Command Name:** `XCAN`
**Device Type:** PI Hexapods only (device name must match `hex-*` pattern)

## Parameters

### Required Parameters

| Parameter | Type | Description | Valid Values |
|-----------|------|-------------|--------------|
| `mode` | string | Scan mode | `coarse` or `fine` |
| `channel` | string | Data channel to optimize | Any valid DataStore channel name |

### Optional Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `settling_time_ms` | integer | 300 | Motion settling time in milliseconds |

## Scan Modes

### Coarse Mode (`mode=coarse`)
- **Step sizes:** 10 → 5 → 2 → 1 µm (0.010, 0.005, 0.002, 0.001 mm)
- **Use case:** Initial rough positioning, larger search area
- **Typical duration:** 2-5 minutes depending on data
- **Corresponds to:** "Coarse" button in UI

### Fine Mode (`mode=fine`)
- **Step sizes:** 2 → 1 → 0.5 µm (0.002, 0.001, 0.0005 mm)
- **Use case:** Precise positioning after coarse scan
- **Typical duration:** 1-3 minutes depending on data
- **Corresponds to:** "Fine" button in UI

## Scan Execution Details

### Axis Scan Order
The scan executes in **ZYX** order:
1. **Z axis** - First axis scanned
2. **Y axis** - Second axis scanned
3. **X axis** - Third axis scanned

For each axis, the scan:
1. Starts at current position
2. Performs adaptive search using step sizes
3. Finds local maximum for that axis
4. Moves to found maximum before scanning next axis

### Scan Algorithm
- **Strategy:** Coarse-to-fine adaptive search
- **Direction:** Bidirectional (+ and - from current position)
- **Termination:** Based on consecutive decreases or distance limit
- **Safety:** Respects max distance limit (200µm = 0.2mm per direction)

### Scan Parameters (Fixed)

| Parameter | Value | Description |
|-----------|-------|-------------|
| Velocity | 50% | Motion speed during scan |
| Averaging Count | 3 | Number of measurements per position |
| Consecutive Decreases Limit | 3 | Stop after 3 consecutive drops |
| Significant Decrease Threshold | 5% | What constitutes a "decrease" |
| Max Total Distance | 0.2 mm | Maximum travel per direction (200µm) |
| Improvement Threshold | 1% | Minimum improvement to continue |
| Measurement Timeout | 5 seconds | Max wait for each measurement |
| Scan Timeout | 10 minutes | Maximum total scan duration |
| Max Step Size | 0.5 mm | Safety limit on single step (500µm) |

## Device Name Validation

**The XCAN command ONLY accepts PI hexapod devices.**

- ✅ Valid: `hex-left`, `hex-right`, `hex-main`, `hex-alignment`
- ❌ Invalid: `Gantry`, `stage1`, `LinearMotor`, etc.

Device names must match the pattern: `^hex-.*`

If an invalid device name is provided, the command will fail with:
```
XCAN: Device must be a PI hexapod (hex-*), got: {deviceName}
```

## Usage Examples

### Example 1: Basic Coarse Scan
```json
{
  "manager": "motion",
  "device": "hex-left",
  "method": "XCAN",
  "params": {
    "mode": "coarse",
    "channel": "PD_Signal"
  }
}
```

### Example 2: Fine Scan with Custom Settling Time
```json
{
  "manager": "motion",
  "device": "hex-right",
  "method": "XCAN",
  "params": {
    "mode": "fine",
    "channel": "OpticalPower",
    "settling_time_ms": "500"
  }
}
```

### Example 3: Two-Stage Scan (Coarse → Fine)
```json
[
  {
    "manager": "motion",
    "device": "hex-left",
    "method": "XCAN",
    "params": {
      "mode": "coarse",
      "channel": "PD_Signal"
    }
  },
  {
    "manager": "motion",
    "device": "hex-left",
    "method": "XCAN",
    "params": {
      "mode": "fine",
      "channel": "PD_Signal",
      "settling_time_ms": "400"
    }
  }
]
```

## Behavior

### Success Conditions
The command succeeds when:
- ✅ Scan completes without errors
- ✅ A valid peak is found
- ✅ Device is positioned at optimal location

Return value: `true`

### Failure Conditions
The command fails when:
- ❌ Device name doesn't match `hex-*` pattern
- ❌ Required parameters are missing
- ❌ Data channel doesn't exist
- ❌ DataStore is not available
- ❌ Scan encounters an error
- ❌ No peak is found during scan
- ❌ User requests stop during scan

Return value: `false` with error message

### Stop Behavior
The XCAN command respects the sequence stop flag:
- User presses STOP button → scan halts immediately
- Device returns to best position found so far
- Command returns `false` with "Scan stopped by user" message

## Logging

The XCAN command logs comprehensive information:

**Start:**
```
XCAN: Starting coarse scan on hex-left for channel 'PD_Signal' (settling=300ms)
```

**During Scan:**
```
Scan: 25.5% - Scanning Z axis, step 10µm
Scan: 50.0% - Scanning Y axis, step 5µm
Scan: 75.0% - Scanning X axis, step 2µm
```

**Success:**
```
XCAN: Scan complete - Peak value: 0.856234
XCAN: Scan completed successfully
```

**Error:**
```
XCAN: Scan error - Device motion timeout
```

**Stop:**
```
XCAN: Stop requested, halting scan
```

## Integration with Recipe/Program System

### In Recipe JSON
```json
{
  "process_type": "CustomSequence",
  "enabled": true,
  "name": "Align Hexapod Left",
  "actions": [
    {
      "manager": "motion",
      "device": "hex-left",
      "method": "MoveToNamedPosition",
      "params": {
        "position": "safe"
      }
    },
    {
      "manager": "motion",
      "device": "hex-left",
      "method": "XCAN",
      "params": {
        "mode": "coarse",
        "channel": "PD_Signal"
      }
    },
    {
      "manager": "motion",
      "device": "hex-left",
      "method": "XCAN",
      "params": {
        "mode": "fine",
        "channel": "PD_Signal"
      }
    }
  ]
}
```

### In Sequence Builder
```cpp
SequenceBuilder builder;
builder
  .Motion("hex-left", "MoveToNamedPosition")
    .Param("position", "safe")
  .Motion("hex-left", "XCAN")
    .Param("mode", "coarse")
    .Param("channel", "PD_Signal")
  .Motion("hex-left", "XCAN")
    .Param("mode", "fine")
    .Param("channel", "PD_Signal")
    .Param("settling_time_ms", "400");

auto sequence = builder.Build();
```

## Dependencies

The XCAN command requires:
- `UnifiedMotionManager` - For motion control
- `DataStore` - For reading sensor data
- `AdaptiveScanController` - For scan algorithm
- `SequenceContext` - For stop flag and DataStore access
- `ILogger` - For logging (optional but recommended)

## Technical Implementation

### File Location
`include/sequence/MotionCommandHandler.cpp` (lines 469-651)

### Key Classes Used
- `AdaptiveScanController` - Performs the actual scanning
- `ScanParameters` - Configuration for scan behavior
- `ScanResults` - Contains peak position and value
- `ScanError` - Error information if scan fails

### Synchronization
The command uses:
- `std::atomic<bool>` for completion flags
- `std::mutex` for thread-safe state access
- `std::condition_variable` for waiting on completion
- Callbacks to receive scan events from background thread

### Stop Flag Monitoring
The command checks `context->stopRequested` every 100ms:
```cpp
while (!scanComplete) {
  if (context && context->stopRequested.load()) {
    scanController.HaltScan();
    return false;
  }
  completionCV.wait_for(lock, std::chrono::milliseconds(100));
}
```

## Comparison: XCAN vs UI Scanning

| Feature | XCAN Command | UI Scanning (RunProgramPage) |
|---------|--------------|------------------------------|
| Device Selection | Automatic from sequence | Manual button selection |
| Mode Selection | Parameter `mode` | Separate buttons (Coarse/Fine) |
| Channel Selection | Parameter `channel` | Dropdown selection |
| Stop Behavior | Respects sequence stop flag | UI stop button |
| Logging | SequenceContext logger | UI log widget + LogManager |
| Blocking | Yes (waits for completion) | No (UI remains responsive) |
| Use Case | Automated sequences/programs | Manual interactive operation |

## Error Messages Reference

| Error Message | Cause | Solution |
|---------------|-------|----------|
| `Device must be a PI hexapod (hex-*)` | Invalid device type | Use hex-left, hex-right, etc. |
| `Missing required parameter 'mode'` | No mode specified | Add `"mode": "coarse"` or `"mode": "fine"` |
| `Invalid mode '{mode}'` | Invalid mode value | Use "coarse" or "fine" only |
| `Missing required parameter 'channel'` | No channel specified | Add `"channel": "PD_Signal"` |
| `DataStore not available in context` | Context setup issue | Ensure DataStore is initialized |
| `Failed to start scan` | Controller initialization failed | Check device connection and state |
| `Scan failed - {error}` | Runtime scan error | See detailed error message |
| `Scan completed but no peak found` | No maximum detected | Check sensor data and channel |
| `Scan stopped by user` | Stop button pressed | Expected behavior, not an error |

## Best Practices

### 1. Two-Stage Scanning
For best results, perform coarse then fine:
```json
["XCAN coarse", "XCAN fine"]
```

### 2. Move to Safe Position First
Always move to a known good starting position:
```json
["MoveToNamedPosition(safe)", "XCAN coarse"]
```

### 3. Settling Time Tuning
- Start with default (300ms)
- Increase for heavier payloads (400-500ms)
- Decrease for lighter setups (200-250ms)

### 4. Channel Verification
Ensure channel exists and updates properly:
```cpp
// Before scanning
if (dataStore->ChannelExists(channelName)) {
  // Good to scan
}
```

### 5. Timeout Handling
XCAN has built-in 10-minute timeout. For longer scans, consider:
- Reducing averaging count
- Increasing step sizes
- Splitting into multiple scan commands

## Future Enhancements

Potential improvements:
1. **Custom Axis Selection** - Allow specifying which axes to scan
2. **Custom Step Sizes** - Override default step size arrays
3. **Result Capture** - Save peak position to context variables
4. **Progress Reporting** - Expose progress percentage to sequence
5. **Multi-Channel Scanning** - Optimize multiple channels simultaneously
6. **Velocity Profiles** - Allow custom velocity settings
7. **Scan Strategies** - Support different scan algorithms (spiral, raster, etc.)

## See Also

- `AdaptiveScanController` documentation
- `ScanExecutor` class for UI-based scanning
- `ScanControlWidget` for manual scanning interface
- Motion command handler documentation
- Sequence builder guide

---

**Last Updated:** 2025-10-29
**Version:** 1.0
**Author:** Claude Code
