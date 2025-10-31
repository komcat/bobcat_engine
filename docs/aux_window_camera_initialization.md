# AuxWindow Camera Auto-Initialization Fix

**Date:** October 31, 2025
**Component:** AuxViewWindow, CameraClickToMoveWidget
**Issue:** Camera live feed not starting automatically on application startup

---

## Problem Summary

The Auxiliary Display window was showing a frozen/still frame on application startup instead of a live camera feed. Users had to manually switch cameras in the dropdown to activate the live feed.

---

## Root Causes Identified

### 1. Initialization Order Issue
**Problem:** AuxWindow was being initialized before the main window UI was fully created and shown.

**Impact:** Camera subscription and UI rendering happened before the Qt event loop was ready, causing timing issues.

**Solution:** Moved AuxWindow initialization to occur AFTER main window creation and display.

**Location:** `Application.cpp:651-654`
```cpp
// ✅ MOVED: From initializeManagers() to createMainWindow()
std::cout << "[Application] Main window shown, now initializing AuxWindow..." << std::endl;
initializeAuxWindowManager();
std::cout << "[Application] AuxWindow initialization complete" << std::endl;
```

### 2. QComboBox Signal Not Triggering
**Problem:** When populating a QComboBox, Qt automatically selects the first item (index 0). Later calling `setCurrentIndex(0)` doesn't trigger the `currentIndexChanged` signal because the index hasn't actually changed.

**Impact:** Delayed camera selection called `setCurrentIndex(0)`, but the combobox was already at index 0, causing signal misfires or incorrect camera selection.

**Solution:** Explicitly set the combobox to no selection (`setCurrentIndex(-1)`) after populating.

**Location:** `CameraClickToMoveWidget.cpp:57-59`
```cpp
// ✅ FIX: Set to no selection (-1) so delayed selection will properly trigger signal
m_cameraSelector->setCurrentIndex(-1);
std::cout << "[CameraClickToMoveWidget] Populated " << cameraIds.size()
          << " cameras (no selection, delayed)" << std::endl;
```

### 3. Camera Restart on Re-grabbing
**Problem:** Calling `StartGrabbing()` on an already-grabbing camera causes it to stop and restart, losing frames.

**Impact:** Initial camera subscription was restarting the camera stream unnecessarily.

**Solution:** Check `IsGrabbing()` before calling `StartGrabbing()`.

**Location:** `CameraClickToMoveWidget.cpp:312-316`
```cpp
// ✅ FIX: Only start grabbing if not already running
ICameraHardware* camera = m_cameraManager->GetCameraHardware(cameraId);
if (camera && !camera->IsGrabbing()) {
  std::cout << "[CameraClickToMoveWidget] Camera not grabbing - starting now..." << std::endl;
  m_cameraManager->StartGrabbing(cameraId);
}
```

---

## Implementation Details

### Initialization Flow

```
Application.cpp
  ├─ createMainWindow()
  │   ├─ MainWindow created
  │   ├─ m_mainWindow->show()  // Qt event loop processes window
  │   └─ initializeAuxWindowManager()  // ✅ Moved here
  │
  └─ AuxWindowManager::initialize()
      ├─ Creates AuxViewWindow
      ├─ show() - displays window
      └─ initializeCameraSelection()  // ✅ Explicit delayed selection
          └─ CameraClickToMoveWidget::selectFirstCameraDelayed()
              └─ QTimer::singleShot(800ms) - waits for Qt rendering
                  ├─ Search for "camera_1" by name
                  ├─ setCurrentIndex(camera1Index)
                  └─ Signal triggers -> subscribeToCameraFeed()
```

### Key Files Modified

1. **Application.cpp** - Initialization order
   - Line 199: Removed `initializeAuxWindowManager()` from `initializeManagers()`
   - Lines 651-654: Added call after `m_mainWindow->show()`

2. **AuxWindowManager.cpp** - Explicit camera initialization
   - Lines 60-62: Call `initializeCameraSelection()` after showing window

3. **AuxViewWindow.h/cpp** - Forwarding method
   - Added `initializeCameraSelection()` method (lines 41, 115-124)

4. **CameraClickToMoveWidget.h** - Public method and flag
   - Line 46: Added `selectFirstCameraDelayed()` public method
   - Line 116: Added `m_initialCameraSelected` flag

