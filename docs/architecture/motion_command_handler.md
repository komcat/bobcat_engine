# MotionCommandHandler Layout

`include/sequence/MotionCommandHandler.{h,cpp}` + `include/sequence/motion/`

The motion command handler is split into a thin orchestrator plus 9 per-family helpers. Touch the helper for the command family you're working in — **not** the orchestrator.

| File | Owns |
|------|------|
| `MotionCommandHandler.{h,cpp}` | Public `ExecuteCommand` API, the dispatch ladder (9 `TryExecute` calls), shared cross-family helpers (`ResolveNumericValue`, `WaitForMotionWithStopCheck`, `WaitForMultipleRelativeMotionsWithStopCheck`, `InvokeRetraceEditCallback`), the nested `TargetPosition` type, retrace-edit-mode state. The orchestrator — keep it thin (~650 lines). |
| `motion/MotionCommandDeps.h` | `struct MotionCommandDeps` — bundles the manager pointers + `parent` callback handle that every helper receives. Built once at the top of `ExecuteCommand` and reused. |
| `motion/MotionBasicCommands.{h,cpp}` | Family A: `MoveToPosition`, `MoveToAbs`, `MoveRelative`, `MoveToNamedPosition`, `StopAxis`, `EmergencyStop`, `HomeAxis`, `EnableServo`, `WaitForMotionComplete`. Calls `deps.parent->WaitForMotionWithStopCheck` / `ResolveNumericValue` / `InvokeRetraceEditCallback`. |
| `motion/MotionVelocityCommands.{h,cpp}` | Family B: `SetVelocity`, `SetAllAxisVelocity`, `SetSystemVelocity`, `GetSystemVelocity`. Self-contained. |
| `motion/MotionScanCommands.{h,cpp}` | Family C: `XCAN`, `XCANExtended`, `AxisScan` — 1D adaptive scans backed by `AdaptiveScanController` + `DataStore`. |
| `motion/MotionGridScanCommands.{h,cpp}` | Family D: `GridScan`, `GridScanExtended`, `GridScanDualChannel` — 2D grid scans. Owns the `FormatSI` / `MakeTimestamp` / `FormatTime` / `WriteGridScanFiles` file-static helpers (JSON + summary text written under `logs/scanning/grid/`). |
| `motion/MotionGraphContextCommands.{h,cpp}` | Family E: `MoveToGraphNode`, `MoveToContextPosition`, `SavePositionToContext`, `MoveToAbsFromContext`, `MoveToContextXY`. Touches `MotionGraphManager`, `ProductPositionManager`, `VisionManager`. |
| `motion/MotionParallelCommands.{h,cpp}` | Family F: `ParallelMove`, `ParallelMoveRelative`. Owns `ParallelMoveSpec` + `ParallelRelativeMoveSpec` data types. Uses `GraphMoveWorker` for true parallel graph-node execution. |
| `motion/MotionPositionFileCommands.{h,cpp}` | Family G: `UpdatePositionAxis`, `UpdatePositionAll` — edit named positions in the product file. |
| `motion/MotionLinkedDeviceCommands.{h,cpp}` | Family H: `RegisterDeviceLink`, `CompensateDeviceLink` — primary↔linked device offset registration and compensation moves. Touches `DeviceLinkManager` + `CoordinateTransformer`. |
| `motion/MotionFastAlignCommands.{h,cpp}` | Family I: `FastAlignXY`, `FastAlignZXY`. PI-only commands that bypass `UnifiedMotionManager` and talk directly to `PIController` (FSM/GetFastScanStatus/GetAnalogVoltage primitives). |

**Helper contract:** every helper exposes a single `static std::optional<bool> TryExecute(deviceName, methodName, params, errorMessage, context, deps)`. Returning `std::nullopt` means "method isn't mine" (orchestrator falls through to the next helper); returning `true`/`false` means "handled — that was the success result." This lets the orchestrator's `ExecuteCommand` body stay flat (`if (auto r = HelperX::TryExecute(...)) return *r;` × 9, then "Unknown method" fallthrough) without nesting or chained `else if`.

**Adding a new motion command:** add an `if (methodName == "YourCommand")` branch inside the helper that owns the relevant family — do not add it to the orchestrator. If the command starts a new family, add a new helper file under `motion/`, register it in CMakeLists.txt next to its siblings, add `#include "motion/YourHelper.h"` to the orchestrator and one `if (auto r = YourHelper::TryExecute(...))` call to the dispatch ladder.

**Interlock note:** `InterlockManager::CheckMove()` is enforced inside the 5 move methods of `UnifiedMotionManager` itself, not in the command handler. Anything that goes through `UnifiedMotionManager::Move*()` is automatically protected — split helper command files do not need their own interlock checks.
