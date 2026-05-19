# RunProgramPage Layout

`include/AppUI/RunProgramPage/`

The F1 "Process" page is composed from focused helpers — touch the helper for the area you're working in, **not** the orchestrator.

| File | Owns |
|------|------|
| `RunProgramPage.{h,cpp}` | Top-level page, layout assembly, signal wiring. The orchestrator — keep it thin. |
| `RunPageLeftPanel` | Left-side navigation/control panel. |
| `ProgramEditPanel` | Program (recipe sequence) editing UI. |
| `ProgramSettingsStore` | Per-program UI settings persistence (QSettings-backed). |
| `ExecutionController` | Run/pause/stop/step orchestration against `ProgramManager` + `SequenceEngine`. |
| `ExecutionStatusWidget` | Live execution status display. |
| `UserInteractionOverlay` | Modal overlays for prompts, manual alignment, capture confirm, etc. |
| `ScanController` + `ScanControlWidget` | Background scan kickoff/stop (uses SequenceContext stop flag). |
| `SequenceContextViewer` | Live key/value view of `SequenceContext`. |
| `VisionHistoryWidget` | Captured vision results history. |
| `SlotManagerWidget` | DUT slot selection/management. |
| `RollCorrectionController` + `RollCorrectionWidget` | Hex W-axis roll correction. Widget is pure UI; controller owns the worker threads + stop flags (see "Roll Correction" section below). |
| `RetraceEditWidget`, `ManualAdjustWidget`, `ManualAlignPointWidget`, `UserPromptWidget`, `UserInputWidget`, `CaptureConfirmWidget` | User-interaction dialogs/widgets used during recipe execution. |
| `MotionInspectorWidget` | Live motion state inspection. |
| `DependencyGraphScene/Window`, `DependencyEdgeItem`, `RecipeNodeItem`, `LoopConfigDialog` | Recipe dependency graph view + loop config dialog. |

When extending the run page, prefer adding/expanding a helper over enlarging `RunProgramPage.cpp`.

## Roll Correction

Corrects hexapod roll (W axis) using dual-channel peak position measurement. Split into two files, mirroring the `ScanController` + `ScanControlWidget` pattern:

| File | Owns |
|------|------|
| `RollCorrectionWidget.{h,cpp}` | **Pure UI** — register/move/correct buttons, position display, spacing input, channel dropdown. Computes and displays the roll angle. Emits signals; contains no business logic. |
| `RollCorrectionController.{h,cpp}` | **Business logic** — wires the widget's signals, runs the worker threads, owns the `SequenceContext` stop flags and the running state. Constructed by `RunProgramPage` with a `Deps` struct (`context`, `widget`, `appendLog` callback). |

`RunProgramPage` keeps the `RollCorrectionWidget` pointer only because it's the tab body and the page feeds it the live DataStore channel list; all roll-correction behavior lives in the controller.

### Workflow

1. Register CH1/CH2 peak X positions per hex device (hex-left, hex-right)
2. Compute roll angle: `arctan(deltaX / spacing)` (in `RollCorrectionWidget::computeAndDisplayResults`)
3. **Correct W** — `RollCorrectionController::onCorrectWRequested` moves to CH1 peak, shifts X halfway, then applies the W rotation
4. The W rotation is split into **micro-steps of ≤ 0.02°**. Around each micro-step the controller samples the tracking channel's optical power; a > 10% drop vs. the pre-step (rolling) baseline triggers a 0.5 µm XY regain scan (`XCANExtended`) so power never collapses into the noise floor
5. All moves use a safe reduced velocity with automatic restore
6. "Fine Grid Scan" button runs `GridScanDualChannel` on a background thread; "STOP Scan" uses `SequenceContext`-based stop control
