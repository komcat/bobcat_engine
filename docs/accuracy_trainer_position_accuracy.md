# Position Accuracy Trainer

## Overview

The Position Accuracy Trainer corrects multi-point coordinate frame drift between a taught (offline) calibration and the live machine state. It builds a local coordinate frame from 2-3 fiducial reference points, stores bonding positions in that frame, then re-measures the references live to compute a rigid body transform that maps all bonding positions to their actual corrected world coordinates.

## UI Menu Items

The AccuracyTrainerPage provides these navigation tabs:

| Tab | Widget | Purpose |
|-----|--------|---------|
| **Welcome** | — | Overview and instructions |
| **Teach Reference** | `TeachReferenceWidget` | Jog gantry to 2-3 fiducial markers and capture their global XYZ |
| **Teach Bonding** | `TeachBondingWidget` | Jog gantry to bonding targets (2 or 3-corner capture) and record in local frame |
| **Teach Multi-Device** | `TeachMultiDeviceWidget` | Register hex device positions (single-point, 2-corner, or 3-corner) with Z placement params |
| **Run Alignment** | `RunAlignmentWidget` | Execute live alignment: re-measure refs, compute transform, move to corrected positions |
| **Multi-Device Run** | `MultiDeviceRunAlignmentWidget` | Run alignment with hex device registration and bonding moves |
| **Edit Z Offset** | `EditZOffsetWidget` | Configure component thickness, bondline thickness, and Z adjustment per hex device |

## Data Structure

### Config File: `M2AA/accuracy_trainer/M2_PE_V2.json`

```json
{
  "name": "M2_PE_V2",
  "uid": "b939720f-2282-4517-a1a9-8e9b0b96fb0c",
  "deviceName": "gantry-main",
  "version": 2,

  "referencePoints": [
    {
      "name": "Ref1",
      "globalX": 96.5459, "globalY": 63.2307, "globalZ": 0.2154,
      "visionProfilePath": "",
      "valid": true
    },
    {
      "name": "Ref2",
      "globalX": 96.5079, "globalY": 68.3708, "globalZ": 0.2154,
      "visionProfilePath": "",
      "valid": true
    }
  ],

  "origin": { "x": 96.5459, "y": 63.2307 },
  "xAxis":  { "x": -0.00739, "y": 0.99997 },
  "yAxis":  { "x": -0.99997, "y": -0.00739 },

  "bondingPoints": [
    {
      "name": "Bond1",
      "corners": {
        "tl": [93.2895, 65.1867],
        "br": [93.6759, 66.3641],
        "tr": null
      },
      "hasThirdCorner": false,
      "globalX": 93.4827, "globalY": 65.7754, "globalZ": 0.2154,
      "localX": 2.5673, "localY": 3.0443,
      "valid": true
    }
  ],

  "bondingDeviceAssignments": { "Bond1": "hex-right" },

  "hexDevicePositions": [
    {
      "deviceName": "hex-right",
      "gantryTeachMode": 1,
      "hexPos": {
        "x": 7.680, "y": -14.270, "z": -3.460,
        "u": 0.0000026, "v": 0.0000057, "w": 0.0000008
      },
      "gantryView": { "x": 92.9496, "y": 76.3822, "z": -11.6846 },
      "zCompPos":   { "x": 92.9496, "y": 76.3822, "z": -11.6846 },
      "zPlacePos":  { "x": 93.9496, "y": 65.3822, "z": 1.1154 },
      "componentThickness": 0.58,
      "bondlineThickness": 0.0,
      "hexZAdjustment": -12.22,
      "valid": true
    }
  ]
}
```

## Calculation Example (Using M2AA Real Data)

### Step 1: Build Reference Frame from Taught Points

```
Ref1 = (96.5459, 63.2307)
Ref2 = (96.5079, 68.3708)

dx = 96.5079 - 96.5459 = -0.0380
dy = 68.3708 - 63.2307 =  5.1401
len = sqrt(0.0380^2 + 5.1401^2) = sqrt(0.001444 + 26.421) = 5.14024

X-axis = (dx/len, dy/len) = (-0.00739, 0.99997)
Y-axis = (-xAxisY, xAxisX) = (-0.99997, -0.00739)   ← CCW 90 deg

Origin = Ref1 = (96.5459, 63.2307)
```

The X-axis is nearly pure +Y (0.99997), meaning the reference frame is rotated ~89.58 deg from the machine frame. The two fiducials are almost vertically aligned.

### Step 2: Convert Bonding Point to Local Coordinates

```
Bond1 global = (93.4827, 65.7754)

dx = 93.4827 - 96.5459 = -3.0632
dy = 65.7754 - 63.2307 =  2.5447

localX = dx * xAxisX + dy * xAxisY
       = (-3.0632)(-0.00739) + (2.5447)(0.99997)
       = 0.02264 + 2.5446
       = 2.5673   ✓ (matches JSON)

localY = dx * yAxisX + dy * yAxisY
       = (-3.0632)(-0.99997) + (2.5447)(-0.00739)
       = 3.0631 + (-0.0188)
       = 3.0443   ✓ (matches JSON)
```

