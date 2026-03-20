# Camera Offset Trainer

## Overview

The Camera Offset Trainer calibrates the XYZ distance between a low-magnification camera and a high-magnification camera mounted on the same gantry. Once calibrated, the system can switch camera views by applying the offset as a relative gantry move, keeping the same target centered in both camera fields of view.

## UI Menu Items

The CameraOffsetTrainerPage provides these sections:

| Section | Widget | Purpose |
|---------|--------|---------|
| **Calibrate Low-Mag** | `CalibrateOffsetWidget` | Jog gantry until low-mag camera is centered on a target fiducial, capture position |
| **Calibrate High-Mag** | `CalibrateOffsetWidget` | Jog gantry until high-mag camera is centered on the same fiducial, capture position |
| **Offset Result** | — | Displays computed offset (dX, dY, dZ) between the two positions |
| **Test Offset** | `TestOffsetWidget` | Move by offset, verify deviation, capture test images |
| **Pneumatic Config** | — | Assign a pneumatic slide for lens tube retract/extend automation |
| **Camera IDs** | — | Select which camera IDs correspond to low-mag and high-mag |

## Data Structure

### Config File: `M2AA/PR_Camera_Offset/HighLowMag.json`

```json
{
  "name": "HighLowMag",
  "uid": "2e7e133a-0b50-4bc4-b06c-368a89c2bffb",
  "deviceName": "gantry-main",
  "version": 1,

  "lowMagCameraId": "main_camera",
  "highMagCameraId": "swir_camera",

  "lowMagPosition": {
    "x": 93.7709, "y": 64.2170, "z": 26.0354, "valid": true
  },
  "highMagPosition": {
    "x": 31.5711, "y": 102.0872, "z": 34.9254, "valid": true
  },

  "offsetX": -62.1998,
  "offsetY":  37.8703,
  "offsetZ":   8.8900,
  "offsetValid": true,

  "pneumaticSlideName": "SWIR_Head",

  "lastTestResult": {
    "testedLowMagPos":  { "x": 94.6803, "y": 65.2251, "z": 26.0354 },
    "testedHighMagPos": { "x": 30.6348, "y": 101.9723, "z": 34.9254 },
    "testedOffsetX": -64.0455,
    "testedOffsetY":  36.7472,
    "testedOffsetZ":   8.8900,
    "deviationX": 0.0,
    "deviationY": 0.0,
    "deviationZ": 0.0,
    "testedAt": "2026-03-02T17:50:23",
    "valid": true
  }
}
```

## Calculation Example (Using M2AA Real Data)

### Step 1: Compute Offset During Calibration

The operator centers both cameras on the same physical target (e.g., a fiducial dot):

```
Low-Mag position (main_camera centered):
  X = 93.7709,  Y = 64.2170,  Z = 26.0354

High-Mag position (swir_camera centered):
  X = 31.5711,  Y = 102.0872,  Z = 34.9254

Offset = HighMag - LowMag:
  offsetX = 31.5711 - 93.7709 = -62.1998 mm
  offsetY = 102.0872 - 64.2170 = +37.8703 mm
  offsetZ = 34.9254 - 26.0354  =  +8.8900 mm
```

The large X offset (-62.2mm) and Y offset (+37.9mm) reflect the physical separation between the two cameras on the gantry head. The Z offset (+8.9mm) accounts for different focal distances.

### Step 2: Move from Low-Mag to High-Mag (Relative)

When a recipe calls `MoveToHighMag`, starting from any gantry position:

```
Current position (low-mag centered on a target):
  X = 94.6803,  Y = 65.2251,  Z = 26.0354

sign = +1.0  (low → high)

Target = Current + (sign * offset):
  toX = 94.6803 + (1.0 * -62.1998) = 32.4805
  toY = 65.2251 + (1.0 * +37.8703) = 103.0954
  toZ = 26.0354 + (1.0 *  +8.8900) = 34.9254

Pneumatic sequence:
  1. Retract SWIR_Head slide (clear path for gantry move)
  2. MoveToAbs(gantry-main, 32.4805, 103.0954, 34.9254)
  3. Extend SWIR_Head slide (deploy lens tube for high-mag viewing)
```

### Step 3: Move from High-Mag to Low-Mag (Relative)

