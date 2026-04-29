# CustomServerCommandHandler Layout

`include/sequence/CustomServerCommandHandler.{h,cpp}` + `include/sequence/customserver/`

Same pattern as Motion/Vision — orchestrator stays thin (~70 lines), per-family helpers under `customserver/`. The orchestrator preserves a two-tier dispatch: **native** handlers (no TCP server needed) run first, then the `m_context->beamServerClient` null check, then the **TCP-based** families.

| File | Owns |
|------|------|
| `CustomServerCommandHandler.{h,cpp}` | Two-tier dispatch ladder + `m_context` pointer + the `beamServerClient` null guard. ~70 lines. No per-command logic. |
| `customserver/CustomServerCommandDeps.h` | `struct CustomServerCommandDeps` — `ApplicationContext* app` + `CustomServerCommandHandler* parent`. Mirrors Vision's compressed shape (single context pointer covers all manager access). |
| `customserver/CustomServerAnalysisCommands.{h,cpp}` | "Native" family (no TCP needed): `FitSlices` (Gaussian M² beam fit from context data), `D4SigmaZScanFolder` (process a folder of Z-scan images through the vision pipeline). Owns the file-static `d4sigma_crop` helper (HAS_OPENCV-gated). Uses `LevenbergMarquardt` + optional opencv. |
| `customserver/CustomServerConnectionCommands.{h,cpp}` | `Connect`, `Disconnect` — TCP connection lifecycle to the beam server. |
| `customserver/CustomServerFitCommands.{h,cpp}` | `Fit`, `Save`, `Reset` — TCP-based fit operations + persistence. |
| `customserver/CustomServerDetectionCommands.{h,cpp}` | `Detect`, `DetectN`, `DetectFile`, `DetectFolder` — beam detection (live + file + folder). Largest helper (~970 LOC). Touches camera + filesystem + regex. |

Helper contract identical to Motion/Vision: `static std::optional<bool> TryExecute(...)`. Each helper bundles its own private static `Handle*` methods + a small anon-namespace `safeStod` / `safeStoi` (intentionally duplicated rather than shared via a header — keeps each helper self-contained, total cost ~15 LOC × 3 = trivial).

`m_logger` is reached as `deps.app->logManager` (the ApplicationContext field, same instance the dispatcher injects into `ICommandHandler::m_logger`).

**Adding a new beam-server command:** if it doesn't need TCP, add it to `Analysis`. If it manages connection state, `Connection`. If it operates on the fit state, `Fit`. Otherwise `Detection`. New family → new helper under `customserver/`, mirror the existing wiring.
