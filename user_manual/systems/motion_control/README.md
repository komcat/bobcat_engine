# Motion Control

Motion device operation and commands.

## Supported Controllers

| Type | Description |
|------|-------------|
| ACS | Gantry systems (XYZ, multi-axis) |
| PI | Hexapod controllers (6-axis) |

## Motion Commands

### Move to Position
Move to a named position.
- **device**: Motion controller name
- **position**: Named position (e.g., "home", "pickup")

### Move Relative
Move by specified offset from current position.
- **device**: Motion controller
- **axis**: X, Y, Z, U, V, W
- **distance**: Offset in mm (positive or negative)

### Move Relative Multi-Axis
Move multiple axes simultaneously.
- **device**: Motion controller
- **offsets**: X, Y, Z offsets in mm

### Move Relative Theta
Rotate UVW axes by angle.
- **device**: Motion controller (requires UVW axes)
- **angle**: Rotation in degrees

### Set Velocity
Configure motion speed.
- **device**: Motion controller
- **velocity**: Speed in mm/s
- **acceleration**: Accel in mm/s²

## Position Notation

### Named Positions
Saved in `motor_devices.json`:
```
home, safe, pickup, place
```

### Offset Positions
Add runtime offsets to named positions:
```
lensgrip+Y1      (lensgrip + 1mm Y)
safe+X-2+Z5      (safe - 2mm X + 5mm Z)
```

## Safety Guidelines

1. Start with low velocity for new positions
2. Test moves manually before automation
3. Use graphs for complex paths
4. Verify soft limits are configured
