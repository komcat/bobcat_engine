# Auxiliary Display Window

Secondary display for camera view and live data monitoring.

## Layout

| Left Panel | Right Panel |
|------------|-------------|
| Camera View with Click-to-Move | Live Chart View |

## Camera View

### Controls

- **Camera** dropdown: Select camera source
- **Click to Move** checkbox: Enable click-to-move feature (main_camera only)
- **Crosshair Overlay** checkbox: Show center crosshair
- **Zoom** buttons: 1.0x, 1.5x, 2.0x magnification

### Click-to-Move

When enabled on main_camera:
1. Click anywhere on camera image
2. Motion system moves to clicked position
3. Uses configured transformation matrix for coordinate conversion

## Live Chart View

See [Live Chart View](live_chart_view.md) for detailed documentation.

## Recipe Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| EnableCrosshair | enabled (true/false) | Toggle crosshair overlay |
| EnableClickToMove | enabled (true/false) | Toggle click-to-move |
| SetChartChannel | channel | Set chart data channel |
| SetTarget | value, channel (opt) | Set target for comparison |
| ClearTarget | channel (opt) | Clear target value |