5. **CameraClickToMoveWidget.cpp** - Core fixes
   - Line 58: Set combobox to no selection (-1)
   - Lines 312-316: Check `IsGrabbing()` before starting
   - Lines 601-670: Delayed selection with camera_1 search by name

---

## Key Learnings

### Qt Widget Initialization Timing

**Problem Pattern:**
```cpp
// ❌ BAD: Auto-select in constructor
m_comboBox->setCurrentIndex(0);  // May not be visible yet
```

**Correct Pattern:**
```cpp
// ✅ GOOD: Delayed selection after window shown
m_comboBox->setCurrentIndex(-1);  // No selection initially
// Later, after show():
QTimer::singleShot(800, [this]() {
  m_comboBox->setCurrentIndex(0);  // Now triggers signal correctly
});
```

### QComboBox Signal Triggering

**Key Rule:** `currentIndexChanged` only fires when the index **actually changes**.

**Problem:**
```cpp
m_comboBox->addItem("Item 1");  // Qt auto-selects index 0
m_comboBox->setCurrentIndex(0);  // No-op, already at 0 - signal doesn't fire!
```

**Solution:**
```cpp
m_comboBox->addItem("Item 1");
m_comboBox->setCurrentIndex(-1);  // Explicitly set to no selection
m_comboBox->setCurrentIndex(0);   // Now changes from -1 → 0, signal fires!
```

### Camera Hardware State Management

Always check camera state before operations:
```cpp
if (camera && !camera->IsGrabbing()) {
  camera->StartGrabbing();
}
```

Prevents unnecessary restarts and frame loss.

---

## Testing Checklist

- [x] AuxWindow opens automatically on startup
- [x] Camera dropdown shows "Select a camera to view live feed" initially
- [x] After 800ms delay, camera_1 automatically selected
- [x] Live feed starts immediately without user interaction
- [x] No frozen frames or still images
- [x] Camera subscription confirmed in logs
- [x] Frame broadcasting working (FIRST/SECOND frame messages)
- [x] Manual camera switching still works correctly
- [x] Refresh timer (5s) doesn't interfere with selection

---

## Debug Logging

Added comprehensive debug output to trace camera selection:

```
[CameraClickToMoveWidget] Populated 2 cameras (no selection, delayed)
[CameraClickToMoveWidget] selectFirstCameraDelayed called (camera count: 2)
[CameraClickToMoveWidget] Scheduling delayed camera selection in 800ms...
[CameraClickToMoveWidget] Timer fired! Camera count: 2, Initial selected: 0
[CameraClickToMoveWidget] ===== CAMERA DROPDOWN CONTENTS =====
[CameraClickToMoveWidget]   Index 0: Display='camera_1', Data='camera_1'
[CameraClickToMoveWidget]   Index 1: Display='camera_2', Data='camera_2'
[CameraClickToMoveWidget] ======================================
[CameraClickToMoveWidget] Checking index 0: 'camera_1' == 'camera_1'? YES
[CameraClickToMoveWidget] *** FOUND camera_1 at index 0 ***
[CameraClickToMoveWidget] ✓ Found camera_1 at index 0, selecting now...
[CameraClickToMoveWidget] Switching to camera: camera_1
[CameraClickToMoveWidget] Subscribed to camera: camera_1
[CRITICAL] UI_LiveVideo_camera_1 received FIRST frame successfully!
```

---

## Related Issues

This fix also resolved:
- Recipe execution progress panel dismissing prematurely (RecipePage.cpp)
- Timer conflicts between consecutive recipe executions

---

## References

- **Issue Report:** Camera shows frozen frame on startup
- **Files Changed:**
  - `Application.cpp`
  - `AuxWindowManager.cpp`
  - `AuxViewWindow.h/cpp`
  - `CameraClickToMoveWidget.h/cpp`
  - `LiveViewCameraWidget.cpp`

---

## Future Considerations

1. **Generalize Delayed Selection Pattern:**
   - Create a helper class for widgets requiring delayed initialization
   - Encapsulate the `-1` index pattern for reusability

2. **Camera Selection Configuration:**
   - Consider making default camera selection configurable
   - Allow users to set preferred camera in settings

3. **Error Handling:**
   - Add fallback if camera_1 not found (currently falls back to index 0)
   - Handle case where no cameras are available more gracefully

---

**Status:** ✅ **RESOLVED**
**Verified:** October 31, 2025
