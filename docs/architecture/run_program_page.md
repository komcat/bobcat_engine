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
| `RollCorrectionWidget` | Hex W-axis roll correction (see "Roll Correction Widget" section below). |
| `RetraceEditWidget`, `ManualAdjustWidget`, `ManualAlignPointWidget`, `UserPromptWidget`, `UserInputWidget`, `CaptureConfirmWidget` | User-interaction dialogs/widgets used during recipe execution. |
| `MotionInspectorWidget` | Live motion state inspection. |
| `DependencyGraphScene/Window`, `DependencyEdgeItem`, `RecipeNodeItem`, `LoopConfigDialog` | Recipe dependency graph view + loop config dialog. |

When extending the run page, prefer adding/expanding a helper over enlarging `RunProgramPage.cpp`.

## Roll Correction Widget

Located in `include/AppUI/RunProgramPage/RollCorrectionWidget.h/.cpp`. Corrects hexapod roll (W axis) using dual-channel peak position measurement.

### Workflow

1. Register CH1/CH2 peak X positions per hex device (hex-left, hex-right)
2. Compute roll angle: `arctan(deltaX / spacing)`
3. Apply 3-step correction: move to CH1 peak, shift X halfway, rotate W per direction table
4. All moves use 1mm/s safe velocity with automatic restore
5. "Fine Grid Scan" button runs `GridScanDualChannel` on background thread; "STOP Scan" uses SequenceContext-based stop control
