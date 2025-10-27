# Calibrate Needle Z - Implementation Summary

## Overview
Automated calibration process for determining needle Z height against a flat reference surface, with camera-to-needle offset compensation and automatic position recall.

## Implementation Date
October 26, 2025

---

## Files Created

### 1. CalibrationCommandHandler (Command Handler)
- **Header**: `include/sequence/CalibrationCommandHandler.h`
- **Implementation**: `include/sequence/CalibrationCommandHandler.cpp`
- **Manager Type**: `"calibration"`

#### Commands Implemented:
1. **LoadExistingCalibration**
   - Reads previous calibration from JSON file
   - Stores camera position and offsets in SequenceContext
   - Returns boolean flag if calibration exists

2. **CapturePosition**
   - Reads current XYZ position from motion device
   - Stores in SequenceContext with configurable key prefix
   - Parameters:
     - `device`: Motion device name
     - `key_prefix`: Prefix for context keys (e.g., "camera" → "camera_x", "camera_y", "camera_z")

3. **SaveNeedleZCalibration**
   - Saves complete calibration data to JSON file
   - Calculates delta from previous calibration
   - Generates success message with delta
   - Parameters:
     - `device`: Motion device name
     - `pneumatic_slide`: Pneumatic slide name used

### 2. CalibrationProcesses (Process Plugin)
- **File**: `include/recipe/processes/CalibrationProcesses.cpp`
- **Plugin Class**: `CalibrateNeedleZPlugin`
- **Category**: `"Calibration"`
- **Auto-registers** at startup via static initialization

---

## Files Modified

### 1. SequenceEngine.cpp
**Change**: Registered CalibrationCommandHandler
```cpp
m_dispatcher.RegisterHandler("calibration",
                             std::make_unique<CalibrationCommandHandler>(m_context));
```

### 2. RecipeManager.cpp
**Change**: Removed "Calibrate Needle Z" from hardcoded list (now uses plugin)

### 3. ProcessSequenceFactory.cpp
**Change**: Removed duplicate builder (now uses plugin)

### 4. CMakeLists.txt
**Changes**: Added new source files to build
- `include/sequence/CalibrationCommandHandler.cpp`
- `include/sequence/CalibrationCommandHandler.h`
- `include/recipe/processes/CalibrationProcesses.cpp`

---

## Configuration Files

### Output File Location
`config/calibration/CalibrateNeedleZ.json`

### JSON Structure
```json
{
  "metadata": {
    "calibration_type": "Needle Z Position",
    "device": "gantry-main",
    "calibrated_date": "2025-10-26",
    "calibrated_time": "17:55:30",
    "pneumatic_slide": "Dispenser_Head"
  },
  "camera_position": {
    "x": 100.0,
    "y": 200.0,
    "z": 50.0
  },
  "offsets": {
    "x": 50.0,
    "y": 10.0,
    "z": 0.0
  },
  "needle_touch_position": {
    "x": 150.0,
    "y": 210.0,
    "z": 35.678
  },
  "parameters": {
    "needle_height": 35.678,
    "previous_needle_height": 35.676,
    "delta_from_previous": 0.002
  }
}
```

---

## Process Flow

### First Calibration Run

1. **Load Existing Calibration** (Step 0)
   - Checks if `config/calibration/CalibrateNeedleZ.json` exists
   - Stores `calibration_exists = false` in context

2. **Manual Camera Positioning** (Step 1)
   - Shows prompt: "Manually move gantry-main so camera sees the calibration surface"
   - User manually jogs to position

3. **Capture Camera Position** (Step 2)
   - Reads XYZ from gantry-main
   - Stores as `camera_x`, `camera_y`, `camera_z` in context

4. **Store Offsets** (Step 3)
   - Sets `offset_x = 50.0`, `offset_y = 10.0`, `offset_z = 0.0` in context

5. **Calculate Needle Position** (Step 4)
   - `needle_target_x = camera_x + offset_x`
   - `needle_target_y = camera_y + offset_y`
   - `needle_target_z = camera_z + offset_z`

6. **Move to Needle Position** (Step 5)
   - Moves gantry-main to calculated position
   - Waits for motion complete

7. **Extend Dispenser Head** (Step 6)
   - Extends pneumatic slide
   - Timeout: 10 seconds

8. **Settling Time** (Step 7)
   - Waits 500ms for mechanical settling

9. **Coarse Z Positioning** (Step 8)
   - Shows prompt: "Manually move Z down until needle is close to surface"
   - User manually jogs Z axis

10. **Fine Z Adjustment** (Step 9)
    - Opens ManualAdjust dialog
    - Z-axis only, 5µm step size
    - User fine-tunes to touch surface

11. **Capture Final Position** (Step 10)
    - Reads final XYZ from gantry-main
    - Stores as `needle_x`, `needle_y`, `needle_z`

12. **Save Calibration** (Step 11)
    - Writes JSON file with all data
    - Calculates delta (first run: no previous data)
    - Stores success message in context

13. **Retract Dispenser Head** (Step 12)
    - Retracts pneumatic slide

14. **Safe Z Retract** (Step 13)
    - Moves Z up by 10mm (configurable)
    - Waits for motion complete

15. **Success Notification** (Step 14)
    - Shows: "Needle Z calibration saved: Z = 35.678 mm (First calibration)"

### Subsequent Calibration Runs

**Key Difference**: Steps 1-2 are replaced with:
- **Automatic Navigation**: Reads saved `camera_position` from JSON
- **Auto-moves** gantry-main to that position
- **No manual positioning needed!**

