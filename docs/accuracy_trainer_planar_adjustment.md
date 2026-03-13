# Planar Adjustment Trainer

## Overview

The Planar Adjustment Trainer measures surface tilt by comparing Z heights at two gantry positions separated in X or Y. It computes tilt angles in both the XZ and YZ planes, then applies an angular correction to a hexapod rotation axis (U, V, or W) to flatten the surface relative to the tool.

## UI Menu Items

The PlanarAdjustmentPage provides these sections:

| Section | Widget | Purpose |
|---------|--------|---------|
| **Teach Point 1** | `TeachPointWidget` | Jog gantry to first surface contact point, capture XYZ |
| **Teach Point 2** | `TeachPointWidget` | Jog gantry to second surface contact point, capture XYZ |
| **Tilt Result** | `TiltResultWidget` | Display computed angles (Angle X, Angle Y) with validity indicators |
| **Apply Correction** | `ApplyCorrectionWidget` | Select source angle, target hex device, target axis, reverse sign, and apply |
| **Retrace & Apply** | `RetraceAndApplyWidget` | Live retrace workflow: re-measure P1/P2, recompute, and apply in one step |

## Data Structure

### Config File: `M2AA/planar_adjustment/IR VERTICAL.json`

```json
{
  "name": "IR VERTICAL",
  "uid": "b3ce477b-e6f8-4954-bd65-5a54e2390966",
  "deviceName": "gantry-main",
  "version": 1,

  "point1": {
    "name": "Point1",
    "x": 31.1029, "y": 101.3402, "z": 8.8854,
    "valid": true
  },
  "point2": {
    "name": "Point2",
    "x": 31.0225, "y": 105.5903, "z": 8.9054,
    "valid": true
  },

  "correction": {
    "sourceAngle": "angle_x",
    "targetDevice": "hex-right",
    "targetAxis": "W",
    "reverseSign": true,
    "applied": true,
    "appliedValue": 0.2696
  }
}
```

### Config File: `M2AA/planar_adjustment/IR HORIZONTAL.json`

```json
{
  "name": "IR HORIZONTAL",
  "uid": "acedf560-eca8-426a-963e-ba299639b4cc",
  "deviceName": "gantry-main",
  "version": 1,

  "point1": {
    "name": "Point1",
    "x": 31.1409, "y": 104.1151, "z": 8.9154,
    "valid": true
  },
  "point2": {
    "name": "Point2",
    "x": 33.8928, "y": 104.1448, "z": 8.9154,
    "valid": true
  },

  "correction": {
    "sourceAngle": "angle_y",
    "targetDevice": "hex-right",
    "targetAxis": "V",
    "reverseSign": false,
    "applied": true,
    "appliedValue": -0.0
  }
}
```

## Tilt Geometry

```
                Side View (YZ plane → Angle X)
                ─────────────────────────────────

    Z ↑         P2 · (y=105.59, z=8.905)
      │            /
      │           / dZ = 0.020
      │          /
      │     P1 ·─────────── (y=101.34, z=8.885)
      │         |  dY = 4.250  |
      └──────────────────────── Y →

    angleX = atan2(dZ, |dY|) = atan2(0.020, 4.250)


                Side View (XZ plane → Angle Y)
                ─────────────────────────────────

    Z ↑         P2 · (x=33.89, z=8.915)
      │            |
      │         P1 · (x=31.14, z=8.915)  ← same Z!
      │         |  dX = 2.752  |
      └──────────────────────── X →

    angleY = atan2(dZ, |dX|) = atan2(0.0, 2.752) = 0.0 deg
```

## Calculation Example 1: IR VERTICAL (with real tilt)

### Step 1: Compute Deltas

```
P1 = (31.1029, 101.3402, 8.8854)
P2 = (31.0225, 105.5903, 8.9054)

dX = 31.0225 - 31.1029 = -0.0804
dY = 105.5903 - 101.3402 = 4.2501
dZ = 8.9054 - 8.8854 = 0.0200
```

### Step 2: Compute Tilt Angles

```
Angle X (tilt in YZ plane, rotation around X-axis):
  |dY| = 4.2501 > 1e-6  →  valid ✓
  angleX = atan2(0.0200, 4.2501) = atan2(0.0200, 4.2501)
         = 0.004706 rad
         = 0.2696 deg   ✓ (matches appliedValue in JSON)

Angle Y (tilt in XZ plane, rotation around Y-axis):
  |dX| = 0.0804 > 1e-6  →  valid ✓
  angleY = atan2(0.0200, 0.0804)
         = 0.2437 rad
         = 13.96 deg
  (Not used — config selects angle_x as source)
```

### Step 3: Compute Correction

```
Source angle: angle_x = 0.2696 deg
reverseSign: true

correction = GetCorrectionValue(0.2696, reverseSign=true)
           = -(-0.2696)          ← default negates, then reverse flips
           = +0.2696 deg

→ MoveRelative("hex-right", "W", +0.2696)
```

