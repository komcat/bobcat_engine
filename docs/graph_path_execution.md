# Graph-Based Path Execution System

## Overview

The **Graph Path Execution System** enables multi-step motion sequences using visual graph-based path planning. Users create motion graphs with nodes (positions) and edges (allowed transitions), then execute paths by clicking on goal nodes. The system automatically calculates the shortest path and executes all waypoints sequentially using asynchronous background threads.

**Key Features:**
- ✅ Visual graph editor with drag-and-drop nodes and edges
- ✅ Dijkstra's algorithm for shortest path calculation
- ✅ Asynchronous execution using worker threads (UI stays responsive)
- ✅ Automatic position verification and completion detection
- ✅ Device-specific graphs with color-coded visualization
- ✅ Multi-step waypoint execution with proper sequencing

---

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     GraphWidget (UI)                         │
│  - Visual graph editor                                       │
│  - Path calculation and visualization                        │
│  - Execution controls                                        │
└────────────┬────────────────────────────────┬───────────────┘
             │                                 │
             ▼                                 ▼
┌─────────────────────┐          ┌───────────────────────────┐
│ MotionGraphManager  │          │    SequenceWorker         │
│  - Graph storage    │          │  - Async execution thread │
│  - Path planning    │          │  - Non-blocking UI        │
│  - Dijkstra's algo  │          │  - Signal-based feedback  │
└─────────────────────┘          └──────────┬────────────────┘
                                             │
                                             ▼
                                  ┌──────────────────────────┐
                                  │    SequenceEngine        │
                                  │  - Command execution     │
                                  │  - Device routing        │
                                  │  - Stop flag monitoring  │
                                  └──────────┬───────────────┘
                                             │
                                             ▼
                                  ┌──────────────────────────┐
                                  │ MotionCommandHandler     │
                                  │  - MoveToNamedPosition   │
                                  │  - WaitForMotionComplete │
                                  │  - Position verification │
                                  └──────────────────────────┘
```

---

## Key Components

### 1. SequenceWorker (New)

**Purpose:** Execute pre-built sequences asynchronously in a background thread.

**Location:** `include/sequence/SequenceWorker.h`, `include/sequence/SequenceWorker.cpp`

**Design Pattern:** QThread-based worker (similar to RecipeWorker)

#### Interface

```cpp
class SequenceWorker : public QThread
{
  Q_OBJECT

public:
  SequenceWorker(
    SequenceEngine* engine,
    SequenceBuilder* sequence,  // Takes ownership
    QObject* parent = nullptr
  );

  void requestStop();  // Graceful stop

signals:
  void executionStarted();
  void executionCompleted(bool success, QString errorMessage);
  void executionStopped();
  void logMessage(QString message);

protected:
  void run() override;  // Executes in background thread
};
```

#### Usage Pattern

```cpp
// 1. Build sequence
SequenceBuilder* sequence = engine->CreateSequence("MyPath");
sequence->AddAction(device, "MoveToNamedPosition", {{"positionName", "home"}});
sequence->AddAction(device, "WaitForMotionComplete", {});

// 2. Create worker
SequenceWorker* worker = new SequenceWorker(engine, sequence, parent);

// 3. Connect signals
connect(worker, &SequenceWorker::executionCompleted,
        this, &MyClass::onExecutionCompleted);
connect(worker, &SequenceWorker::finished,
        worker, &QObject::deleteLater);

