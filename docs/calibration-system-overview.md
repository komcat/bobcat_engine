# Calibration System Overview

## Architecture

The calibration system consists of **three levels** of calibration data:

### 1. **System-Level Calibration** (Hardware offset - Static)
- **File**: `config/calibration/needle_camera_offset.json`
- **Purpose**: Determine the physical XYZ offset from camera center to needle tip
- **Scope**: Global for the machine (not process-specific)
- **Updated by**: "Calibrate Camera-to-Needle Offset" process
- **Used by**: All calibration and dispense processes

### 2. **Process-Level Navigation** (Location data - Per UID)
- **Files**: `config/calibration/navigation_{UID}.json`
- **Purpose**: Remember camera position for auto-navigation
- **Scope**: Specific to each process instance
- **Updated by**: Each calibration process (Calibrate Needle Z, etc.)
- **Used by**: Same process on subsequent runs

### 3. **Process-Level Calibration Results** (Measurement data - Per UID)
- **Files**: `config/calibration/calibration_{UID}.json`
- **Purpose**: Store calibration results (needle height, etc.)
- **Scope**: Specific to each process instance
- **Updated by**: Each calibration process
- **Used by**: Dispense and other processes that reference the calibration UID

---

## File Structure

```
config/calibration/
├── needle_camera_offset.json              ← SYSTEM-LEVEL (static)
│
├── navigation_calibrate_needle_z_001.json ← PROCESS-LEVEL (Product A)
├── calibration_calibrate_needle_z_001.json
│
├── navigation_calibrate_needle_z_002.json ← PROCESS-LEVEL (Product B)
├── calibration_calibrate_needle_z_002.json
│
└── navigation_calibrate_needle_z_003.json ← PROCESS-LEVEL (Product C)
    calibration_calibrate_needle_z_003.json
```

---

## Calibration Processes

### **Process 1: Calibrate Camera-to-Needle Offset** ⭐ NEW
**Category**: Calibration
**File**: `include/recipe/processes/CalibrateCameraToNeedleOffset.cpp`

#### Purpose
One-time system calibration to determine the physical offset between camera and needle.

#### Steps
1. User positions camera over flat surface
2. User positions needle for imprint
3. System captures needle position
4. Needle makes imprint (manual or automatic)
5. User moves camera to center on imprint
6. System captures camera position
7. Calculate: `offset = needle_position - camera_position`
8. Save to `needle_camera_offset.json`

#### Output File Format
```json
{
  "metadata": {
    "calibration_type": "Camera to Needle Offset",
    "device": "gantry-main",
    "last_calibrated_date": "2025-10-27",
    "last_calibrated_time": "15:30:00"
  },
  "offset": {
    "x": 50.234,
    "y": 10.567,
    "z": 0.123
  },
  "positions": {
    "needle_imprint": {"x": 150.234, "y": 210.567, "z": 35.123},
    "camera_centered": {"x": 100.000, "y": 200.000, "z": 35.000}
  }
}
```

#### When to Run
- **Once** when setting up the machine
- After replacing/adjusting camera or dispenser head
- Periodically to verify accuracy

---

### **Process 2: Calibrate Needle Z** (Updated)
**Category**: Calibration
**File**: `include/recipe/processes/CalibrationProcesses.cpp`

#### Purpose
Product-specific calibration to determine needle Z height against flat surface.

#### Steps (Updated)
0. **Load system offset** (if exists)
   - Uses values from `needle_camera_offset.json`
   - Falls back to configured parameters if not available
1. Load navigation data for this UID (if exists)
2. Auto-navigate to saved position OR user positions manually
3. Capture camera position
4. Save navigation data
5. Calculate needle position using offset
6. Move to needle position
7. Extend dispenser head
8. User coarse Z positioning
9. User fine Z adjustment (5 µm steps)
10. Capture final needle position
11. Save calibration results (with delta tracking)
12. Retract and move to safe position

