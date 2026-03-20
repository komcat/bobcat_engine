# Accuracy Trainer System Architecture

## Overview

The Accuracy Trainer system provides four independent calibration tools for precision motion correction. Each trainer follows a consistent three-layer architecture and has its own detailed documentation.

## Module Documentation

| Module | Document | Purpose |
|--------|----------|---------|
| **Position Accuracy** | [accuracy_trainer_position_accuracy.md](accuracy_trainer_position_accuracy.md) | Multi-point coordinate frame correction with hex device registration |
| **Camera Offset** | [accuracy_trainer_camera_offset.md](accuracy_trainer_camera_offset.md) | Low/High-mag camera XYZ offset calibration with pneumatic automation |
| **Planar Adjustment** | [accuracy_trainer_planar_adjustment.md](accuracy_trainer_planar_adjustment.md) | Two-point surface tilt measurement and hex rotation axis correction |
| **Z Offset** | [accuracy_trainer_z_offset.md](accuracy_trainer_z_offset.md) | Two-surface Z gap measurement with global-to-local hex correction |

## Common Architecture

All four trainers share this three-layer pattern:

```
┌─────────────────────────────────────────────────────────┐
│  UI Layer (Interactive)                                  │
│  *Page.h/cpp + widget files in include/AppUI/*/         │
├─────────────────────────────────────────────────────────┤
│  Command Layer (Headless)                                │
│  *CommandHandler.h/cpp in include/sequence/             │
│  *Processes.cpp in include/recipe/processes/            │
├─────────────────────────────────────────────────────────┤
│  Data Layer (Persistence + Math)                         │
│  *Data.h (structs + math) + *Store.h/cpp (JSON I/O)    │
└─────────────────────────────────────────────────────────┘
```

## Execution Pipeline

```
Recipe JSON step
    → ProcessPluginRegistry::GetPlugin(processType)
    → IProcessPlugin::BuildSequence(builder, params)
    → SequenceEngine::ExecuteStep(action)
    → CommandDispatcher::ExecuteCommand(handlerType, params, context)
    → *CommandHandler::ExecuteCommand(params, context)
    → Load training set → Compute corrections → Apply via motion manager
    → Store results in SequenceContext
```

## Handler Registration

```cpp
m_dispatcher.RegisterHandler("accuracytrainer",   ...);  // RunAlignment
m_dispatcher.RegisterHandler("cameraoffset",       ...);  // MoveToHighMag/LowMag/Abs
m_dispatcher.RegisterHandler("planaradjustment",   ...);  // RunPlanarAdjustment
m_dispatcher.RegisterHandler("zoffset",            ...);  // RunZOffset
```

## Summary Table

| Trainer | Measures | Corrects | Target Axes | Motion Command |
|---------|----------|----------|-------------|----------------|
| Position Accuracy | XY frame drift + rotation + scale | Bonding positions in world coords | Gantry XY, Hex XYZUVW | `MoveToAbs`, `MoveToAbs6` |
| Camera Offset | XYZ distance between two cameras | Gantry position when switching cameras | Gantry XYZ | `MoveToAbs` (relative offset) |
| Planar Adjust | Surface tilt angle (degrees) | Hex rotation to flatten surface | Hex U, V, or W | `MoveRelative` |
| Z Offset | Z height gap between two surfaces | Hex Z to achieve target gap | Hex XYZUVW (via transform) | `MoveToAbs6` |

## Config File Locations

```
config/<product>/
├── accuracy_trainer/<name>.json     # Position Accuracy training sets
├── PR_Camera_Offset/<name>.json     # Camera Offset calibration sets
├── planar_adjustment/<name>.json    # Planar Adjustment training sets
└── z_offset/<name>.json             # Z Offset training sets
```
