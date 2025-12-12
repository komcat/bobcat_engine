# Basler ace vs ace2 Camera Parameter Differences

This document describes the parameter naming differences between Basler ace (older) and ace2 (newer) camera models when using the Pylon SDK.

## Overview

Basler ace2 cameras use SFNC (Standard Features Naming Convention) compliant parameter names, while older ace cameras use legacy Basler-specific names. Our `PylonCamera` class handles both automatically by trying ace2 parameters first and falling back to ace parameters.

## How to Identify Camera Model from Name

| Model Prefix | Camera Series | Example |
|--------------|---------------|---------|
| `acA` | ace (older) | `acA1300-60gm`, `acA1920-40gc` |
| `a2A` | ace2 (newer) | `a2A1920-51gcPRO`, `a2A2590-22gcPRO` |

So if you see `acA1300-60gm`, it's an **ace** camera using legacy parameters (`ExposureTimeAbs`, `GainRaw`).

## Parameter Differences

### Exposure Time

| Camera Model | Parameter Name | Type | Unit |
|--------------|----------------|------|------|
| ace2 (newer) | `ExposureTime` | Float | Microseconds |
| ace (older)  | `ExposureTimeAbs` | Float | Microseconds |

**Note:** Both use the same type and unit - only the parameter NAME differs. Values are directly compatible.

**Code Reference:** `pylon_camera.cpp:944-1003` (`SetExposureTime` function)

```cpp
// Try ace2/newer cameras first: ExposureTime (float, microseconds)
if (m_camera.GetNodeMap().GetNode("ExposureTime")) {
    Pylon::CFloatParameter exposureTime(m_camera.GetNodeMap(), "ExposureTime");
    // ...
}
// Fallback for older ace cameras: ExposureTimeAbs (float, microseconds)
else if (m_camera.GetNodeMap().GetNode("ExposureTimeAbs")) {
    Pylon::CFloatParameter exposureTime(m_camera.GetNodeMap(), "ExposureTimeAbs");
    // ...
}
```

### Gain

| Camera Model | Parameter Name | Type | Unit |
|--------------|----------------|------|------|
| ace2 (newer) | `Gain` | Float | dB |
| ace (older)  | `GainRaw` | Integer | Raw units (camera-specific) |

**Note:** Unlike exposure, gain has DIFFERENT types and units between ace and ace2. The code normalizes both to a 0-10 scale by mapping to each camera's min/max range.

**Code Reference:** `pylon_camera.cpp:1007-1091` (`SetGain` function)

```cpp
// Try ace2/newer cameras first: Gain (float, in dB)
if (m_camera.GetNodeMap().GetNode("Gain")) {
    Pylon::CFloatParameter gainParam(m_camera.GetNodeMap(), "Gain");
    // ...
}
// Fallback for older ace cameras: GainRaw (integer)
else if (m_camera.GetNodeMap().GetNode("GainRaw")) {
    Pylon::CIntegerParameter gainRaw(m_camera.GetNodeMap(), "GainRaw");
    // ...
}
```

### Gain Selector

| Camera Model | Value |
|--------------|-------|
| ace2 (newer) | `All` |
| ace (older)  | `AnalogAll` |

**Code Reference:** `pylon_camera.cpp:1012-1026`

```cpp
// Try "All" first (ace2), then "AnalogAll" (ace)
try {
    gainSelector.SetValue("All");
}
catch (...) {
    try {
        gainSelector.SetValue("AnalogAll");
    }
    catch (...) {
        // If neither works, continue without setting selector
    }
}
```

## Summary Table

| Feature | ace (older) | ace2 (newer) | Conversion Needed? |
|---------|-------------|--------------|-------------------|
| Exposure parameter | `ExposureTimeAbs` | `ExposureTime` | No (same unit: μs) |
| Gain parameter | `GainRaw` (integer) | `Gain` (float, dB) | Yes (normalize to 0-10) |
| Gain selector | `AnalogAll` | `All` | N/A |

## How Detection Works

The code detects the camera model **once on connection** by parsing the model name prefix, then uses the correct parameter directly.

### Detection Logic (in `DetectCameraModel()`)

```cpp
std::string modelName = m_camera.GetDeviceInfo().GetModelName();
std::string prefix = modelName.substr(0, 3);

if (prefix == "a2A") {
    m_cameraModel = BaslerCameraModel::ACE2;  // Uses ExposureTime, Gain
}
else if (prefix == "acA") {
    m_cameraModel = BaslerCameraModel::ACE;   // Uses ExposureTimeAbs, GainRaw
}
else {
    m_cameraModel = BaslerCameraModel::UNKNOWN;  // Fallback mode
}
```

### Parameter Selection (in `SetExposureTime()`, etc.)

```cpp
// Determine parameter based on detected model
std::string paramName;
if (m_cameraModel == BaslerCameraModel::ACE2) {
    paramName = "ExposureTime";
}
else if (m_cameraModel == BaslerCameraModel::ACE) {
    paramName = "ExposureTimeAbs";
}
else {
    // UNKNOWN - use fallback (try both)
    if (m_camera.GetNodeMap().GetNode("ExposureTime")) {
        paramName = "ExposureTime";
    }
    else if (m_camera.GetNodeMap().GetNode("ExposureTimeAbs")) {
        paramName = "ExposureTimeAbs";
    }
}
```

### Benefits of Model Detection
- **Faster**: No need to try multiple parameters each time
- **Clearer logging**: Shows which model was detected on connect
- **Fallback support**: Unknown models still work via parameter detection

## Implementation Notes

1. ace2 parameters are tried first (SFNC compliant)
2. Falls back to ace parameters if ace2 parameters are not available
3. Gain values are normalized to a 0-10 scale in our API regardless of camera model
4. The same pattern is used for reading values (`GetExposureTime`, `GetGain`)

## References

- Basler Pylon SDK Documentation
- SFNC (Standard Features Naming Convention) specification