#### Output Files
**Navigation** (`navigation_{UID}.json`):
```json
{
  "metadata": {
    "uid": "calibrate_needle_z_001",
    "device": "gantry-main",
    "last_updated_date": "2025-10-27"
  },
  "camera_position": {"x": 100.0, "y": 200.0, "z": 50.0},
  "offsets": {"x": 50.234, "y": 10.567, "z": 0.123}
}
```

**Calibration** (`calibration_{UID}.json`):
```json
{
  "metadata": {
    "uid": "calibrate_needle_z_001",
    "calibration_type": "Needle Z Position",
    "device": "gantry-main",
    "calibrated_date": "2025-10-27"
  },
  "needle_touch_position": {"x": 150.234, "y": 210.567, "z": 35.678},
  "parameters": {
    "needle_height": 35.678,
    "previous_needle_height": 35.676,
    "delta_from_previous": 0.002
  }
}
```

---

## Command Handler Methods

### CalibrationCommandHandler Methods

#### System-Level Offset
- **LoadCameraToNeedleOffset**: Read static offset file
- **SaveCameraToNeedleOffset**: Write static offset file

#### Process-Level Navigation
- **LoadExistingCalibration** (renamed from previous): Load navigation data by UID
- **SaveNavigationData**: Save navigation data by UID

#### Process-Level Calibration
- **LoadCalibratedHeight**: Load needle height from calibration file by UID
- **SaveNeedleZCalibration**: Save calibration results by UID

#### Utility
- **CapturePosition**: Capture current device position

---

## Usage Workflow

### Initial Setup (One-Time)

```
Recipe: "System Calibration"

Step 1: Calibrate Camera-to-Needle Offset
  - User positions camera over flat surface
  - User positions needle and makes imprint
  - User centers camera on imprint
  - System calculates offset: (50.234, 10.567, 0.123)
  - Saves to: needle_camera_offset.json ✓
```

**Result**: System now knows the camera-to-needle offset!

---

### Product A Recipe (First Run)

```
Recipe: "Product A Process"

Step 1: Calibrate Needle Z (UID: calibrate_needle_z_001)
  - Loads system offset: (50.234, 10.567, 0.123) ← from needle_camera_offset.json
  - No navigation data yet (first run)
  - User manually positions camera
  - System captures camera position: (100.0, 200.0, 50.0)
  - Saves to: navigation_calibrate_needle_z_001.json ✓
  - System calculates needle position: (150.234, 210.567, 50.123)
  - User fine-tunes Z touch at: 35.678
  - Saves to: calibration_calibrate_needle_z_001.json ✓

Step 2: Dispense at Point 1 (UID: dispense_001)
  - Loads needle height: 35.678 ← from calibration_calibrate_needle_z_001.json
  - Adds offset: 0.5 mm
  - Dispenses at Z = 36.178 mm ✓
```

---

### Product A Recipe (Subsequent Runs)

```
Recipe: "Product A Process"

Step 1: Calibrate Needle Z (UID: calibrate_needle_z_001)
  - Loads system offset: (50.234, 10.567, 0.123)
  - Loads navigation data: (100.0, 200.0, 50.0) ← from navigation file
  - AUTO-NAVIGATES to saved position (no manual jogging!) ✓
  - User only does fine Z adjustment
  - Updates: calibration_calibrate_needle_z_001.json
  - Shows delta: Δ +0.002 mm from previous

Step 2: Dispense at Point 1
  - Uses updated needle height ✓
```

---

### Product B Recipe (Independent)

```
Recipe: "Product B Process"

Step 1: Calibrate Needle Z (UID: calibrate_needle_z_002)
  - Loads system offset: (50.234, 10.567, 0.123) ← same system offset
  - No navigation data for THIS UID (first run for Product B)
  - User positions camera at DIFFERENT location
  - Creates: navigation_calibrate_needle_z_002.json ✓
  - Creates: calibration_calibrate_needle_z_002.json ✓
  - Product B calibration is INDEPENDENT from Product A!
```

---

## Benefits

### System-Level Offset
✅ **One-time calibration** - Set once, use everywhere
✅ **Accuracy** - Precise measurement of hardware offset
✅ **Consistency** - All processes use same reference
✅ **Maintenance** - Easy to recalibrate after hardware changes

