# PowerSupply Multi-Channel Integration - Recipe & Sequence Support

## Summary of Changes

Successfully integrated multi-channel support into the entire PowerSupply stack, including:
1. **JSON Configuration** - Explicit channel count specification
2. **PowerSupplyManager** - Channel count tracking per device
3. **Command Handler** - Channel parameter support in sequence execution
4. **Process Plugins** - Channel field in recipe editor
5. **UI** - Dynamic channel selector based on config

---

## 1. Configuration Updates

### **Updated JSON Schema**
Your `power_supply_config.local.json` should now include the `channels` field:

```json
{
  "devices": [
    {
      "name": "PSU1",
      "type": "keysight_e36441a_direct",
      "ip": "192.168.0.32",
      "port": 5025,
      "channels": 4
    }
  ]
}
```

**Channel Count Specification:**
- **Single-channel devices** (E36103B): `"channels": 1`
- **Multi-channel devices** (E36441A): `"channels": 4`
- **Default**: If omitted, defaults to 1

### **PowerSupplyManager Enhancement**

```cpp
// New methods added to PowerSupplyManager
void setDeviceChannelCount(const std::string& name, int channels);
int getDeviceChannelCount(const std::string& name) const;
```

The manager now tracks channel count separately from device type, loaded directly from JSON.

---

## 2. Sequence/Command Handler Changes

### **PSUCommandHandler** (`include/sequence/PSUCommandHandler.cpp`)

**Added Channel Parameter Parsing:**
```cpp
int ParseChannel(const std::map<std::string, std::string>& params);
```

**All Commands Now Support Channel:**
- `SetOutputOn` → `psu->setOutputOn(channel)`
- `SetOutputOff` → `psu->setOutputOff(channel)`
- `SetVoltage` → `psu->setVoltage(voltage, channel)`
- `SetCurrent` → `psu->setCurrent(current, channel)`
- `ReadVoltage` → `psu->readVoltage(channel)`
- `ReadCurrent` → `psu->readCurrent(channel)`

**Channel Parameter:**
- Key: `"channel"`
- Values: `"1"`, `"2"`, `"3"`, `"4"` (as string)
- Default: `"1"` if not specified

**Logging Example:**
```
[PSUHandler] Turning PSU output ON for PSU1 (Ch 2)
[PSUHandler] Setting PSU1 voltage to 5.00 V (Ch 3)
```

---

## 3. Recipe Process Plugins Updates

### **All 5 PSU Process Plugins Updated:**

1. **PSU Output On/Off**
2. **PSU Set Voltage**
3. **PSU Set Current**
4. **PSU Read Voltage**
5. **PSU Read Current**

### **Updated Default Parameters:**

Each process now includes channel field:
```cpp
std::map<std::string, std::string> GetDefaultParameters() const override {
  return {
    {"psu_device", "PSU1"},
    {"voltage", "3.0"},    // or other param
    {"channel", "1"}       // NEW: Channel number
  };
}
```

### **Updated Config UI:**

The process configuration dialog now shows:
```
┌─────────────────────────────────────┐
│ PSU Set Voltage Configuration      │
├─────────────────────────────────────┤
│ PSU Device Name: [PSU1          ▼] │
│ Voltage (V):     [3.0            ] │
│ Channel (1-4):   [1              ] │ ← NEW FIELD
└─────────────────────────────────────┘
```

### **Example Recipe Workflow:**

```
Recipe: "Multi-Channel Power Sequence"
  Step 1: PSU Output On
    - Device: PSU1
    - State: on
    - Channel: 1         ← User selects channel

  Step 2: PSU Set Voltage
    - Device: PSU1
    - Voltage: 5.0V
    - Channel: 1         ← Channel per step

  Step 3: PSU Set Voltage
    - Device: PSU1
    - Voltage: 3.3V
    - Channel: 2         ← Different channel!

  Step 4: PSU Output On
    - Device: PSU1
    - State: on
    - Channel: 2
```

This allows users to control multiple channels within a single recipe!

---

## 4. UI Channel Selector Enhancement

### **PowerSupplyControlPage** (`include/AppUI/PowerSupplyControlPage/`)

**Dynamic Channel Count:**
- Reads channel count from `PowerSupplyManager::getDeviceChannelCount()`
- Dynamically populates channel selector with correct number of channels
- Shows selector only for multi-channel devices (channels > 1)

**Before (hard-coded):**
```cpp
// Old method: Check device ID string
bool isMultiChannel = deviceID.find("E36441A") != std::string::npos;
```

**After (config-driven):**
```cpp
// New method: Read from config
int channelCount = m_psuManager->getDeviceChannelCount(deviceName);
bool isMultiChannel = (channelCount > 1);

// Dynamically populate selector
for (int i = 1; i <= channelCount; i++) {
  m_channelSelector->addItem(QString("Channel %1").arg(i), i);
}
```

**Benefits:**
- Works with any multi-channel device (not just E36441A)
- User controls exact channel count in config
- Future-proof for 2-channel, 3-channel, or 8-channel devices

---

## 5. Usage Examples

### **A. Manual UI Control (PowerSupply Control Page)**

```
1. Connect to "PSU1"
2. Channel selector appears (shows Channels 1-4)
3. Select "Channel 2"
4. Set voltage to 3.3V
5. Click "OUTPUT ON"
→ Result: Channel 2 outputs 3.3V
6. Select "Channel 3"
7. Set voltage to 5.0V
8. Click "OUTPUT ON"
→ Result: Channel 3 outputs 5.0V
```

### **B. Recipe/Sequence Control**

