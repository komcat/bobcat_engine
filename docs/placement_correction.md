# Placement Correction — How It Works (v3, Normal-Placement Model)

This document explains the retrace math used by the Placement Correction
Trainer (`include/AppUI/PlacementCorrectionTrainer/`) and walks through a
worked example using the real saved training set
`config/SIPHOG888/placement_correction/VISION LEFT FIRST LIGHT.json`.

---

## The problem it solves

A hex-device holds a lens. At run time the motion system drops the hex to a
predetermined named pose (e.g. `lensplace`) — the lens always lands at the
same **physical** spot relative to the hex. But the **optical** optimum
("first light") is a slightly different spot: thermal drift, mechanical
re-positioning, or teach-time inaccuracies mean the landing spot and the
ideal spot diverge. The retrace flow figures out the difference and writes a
**corrected** hex pose (`PLACE_ADJ`) that the recipe then uses so the lens
lands at first light, not just at the default mechanical landing.

---

## Three taught lines

| Line | Role | Re-measured during retrace? |
|---|---|---|
| **Reference** (2 pts) | A fixed landmark on the fixture (e.g. SLED edge). Defines the local coordinate frame. | ✅ Yes |
| **Target** (2 pts) | The **ideal** (first-light) lens position. Captured once at teach time, never re-measured. | ❌ No |
| **Normal Placement** (2 pts) | Where the lens **physically lands** when the hex is at its default named pose. | ✅ Yes |

At teach time the system computes and caches:

```
designOffset = midpoint(target_line) − midpoint(normal_line)
```

This is the **engineering intent** — the constant vector "from where the
lens actually lands → where we want it to be", expressed in gantry global
coordinates (mm).

---

## The correlation anchor: Target Position (master pose)

The JSON field `config.masterPositionName` (UI label: **"Target Position"**,
tooltip: *"first light position"*) is the name of a hex-local pose stored in
`motor_devices.json`. It correlates the two coordinate frames:

> "When the hex is at `masterX/Y/Z/U/V/W`, the lens it holds is at the
> first-light spot visible in the gantry-main camera."

So the master pose anchors hex-local ⇄ gantry-global. The transformation
matrix (`config/transformation_matrix.json`) handles the rotational
relationship between small displacements in the two frames. Together the
master pose + matrix let us take a gantry-global XY correction and write a
hex-local XYZUVW delta.

---

## Retrace wizard (6 states)

```
Idle → WaitRefP1 → WaitRefP2 → WaitNrmP1 → WaitNrmP2 → Review
```

1. **Ref P1 / P2** — auto-nav gantry to taught reference positions, capture
   live XY. These two points define the **new** local coordinate frame.
2. **Nrm P1 / P2** — auto-nav gantry to taught normal-placement positions,
   capture live XY. This is where the lens actually landed this cycle.
3. **Review** — compute the correction and let the operator Save it.

Vision profiles can be attached to any point for auto-detect (iterates
detect→move→detect until residual < 5 µm, then captures position).

---

## The math (computeRetraceResults)

Let:
- `refA_t` = taught reference angle (atan2 of taught Ref line), stored in
  `set.referenceLine.angleDeg`.
- `refA_r` = retraced reference angle (from retraced Ref line).
- `Δθ` = `refA_r − refA_t` — how much the frame rotated since teach time.
- `nC_r` = midpoint of **retraced** normal placement line (gantry global).
- `dO` = cached `(designOffsetX, designOffsetY)` from the set.
- `liveHex` = current hex pose (XYZUVW) read at the start of Review.

### Step 1: rotate the design offset

```
rotatedOffset = R(Δθ) · designOffset
```

where R(Δθ) is the standard 2D rotation matrix. The reason: the reference
frame may have rotated slightly; the ideal offset between "landed" and
"ideal" rotates with it.

### Step 2: correction in gantry global

The lens is physically at `nC_r`; we want it at `nC_r + rotatedOffset`.
The hex translates the lens 1:1, so the hex must move by:

```
correctionGlobal = rotatedOffset
```

(The ideal target point `idealTarget = nC_r + rotatedOffset` is also
displayed in the Review text for verification.)

### Step 3: transform to hex-local

The gantry-global XY correction is rotated into hex-local XYZ using the
per-device matrix loaded by `TransformationMatrixLoader`. This yields
`(corrLocalX, corrLocalY, corrLocalZ)`.

### Step 4: apply to live hex + tune offsets

