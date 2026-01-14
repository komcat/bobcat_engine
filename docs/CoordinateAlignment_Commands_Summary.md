# Coordinate Alignment Commands Summary

This document describes the coordinate alignment system for 3-point vision-based alignment and related motion commands.

## Overview

The coordinate alignment system enables:
1. Teaching a reference coordinate system from 3 fiducial vision results
2. Re-aligning to a live coordinate system when parts move/rotate
3. Transforming local coordinates to live world coordinates
4. Moving devices directly to local coordinate positions

## File Storage Locations

| Type | Directory |
|------|-----------|
| Taught Systems | `config/coordalign/taught_systems/*.json` |
| Live Systems | `config/coordalign/live_systems/*.json` |
| Local Points | `config/coordalign/local_points/*.json` |

All coordinate systems and points are persisted to disk for use across sessions and recipes.

---

## Coordinate Alignment Commands (Device: `coordalign`)

### TeachCoordinateSystem

Creates a taught coordinate system from 3 fiducial vision results already in context.

| Parameter | Description | Example |
|-----------|-------------|---------|
| `uid` | Unique identifier for the system | `"Bench_coord_system"` |
| `fid1_prefix` | Context prefix for fiducial 1 | `"dx_Fid1"` |
| `fid2_prefix` | Context prefix for fiducial 2 | `"dx_Fid2"` |
| `fid3_prefix` | Context prefix for fiducial 3 | `"dx_Fid3"` |
| `fid1_profile` | Vision profile path for fid1 | `"config/vision/FID1.json"` |
| `fid2_profile` | Vision profile path for fid2 | `"config/vision/FID2.json"` |
| `fid3_profile` | Vision profile path for fid3 | `"config/vision/FID3.json"` |
| `motion_device` | Motion device name | `"gantry-main"` |
| `camera_name` | Camera name | `"main_camera"` |
| `description` | Optional description | `""` |

**Coordinate System Definition:**
- Origin: Fid1 world position
- X-axis: Unit vector from Fid1 to Fid2
- Y-axis: Perpendicular to X-axis (right-hand rule)

---

### AlignLiveCoordinateSystem

Re-executes vision at taught positions and creates a live coordinate system.

| Parameter | Description | Example |
|-----------|-------------|---------|
| `taught_uid` | UID of taught system to align to | `"Bench_coord_system"` |
| `live_uid` | UID for the new live system | `"live_session"` |
| `output_prefix` | Context prefix for results | `"transform"` |

**Context Output:**
- `{prefix}.origin_x_mm`, `{prefix}.origin_y_mm` - Live origin
- `{prefix}.rotation_deg`, `{prefix}.rotation_rad` - Rotation from taught
- `{prefix}.scale` - Scale factor
- `{prefix}.fid3_error_mm` - Fid3 prediction error
- `{prefix}.success` - Boolean success flag

---

### TeachLocalPoint

Teaches a point in local coordinates relative to a taught coordinate system.

| Parameter | Description | Example |
|-----------|-------------|---------|
| `uid` | Unique identifier for the point | `"target_point_SLED"` |
| `coord_system_uid` | Reference coordinate system | `"Bench_coord_system"` |
| `motion_device` | Motion device name | `"gantry-main"` |
| `world_x` | World X (optional, uses device pos if empty) | `""` or `"173.9"` |
| `world_y` | World Y (optional, uses device pos if empty) | `""` or `"80.5"` |
| `description` | Optional description | `"point to sled emitter"` |

**Behavior:**
- If `world_x`/`world_y` are empty: uses current device position
- Converts world position to local coordinates using taught system axes
- Saves to `config/coordalign/local_points/{uid}.json`

---

### TransformLocalPoint

Transforms a stored local point to live world coordinates.

| Parameter | Description | Example |
|-----------|-------------|---------|
| `point_uid` | UID of the local point | `"target_point_SLED"` |
| `live_uid` | UID of live coordinate system | `"live_session"` |
| `output_prefix` | Context prefix for output | `"tgtSled"` |

**Context Output:**
- `{prefix}.x` - Transformed world X coordinate
- `{prefix}.y` - Transformed world Y coordinate

---

### TransformPoint

Transforms arbitrary local coordinates to live world coordinates.

| Parameter | Description | Example |
|-----------|-------------|---------|
| `live_uid` | UID of live coordinate system | `"live_session"` |
| `input_x` | Local X (value or `$context_key`) | `"-1.9288"` or `"$local.x"` |
| `input_y` | Local Y (value or `$context_key`) | `"-2.8277"` or `"$local.y"` |
| `output_prefix` | Context prefix for output | `"target"` |

---

