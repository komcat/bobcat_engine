# Placement Correction Trainer Module

Located in `include/AppUI/PlacementCorrectionTrainer/` (9 files). Corrects hexapod placement angle by comparing reference and target line angles measured on the gantry.

> See also `docs/placement_correction.md` for deeper feature docs.

## Architecture

```
PlacementCorrectionPage (main container)
├── TeachPlacementLineWidget  - Teach 2 points each for reference and target lines
├── PlacementResultWidget     - Show angle delta and corrected U value
├── PlacementRetraceWidget    - Re-measure and validate correction
├── ExposureAdjustWidget      - Camera exposure controls during measurement
├── ImageGalleryWidget        - Captured placement images
└── SummaryDiagramWidget      - Visual summary of corrections

PlacementCorrectionStore     - JSON persistence to {productDir}/placement_correction/
PlacementCorrectionData.h    - Structs (PlacementLinePoints, PlacementCorrectionConfig, PlacementCorrectionSet) + math
```

## Data Flow

1. **Teach**: Capture 2 gantry XY points each for a reference line and target line
2. **Compute**: `PlacementCorrectionMath::ComputeLineAngle()` for each line
3. **Correct (U axis)**: At retrace, the master line (reference) and target line (normal-placement) are both re-measured in the same frame. The U correction rotates the target until it is parallel to the master: `ΔU = Am − At` (Am = master/reference angle, At = target/normal-placement angle), then `correctedU = liveHexU + ΔU` (with optional sign reversal via `reverseSign`). Computed in `PlacementRetraceWidget::computeRetraceResults()`. Auto-disabled when `|ΔU| > 3°` (likely bad measurement). The XY correction still uses frame Δθ (retraced ref − taught ref) to rotate the cached design offset — that is independent of the U logic.
4. **Config**: `PlacementCorrectionConfig` maps a master position (golden reference, never modified) to a target position that receives the corrected values
