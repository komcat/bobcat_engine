# MotionCommandHandler Summary

The `MotionCommandHandler` is a command handler for the SequenceEngine that provides comprehensive motion control capabilities for ACS (gantry) and PI (hexapod) controllers.

## Overview

- **File**: `include/sequence/MotionCommandHandler.h` / `.cpp`
- **Manager Type**: `"motion"`
- **Supported Controllers**: ACS (gantry-main), PI (hex-left, hex-right)

## Dependencies

- `UnifiedMotionManager` - Unified interface for motion control
- `ProductPositionManager` - Graph and named position management
- `DataStore` - For XCAN scan data storage
- `GraphMoveWorker` - Worker threads for parallel graphNode execution

---

## Available Commands

### Basic Motion Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `MoveToPosition` | `axis`, `position`, `blocking?` | Move single axis to absolute position |
| `MoveToAbs` | `x?`, `y?`, `z?`, `u?`, `v?`, `w?` | Move multiple axes to absolute positions |
| `MoveRelative` | `axis`, `distance` | Move single axis by relative distance |
| `MoveToNamedPosition` | `positionName` | Move to a saved named position |

### Motion Control Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `StopAxis` | `axis` | Stop motion on specific axis |
| `EmergencyStop` | - | Emergency stop all axes on device |
| `HomeAxis` | `axis` | Home specific axis |
| `WaitForMotionComplete` | `timeout?` | Wait for all motion to complete |

### Configuration Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `SetVelocity` | `axis`, `velocity` | Set velocity for specific axis (ACS) |
| `SetSystemVelocity` | `velocity` | Set system-wide velocity (PI hexapods) |
| `EnableServo` | `enable` | Enable/disable servo (PI hexapods) |

### Advanced Motion Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `MoveToGraphNode` | `graphName`, `targetNode` | Navigate through motion graph to target node |
| `MoveToContextPosition` | `contextKeyX`, `contextKeyY`, ... | Move to position stored in sequence context |
| `XCAN` | `scanType`, `device`, ... | Execute adaptive scanning operations |

---

## Parallel Move Commands

### ParallelMove

Moves multiple devices simultaneously. Supports two move types:

#### Move Types

1. **`position`** - Direct move to named position (simple, fast)
2. **`graphNode`** - Navigate through motion graph (follows waypoints)

#### JSON Format

```json
{
  "moves": [
    {
      "device": "hex-left",
      "type": "graphNode",
      "target": "lensgrip",
      "graph": "ProcessFlow"
    },
    {
      "device": "gantry-main",
      "type": "graphNode",
      "target": "See Grip collimate lens",
      "graph": "ProcessFlow"
    }
  ]
}
```

#### Execution Flow (graphNode)

```
PHASE 1: Separate moves by type (position vs graphNode)

PHASE 2: Create GraphMoveWorker for each graphNode move
         - Each worker runs in separate QThread
         - All workers start simultaneously (true parallel!)

PHASE 3: Start all position moves (non-blocking)

PHASE 4: Unified wait loop
         - Poll all workers and position moves every 50ms
         - Check for stop requests
         - Check for timeout (60 seconds)

PHASE 5: Verify all moves completed successfully
```

#### Parallel Execution Diagram

```
BEFORE (Sequential - OLD):
hex-left:    [home] -> [approach] -> [lensgrip] ----DONE----
                                                      |
gantry-main: ----------------------------------------[safe] -> [SeeLens] -> [SeeGrip]

AFTER (Parallel - NEW):
hex-left:    [home] -> [approach] -> [lensgrip] ----DONE (waits)
gantry-main: [safe] -> [SeeLens] ----> [SeeGrip] ----DONE
             ^                                        ^
             |-------- BOTH MOVING SIMULTANEOUSLY ----|
```

#### Stop Handling

When user clicks Stop:
1. `context->IsStopRequestedAny()` detected in polling loop
2. `requestStop()` called on all GraphMoveWorkers
3. `EmergencyStop()` called on all devices (physically stops motion)
4. Workers exit cleanly with error message
5. ParallelMove returns `false` with "Stopped by user request"

---

### ParallelMoveRelative

Moves multiple devices by relative distances simultaneously.

#### JSON Format

```json
{
  "moves": [
    {"device": "hex-left", "axis": "X", "distance": 4.0},
    {"device": "hex-right", "axis": "Z", "distance": -2.0}
  ]
}
```

---

## GraphMoveWorker

Worker class for executing graph path traversal in a separate thread.

### Class Definition

```cpp
class GraphMoveWorker : public QThread
{
    Q_OBJECT
public:
    GraphMoveWorker(
        UnifiedMotionManager* motionManager,
        ProductPositionManager* positionManager,
        const std::string& deviceName,
        const std::string& graphName,
        const std::string& targetNodeName,
        QObject* parent = nullptr
    );

    void requestStop();
    bool isCompleted() const;
    bool isSuccess() const;
    QString getErrorMessage() const;

signals:
    void moveStarted(QString deviceName, QString targetNode);
    void waypointReached(QString deviceName, QString waypointName, int index, int total);
    void moveCompleted(QString deviceName, bool success, QString message);
    void logMessage(QString message);

protected:
    void run() override;
};
```

### Worker Execution

1. Find current position in graph
2. Calculate path using Dijkstra's algorithm
3. For each waypoint in path:
   - Check stop flag
   - Execute blocking move to waypoint
   - Wait for settling time (50ms hexapod, 200ms gantry)
   - Emit `waypointReached` signal
4. Emit `moveCompleted` signal

---

## Context Value Resolution

The handler supports resolving values from SequenceContext:

```cpp
// Direct value
"position": "100.5"

// Context reference
"position": "context:needle_target_x"
```

---

## Error Handling

All commands return `false` on failure with `errorMessage` populated:

- Missing required parameters
- Device not found
- Position not found
- Motion timeout
- Graph path not found
- Stop requested by user

---

## Usage Example (Recipe JSON)

```json
{
  "name": "Parallel Move to Grip",
  "type": "Parallel Move",
  "enabled": true,
  "parameters": {
    "moves": [
      {
        "device": "hex-left",
        "type": "graphNode",
        "target": "lensgrip",
        "graph": "ProcessFlow"
      },
      {
        "device": "gantry-main",
        "type": "graphNode",
        "target": "See Grip collimate lens",
        "graph": "ProcessFlow"
      }
    ]
  }
}
```

---

## Files

| File | Description |
|------|-------------|
| `MotionCommandHandler.h` | Header with command handler class |
| `MotionCommandHandler.cpp` | Implementation (~1600 lines) |
| `GraphMoveWorker.h` | Worker thread header |
| `GraphMoveWorker.cpp` | Worker thread implementation |

---

## Version History

- **Initial**: Basic motion commands (MoveToPosition, MoveRelative, etc.)
- **v2**: Added MoveToGraphNode for single-device graph navigation
- **v3**: Added ParallelMove with position type support
- **v4**: Added ParallelMove graphNode with true parallel execution via GraphMoveWorker