// 4. Start (returns immediately - UI stays responsive!)
worker->start();
```

#### Lifecycle

1. **Construction:** Worker takes ownership of the sequence
2. **Start:** `worker->start()` launches background thread and returns immediately
3. **Execution:** Sequence executes in background thread
4. **Completion:** Emits `executionCompleted(bool, QString)` signal
5. **Cleanup:** Worker auto-deletes via `deleteLater()` after `finished()` signal

#### Thread Safety

- ✅ **Safe:** Sequence execution runs entirely in worker thread
- ✅ **Safe:** Qt signal/slot mechanism handles cross-thread communication
- ⚠️ **Important:** Do not modify the sequence after starting the worker

---

### 2. GraphWidget Path Execution

**Location:** `include/AppUI/MotionPages/GraphWidget.cpp`

#### Execution Flow

```cpp
void GraphWidget::onExecutePath()
{
  // 1. Validate prerequisites
  if (m_sequenceWorker) {
    // Already executing - prevent double execution
    return;
  }

  // 2. Get current physical position
  PositionStruct currentPos = m_motionManager->GetCurrentPosition(device);

  // 3. Build sequence from path waypoints
  SequenceBuilder* sequence = m_sequenceEngine->CreateSequence("GraphPathExecution");

  for (const QString& posName : m_currentPath.positionNames) {
    // Skip positions we're already at (within 0.1mm tolerance)
    if (alreadyAtPosition(posName)) continue;

    // Add move + wait for each waypoint
    sequence->AddAction(device, "MoveToNamedPosition", {{"positionName", posName}});
    sequence->AddAction(device, "WaitForMotionComplete", {});
  }

  // 4. Start async execution
  m_sequenceWorker = new SequenceWorker(m_sequenceEngine, sequence, this);
  connect(m_sequenceWorker, &SequenceWorker::executionCompleted,
          this, &GraphWidget::onSequenceExecutionCompleted);
  m_sequenceWorker->start();  // UI stays responsive!
}
```

#### Completion Handler

```cpp
void GraphWidget::onSequenceExecutionCompleted(bool success, QString errorMessage)
{
  m_sequenceWorker = nullptr;  // Worker auto-deletes

  if (success) {
    // Update UI: "Path execution completed!"
    // Keep m_isExecutingPath = true (timer will detect arrival)
  } else {
    // Show error dialog
    // Clear execution tracking
  }
}
```

---

### 3. Position Verification

**Location:** `include/AppUI/MotionPages/GraphWidget.cpp:1141-1222`

The system uses **physical position verification** instead of relying on commanded positions:

```cpp
void GraphWidget::updateCurrentPosition()
{
  // Called every 500ms by QTimer

  // 1. Get actual motor coordinates
  PositionStruct currentPos = m_motionManager->GetCurrentPosition(device);

  // 2. Find matching named position (0.1mm tolerance)
  const double TOLERANCE = 0.1;
  for (const auto& [name, savedPos] : allPositions) {
    double distance = euclideanDistance(currentPos, savedPos);
    if (distance < TOLERANCE) {
      // Motor is at this position
      updateUI(name);

      // Check if goal reached
      if (m_isExecutingPath && name == m_goalPositionName) {
        m_isExecutingPath = false;
        showCompletionMessage();
      }
      break;
    }
  }
}
```

**Benefits:**
- ✅ Detects when motor actually reaches destination (not just commanded)
- ✅ Handles motion errors and stops gracefully
- ✅ Provides real-time feedback during multi-step execution

---

## Usage Guide

### Creating a Motion Graph

1. **Select Device** - Use device button stack on left panel
2. **Create Graph** - Click "New Graph" button in Graph tab
3. **Add Nodes:**
   - Click "Create Node" button
   - Select position from dropdown (must be saved in product file)
   - Node appears on canvas with device-specific color
4. **Add Edges:**
   - Click "Create Edge" button
   - Click first node (becomes yellow)
   - Click second node (edge created)
   - Edges are bidirectional
5. **Save** - Graphs auto-save to product file

### Executing a Path

#### Method 1: Goal Node Selection (Automatic Path)

1. Select goal node from "Goal Node" dropdown
2. Click "Calculate Path" - shows path info with waypoints and cost
3. Click "Execute Movement" - path executes asynchronously
4. Monitor "Current Position" label for real-time updates

#### Method 2: Direct Node Click (Future Feature)

1. Click any node in the graph
2. System auto-calculates path from current position
3. One-click execution

### Multi-Step Path Execution

**Example:** Moving from `approachlensplace` to `lensgrip` via intermediate waypoints.

**Calculated Path:**
```
approachlensplace → home → approachlensgrip → lensgrip
```

**Sequence Generated:**
```cpp
// Step 1: Move to home
sequence->AddAction("hex-left", "MoveToNamedPosition", {{"positionName", "home"}});
sequence->AddAction("hex-left", "WaitForMotionComplete", {});

// Step 2: Move to approachlensgrip
sequence->AddAction("hex-left", "MoveToNamedPosition", {{"positionName", "approachlensgrip"}});
sequence->AddAction("hex-left", "WaitForMotionComplete", {});

// Step 3: Move to lensgrip
sequence->AddAction("hex-left", "MoveToNamedPosition", {{"positionName", "lensgrip"}});
sequence->AddAction("hex-left", "WaitForMotionComplete", {});
```

**Execution:**
- Each `MoveToNamedPosition` starts motion (non-blocking)
- Each `WaitForMotionComplete` waits for completion before next move
- **No "already in waypoint motion" errors!**

**UI Feedback:**
```
Current Position: approachlensplace (green)
Path: home → approachlensgrip → lensgrip → END (cost: 0.03, max speed: 0)
Movement completed!
Path execution completed!

