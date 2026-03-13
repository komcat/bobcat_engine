# Z Offset Trainer

## Overview

The Z Offset Trainer measures the vertical height difference between two surfaces (a reference datum and a target), then computes a correction to achieve a desired Z gap. The correction is transformed from global machine Z to the hexapod's local coordinate frame via a transformation matrix, accounting for any tilt the hex may have.

## UI Menu Items

The ZOffsetPage provides these sections:

| Section | Widget | Purpose |
|---------|--------|---------|
| **Teach Reference** | `TeachZPointWidget` | Jog gantry Z to touch reference surface, capture XYZ |
| **Teach Target** | `TeachZPointWidget` | Jog gantry Z to touch target surface, capture XYZ |
| **Z Difference** | `ZOffsetResultWidget` | Display measured Z diff (mm and um), target Z diff setting |
| **Apply Correction** | `ZOffsetApplyWidget` | Select target hex device, target Z diff, reverse sign, and apply |
| **Retrace & Apply** | `ZOffsetRetraceWidget` | Live retrace: navigate to surfaces, re-measure, apply correction |

## Data Structure

### Config File: `M2AA/z_offset/PIC_SOVA.json`

```json
{
  "name": "PIC_SOVA",
  "uid": "0db10629-ec40-43dd-9ebe-b18e96d195f8",
  "deviceName": "gantry-main",
  "version": 1,

  "reference": {
    "name": "Reference",
    "x": 34.0685, "y": 102.6701, "z": 8.8654,
    "valid": true
  },
  "target": {
    "name": "Target",
    "x": 33.7685, "y": 102.6701, "z": 9.0154,
    "valid": true
  },

  "correction": {
    "targetDevice": "hex-right",
    "targetZDiffUm": 150.0,
    "reverseSign": true,
    "applied": true,
    "appliedValueMm": -1.4155e-15
  }
}
```

## Z Offset Geometry

```
                            Gantry Z axis (positive = down toward surface)
                            ─────────────────────────────────────────────

    Z ↑
      │
 8.87 │     Reference ·─────── z = 8.8654 mm  (lower surface)
      │
 9.02 │     Target    ·─────── z = 9.0154 mm  (higher surface)
      │
      │     measuredZDiff = target.z - reference.z
      │                   = 9.0154 - 8.8654
      │                   = +0.150 mm = +150 um
      │
      │     Target Z gap desired: 150 um
      │     → Correction = 0.0 mm (already at target!)
      └───────────────────────────────────────
```

## Calculation Example (Using M2AA Real Data)

### Step 1: Compute Measured Z Difference

```
Reference surface: z = 8.8654 mm
Target surface:    z = 9.0154 mm

measuredZDiff = target.z - reference.z
              = 9.0154 - 8.8654
              = 0.1500 mm (150.0 um)
```

### Step 2: Compute Correction

```
targetZDiffUm = 150.0 um  (desired gap)
targetZDiffMm = 150.0 / 1000.0 = 0.150 mm

correction = targetZDiffMm - measuredZDiffMm
           = 0.150 - 0.150
           = 0.000 mm

reverseSign = true
correction = -(0.000) = -0.000 mm  ≈ 0

→ appliedValueMm ≈ -1.4155e-15  ✓ (floating point zero, matches JSON)
→ Correction is ~0, move skipped
```

The measured gap already matches the desired gap — no correction needed.

### Step 3: Hypothetical Example (Gap Mismatch)

Suppose after retrace the surfaces measure differently:

```
Live Reference: z = 8.8654 mm
Live Target:    z = 8.9654 mm  (only 100um gap instead of 150um)

measuredZDiff = 8.9654 - 8.8654 = 0.100 mm (100 um)

correction = (150.0 / 1000.0) - 0.100
           = 0.150 - 0.100
           = 0.050 mm  (need to move hex Z by +50um)

reverseSign = true
correction = -(0.050) = -0.050 mm
```

### Step 4: Global-to-Local Transformation

The hex device operates in its own rotated/tilted coordinate frame. A pure global Z correction must be decomposed into the hex's local axes:

