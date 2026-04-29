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
2. **Compute**: `PlacementCorrectionMath::ComputeLineAngle()` for each line, delta angle = target - reference
3. **Correct**: Apply delta to hex device U axis: `correctedU = masterU + deltaAngle` (with optional sign reversal)
4. **Config**: `PlacementCorrectionConfig` maps a master position (golden reference, never modified) to a target position that receives the corrected values
