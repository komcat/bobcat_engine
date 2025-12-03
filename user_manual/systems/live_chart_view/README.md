# Live Chart View

Real-time data visualization with target comparison.

## Overview

The Live Chart View displays streaming data from sensors (SMU, power meters, etc.) with optional target comparison for quality assessment.

## Controls

| Button | Function |
|--------|----------|
| **Set Target** (green) | Set target value for percentage comparison |
| **Clear Target** (red X) | Remove target, return to absolute value display |
| **Channel** dropdown | Select data channel to monitor |
| **Refresh** (blue) | Refresh available channel list |

## Display Modes

### Absolute Mode (No Target)
- Shows current value with SI prefix (nA, µA, mA, etc.)
- Color based on value magnitude

### Target Mode (Target Set)
- Shows current value + percentage + status code
- Example: `PASS 97.5%`

## Status Codes & Colors

| Code | Percentage | Color | Meaning |
|------|------------|-------|---------|
| NWRK | < 50% | Gray | Need work - value far below target |
| MRGN | 50% - 80% | Cyan | Marginal - approaching target |
| PASS | 80% - 120% | Green | Pass - within acceptable range |
| EXCP | > 120% | Purple | Exceptional - exceeds target |

## Setting a Target

1. Select desired channel from dropdown
2. Click **Set Target** button
3. Enter target value in base units (e.g., `1.5e-6` for 1.5 µA)
4. Click OK

**Note:** Targets persist across program restarts.

## Chart Display

- X-axis: Time (last 5 seconds)
- Y-axis: Value in base units (auto-scaled)
- Update rate: 10 Hz

## Recipe Commands

```
Set Chart Target:    channel (optional), value (required)
Clear Chart Target:  channel (optional)
```