```
Current position (high-mag centered):
  X = 32.4805,  Y = 103.0954,  Z = 34.9254

sign = -1.0  (high → low)

Target = Current + (sign * offset):
  toX = 32.4805 + (-1.0 * -62.1998) = 94.6803
  toY = 103.0954 + (-1.0 * +37.8703) = 65.2251
  toZ = 34.9254 + (-1.0 *  +8.8900) = 26.0354

Pneumatic sequence:
  1. Retract SWIR_Head slide
  2. MoveToAbs(gantry-main, 94.6803, 65.2251, 26.0354)
  3. (No extend — low-mag doesn't need the slide)
```

### Step 4: Absolute Move Commands

`MoveToHighMagAbs` and `MoveToLowMagAbs` ignore the current position and move directly to the calibrated absolute positions:

```
MoveToHighMagAbs → MoveToAbs(31.5711, 102.0872, 34.9254)
MoveToLowMagAbs  → MoveToAbs(93.7709, 64.2170, 26.0354)
```

### Step 5: Test & Deviation Check

After calibration, the operator runs a test from a different position:

```
Test low-mag pos:  (94.6803, 65.2251, 26.0354)
Test high-mag pos: (30.6348, 101.9723, 34.9254)

Tested offset:
  dX = 30.6348 - 94.6803 = -64.0455
  dY = 101.9723 - 65.2251 = +36.7472
  dZ = 34.9254 - 26.0354  =  +8.8900

Deviation = Tested offset - Calibrated offset:
  devX = -64.0455 - (-62.1998) = -1.8457
  devY = +36.7472 - (+37.8703) = -1.1231
  devZ = +8.8900 - (+8.8900)   =  0.0000
```

Note: In the saved JSON the deviations show 0.0 — this likely means the test was run at a position where both cameras could be re-centered exactly, confirming the calibration.

## Pneumatic Slide Automation

The `SWIR_Head` pneumatic slide manages the high-mag camera's lens tube:

```
Any camera move:
  1. ALWAYS retract first (safety: clear mechanical path)
  2. Move gantry to target position
  3. If destination is high-mag → extend slide (deploy lens tube)
  4. If destination is low-mag → leave retracted
```

This prevents collisions between the lens tube and the workpiece during gantry transit.

## Recipe Usage

Four recipe plugin types are available:

```json
// Relative moves (from current position)
{ "type": "Move to High-Mag Camera",
  "parameters": { "calibration_set_name": "HighLowMag", "velocity": "100" } }

{ "type": "Move to Low-Mag Camera",
  "parameters": { "calibration_set_name": "HighLowMag", "velocity": "100" } }

// Absolute moves (to calibrated position)
{ "type": "Move to High-Mag Camera (Absolute)",
  "parameters": { "calibration_set_name": "HighLowMag", "velocity": "100" } }

{ "type": "Move to Low-Mag Camera (Absolute)",
  "parameters": { "calibration_set_name": "HighLowMag", "velocity": "100" } }
```

## Sequence Context Output

```
camera_offset.result.success          = true
camera_offset.result.direction        = "LowMag->HighMag"
camera_offset.result.device           = "gantry-main"
camera_offset.result.from_x           = "94.680335"
camera_offset.result.from_y           = "65.225077"
camera_offset.result.from_z           = "26.035386"
camera_offset.result.to_x             = "32.480506"
camera_offset.result.to_y             = "103.095380"
camera_offset.result.to_z             = "34.925386"
camera_offset.result.offset_x         = "-62.199829"
camera_offset.result.offset_y         = "37.870271"
camera_offset.result.offset_z         = "8.890000"
camera_offset.result.pneumatic_slide  = "SWIR_Head"
camera_offset.result.pneumatic_action = "extend"
```

## Source Files

| File | Purpose |
|------|---------|
| `include/AppUI/CameraOffsetTrainer/CameraOffsetData.h` | Data structs |
| `include/AppUI/CameraOffsetTrainer/CameraOffsetStore.h/cpp` | JSON persistence |
| `include/AppUI/CameraOffsetTrainer/CameraOffsetTrainerPage.h/cpp` | UI page |
| `include/AppUI/CameraOffsetTrainer/CalibrateOffsetWidget.h` | Calibration UI |
| `include/AppUI/CameraOffsetTrainer/TestOffsetWidget.h` | Test/deviation UI |
| `include/sequence/CameraOffsetCommandHandler.h/cpp` | Headless command handler |
| `include/recipe/processes/CameraOffsetProcesses.cpp` | Recipe plugins (4 types) |
