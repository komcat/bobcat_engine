# SMU Control Page Implementation

## Overview

Created a simple SMU control page following the Power Supply pattern with dark theme.

## Features

### Two-Panel Layout

**Left Panel (30%)** - Device List:
- List of SMU devices from SMUManager
- Connect/Disconnect buttons
- Connection status indicator

**Right Panel (70%)** - Controls:
1. **Channel Configuration**
   - Channel count display
   - Channel selection combo box
   - Measurement mode selector (ACCURATE/FAST/ULTRA_FAST)

2. **Current Measurements**
   - Large display for selected channel
   - Small displays for all channels
   - Refresh button
   - Auto-refresh every 2 seconds when connected

3. **Continuous Polling**
   - Start/Stop polling buttons
   - Polling status indicator
   - 100ms polling interval (10Hz UI update)

## UI Components

### Channel Configuration Section
```
┌─ Channel Configuration ──────────────┐
│ Channel Count:    2                  │
│ Select Channel:   [Channel 1 ▼]      │
│ Measurement Mode: [ACCURATE ▼]       │
└──────────────────────────────────────┘
```

**Measurement Modes**:
- ACCURATE (1.0 NPLC, ~10 Hz)
- FAST (0.1 NPLC, ~80 Hz)
- ULTRA_FAST (0.01 NPLC, ~150 Hz)

### Measurements Section
```
┌─ Current Measurements ───────────────┐
│ Selected Channel:                    │
│ ┌────────────────────────────────┐   │
│ │      -1.042e-05 A              │   │
│ └────────────────────────────────┘   │
│                                      │
│ All Channels:                        │
│ Ch1: -1.04e-05 A                     │
│ Ch2: 2.30e-13 A                      │
│                                      │
│ [Refresh]                            │
└──────────────────────────────────────┘
```

### Polling Section
```
┌─ Continuous Polling ─────────────────┐
│ Status: Stopped                      │
│ [Start Polling] [Stop Polling]       │
└──────────────────────────────────────┘
```

## Color Scheme (Dark Theme)

Following PSU page pattern:

| Element | Color |
|---------|-------|
| Background | #2d2d30 (dark gray) |
| Panels | #1e1e1e (darker) |
| Text | #d4d4d4 (light gray) |
| Primary Button | #0e639c (blue) |
| Success | #27ae60 (green) |
| Error | #e74c3c (red) |
| Border | #3f3f46 (medium gray) |

## Files Created

### 1. SMUControlPage.h
**Location**: `include/AppUI/SMUControlPage/SMUControlPage.h`

**Class**: `SMUControlPage : public QWidget`

**Key Members**:
- `m_deviceList` - QListWidget for device selection
- `m_channelComboBox` - Channel selector
- `m_modeComboBox` - Measurement mode selector
- `m_currentChannelReadLabel` - Large current display
- `m_channelReadLabels` - Vector of channel labels
- `m_startPollingButton`, `m_stopPollingButton` - Polling controls
- `m_refreshTimer` - Auto-refresh timer

### 2. SMUControlPage.cpp
**Location**: `include/AppUI/SMUControlPage/SMUControlPage.cpp`

**Key Methods**:
- `onDeviceSelected()` - Device selection handler
- `onConnectClicked()`, `onDisconnectClicked()` - Connection management
- `onChannelSelected()` - Channel selection handler
- `onModeChanged()` - Configure measurement mode
- `onStartPollingClicked()`, `onStopPollingClicked()` - Polling control
- `updateMeasurements()` - Refresh current readings
- `updateChannelList()` - Populate channel dropdown

## Integration

### HardwareInstrumentPage.h
```cpp
class SMUControlPage;  // Forward declaration

class HardwareInstrumentPage {
  // ...
  SMUControlPage* m_smuControlPage;  // Member variable
};
```

### HardwareInstrumentPage.cpp
```cpp
#include "SMUControlPage.h"

void HardwareInstrumentPage::createContentArea() {
  // ...
  // Index 9: SMU
  m_smuControlPage = new SMUControlPage(m_context);
  m_contentStack->addWidget(m_smuControlPage);
}

void HardwareInstrumentPage::showSMU() {
  m_contentStack->setCurrentIndex(9);
  // ... button state management
}
```

### CMakeLists.txt
Added:
```cmake
# Include directory
${CMAKE_SOURCE_DIR}/include/AppUI/SMUControlPage

# Source file
include/AppUI/SMUControlPage/SMUControlPage.cpp

# Header file
include/AppUI/SMUControlPage/SMUControlPage.h
```

## Usage Flow

1. **Navigate to SMU Page**:
   - Click "SMU" button in Hardware sidebar

2. **Connect to Device**:
   - Select device from list (e.g., "Keithley_6482")
   - Click "Connect" button
   - Status shows "Connected" in green

3. **Select Channel**:
   - Use "Select Channel" dropdown
   - Choose Channel 1 or Channel 2

4. **Choose Measurement Mode**:
   - Select ACCURATE, FAST, or ULTRA_FAST
   - Device automatically reconfigures