```
correctedX = liveHex.x + corrLocalX + tuneOffsetX
correctedY = liveHex.y + corrLocalY + tuneOffsetY
correctedZ = liveHex.z + corrLocalZ + tuneOffsetZ
correctedU = liveHex.u + Δθ          + tuneOffsetU   (if applyUCorrection)
correctedV = liveHex.v + tuneOffsetV
correctedW = liveHex.w + tuneOffsetW
```

The corrected pose is written back to `config.targetPositionName`
(UI label: **"Position after correction"**, e.g. `PLACE_ADJ`) in
`motor_devices.local.json`. The recipe's next placement step uses
`PLACE_ADJ` instead of the default pose and the lens lands at first light.

---

## Worked example — `VISION LEFT FIRST LIGHT.json`

All units in mm and degrees. Coordinates are gantry-main global.

### Taught data

**Reference line** (SLED edge, viewed through `vision_SLED_emitter.json` at
P1 and `vision_PIC1.json` at P2):

| | X | Y |
|---|---|---|
| Ref P1 | 171.9375 | 80.1461 |
| Ref P2 | 178.5090 | 80.0365 |

- Midpoint `RC_t`: `(175.2233, 80.0913)`
- Angle `refA_t` = `atan2(80.0365 − 80.1461, 178.5090 − 171.9375)`
  = `atan2(−0.1096, 6.5716)` ≈ **−0.955°**

**Target line** (first-light lens apex, vision profile
`vision_collimate_apex.json`). Note P1 == P2: target is effectively a
single point — no usable angle, which is fine because the v3 math doesn't
use taught target angle.

| | X | Y |
|---|---|---|
| Tgt P1 | 172.9940 | 80.1408 |
| Tgt P2 | 172.9940 | 80.1408 |

- Midpoint `TC_t`: `(172.9940, 80.1408)`

**Normal placement line** (lens apex after default `lensplace` drop):

| | X | Y |
|---|---|---|
| Nrm P1 | 173.1333 | 80.2217 |
| Nrm P2 | 173.1255 | 80.2377 |

- Midpoint `NC_t`: `(173.1294, 80.2297)`

### designOffset (cached)

```
designOffset = TC_t − NC_t
             = (172.9940 − 173.1294,  80.1408 − 80.2297)
             = (−0.1354,  −0.0889)  mm
```

Interpretation: the ideal lens spot is **0.135 mm in −X and 0.089 mm in −Y**
from where the default `lensplace` drop puts the lens.

### Master pose (Target Position / first light)

From `config`:

```
masterPositionName = "PLACE_240414"
masterX = -5.7240   masterY = -4.6200   masterZ = -0.8530
masterU = -2.0000°  masterV = 0.0000°   masterW = 0.0000°
```

This is the hex pose where the lens is at the first-light spot in the
gantry camera's view.

### Retrace-time measurements (hypothetical)

Say a new retrace yields:

**Retraced reference:**

| | X | Y |
|---|---|---|
| Ref P1' | 171.9400 | 80.1500 |
| Ref P2' | 178.5110 | 80.0390 |

- `RC_r` ≈ `(175.2255, 80.0945)`
- `refA_r` = `atan2(−0.1110, 6.5710)` ≈ **−0.968°**

**Retraced normal:**

| | X | Y |
|---|---|---|
| Nrm P1' | 173.1400 | 80.2300 |
| Nrm P2' | 173.1325 | 80.2465 |

- `NC_r` ≈ `(173.1363, 80.2383)`

### Correction computation

Frame rotation:
```
Δθ = refA_r − refA_t = −0.968° − (−0.955°) = −0.013°
```

Very small — frame effectively stable. Rotate designOffset by −0.013°:
```
cos(−0.013°) ≈ 0.99999997
sin(−0.013°) ≈ −0.000227

rotatedOffsetX = 0.99999997·(−0.1354) − (−0.000227)·(−0.0889)
              ≈ −0.1354 − 0.0000202
              ≈ −0.13542
rotatedOffsetY = (−0.000227)·(−0.1354) + 0.99999997·(−0.0889)
              ≈ 0.0000307 − 0.0889
              ≈ −0.08887
```

(Essentially unchanged at this Δθ magnitude.)

Ideal target in gantry global:
```
idealTarget = NC_r + rotatedOffset
            = (173.1363 + (−0.1354), 80.2383 + (−0.0889))
            = (173.0009, 80.1494)
```

Sanity check: this is **very close** to the taught target midpoint
`(172.9940, 80.1408)`. The difference (≈ 0.007 mm in X, 0.009 mm in Y)
reflects the actual drift between teach and retrace — that's the
correction the hex needs to apply.

Hex correction (gantry global):
```
correctionGlobal = rotatedOffset = (−0.1354, −0.0889)
```

### Transform to hex-local