```
Global delta vector:
  globalDelta = (0, 0, -0.050, 0, 0, 0)   ← X=0, Y=0, Z=-0.050mm, U=V=W=0

Load transformation matrix for "hex-right" from:
  config/transformation_matrix.json

Transform:
  localDelta = TransformGlobalToLocal(globalDelta, matrix)

Example result (depends on hex orientation):
  localDelta = (dX: 0.002, dY: -0.001, dZ: -0.049, dU: 0, dV: 0, dW: 0)

Apply to hex current position:
  hexCurrent = (7.680, -14.270, -3.460, 0.000003, 0.000006, 0.000001)

  hexTarget.x = 7.680 + 0.002  = 7.682
  hexTarget.y = -14.270 - 0.001 = -14.271
  hexTarget.z = -3.460 - 0.049 = -3.509
  hexTarget.u = 0.000003  (unchanged)
  hexTarget.v = 0.000006  (unchanged)
  hexTarget.w = 0.000001  (unchanged)

→ MoveToAbs6("hex-right", hexTarget)
```

The small dX and dY components arise because the hex's Z axis is not perfectly aligned with the global Z axis. The transformation matrix accounts for this misalignment.

## Why Global-to-Local Transform?

```
                    Global Frame                 Hex Local Frame
                    ────────────                 ───────────────

    Z ↑ (vertical)                     Z_hex ↑ (tilted)
    │                                      /
    │                                     / ← hex Z axis is tilted
    │                                    /     relative to global Z
    │                                   /
    └───── X                           └───── X_hex

    A "move Z by -0.05mm" in global frame
    becomes "move a bit in X, Y, AND Z" in hex local frame
    to achieve the same vertical displacement.
```

Without the transformation, a pure hex-Z move would not be truly vertical — it would follow the hex's tilted Z axis, resulting in an incorrect gap.

## Live Retrace Workflow

When run from a recipe with a manual callback:

```
Step 1: MoveToAbs(gantry-main, savedRef.x, savedRef.y, savedRef.z)
Step 2: Show dialog → "Confirm Reference surface contact, click OK"
Step 3: Capture live Reference Z from current gantry position
Step 4: MoveToAbs(gantry-main, savedTarget.x, savedTarget.y, savedTarget.z)
Step 5: Show dialog → "Confirm Target surface contact, click OK"
Step 6: Capture live Target Z from current gantry position
Step 7: measuredZDiff = liveTarget.z - liveReference.z
Step 8: correction = (targetZDiffUm / 1000) - measuredZDiff
Step 9: If reverseSign: correction = -correction
Step 10: Transform correction from global Z to local hex axes
Step 11: MoveToAbs6(hexTarget)
Step 12: Save updated training set with new reference/target positions
```

## How Correction Applies to Axes

| What | Value | Target |
|------|-------|--------|
| Input | Measured Z gap (mm) between two surfaces | — |
| Desired | `targetZDiffUm` (microns) | — |
| Correction | `(targetZDiff_mm - measuredZDiff_mm)` | — |
| Sign | Optionally reversed via `reverseSign` | — |
| Transform | Global Z → local hex XYZUVW via transformation matrix | Hex XYZ |
| Motion | `MoveToAbs6(currentPos + localDelta)` | hex-right or hex-left |

Note: Only XYZ of the hex are modified. UVW (rotation) remain unchanged because the correction is purely translational.

## Recipe Usage

```json
{
  "type": "Run Z Offset",
  "parameters": {
    "training_set_name": "PIC_SOVA",
    "target_device": "hex-right",
    "target_z_diff_um": "150",
    "reverse_sign": "true"
  }
}
```

## Sequence Context Output

```
z_offset.result.success              = true
z_offset.result.training_set         = "PIC_SOVA"
z_offset.result.measured_z_diff_mm   = "0.150000"
z_offset.result.target_z_diff_um     = "150.000000"
z_offset.result.correction_mm        = "-0.000000"
z_offset.result.target_device        = "hex-right"
z_offset.result.reverse_sign         = "true"
```

## Source Files

| File | Purpose |
|------|---------|
| `include/AppUI/ZOffsetTrainer/ZOffsetData.h` | Data structs + Z offset math |
| `include/AppUI/ZOffsetTrainer/ZOffsetStore.h/cpp` | JSON persistence |
| `include/AppUI/ZOffsetTrainer/ZOffsetPage.h/cpp` | UI page |
| `include/AppUI/ZOffsetTrainer/TeachZPointWidget.h` | Z point teaching UI |
| `include/AppUI/ZOffsetTrainer/ZOffsetResultWidget.h` | Z difference display UI |
| `include/AppUI/ZOffsetTrainer/ZOffsetApplyWidget.h` | Correction apply UI |
| `include/AppUI/ZOffsetTrainer/ZOffsetRetraceWidget.h` | Live retrace UI |
| `include/sequence/ZOffsetCommandHandler.h/cpp` | Headless command handler |
| `include/recipe/processes/ZOffsetProcesses.cpp` | Recipe plugin |