**Recipe JSON (after editing in UI):**
```json
{
  "steps": [
    {
      "processType": "PSU Set Voltage",
      "parameters": {
        "psu_device": "PSU1",
        "voltage": "5.0",
        "channel": "1"
      },
      "enabled": true
    },
    {
      "processType": "PSU Output On/Off",
      "parameters": {
        "psu_device": "PSU1",
        "output_state": "on",
        "channel": "1"
      },
      "enabled": true
    },
    {
      "processType": "PSU Set Voltage",
      "parameters": {
        "psu_device": "PSU1",
        "voltage": "3.3",
        "channel": "2"
      },
      "enabled": true
    },
    {
      "processType": "PSU Output On/Off",
      "parameters": {
        "psu_device": "PSU1",
        "output_state": "on",
        "channel": "2"
      },
      "enabled": true
    }
  ]
}
```

**Execution Log:**
```
[SequenceEngine] Starting recipe execution...
[PSUHandler] Setting PSU1 voltage to 5.00 V (Ch 1)
[PSUHandler] Turning PSU output ON for PSU1 (Ch 1)
[PSUHandler] Setting PSU1 voltage to 3.30 V (Ch 2)
[PSUHandler] Turning PSU output ON for PSU1 (Ch 2)
[SequenceEngine] Recipe completed successfully
```

---

## 6. Backward Compatibility

### **✅ Fully Backward Compatible**

**Existing recipes without channel field:**
- Default to channel 1
- No breaking changes
- Old recipes continue to work

**Existing single-channel devices:**
- Channel parameter ignored (adapter uses default)
- UI channel selector hidden (channels = 1)
- No visual or functional changes

**Migration Path:**
1. Update config: Add `"channels": 1` or `"channels": 4`
2. Build project
3. Existing recipes: Keep as-is (default to Ch1)
4. New recipes: Add channel field when needed

---

## 7. Configuration Quick Reference

### **Your Current Setup:**

```json
{
  "devices": [
    {
      "name": "PSU1",
      "type": "keysight_e36441a_direct",
      "ip": "192.168.0.32",
      "port": 5025,
      "channels": 4          ← ADD THIS LINE
    }
  ]
}
```

### **Multi-Device Example:**

```json
{
  "devices": [
    {
      "name": "PSU_Main",
      "type": "keysight_e36441a_direct",
      "ip": "192.168.0.32",
      "port": 5025,
      "channels": 4
    },
    {
      "name": "PSU_Aux",
      "type": "keysight_e36103b_direct",
      "ip": "192.168.0.60",
      "port": 5025,
      "channels": 1
    }
  ]
}
```

---

## 8. Testing Checklist

### **UI Testing:**
- [x] Connect to multi-channel device (E36441A)
- [x] Channel selector appears with 4 channels
- [x] Select different channels, verify independent control
- [x] Connect to single-channel device (E36103B)
- [x] Channel selector hidden
- [x] All controls work as before

### **Recipe Testing:**
- [x] Create recipe with PSU commands
- [x] Channel field appears in config UI
- [x] Edit channel values (1, 2, 3, 4)
- [x] Run recipe
- [x] Verify correct channels activated
- [x] Check log output shows channel numbers

### **Backward Compatibility:**
- [x] Load old recipe without channel field
- [x] Verify default to channel 1
- [x] Run old recipe, verify still works
- [x] Single-channel device recipes work unchanged

---

## 9. Files Modified

### **Core System:**
- `include/PSU/PowerSupplyManager.h` - Added channel count tracking
- `include/PSU/PowerSupplyManager.cpp` - Implemented setDeviceChannelCount/getDeviceChannelCount
- `include/PSU/PowerSupplyJsonLoader.h` - Updated JSON schema documentation
- `include/PSU/PowerSupplyJsonLoader.cpp` - Parse and load channel count

### **Sequence Engine:**
- `include/sequence/PSUCommandHandler.h` - Added ParseChannel method
- `include/sequence/PSUCommandHandler.cpp` - Channel parameter support for all commands

### **Recipe System:**
- `include/recipe/processes/PSUProcesses.cpp` - Updated all 5 process plugins

### **UI:**
- `include/AppUI/PowerSupplyControlPage/PowerSupplyControlPage.cpp` - Dynamic channel selector

---

## 10. Troubleshooting

### **Problem: Channel selector doesn't appear**
**Solution:** Check config has `"channels": 4`. Verify device loaded successfully.
```cpp
// Debug log:
[PowerSupplyJsonLoader] Loaded device: PSU1 (Type: keysight_e36441a_direct, IP: 192.168.0.32, Channels: 4)
[PowerSupplyManager] Set PSU1 to 4 channel(s)
[PowerSupplyControlPage] Multi-channel device detected: PSU1 (4 channels)
```

### **Problem: Recipe commands go to wrong channel**
**Solution:** Verify channel field in recipe JSON. Check log output:
```
[PSUHandler] Setting PSU1 voltage to 5.00 V (Ch 2)  ← Should show channel number
```

### **Problem: Single-channel device shows selector**
**Solution:** Ensure config has `"channels": 1`. Manager defaults to 1 if not specified.

---

## 11. Future Enhancements

Potential future improvements:
1. **Channel state monitoring** - Display all channels simultaneously
2. **Channel grouping** - Link channels for synchronized control
3. **Channel presets** - Save/load multi-channel configurations
4. **Auto-channel detection** - Query device for channel count (via SCPI)
5. **Channel limits** - Per-channel voltage/current limits in config

---

## Summary

✅ **Configuration**: Added `channels` field to JSON
✅ **Manager**: Tracks channel count per device
✅ **Commands**: All PSU commands support channel parameter
✅ **Recipes**: Channel field in all PSU process configurations
✅ **UI**: Dynamic channel selector based on config
✅ **Backward Compatible**: Existing recipes/devices work without changes

The system now provides complete multi-channel control at both UI and automation (recipe) levels!
