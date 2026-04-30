# CoordinateAlignCommandHandler Layout

`include/sequence/CoordinateAlignCommandHandler.{h,cpp}` + `include/sequence/coordalign/`

Same orchestrator + per-family-helpers pattern as Motion / Vision / CustomServer / Calibration, with one difference: this handler carries persistent shared state (3 in-memory maps + a mutex + a config path + 9 disk-persistence methods). The orchestrator stays larger than the others (~770 lines vs Calibration's 106) because that state is genuinely owned here — branches/dispatch sit alongside high-level state ops that wrap the lock + map + disk write so helpers don't poke maps directly.

| File | Owns |
|------|------|
| `CoordinateAlignCommandHandler.{h,cpp}` | Dispatch ladder (4 `TryExecute` calls + "Unknown method" fallthrough), `m_appContext`, the in-memory maps (`m_taughtSystems` / `m_liveSystems` / `m_localPoints`) + `m_mutex` + `m_configPath`, the public state ops (Store / Load / Get / Remove / List for each data type), the cross-family utilities (`GetCurrentTimestamp`, `ResolveNumericValue`, `ReadFiducialFromContext`), the `SetContext` / `SetConfigPath` / `LoadAllFromDisk` lifecycle entry points (called by `SequenceEngine.cpp` after handler construction), and the 9 private `*ToDisk` / `*FromDisk` / `Get*Dir` persistence methods. No per-command logic. |
| `coordalign/CoordAlignTypes.h` | Data types relocated out of the orchestrator header so helpers + math can include just the types: `FiducialPoint`, `TaughtLocalPoint`, `TaughtCoordinateSystem`, `LiveCoordinateSystem`. |
| `coordalign/CoordAlignMath.h` | Inline `namespace coord_align` with the shared 2D math: `CalculateDistance`, `ArePointsCollinear`, `ComputeCoordinateAxes`, `WorldToLocal`, `LocalToWorld`, `TransformPointInternal`, `ComputeBestFitTransform`. Header-only because every function is short and pure; used by all four helper families. |
| `coordalign/CoordAlignCommandDeps.h` | `struct CoordAlignCommandDeps` — `ApplicationContext* app` (covers manager pointers via `app->motionManager` / `app->logManager`) + `CoordinateAlignCommandHandler* parent` (callback handle for the cross-family utilities and state ops). Built once in `ExecuteCommand` and reused. |
| `coordalign/CoordAlignTeachCommands.{h,cpp}` | Family A: `TeachCoordinateSystem`, `ClearCoordinateSystem`, `ListCoordinateSystems`. Owns the TaughtCoordinateSystem lifecycle for `config/<base>/coord_align/taught_systems/<uid>.json`. `ClearCoordinateSystem` is here (not a separate "lifecycle" family) because it's the disposal counterpart of teach; it also wipes the derived live system via the orchestrator's combined `ClearCoordinateSystemEntries(uid, type)` op. |
| `coordalign/CoordAlignAlignCommands.{h,cpp}` | Family B: `AlignLiveCoordinateSystem`, `GetTransform`. Largest helper (~448 LOC). Owns `ExecuteVisionAtPosition` (~180 LOC, anon-namespace, only used here) — the move + vision-execute pipeline that re-runs the taught vision profile at each of the 3 fiducial device positions. Local `M_PI` define for the rotation-degree calculation. `HAS_OPENCV`-gated VisionCommandHandler call inside `ExecuteVisionAtPosition`. |
| `coordalign/CoordAlignLocalPointCommands.{h,cpp}` | Family C: `TeachLocalPoint`, `TransformLocalPoint`, `ListLocalPoints`, `ClearLocalPoint`. Owns the TaughtLocalPoint lifecycle for `config/<base>/coord_align/local_points/<uid>.json`. `TeachLocalPoint` reads the current device position from `motionManager`, optionally accepts explicit `world_x`/`world_y` overrides (resolved through `deps.parent->ResolveNumericValue`), then converts world→local via `coord_align::WorldToLocal` against the taught frame. |
| `coordalign/CoordAlignTransformCommands.{h,cpp}` | Family D: `TransformPoint`, `MoveToLocalXY`. Verb-on-existing-data — owns no map. `TransformPoint` reads a live system (memory-then-disk via `GetLiveSystem`) and applies `coord_align::LocalToWorld`. `MoveToLocalXY` resolves the frame disk-only (live first via `LoadLiveSystem`, then taught via `LoadTaughtSystem` as fallback), computes world XY, and dispatches a blocking `MoveToAbs` to the motion manager keeping current Z. |

Helper contract is identical to Motion/Vision/CustomServer/Calibration: `static std::optional<bool> TryExecute(deviceName, methodName, params, errorMessage, context, deps)` returning `std::nullopt` ("not mine"), `true` (success), or `false` (handled failure with `errorMessage`).

## Public state ops on the orchestrator

These wrap the lock + in-memory map + persistence so helpers don't manage `m_mutex` or call `*ToDisk` directly. Helpers reach them via `deps.parent->...`.

```cpp
// Family A
void StoreTaughtSystem(const TaughtCoordinateSystem& sys);
ClearCoordinateSystemResult ClearCoordinateSystemEntries(uid, type);
CoordinateSystemListing ListCoordinateSystemsSnapshot() const;
bool LoadTaughtSystem(uid, out);

// Family B
void StoreLiveSystem(const LiveCoordinateSystem& live);
bool GetLiveSystem(uid, out);     // memory-first, disk fallback
bool LoadLiveSystem(uid, out);    // disk only

// Family C
void StoreLocalPoint(const TaughtLocalPoint& point);
bool LoadLocalPoint(uid, out);
bool RemoveLocalPoint(uid);
std::vector<TaughtLocalPoint> ListLocalPointsSnapshot() const;
```

Cross-family utilities (also `public:`): `GetCurrentTimestamp()`, `ResolveNumericValue(...)`, `ReadFiducialFromContext(...)`.

`m_logger` is reached as `deps.app->logManager` (the ApplicationContext field, same instance the dispatcher injects into `ICommandHandler::m_logger`).

## Adding a new coordinate-alignment command

If it touches taught systems → `CoordAlignTeachCommands`. Live systems → `CoordAlignAlignCommands`. Local points → `CoordAlignLocalPointCommands`. Pure transformation/motion verbs → `CoordAlignTransformCommands`. If it needs new state access, add a new public state op on the orchestrator (lock + map + persistence) and call it through `deps.parent->`.

If it starts a brand-new family, mirror the playbook: new helper under `coordalign/`, register `.cpp` in CMakeLists `SOURCES` and `.h` in the headers list, include in the orchestrator, add one `TryExecute` call to the dispatch ladder.

## Future cleanup

The orchestrator still owns ~600 lines of state + persistence. A natural next step is to extract `CoordAlignStore` (a class owning the 3 maps + mutex + persistence + config path), which would shrink the orchestrator to ~150 lines matching the other handlers. Deferred from this refactor because pulling state out before any family migrated would have been a much bigger atomic change; doing it after is mechanical (the public state ops on the orchestrator move verbatim onto the new store class, and `deps.store` joins `CoordAlignCommandDeps`).
