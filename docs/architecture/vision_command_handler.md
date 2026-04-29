# VisionCommandHandler Layout

`include/sequence/VisionCommandHandler.{h,cpp}` + `include/sequence/vision/`

Same pattern as MotionCommandHandler, but smaller — 4 helpers instead of 9, and the orchestrator is just a 60-line dispatch ladder. The whole handler is `#ifdef HAS_OPENCV`-gated; with no OpenCV, every TU compiles to nothing (CMake also gates the source list with `if(OPENCV_FOUND)`).

| File | Owns |
|------|------|
| `VisionCommandHandler.{h,cpp}` | `ExecuteCommand` dispatch ladder (4 `TryExecute` calls + "Unknown vision command" fallthrough), the `m_context` pointer that helpers reach via `deps.app`. ~60 lines, no per-command logic. |
| `vision/VisionCommandDeps.h` | `struct VisionCommandDeps` — bundles `ApplicationContext* app` (covers all manager pointers) + `VisionCommandHandler* parent` (callback handle, mirrors Motion's `deps.parent`). Built once in `ExecuteCommand` and reused. |
| `vision/VisionContinuousCommands.{h,cpp}` | `start_continuous`, `stop_continuous`, `enable_cv_inject`, `disable_cv_inject`. Touches `GdiCameraWindowManager` + `SequenceEngine`. No OpenCV headers needed in this file. |
| `vision/VisionTestCommands.{h,cpp}` | `load_image_from_file` — test seam that loads an image from disk into `VisionManager` to bypass live camera capture. |
| `vision/VisionAutoFocusCommands.{h,cpp}` | `auto_focus` — Z-axis sweep using Laplacian-variance focus measure. Owns the file-static `CalculateFocusMeasure` helper (anon namespace) + `ExecuteAutoFocus` implementation. Self-contained — does its own capture loop via `LiveVideoSubscriber`, no parent callbacks. |
| `vision/VisionProfileCommands.{h,cpp}` | The big one (1259 LOC): `load_profile`, `execute_profile`, `execute`, `capture_and_execute`, `get_result`, `show_context_image`. Owns the implementation methods (`LoadProfile`, `Execute`, `CaptureAndExecute`, `GetResult`, `StoreResultInContext`, `ShowContextImage`, `SaveVisionResultWithHistory`), the file-static history-rotation state (`s_historyIndex` + `MAX_HISTORY` + `HISTORY_DIR`), and `ApplyNoMatchGuardToContext` in an anon namespace. |

Helper contract is identical to Motion's: `static std::optional<bool> TryExecute(...)` returning `std::nullopt` (not mine), `true` (success), or `false` (handled failure with `errorMessage`). The orchestrator stays flat — no `else if` chain.

**Adding a new vision command:** put it inside the helper that owns the matching family. If it touches profile execution, results, or images, that's `VisionProfileCommands`. If it starts a brand-new family, mirror Motion's playbook: new helper under `vision/`, add to CMakeLists `if(OPENCV_FOUND)` block, include in orchestrator, one `TryExecute` call in the dispatch ladder.