The hex-right W axis rotates by +0.2696 degrees to compensate for the 0.27-degree tilt measured in the YZ plane.

## Calculation Example 2: IR HORIZONTAL (flat surface)

### Step 1: Compute Deltas

```
P1 = (31.1409, 104.1151, 8.9154)
P2 = (33.8928, 104.1448, 8.9154)

dX = 33.8928 - 31.1409 = 2.7519
dY = 104.1448 - 104.1151 = 0.0297
dZ = 8.9154 - 8.9154 = 0.0000
```

### Step 2: Compute Tilt Angles

```
Angle X:
  |dY| = 0.0297 > 1e-6  →  valid ✓
  angleX = atan2(0.0, 0.0297) = 0.0 deg

Angle Y:
  |dX| = 2.7519 > 1e-6  →  valid ✓
  angleY = atan2(0.0, 2.7519) = 0.0 deg  ← flat surface, no tilt
```

### Step 3: Compute Correction

```
Source angle: angle_y = 0.0 deg
reverseSign: false

correction = GetCorrectionValue(0.0, reverseSign=false)
           = -(0.0)
           = -0.0 deg

→ Correction is ~0, move skipped   ✓ (matches JSON: appliedValue = -0.0)
```

The horizontal measurement confirms the surface is flat in the XZ plane — no V-axis correction needed.

## Live Retrace Workflow

When run from a recipe with a manual callback:

```
Step 1: Show dialog → "Jog to Point 1, then click OK"
Step 2: Capture live P1 from current gantry position
Step 3: Auto-move gantry by taught delta:
          MoveRelative(X, dX=-0.0804)
          MoveRelative(Y, dY=+4.2501)
          MoveRelative(Z, dZ=+0.0200)
Step 4: Show dialog → "Adjust Point 2 if needed, then click OK"
Step 5: Capture live P2 from current gantry position
Step 6: Recompute angles from live P1/P2
Step 7: correction = GetCorrectionValue(liveAngle, reverseSign)
Step 8: MoveRelative(targetDevice, targetAxis, correction)
Step 9: Save updated training set with new P1/P2 values
```

## How Correction Applies to Hex Axes

| Measured Tilt | Source | Target Device | Target Axis | Physical Effect |
|---------------|--------|--------------|-------------|-----------------|
| YZ plane tilt | `angle_x` | hex-right | W | Rotates around W to level in YZ |
| YZ plane tilt | `angle_x` | hex-left | U | Rotates around U to level in YZ |
| XZ plane tilt | `angle_y` | hex-right | V | Rotates around V to level in XZ |
| XZ plane tilt | `angle_y` | hex-left | V | Rotates around V to level in XZ |

The `reverseSign` flag handles cases where the hex rotation axis is oriented opposite to the expected direction (sign convention depends on physical mounting).

## Recipe Usage

```json
{
  "type": "Run Planar Adjustment",
  "parameters": {
    "training_set_name": "IR VERTICAL",
    "source_angle": "angle_x",
    "target_device": "hex-right",
    "target_axis": "W",
    "reverse_sign": "true"
  }
}
```

## Sequence Context Output

```
planar_adjustment.result.success         = true
planar_adjustment.result.training_set    = "IR VERTICAL"
planar_adjustment.result.angle_x_deg     = "0.269623"
planar_adjustment.result.angle_y_deg     = "13.960000"
planar_adjustment.result.correction_deg  = "0.269623"
planar_adjustment.result.target_device   = "hex-right"
planar_adjustment.result.target_axis     = "W"
planar_adjustment.result.reverse_sign    = "true"
```

## Source Files

| File | Purpose |
|------|---------|
| `include/AppUI/PlanarAdjustmentTrainer/PlanarAdjustmentData.h` | Data structs + tilt math |
| `include/AppUI/PlanarAdjustmentTrainer/PlanarAdjustmentStore.h/cpp` | JSON persistence |
| `include/AppUI/PlanarAdjustmentTrainer/PlanarAdjustmentPage.h/cpp` | UI page |
| `include/AppUI/PlanarAdjustmentTrainer/TeachPointWidget.h` | Point teaching UI |
| `include/AppUI/PlanarAdjustmentTrainer/TiltResultWidget.h` | Angle display UI |
| `include/AppUI/PlanarAdjustmentTrainer/ApplyCorrectionWidget.h` | Correction apply UI |
| `include/AppUI/PlanarAdjustmentTrainer/RetraceAndApplyWidget.h` | Live retrace UI |
| `include/sequence/PlanarAdjustmentCommandHandler.h/cpp` | Headless command handler |
| `include/recipe/processes/PlanarAdjustmentProcesses.cpp` | Recipe plugin |
