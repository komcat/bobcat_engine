# Flexible Channel Interface Implementation

## Overview

Implemented a flexible multi-channel interface for SMU devices supporting 1-4 channels with configurable enabled channels via JSON.

## Features Implemented

### 1. Channel Configuration (JSON)

**File**: `config/smu_devices.json`

```json
{
  "devices": [
    {
      "name": "Keithley_6482",
      "type": "keithley_6482",
      "visa_resource": "GPIB0::1::INSTR",
      "enabled": true,
      "channel_count": 2,
      "enabled_channels": [1, 2],
      "measurement_nplc": 5,
      "auto_range": true
    }
  ]
}
```

**Configuration Fields**:
- `channel_count`: Total number of channels (1-4)
- `enabled_channels`: Array of enabled channels (e.g., [1, 2] or [1, 3, 4])

### 2. Interface Methods

**ISMUDevice.h** - Added to interface:

```cpp
// Get channel information
virtual int GetChannelCount() const = 0;
virtual std::vector<int> GetEnabledChannels() const = 0;

// Read specific channel
virtual double ReadChannel(int channel) = 0;

// Read all enabled channels
virtual std::vector<double> ReadAllChannels() = 0;
```

### 3. Implementation

**Keithley6482Adapter** implementation:

```cpp
// Get configuration
int GetChannelCount() const;           // Returns 2 for Keithley 6482
std::vector<int> GetEnabledChannels(); // Returns {1, 2}

// Read specific channel (1 or 2)
double ReadChannel(int channel);

// Read all enabled channels
std::vector<double> ReadAllChannels(); // Returns {ch1_value, ch2_value}

// Backward compatible
double ReadCurrent();  // Reads current selected channel (default: 1)
```

## Usage Examples

### Example 1: Read Specific Channel

```cpp
auto smu = m_context.smuManager->GetDevice("Keithley_6482");

// Read channel 1
double ch1 = smu->ReadChannel(1);
std::cout << "Channel 1: " << ch1 << " A" << std::endl;

// Read channel 2
double ch2 = smu->ReadChannel(2);
std::cout << "Channel 2: " << ch2 << " A" << std::endl;
```

### Example 2: Read All Channels

```cpp
auto smu = m_context.smuManager->GetDevice("Keithley_6482");

// Read all enabled channels at once
std::vector<double> channels = smu->ReadAllChannels();

for (size_t i = 0; i < channels.size(); ++i) {
    std::cout << "Channel " << (i+1) << ": " << channels[i] << " A" << std::endl;
}
```

### Example 3: Check Channel Configuration

```cpp
auto smu = m_context.smuManager->GetDevice("Keithley_6482");

int totalChannels = smu->GetChannelCount();  // Returns 2
auto enabled = smu->GetEnabledChannels();     // Returns {1, 2}

std::cout << "Device has " << totalChannels << " channels" << std::endl;
std::cout << "Enabled channels: ";
for (int ch : enabled) {
    std::cout << ch << " ";
}
std::cout << std::endl;
```

### Example 4: Backward Compatible Usage

```cpp
auto smu = m_context.smuManager->GetDevice("Keithley_6482");

// Old code still works (reads default channel 1)
double current = smu->ReadCurrent();
```

## Channel Validation

The implementation includes validation:

```cpp
// Validates channel number
double ReadChannel(int channel) {
    // Check channel is in valid range (1 to channel_count)
    if (channel < 1 || channel > m_config.ChannelCount) {
        return -999.0;  // Error
    }

    // Check channel is enabled
    if (channel not in enabled_channels) {
        return -999.0;  // Error
    }

    // Read from channel...
}
```

## Device-Specific Examples

### Keithley 6482 (2 channels)

```json
{
  "channel_count": 2,
  "enabled_channels": [1, 2]
}
```

### Single-Channel SMU

```json
{
  "channel_count": 1,
  "enabled_channels": [1]
}
```

### Future 4-Channel Device

```json
{
  "channel_count": 4,
  "enabled_channels": [1, 2, 3, 4]
}
```

### Selective Channel Enable

```json
{
  "channel_count": 4,
  "enabled_channels": [1, 3]  // Only channels 1 and 3 enabled
}
```

## Test Output

The Application.cpp test now shows:

```
[Application] ===== SMU Connection Test =====
[Application] Testing SMU device: Keithley_6482
[Keithley6482] Connected to: KEITHLEY INSTRUMENTS INC.,MODEL 6482...
[Application] ✅ Connected to SMU: Keithley_6482
[Application] Device ID: KEITHLEY INSTRUMENTS INC.,MODEL 6482...
[Application] Supported Mode: MEASURE_ONLY
[Application] Channel Count: 2
[Application] Enabled Channels: 1, 2

[Application] Performing test measurement...
[Application] ✅ Test measurement successful!
[Application] Current reading (default): -1.04264e-05 A

[Application] Testing individual channel reading...
[Application] ✅ Channel 1: -1.04264e-05 A
[Application] ✅ Channel 2: 0.00000e+00 A

[Application] Testing read all channels...
[Application] ✅ Read all channels successful!
[Application]   Channel 1: -1.04264e-05 A
[Application]   Channel 2: 0.00000e+00 A
```

## Files Modified

### 1. `SMUTypes.h`
- Added `#include <vector>`
- Added `ChannelCount` field to `SMUDeviceConfig`
- Added `EnabledChannels` vector to `SMUDeviceConfig`

### 2. `ISMUDevice.h`
- Added `GetChannelCount()` pure virtual method
- Added `GetEnabledChannels()` pure virtual method
- Added `ReadChannel(int channel)` pure virtual method
- Added `ReadAllChannels()` pure virtual method

### 3. `Keithley6482Adapter.h`
- Added override declarations for new channel methods

### 4. `Keithley6482Adapter.cpp`
- Implemented `GetChannelCount()` - returns config value
- Implemented `GetEnabledChannels()` - returns config vector
- Implemented `ReadChannel(int)` - reads specific channel with validation
- Implemented `ReadAllChannels()` - reads all enabled channels simultaneously
- Modified `ReadCurrent()` - now calls `ReadChannel(m_currentChannel)` for backward compatibility

### 5. `SMUJsonLoader.cpp`
- Added parsing for `channel_count` field
- Added parsing for `enabled_channels` array
- Default values: channel_count=1, enabled_channels={1}

### 6. `config/smu_devices.json`
- Added `"channel_count": 2`
- Added `"enabled_channels": [1, 2]`

### 7. `Application.cpp`
- Added channel count display in test
- Added enabled channels display
- Added individual channel reading test
- Added read all channels test

## Benefits

✅ **Flexible**: Supports 1-4 channels per device
✅ **Configurable**: Channel configuration in JSON (no code changes)
✅ **Validated**: Checks channel number and enabled status
✅ **Backward Compatible**: Old code using `ReadCurrent()` still works
✅ **Extensible**: Easy to add new multi-channel SMU devices
✅ **Type-Safe**: Compile-time interface enforcement

## Future Enhancements (Optional)

1. **Per-Channel Configuration**:
   - Individual NPLC per channel
   - Individual range per channel

2. **Synchronized Multi-Channel Reading**:
   - Hardware-triggered simultaneous sampling

3. **Channel-Specific Polling**:
   - Poll different channels at different rates
   - Emit separate signals per channel

4. **Channel Aliasing**:
   - Named channels (e.g., "detector1", "reference")

---

**Date**: 2025-10-31
**Feature**: Flexible multi-channel interface with JSON configuration
**Status**: ✅ Implementation complete - Ready for testing