Pass `(−0.1354, −0.0889, 0)` through the hex-left transformation matrix.
For illustration assume the matrix is effectively identity (in reality
hex-left has a specific rotation — consult `transformation_matrix.json`):

```
(corrLocalX, corrLocalY, corrLocalZ) ≈ (−0.1354, −0.0889, 0)
```

### Apply to live hex

Assuming the hex is at master pose during the retrace
(`liveHex == masterX/Y/Z/U/V/W`):

```
correctedX = −5.7240 + (−0.1354) = −5.8594
correctedY = −4.6200 + (−0.0889) = −4.7089
correctedZ = −0.8530 +  0       = −0.8530
correctedU = −2.0000           = −2.0000   (applyUCorrection = false in this set)
correctedV =  0.0000
correctedW =  0.0000
```

This `PLACE_ADJ` pose is then written to `motor_devices.local.json`. When
the recipe subsequently moves the hex to `PLACE_ADJ`, the lens lands at
the first-light position instead of at `lensplace`.

### Compared to the saved values in this set

```
targetX = -5.6580   targetY = -4.6285   targetZ = -0.8556
```

These are the results from an earlier retrace run. Differences from our
hypothetical numbers above are explained by:

- The live hex pose during the earlier retrace not being exactly at master.
- A different `Δθ` (different actual reference drift that day).
- A different `NC_r` (the lens landed slightly differently).
- A non-identity transformation matrix rotating the correction into
  hex-local coords.

---

## Edge cases and guards

- **`designOffsetValid == false`**: if either target or normal line is not
  taught, retrace refuses to start. The Idle summary lists which line is
  missing.
- **`applyUCorrection`**: some setups (like this one) disable U correction
  because the reference frame is designed to be rotation-stable, or the
  hex has mechanical constraints on U. In that case `correctedU =
  liveHex.u` (no `+ Δθ`).
- **`tuneOffsetX/Y/Z/U/V/W`**: operator-applied fine adjustments, stored
  in the set JSON. Applied **after** the computed correction as a final
  direct addition. Useful for empirical bias ("always shift 5 µm more in
  X") without re-teaching.
- **Zero-length target line** (P1 == P2, as in this set): accepted. The v3
  math doesn't use taught target angle.
- **Vision profile returns no match**: detection fails cleanly (returns
  false); the operator verifies manually and clicks Confirm. See
  `runAutoDetect` no-match guard in `PlacementRetraceWidget.cpp`.

---

## JSON schema (v3)

Top-level fields in the set JSON:

```json
{
  "version": 3,
  "uid": "…",
  "name": "…",
  "measureDevice": "gantry-main",
  "referenceLine":        { PlacementLinePoints },
  "targetLine":           { PlacementLinePoints },
  "normalPlacementLine":  { PlacementLinePoints },  // v3+
  "config":               { PlacementCorrectionConfig },
  "summaryThumbOffsets":  [ {dx,dy} x 4 ]
}
```

`PlacementLinePoints` fields: `p1x/p1y/p1z, p1Exposure, p1ImageFile,
p1VisionProfilePath, p2x/p2y/p2z, p2Exposure, p2ImageFile,
p2VisionProfilePath, valid, name`.

`PlacementCorrectionConfig` fields include:
- `targetDevice` — hex device name ("hex-left" / "hex-right")
- `masterPositionName` / `masterX/Y/Z/U/V/W` — "Target Position" anchor
- `targetPositionName` / `targetX/Y/Z/U/V/W` — "Position after correction"
- `normalPositionName` — optional named hex pose for the Move button
- `cameraId` — gantry-main camera used for retrace
- `applyUCorrection`, `reverseSign`
- `tuneOffsetX/Y/Z/U/V/W`

**Backward-compat**: v1/v2 files load with `normalPlacementLine.valid=false`;
retrace stays disabled until normal-placement is taught and the set is saved.

---

## Key source files

- `include/AppUI/PlacementCorrectionTrainer/PlacementCorrectionData.h` —
  structs, `Recalculate()`, math helpers (`Rotate2D`, `Midpoint`,
  `ComputeLineAngle`, `ComputeDeltaAngle`).
- `PlacementCorrectionStore.cpp` — JSON serialization (v3).
- `PlacementCorrectionPage.{h,cpp}` — trainer container, nav, teach widget
  hosting.
- `TeachPlacementLineWidget.{h,cpp}` — generic line-teaching widget used
  by Reference / Target / Normal.
- `PlacementRetraceWidget.{h,cpp}` — wizard + `computeRetraceResults`.
- `config/transformation_matrix.json` — hex↔gantry rotation matrices.
