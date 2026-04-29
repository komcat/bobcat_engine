# Accuracy Trainer Module

Located in `include/AppUI/AccuracyTrainer/` (10 files). Teaches and corrects bonding position accuracy using a local coordinate frame. Activated via F7 shortcut in MainWindow.

> See also `docs/accuracy_trainer_architecture.md`, `docs/accuracy_trainer_camera_offset.md`, `docs/accuracy_trainer_planar_adjustment.md`, `docs/accuracy_trainer_position_accuracy.md`, `docs/accuracy_trainer_z_offset.md` for deeper feature docs.

## Architecture

```
AccuracyTrainerPage (main container, 30% left nav + 70% right content)
├── TeachBondingWidget      - Teach 2-3 corner positions → compute bonding center
├── TeachReferenceWidget    - Teach 2-3 reference landmarks → compute local coordinate frame
├── TeachMultiDeviceWidget  - Teach hex device 6-axis + gantry viewing positions + corners + Z offsets
├── EditZOffsetWidget       - Edit Z offset parameters (component/bondline thickness)
├── RunAlignmentWidget      - Single-device: capture refs → rigid transform → corrected bonding moves
└── MultiDeviceRunAlignmentWidget - Multi-device: refs + hex registration + coordinate transforms

AccuracyTrainerStore       - JSON persistence (v2 format) to {productDir}/accuracy_trainer/
AccuracyTrainerData.h      - Data structs (ReferencePoint, BondingPoint, HexDeviceTeachData, TrainingSet) + math
AccuracyTrainerLogger      - SQLite3 logging of corner confirmations with taught vs confirmed deltas
```

## Data Flow

1. **Teach Phase**: User teaches reference points (Ref1/Ref2/Ref3) to define a local coordinate frame. Origin = Ref1, X-axis = Ref1→Ref2, Y-axis = perpendicular (CCW 90deg). Bonding points are taught by 2-3 corners, center computed automatically. All stored as `TrainingSet`.
2. **Run Phase**: Move to taught reference positions, user adjusts to actual positions (or vision auto-detects). Compute rigid 2D transform (rotation + translation) from taught→actual. Apply transform to all bonding points, move to corrected positions.
3. **Multi-Device Run**: Additionally registers hex devices by 3-corner capture in gantry frame, uses `TransformationMatrixLoader` to transform bonding positions between gantry and hex coordinate systems.

## Headless Execution

`AccuracyTrainerCommandHandler` (device type `"accuracytrainer"`) runs alignment without Qt widgets. Parameters: `training_set_name`, `register_hex_left`, `register_hex_right`, `move_to_bonding`. Results stored in SequenceContext as `accuracy_trainer.result.*` (success, corrected positions, deltas).

## Key Design Decisions

- **Re-entrancy guard**: `TeachMultiDeviceWidget` is NOT refreshed in `updateCurrentTrainingSet()` to prevent data loss during teaching
- **Vision integration** (conditional `HAS_OPENCV`): `VisionCommandHandler::execute_profile` + `calculate_world_position` for auto-detect in reference and hex corner teaching
- **File format v2**: Includes UUID, Z parameters, hex corner data. v1 files auto-upgraded on load with sensible defaults
- **SQLite logging**: Corner confirmations logged to `logs/accuracy_trainer/accuracy_trainer_log.db` with unique point UIDs (`ref_{name}`, `hex_{device}_{corner}`)