### Step 3: Live Alignment (Hypothetical)

Suppose during live alignment, the user re-measures and the references have shifted slightly:

```
Live Ref1 = (96.5509, 63.2357)   ← shifted +0.005 in both X and Y
Live Ref2 = (96.5129, 68.3758)   ← same shift applied

Live X-axis ≈ (-0.00739, 0.99997)   (nearly identical rotation)
Live Origin  = (96.5509, 63.2357)
```

### Step 4: Compute Corrected Bonding Position

```
correctedX = liveOriginX + localX * liveXAxisX + localY * liveYAxisX
           = 96.5509 + 2.5673 * (-0.00739) + 3.0443 * (-0.99997)
           = 96.5509 + (-0.01897) + (-3.04421)
           = 93.4877

correctedY = liveOriginY + localX * liveXAxisY + localY * liveYAxisY
           = 63.2357 + 2.5673 * 0.99997 + 3.0443 * (-0.00739)
           = 63.2357 + 2.5672 + (-0.02250)
           = 65.7804

Delta from original: dX = 93.4877 - 93.4827 = +0.005
                     dY = 65.7804 - 65.7754 = +0.005
```

The bonding position correctly shifts by the same +0.005mm the references moved.

### Step 5: Hex Z Adjustment Calculation

From the M2AA data:

```
zComp  = -11.6846 mm   (gantry Z at component top surface)
zPlace =   1.1154 mm   (gantry Z at placement surface)
componentThickness = 0.58 mm
bondlineThickness  = 0.00 mm

hexZAdjustment = zComp - zPlace - (componentThickness + bondlineThickness)
               = -11.6846 - 1.1154 - (0.58 + 0.0)
               = -12.80 - 0.58
               = -12.22 mm   ✓ (matches JSON: -12.22)
```

This Z adjustment is transformed from global Z to local hex axes via the transformation matrix before applying.

### Step 6: Hex Lateral Move (Two-Stage)

When Bond1 is assigned to hex-right, the system computes:

```
Stage 1 - Lateral:
  gantryDeltaX = correctedBondX - hexRegistrationCenterX
  gantryDeltaY = correctedBondY - hexRegistrationCenterY
  globalDelta = (gantryDeltaX, gantryDeltaY, 0, 0, 0, 0)
  localDelta = TransformGlobalToLocal(globalDelta, matrix)
  hexTarget = currentHexPos + localDelta
  → MoveToAbs6(hexTarget)

Stage 2 - Height:
  globalDelta = (0, 0, hexZAdjustment, 0, 0, 0)  = (0, 0, -12.22, 0, 0, 0)
  localDelta = TransformGlobalToLocal(globalDelta, matrix)
  hexTarget = currentHexPos + localDelta
  → MoveToAbs6(hexTarget)
```

The two stages are separated to avoid crash risk during diagonal moves.

## Recipe Usage

```json
{
  "type": "Run Accuracy Trainer Alignment",
  "parameters": {
    "training_set_name": "M2_PE_V2",
    "register_hex_left": "false",
    "register_hex_right": "true",
    "move_to_bonding": "true"
  }
}
```

## Sequence Context Output

```
accuracy_trainer.result.success       = true
accuracy_trainer.result.training_set  = "M2_PE_V2"
accuracy_trainer.result.ref_count     = "2"
accuracy_trainer.result.bond_count    = "1"
accuracy_trainer.result.scale         = "1.000000"
accuracy_trainer.result.live_origin_x = "96.550900"
accuracy_trainer.result.live_origin_y = "63.235700"
accuracy_trainer.result.bond_0.name         = "Bond1"
accuracy_trainer.result.bond_0.corrected_x  = "93.487700"
accuracy_trainer.result.bond_0.corrected_y  = "65.780400"
accuracy_trainer.result.bond_0.corrected_z  = "0.215400"
accuracy_trainer.result.bond_0.delta_x      = "0.005000"
accuracy_trainer.result.bond_0.delta_y      = "0.005000"
accuracy_trainer.result.bond_0.device       = "hex-right"
accuracy_trainer.result.hex_0.device        = "hex-right"
accuracy_trainer.result.hex_0.x             = "7.680010"
accuracy_trainer.result.hex_0.y             = "-14.270007"
accuracy_trainer.result.hex_0.z             = "-3.460000"
accuracy_trainer.result.hex_0.z_adjustment  = "-12.220000"
```

## Source Files

| File | Purpose |
|------|---------|
| `include/AppUI/AccuracyTrainer/AccuracyTrainerData.h` | Data structs + transform math |
| `include/AppUI/AccuracyTrainer/AccuracyTrainerStore.h/cpp` | JSON persistence |
| `include/AppUI/AccuracyTrainer/AccuracyTrainerPage.h/cpp` | UI page |
| `include/sequence/AccuracyTrainerCommandHandler.h/cpp` | Headless command handler |
| `include/recipe/processes/AccuracyTrainerProcesses.cpp` | Recipe plugin |