5. **View Measurements**:
   - Selected channel shows in large display
   - All channels shown below
   - Auto-refreshes every 2 seconds
   - Click "Refresh" for manual update

6. **Start Polling** (Optional):
   - Click "Start Polling"
   - Measurements update at ~10 Hz in UI
   - Status shows "Polling" in green
   - Click "Stop Polling" to stop

## Example Screenshots (Text-Based)

### Connected State
```
╔═══════════════════════════════════════════════════════════════╗
║ SMU Devices              │ SMU Control                        ║
║                          │                                    ║
║ Keithley_6482 ◄          │ ┌─ Channel Configuration ────┐    ║
║                          │ │ Channel Count: 2            │    ║
║ Connected ✓              │ │ Select Channel: [Ch1 ▼]     │    ║
║ [Connect] [Disconnect]   │ │ Mode: [FAST ▼]              │    ║
║                          │ └─────────────────────────────┘    ║
║                          │                                    ║
║                          │ ┌─ Current Measurements ──────┐    ║
║                          │ │ Selected Channel:           │    ║
║                          │ │ ┌───────────────────────┐   │    ║
║                          │ │ │   -1.042e-05 A        │   │    ║
║                          │ │ └───────────────────────┘   │    ║
║                          │ │ All Channels:               │    ║
║                          │ │ Ch1: -1.04e-05 A            │    ║
║                          │ │ Ch2: 2.30e-13 A             │    ║
║                          │ │ [Refresh]                   │    ║
║                          │ └─────────────────────────────┘    ║
║                          │                                    ║
║                          │ ┌─ Continuous Polling ────────┐    ║
║                          │ │ Status: Stopped             │    ║
║                          │ │ [Start] [Stop]              │    ║
║                          │ └─────────────────────────────┘    ║
╚═══════════════════════════════════════════════════════════════╝
```

## Architecture - Loose Coupling with DataStore

### Auto-Subscription Pattern

All SMU measurements automatically flow to DataStore with **zero coupling** between components:

```
Keithley6482Adapter (device)
    ↓ measurementReady signal
SMUManager (broker)
    ↓ measurementReady signal (forwarded)
Application.cpp (subscriber)
    ↓ addData()
DataStore (storage)
```

**Key Benefits**:
- ✅ **Loose coupling**: Devices don't know about DataStore
- ✅ **Automatic subscription**: No manual connection needed
- ✅ **Consistent naming**: `SMU_DeviceName_Ch1`, `SMU_DeviceName_Ch2`
- ✅ **Extensible**: Easy to add more subscribers (UI, logging, etc.)
- ✅ **Thread-safe**: Qt signals handle cross-thread communication

**Implementation Details**:
- `SMUManager::AddDevice()` auto-subscribes to device measurements
- `Application::initializeSMUSystem()` auto-subscribes SMUManager to DataStore
- UI subscribes to SMUManager for real-time display (optional)
- No direct coupling between any components

### DataStore Channel Naming

Measurements are stored with the following naming convention:
- Format: `SMU_{DeviceName}_Ch{ChannelNumber}`
- Example: `SMU_Keithley_6482_Ch1`, `SMU_Keithley_6482_Ch2`
- Includes timestamp from device measurement

## Design Decisions

### Why Simple?

Following PSU pattern for consistency:
- ✅ Two-panel layout (device list + controls)
- ✅ Dark theme matching app theme
- ✅ Minimal, functional design
- ✅ No fancy charts or graphs (keep it simple)
- ✅ Focus on core functionality

### Channel Management

- **Dynamic channel list**: Populated from device configuration
- **Validation**: Only enabled channels shown
- **Simultaneous reading**: "All Channels" section reads all at once

### Polling Implementation

- **UI-safe**: Uses QTimer (Qt event loop)
- **Configurable interval**: 100ms default (10Hz UI update)
- **Backend faster**: Device may poll faster (22Hz with FAST mode)
- **UI throttling**: Only updates display at 10Hz to avoid flicker

## Testing Checklist

- [ ] Build succeeds without errors
- [ ] SMU page appears in sidebar
- [ ] Device list populates
- [ ] Connect/Disconnect works
- [ ] Channel selector shows correct channels
- [ ] Measurement mode selector works
- [ ] Current readings update
- [ ] Refresh button works
- [ ] Auto-refresh works (2 second interval)
- [ ] Start/Stop polling works
- [ ] Polling status updates
- [ ] Theme matches other pages
- [ ] No crashes on disconnect
- [ ] Error messages display correctly

## Future Enhancements (Optional)

1. **Real-time Graph**: Plot current vs. time
2. **Data Logging**: Save measurements to CSV
3. **Statistics**: Min/Max/Average/StdDev display
4. **Multi-Device**: Control multiple SMUs simultaneously
5. **Compliance Monitoring**: Alert when over range
6. **Custom Polling Rate**: Slider to adjust polling interval
7. **Channel Comparison**: Side-by-side channel comparison

---

**Date**: 2025-10-31
**Feature**: Simple SMU control page with dark theme
**Status**: ✅ Implementation complete - Ready for testing
**Pattern**: Follows PowerSupplyControlPage design