Current Position: home (green)  ← Updates in real-time
Current Position: approachlensgrip (green)
Current Position: lensgrip (green)  ← Goal reached!
```

---

## Technical Details

### Path Planning Algorithm

**Dijkstra's Shortest Path** (location: `include/graph/MotionGraphManager.cpp:182-355`)

```cpp
GraphPath MotionGraphManager::findPath(
  const QString& deviceName,
  const QString& graphName,
  const QString& startNodeId,
  const QString& goalNodeId) const
{
  // 1. Initialize distances
  QMap<QString, double> distances;  // Best known distance
  QMap<QString, QString> predecessors;  // Previous node in path
  QSet<QString> unvisited;

  distances[startNodeId] = 0.0;

  // 2. Main loop
  while (!unvisited.isEmpty()) {
    QString currentNode = findMinDistanceNode(unvisited, distances);

    if (currentNode == goalNodeId) break;  // Found!

    // 3. Update neighbors
    for (const QString& neighbor : graph->getNeighbors(currentNode)) {
      double edgeWeight = calculateEdgeWeight(edge);
      double altDistance = distances[currentNode] + edgeWeight;

      if (altDistance < distances[neighbor]) {
        distances[neighbor] = altDistance;
        predecessors[neighbor] = currentNode;
      }
    }

    unvisited.remove(currentNode);
  }

  // 4. Reconstruct path
  QList<QString> path = reconstructPath(predecessors, startNodeId, goalNodeId);
  return createGraphPath(path, distances[goalNodeId]);
}
```

**Edge Weights:**
- Based on `maxSpeed` parameter (inverse relationship)
- Lower speed = higher cost (prefer faster routes)
- Unlimited speed (0) = very low cost (0.01)

### Sequence Execution Details

**MotionCommandHandler Implementation** (location: `include/sequence/MotionCommandHandler.cpp`)

#### MoveToNamedPosition (lines 260-286)

```cpp
// Starts motion in NON-BLOCKING mode
bool success = motionManager->MoveToNamedPosition(deviceName, positionName, false);

