# Rotation Scanning Algorithm Guide

## Overview

The Rotation Scanning algorithm optimizes a hexapod's rotation axis (U, V, or W) by iteratively rotating and re-aligning the translation axes after each rotation step. This is useful for angular alignment optimization where the optimal rotation angle is unknown.

## Axis Mapping

| Rotation Axis | Rotation About | Translation Scan |
|---------------|----------------|------------------|
| **W (Roll)**  | X axis         | X, Y             |
| **V (Pitch)** | Y axis         | X, Z             |
| **U (Yaw)**   | Z axis         | Y, Z             |

## Algorithm Flow

```
┌─────────────────────────────────────────────────────────────┐
│  1. RECORD BASELINE                                         │
│     - Read current value from data channel                  │
│     - Store start position (XYZUVW)                         │
│     - Set globalPeak = baseline                             │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  2. SCAN POSITIVE DIRECTION                                 │
│     Loop:                                                   │
│       a. Rotate: axis += rotationStep (e.g., W += 0.2°)    │
│       b. Run translation scan (XY for W axis)              │
│          - Track peak value and 6-axis position            │
│       c. If peak > baseline × (1 + threshold):             │
│          → Update globalPeak, reset counter, continue      │
│       d. Else:                                              │
│          → Increment noImprovement counter                 │
│          → If counter >= maxNoImprovement: STOP            │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  3. EVALUATE POSITIVE DIRECTION                             │
│     If improved → Move to peak, DONE ✓                      │
│     Else → Return to start, try negative                    │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  4. SCAN NEGATIVE DIRECTION (same as step 2)                │
│     Loop with axis -= rotationStep                          │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  5. FINAL RESULT                                            │
│     If improved → Move to peak, DONE ✓                      │
│     Else → Return to start position (no improvement)        │
└─────────────────────────────────────────────────────────────┘
```

## Parameters

### Rotation Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `rotationAxis` | W | Axis to optimize (U, V, or W) |
| `rotationStep` | 0.2° | Rotation increment per step |
| `improvementThreshold` | 1% | Minimum improvement to continue |
| `maxNoImprovementCount` | 2 | Stop after N rotations without improvement |
| `maxRotationRange` | 5.0° | Safety limit for total rotation |

### Translation Scan Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `translationStepSizes` | [2, 1, 0.5] µm | Fine step sizes for XY/XZ/YZ scan |
| `motionSettleTimeMs` | 400 ms | Wait time after each move |
| `consecutiveDecreasesLimit` | 3 | Stop direction after 3 decreases |
| `maxTranslationDistance` | 0.2 mm | Max distance per direction |

## Usage

### From UI (ScanControlWidget)

1. Select **Device** (hex-right or hex-left)
2. Select **Rotation Axis** (W/V/U)
3. Set **Step** (0.2° default)
4. Set **Threshold** (1% default)
5. Set **Max No-Imp** (2 default)
6. Select **Data Channel**
7. Click **Start Rotation Scan**

### From Code

```cpp
#include "RotationScanController.h"
#include "RotationScanTypes.h"

// Create controller
RotationScanController controller(
    motionManager,
    dataStore,
    "hex-right",
    "power_channel"
);

// Configure parameters
RotationScanParameters params;
params.rotationAxis = RotationAxis::W;  // Roll
params.rotationStep = 0.2;              // degrees
params.improvementThreshold = 0.01;     // 1%
params.maxNoImprovementCount = 2;
params.translationStepSizes = { 0.002, 0.001, 0.0005 };  // mm

// Set callbacks
controller.SetCompletionCallback([](const RotationScanResults& results) {
    if (results.success) {
        std::cout << "Peak: " << results.globalPeak.value << std::endl;
        std::cout << "Total rotation: " << results.totalRotation << "°" << std::endl;
    }
});

controller.SetProgressCallback([](const RotationScanProgressEvent& event) {
    std::cout << event.message << std::endl;
});

// Start scan (runs in background thread)
controller.StartScan(params);

// To stop gracefully (returns to best position):
controller.HaltScan();
```

## Behavior Summary

| Scenario | Result |
|----------|--------|
| Positive direction improves | Stop immediately, return to peak |
| Positive fails, negative improves | Return to peak from negative |
| Both directions fail | Return to original start position |
| Halt/Stop during scan | Return to best known peak (XYZUVW) |

## Files

| File | Purpose |
|------|---------|
| `RotationScanTypes.h` | Parameters, enums, data structures |
| `RotationScanStrategy.h` | Strategy class and context interface |
| `RotationScanStrategy.cpp` | Core algorithm implementation |
| `RotationScanController.h` | Thread management and orchestration |
| `RotationScanController.cpp` | Controller implementation |

## Example Scan Output

```
Starting ROTATION scan on hex-right
  Rotation axis: W, step: 0.20°
  Threshold: 1.0%, max no-improvement: 2
  Translation axes: X, Y, steps: 2→1→0.5 µm
  Data channel: power_meter

Baseline recorded: 1.300000 at position W=0.000
Scanning positive W direction...
Rotating W by +0.20° (total: 0.20°)
Running translation scan at W=0.20°
Translation scan: X axis, 2.0 µm steps
Translation scan: Y axis, 2.0 µm steps
...
Rotation step result: peak=1.450000, improvement=11.54% [IMPROVED]
Rotating W by +0.20° (total: 0.40°)
...
Rotation step result: peak=1.420000, improvement=9.23% [IMPROVED]
Rotating W by +0.20° (total: 0.60°)
...
Rotation step result: peak=1.380000, improvement=6.15% [IMPROVED]
Peak value decreased - stopping positive direction
Positive direction improved - returning to peak

✓ Rotation scan complete on hex-right
  Peak value: 1.450000
  Total rotation: 0.20°
  Improvement found in POSITIVE direction
```