### UID-Based Files
✅ **Multi-product support** - Each product has its own calibration location
✅ **Auto-navigation** - Returns to exact position automatically
✅ **Delta tracking** - Monitor wear/drift per product
✅ **Isolation** - Products don't interfere with each other

### Integration
✅ **Hierarchical** - System → Navigation → Calibration
✅ **Flexible** - Can override system offset if needed
✅ **Traceable** - Full audit trail with timestamps
✅ **Reusable** - Multiple dispense steps can reference same calibration

---

## Files Modified/Created

### New Files
- `include/recipe/processes/CalibrateCameraToNeedleOffset.cpp` ⭐
- `docs/calibration-system-overview.md` ⭐

### Modified Files
- `include/sequence/CalibrationCommandHandler.h`
  - Added: `SaveCameraToNeedleOffset`, `LoadCameraToNeedleOffset`
  - Added: `GetCameraToNeedleOffsetFilePath`

- `include/sequence/CalibrationCommandHandler.cpp`
  - Implemented system-level offset save/load
  - Updated ExecuteCommand with new methods

- `include/recipe/processes/CalibrationProcesses.cpp`
  - Added Step 0a/0b: Load and use system offset
  - Updated step numbering

### Files to Add to CMakeLists.txt
```cmake
include/recipe/processes/CalibrateCameraToNeedleOffset.cpp
```

---

## Testing Procedure

### 1. Test System Offset Calibration
```
1. Create recipe with "Calibrate Camera-to-Needle Offset"
2. Run calibration
3. Verify needle_camera_offset.json created
4. Check offset values are reasonable
```

### 2. Test Calibrate Needle Z (First Run)
```
1. Create recipe with "Calibrate Needle Z" (UID: cal_001)
2. Run calibration
3. Verify system offset is loaded automatically
4. Verify navigation_cal_001.json created
5. Verify calibration_cal_001.json created
```

### 3. Test Calibrate Needle Z (Second Run)
```
1. Run same recipe again
2. Verify auto-navigation to saved position
3. Verify delta is calculated and shown
4. Verify calibration_cal_001.json updated
```

### 4. Test Multiple Products
```
1. Create second process with different UID (cal_002)
2. Run calibration
3. Verify separate navigation_cal_002.json created
4. Verify independent from cal_001
```

### 5. Test Dispense Integration
```
1. Create dispense step with calibration_uid = "cal_001"
2. Run dispense
3. Verify needle height is loaded correctly
4. Verify Z position calculation is correct
```

---

## Future Enhancements

### Planned Features
- **Calibrate Needle XY**: Use similar approach for XY calibration
- **Auto-validation**: Verify calibration before dispense
- **Offset history**: Track system offset changes over time
- **Multi-needle support**: Separate offsets for different needles
- **Temperature compensation**: Factor in thermal expansion

---

## Troubleshooting

### System Offset Not Found
**Symptom**: Calibrate Needle Z uses configured parameters instead of system offset
**Cause**: `needle_camera_offset.json` doesn't exist
**Solution**: Run "Calibrate Camera-to-Needle Offset" first

### Navigation Not Working
**Symptom**: Always asks for manual positioning
**Cause**: Navigation file doesn't exist for this UID
**Solution**: Normal on first run; subsequent runs will auto-navigate

### Wrong Calibration Height
**Symptom**: Dispense uses incorrect Z height
**Cause**: Wrong calibration_uid referenced
**Solution**: Check dispense step's calibration_uid parameter matches calibration step UID

### Delta Shows Large Change
**Symptom**: Delta > 0.1 mm between calibrations
**Cause**: Needle wear, surface change, or thermal drift
**Action**: Investigate cause, document change, consider recalibrating system offset

---

## References
- `include/sequence/CalibrationCommandHandler.h` - Command handler interface
- `include/recipe/IProcessPlugin.h` - Plugin architecture
- `docs/calibrate-needle-z-implementation.md` - Original implementation
- `include/recipe/new process addition.md` - Adding new processes

---

**Document Version**: 1.0
**Last Updated**: 2025-10-27
**Author**: System