### MoveToLocalXY

Transforms local coordinates and moves device to the resulting world position.

| Parameter | Description | Example |
|-----------|-------------|---------|
| `motion_device` | Device to move | `"gantry-main"` |
| `coord_system_uid` | Coordinate system UID (live or taught) | `"live_session"` |
| `local_x` | Local X (value or `$context_key`) | `"-1.9288"` |
| `local_y` | Local Y (value or `$context_key`) | `"-2.8277"` |
| `output_prefix` | Optional: store world coords | `"moved"` |

**Behavior:**
- Tries live system first, falls back to taught system
- Transforms local to world coordinates
- Moves device X/Y to world position, **keeps current Z**
- Optionally stores `{prefix}.world_x`, `{prefix}.world_y`, `{prefix}.z` in context

---

### GetTransform

Retrieves transformation parameters for a live system.

| Parameter | Description | Example |
|-----------|-------------|---------|
| `live_uid` | UID of live coordinate system | `"live_session"` |
| `output_prefix` | Context prefix for output | `"xform"` |

---

### ListCoordinateSystems

Lists all stored taught and live coordinate systems.

No parameters required.

---

### ListLocalPoints

Lists all taught local points.

No parameters required.

---

### ClearCoordinateSystem

Removes a coordinate system by UID.

| Parameter | Description | Example |
|-----------|-------------|---------|
| `uid` | UID to clear | `"live_session"` |
| `type` | `"taught"`, `"live"`, or `"both"` | `"both"` |

---

### ClearLocalPoint

Removes a local point by UID.

| Parameter | Description | Example |
|-----------|-------------|---------|
| `uid` | UID of local point to remove | `"target_point_SLED"` |

---

## Motion Commands

### MoveToContextXY (Device: motion device name)

Moves device to X/Y coordinates stored in context variables, keeping current Z.

| Parameter | Description | Example |
|-----------|-------------|---------|
| `x_key` | Context key for X coordinate | `"tgtSled.x"` |
| `y_key` | Context key for Y coordinate | `"tgtSled.y"` |
| `blocking` | Optional: `"false"` for non-blocking | `"true"` (default) |

**Usage:** After `TransformLocalPoint` outputs to `tgtSled.x` and `tgtSled.y`:
```
Device: gantry-main
Method: MoveToContextXY
Params:
  x_key: tgtSled.x
  y_key: tgtSled.y
```

---

## Recipe Plugins

### Calibration Category

| Plugin Name | Description |
|-------------|-------------|
| Teach Coordinate System | Create taught system from 3 fiducials |
| Align Live Coordinate System | Re-align and create live system |
| Transform Point | Transform local coords to world |
| Transform Local Point | Transform stored local point to world |
| Teach Local Point | Teach a point in local coordinates |
| Move to Local XY | Transform and move to local position |
| List Coordinate Systems | List all systems |
| List Local Points | List all local points |
| Clear Coordinate System | Remove a system |
| Clear Local Point | Remove a local point |

### Motion Control Category

| Plugin Name | Description |
|-------------|-------------|
| Move to Context XY | Move to X/Y from context variables |

---

## Typical Workflow

### 1. Initial Setup (Master Teaching)

```
1. GotoDetect (Fid1) -> context: dx_Fid1.*
2. GotoDetect (Fid2) -> context: dx_Fid2.*
3. GotoDetect (Fid3) -> context: dx_Fid3.*
4. TeachCoordinateSystem (uid: "Bench_coord_system")
5. Move to target position manually
6. TeachLocalPoint (uid: "target_point_SLED")
```

### 2. Production Run (Per Part)

```
1. AlignLiveCoordinateSystem (taught: "Bench_coord_system", live: "live_session")
2. TransformLocalPoint (point: "target_point_SLED", output: "tgtSled")
3. MoveToContextXY (x_key: "tgtSled.x", y_key: "tgtSled.y")
   -- OR --
   MoveToLocalXY (coord: "live_session", local_x: "-1.9288", local_y: "-2.8277")
```

### 3. Direct Local Coordinate Movement

```
1. AlignLiveCoordinateSystem (once per session)
2. MoveToLocalXY (local_x: "0", local_y: "0")    -> Move to Fid1 position
3. MoveToLocalXY (local_x: "5", local_y: "0")    -> Move 5mm along X-axis
4. MoveToLocalXY (local_x: "0", local_y: "5")    -> Move 5mm along Y-axis
```

---

## Error Handling

All commands return descriptive error messages:
- Missing parameters
- Coordinate system not found (checks file storage)
- Transform not valid
- Motion failures

Live systems are loaded from file if not in memory, enabling cross-session usage.
