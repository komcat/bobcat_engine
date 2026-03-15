# Beam Optimizer Architecture

Native beam optimization system for real-time hexapod alignment using SWIR beam detection feedback.

## Overview

The Beam Optimizer decouples beam detection results from hardware control, allowing the SWIR beam detection pipeline to publish results that any consumer can act on. Currently used for manual hexapod alignment with real-time Encircled Energy (EE) feedback.

```
SwirBeamWindow                    BeamResultBroker              BeamOptimizerManager
(detection thread)                (pub-sub hub)                 (hardware facade)
    |                                  |                              |
    | RunBeamDetection()               |                              |
    |   - d4sigma                      |                              |
    |   - encircled energy             |                              |
    |   - saturation check             |                              |
    |   - profiles                     |                              |
    |                                  |                              |
    +-- Publish(results) ------------->|                              |
    |                                  +-- callback ----------------->|
    |                                  |   OnBeamResultsUpdated()     |
    |                                  |                              |
    |                                  +-- GetLatestResults() ------->| (polling)
    |                                  |                              |
    |   DrawBeamOverlays()             |     MoveHexRelative()        |
    |     - bounding box               |     SetExposure()            |
    |     - centroid cross             |     SetPSUCurrent()          |
    |     - EE circle                  |     GetHexPosition()         |
    |     - EE% label                  |     ...                      |
```

## Files

| File | Purpose |
|------|---------|
| `include/BeamOptimizer/BeamResultBroker.h/.cpp` | Thread-safe pub-sub broker for beam detection results |
| `include/BeamOptimizer/BeamOptimizerManager.h/.cpp` | Hardware control facade (motion, camera, PSU) with beam result subscription |

## BeamResultBroker

Thread-safe pub-sub hub between the detection thread and consumers.

### Publisher Side
- `Publish(results)` — called from `SwirBeamWindow` detection thread after each `RunBeamDetection()` cycle
- Stores latest results and notifies all subscribers

### Subscriber Side
- `Subscribe(callback)` — returns a subscription ID; callback invoked on the publisher's thread
- `Unsubscribe(id)` — removes a subscription
- `GetLatestResults()` — polling alternative to callbacks (thread-safe copy)

### Thread Safety
- Single mutex protects subscriber list and latest results
- Callbacks are invoked outside the lock (copy-then-call pattern) to avoid deadlocks
- `BeginShutdown()` prevents new publishes during teardown

## BeamOptimizerManager

High-level hardware facade that subscribes to beam results and provides a clean API for optimization routines.

### Dependencies (injected before Initialize)
- `UnifiedMotionManager` — hexapod and gantry motion
- `CameraManager` — SWIR camera exposure/gain control
- `PowerSupplyManager` — laser PSU current/voltage/output
- `LogManager` — structured logging
- `BeamResultBroker` — beam result subscription

### Configuration

```cpp
struct BeamOptimizerConfig {
    std::string hexDevice = "hex-right";      // Hexapod device ID
    std::string gantryDevice = "gantry-main"; // Gantry device ID
    std::string cameraId = "swir_camera";     // Camera ID
    std::string psuDevice;                     // PSU device (empty = auto-discover)
    int psuChannel = 1;                        // PSU channel
};
```

### API

#### Beam Results
- `GetLatestBeamResults()` — thread-safe snapshot of all beam results
- `GetBeamByLabel(label)` — find specific beam ("B0", "B1")

#### Motion Control
- `MoveHexAbsolute(x, y, z, u, v, w)` — 6-axis absolute move
- `MoveHexRelative(axis, distance)` — single-axis relative move
- `MoveHexToNamedPosition(name)` — move to saved position
- `MoveGantryZ(z)` — gantry Z focus adjustment
- `GetHexPosition(x, y, z, u, v, w)` — read current hex position

#### Camera Control
- `SetExposure(microseconds)` — set camera exposure time
- `SetGain(gain)` — set camera gain
- `CaptureImage(filename)` — save current frame

#### PSU Control
- `SetPSUCurrent(milliamps)` — set laser drive current
- `SetPSUVoltage(volts)` — set voltage limit
- `SetPSUOutput(enabled)` — enable/disable output
- `ReadPSUCurrent(outMilliamps)` — read actual current

### PSU Auto-Discovery
On `Initialize()`, if no PSU device is configured, the manager auto-discovers the first available PSU from `PowerSupplyManager`. The SWIR window also auto-connects disconnected PSUs when it first discovers them during PSU polling.

## Encircled Energy (EE)

EE is computed per-beam in `RunBeamDetection()` via `ComputeEncircledEnergy()`:

```
radius = max(dx, dy) / 2
EE = (energy within radius of centroid) / (total ROI energy)
```

- Computed on the background-subtracted float image
- Single-pass over ROI pixels
- Range: 0.0 (no concentration) to 1.0 (all energy within circle)
- Well-focused beam: EE > 80%

EE is displayed in the overlay (green circle + percentage in label) and logged via BEAM OPTIMIZE console output. It serves as the primary feedback metric for manual alignment.

## Lifecycle

```
Application::initializeManagers()
    |
    +-- new BeamResultBroker
    +-- new BeamOptimizerManager
    |     SetMotionManager(), SetCameraManager(), ...
    |     SetBeamResultBroker()
    |     Initialize()  -->  Subscribe to broker
    |
    +-- SwirBeamWindowManager
          SetBeamResultBroker()  -->  Detection thread calls Publish()

Application shutdown:
    BeamResultBroker::BeginShutdown()
    BeamOptimizerManager::Shutdown()  -->  Unsubscribe
```

## Current State

The system is set up for **manual optimization**: the user observes EE% and beam parameters in real-time while manually adjusting the hexapod via jog controls. The `BeamOptimizerManager` provides the hardware API needed for future automated optimization routines (e.g., hill-climbing on EE).