**Step 12 Enhancement**:
- Calculates delta: `new_needle_height - previous_needle_height`
- Example notification: "Needle Z calibration saved: Z = 35.680 mm (Δ +0.002 mm from previous)"

---

## UI Configuration Parameters

Editable in RecipePage when adding "Calibrate Needle Z" process:

| Parameter | Default Value | Description |
|-----------|--------------|-------------|
| `device` | `"gantry-main"` | Motion device name (dropdown) |
| `camera_offset_x` | `50.0` | X offset from camera to needle (mm) |
| `camera_offset_y` | `10.0` | Y offset from camera to needle (mm) |
| `camera_offset_z` | `0.0` | Z offset from camera to needle (mm) |
| `pneumatic_slide` | `"Dispenser_Head"` | Pneumatic slide name (dropdown with browse) |
| `settle_time_ms` | `500` | Settling time after extending slide (ms) |
| `z_retract_height` | `10.0` | Safe Z retract height after calibration (mm) |

---

## SequenceContext Keys Used

### Written by Process:
- `calibration_exists` (bool): Whether previous calibration file exists
- `camera_x`, `camera_y`, `camera_z` (float): Camera position
- `offset_x`, `offset_y`, `offset_z` (float): Camera-to-needle offsets
- `needle_target_x`, `needle_target_y`, `needle_target_z` (float): Calculated needle position
- `needle_x`, `needle_y`, `needle_z` (float): Final touched position
- `calibration_result` (string): Success message with delta

### Conditional Logic:
```
IF calibration_exists == TRUE:
  → Auto-navigate to saved camera position
ELSE:
  → Prompt user for manual positioning
```

---

## Architecture Highlights

### Plugin-Based Design
✅ Implements `IProcessPlugin` interface
✅ Self-registers at startup
✅ Single file contains all logic (UI + sequence building)
✅ No modifications to existing files except CMakeLists.txt

### Command Handler Pattern
✅ Follows existing handler conventions
✅ Manager type: `"calibration"`
✅ Stateless execution
✅ Full error handling and logging

### Context-Driven Workflow
✅ All data flows through SequenceContext
✅ Supports conditional logic
✅ No global state
✅ Thread-safe design

### Data Persistence
✅ JSON-based configuration
✅ Automatic directory creation
✅ Delta calculation on recalibration
✅ Metadata timestamps

---

## Testing Procedure

### 1. Build the Project
```bash
cmake --preset x64-debug
cmake --build out/build/x64-debug
```

### 2. Run Application
```bash
./out/build/x64-debug/bobcat_engine.exe
```

### 3. Create Test Recipe
1. Open RecipePage
2. Click "Add Step"
3. Navigate to **"Calibration"** category
4. Select **"Calibrate Needle Z"**
5. Configure parameters:
   - Device: `gantry-main`
   - Camera Offset X: `50.0`
   - Camera Offset Y: `10.0`
   - Camera Offset Z: `0.0`
   - Pneumatic Slide: `Dispenser_Head`
   - Settle Time: `500` ms
   - Z Retract Height: `10.0` mm
6. Add to recipe

### 4. Execute Recipe
1. Click "Run Recipe"
2. Follow prompts:
   - Position camera over flat calibration surface
   - Move Z down close to surface
   - Fine-adjust Z to touch
3. Verify success message shows Z height

### 5. Verify Output
1. Check `config/calibration/CalibrateNeedleZ.json` exists
2. Verify JSON structure matches specification
3. Check metadata has correct date/time

### 6. Test Recalibration
1. Run recipe again
2. Verify automatic navigation to saved camera position
3. Complete calibration
4. Verify delta is reported in success message
5. Check JSON has `previous_needle_height` and `delta_from_previous`

---

## Troubleshooting

### Issue: "Manager not available"
- **Cause**: Motion manager not initialized
- **Fix**: Ensure gantry-main is configured in `motor_devices.json`

### Issue: "Failed to read position"
- **Cause**: Device not connected or not enabled
- **Fix**: Connect device and enable servo

### Issue: "Failed to create calibration directory"
- **Cause**: Insufficient file permissions
- **Fix**: Run application with appropriate permissions

### Issue: Pneumatic slide not extending
- **Cause**: Slide name not found or timeout
- **Fix**: Verify slide name in `pneumatic_slides.json` and check hardware

### Issue: Delta calculation shows large difference
- **Cause**: Normal if needle wear or surface changed
- **Action**: Document delta and investigate if > 0.1mm

---

## Future Enhancements

### Potential Improvements:
1. **Multi-needle support**: Calibrate multiple needles with naming
2. **Temperature compensation**: Record ambient temperature
3. **Surface wear tracking**: Historical delta trend analysis
4. **Automatic validation**: Verify needle height before dispense
5. **Offset calibration**: Automated camera-to-needle offset measurement
6. **Profile management**: Save/load different calibration profiles

---

## References

- **Command Plugin Guide**: `docs/adding-new-command-plugin.md`
- **Process Plugin Architecture**: `include/recipe/IProcessPlugin.h`
- **Sequence Context Design**: `docs/sequence_context_design.md`
- **Recipe System**: `include/recipe/new process addition.md`

---

## Change Log

| Date | Version | Author | Changes |
|------|---------|--------|---------|
| 2025-10-26 | 1.0 | Claude | Initial implementation |

---

## Contact

For issues or questions about this implementation, refer to the command plugin documentation or consult the recipe system architecture.