// Returns immediately - does NOT wait for completion
// Subsequent WaitForMotionComplete handles the waiting
return true;
```

#### WaitForMotionComplete (lines 445-467)

```cpp
bool WaitForMotionComplete(device, context, timeout=30000)
{
  // Unified wait method with safety checks
  return WaitForMotionWithStopCheck(manager, device, context, timeout);
}
```

#### WaitForMotionWithStopCheck (lines 665-762)

```cpp
bool WaitForMotionWithStopCheck(
  manager, device, context, timeoutMs, targetPos*)
{
  auto startTime = std::chrono::steady_clock::now();

  // Initial delay to ensure motion starts
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  while (true) {
    // 1. Check stop flags
    if (context->IsStopRequestedAny()) {
      LogWarning("Motion stopped by user");
      manager->StopMotion(device);
      return false;
    }

    // 2. Check timeout
    auto elapsed = std::chrono::steady_clock::now() - startTime;
    if (elapsed > std::chrono::milliseconds(timeoutMs)) {
      LogError("Motion timeout");
      return false;
    }

    // 3. Check if motion complete
    if (!manager->IsMoving(device)) {
      // 4. Verify position if targetPos provided
      if (targetPos) {
        PositionStruct actual = manager->GetCurrentPosition(device);
        if (!withinTolerance(actual, *targetPos, 0.05)) {
          LogError("Position verification failed");
          return false;
        }
      }
      return true;  // Success!
    }

    // Wait before next check
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}
```

**Safety Features:**
- ✅ Stop flag checking every 50ms
- ✅ 30-second timeout protection
- ✅ Position verification (±0.05mm tolerance)
- ✅ Automatic error logging

---

## Comparison: Old vs New Approach

### ❌ Old Approach (Direct Loop - Failed)

```cpp
// Sent moves too rapidly in a loop
for (const QString& pos : path) {
  motionManager->MoveToNamedPosition(device, pos, false);  // Non-blocking
  // No wait! Next move sent immediately
}
// Result: "already in waypoint motion" error
```

**Problems:**
- Sent all moves rapidly without waiting
- Motion controller rejected second move
- UI froze during execution

### ✅ New Approach (Sequence + Worker - Success!)

```cpp
// Build sequence with proper waits
for (const QString& pos : path) {
  sequence->AddAction(device, "MoveToNamedPosition", {{"positionName", pos}});
  sequence->AddAction(device, "WaitForMotionComplete", {});  // Wait!
}

// Execute in background thread
SequenceWorker* worker = new SequenceWorker(engine, sequence);
worker->start();  // UI stays responsive
```

**Benefits:**
- ✅ Each move waits for completion before next
- ✅ UI stays responsive (background thread)
- ✅ Proper error handling and stop support
- ✅ Position verification after each move

---

## File Locations

### Core Components

| File | Purpose |
|------|---------|
| `include/sequence/SequenceWorker.h` | Worker thread header |
| `include/sequence/SequenceWorker.cpp` | Worker thread implementation |
| `include/AppUI/MotionPages/GraphWidget.h` | Graph widget header |
| `include/AppUI/MotionPages/GraphWidget.cpp` | Graph widget implementation |
| `include/graph/MotionGraphManager.h` | Graph storage and path planning header |
| `include/graph/MotionGraphManager.cpp` | Dijkstra's algorithm implementation |
| `include/graph/MotionGraph.h` | Graph data structures |

### Supporting Files

| File | Purpose |
|------|---------|
| `include/AppUI/MotionPages/MotionNodeItem.h` | Visual node representation |
| `include/AppUI/MotionPages/MotionEdgeItem.h` | Visual edge representation |
| `include/sequence/MotionCommandHandler.cpp` | Motion command execution |
| `include/sequence/SequenceEngine.cpp` | Sequence execution engine |

---

## Configuration

### Product File Format

Graphs are stored in the product JSON file alongside positions:

```json
{
  "product_name": "SIPHOG888",
  "positions": {
    "hex-left": {
      "home": {"x": 4.64001, "y": 1, "z": -2, ...},
      "lensgrip": {"x": -1.72999, "y": -4.57998, "z": -3.73999, ...},
      ...
    }
  },
  "graphs": {
    "hex-left": {
      "default": {
        "name": "default",
        "nodes": [
          {"id": "node_1", "position": "home", "device": "hex-left", "x": 100, "y": 100},
          {"id": "node_2", "position": "lensgrip", "device": "hex-left", "x": 300, "y": 200}
        ],
        "edges": [
          {"id": "edge_1", "fromNodeId": "node_1", "toNodeId": "node_2", "maxSpeed": 0}
        ]
      }
    }
  }
}
```

### Settings

- **Position Tolerance:** 0.1mm (for position matching)
- **Position Update Rate:** 500ms (timer interval)
- **Motion Timeout:** 30 seconds per move
- **Position Verification Tolerance:** ±0.05mm

---

## Troubleshooting

### Issue: "Sequence engine not connected"

**Cause:** GraphWidget doesn't have SequenceEngine reference.

**Solution:**
```cpp
// In MotionPage constructor:
graphWidget->setSequenceEngine(m_sequenceEngine);
```

### Issue: "Path execution already in progress"

**Cause:** Clicked "Execute Movement" while previous execution still running.

**Solution:** Wait for completion or restart application. Check for hung worker threads.

### Issue: Position stuck at "Unknown"

**Cause:** Motor not at any saved position or tolerance too tight.

**Solution:**
1. Check if position is saved in product file
2. Verify motor is actually at the position (not just commanded)
3. Increase tolerance if needed (currently 0.1mm)

### Issue: Multi-step path stops after first waypoint

**Cause:** SequenceEngine error or motion controller issue.

**Solution:**
1. Check console output for error messages
2. Verify all positions in path exist
3. Check motion controller is not in error state
4. Review SequenceEngine logs for stop flags

### Issue: UI freezes during execution

**Cause:** Not using SequenceWorker (executing on UI thread).

**Solution:** Ensure using SequenceWorker pattern, not direct `ExecuteSequence()` call.

---

## Future Enhancements

### Planned Features

1. **Click-to-Execute:** Click any node to automatically execute path from current position
2. **Path Visualization:** Highlight path on graph before execution with animated preview
3. **Speed Control:** Per-edge speed limits with visual indicators
4. **Path Recording:** Record manual movements and convert to graph
5. **Graph Templates:** Pre-built graph templates for common workflows
6. **Multi-Device Coordination:** Synchronized movement of multiple devices
7. **Collision Avoidance:** Automatic path adjustment to avoid collisions

### Possible Improvements

- **Progress Indicator:** Show which waypoint is currently executing
- **Path Cost Visualization:** Color-code edges by cost/speed
- **Undo/Redo:** Graph editing history
- **Graph Validation:** Check for disconnected nodes, unreachable positions
- **Export/Import:** Share graphs between products

---

## Related Documentation

- [Sequence System Guide](sequence_context_usage_guide.md)
- [Recipe System Guide](ADDING_NEW_COMMANDS_GUIDE.md)
- [Motion Control System](../CLAUDE.md#motion-control-system)
- [Product Position Manager](../CLAUDE.md#configuration-system)

---

## Version History

- **v1.0** (2025-01-30) - Initial implementation with SequenceWorker async execution
  - Graph-based path planning
  - Multi-step waypoint execution
  - Real-time position tracking
  - Non-blocking UI with worker threads

---

## Contact

For questions or issues with the graph path execution system:
1. Check console output for detailed error messages
2. Review this documentation
3. Consult `include/sequence/SequenceEngine.cpp` logging
4. Check GitHub issues: https://github.com/anthropics/claude-code/issues
