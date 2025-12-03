# Recipe Page

Recipe editing and process configuration.

## Layout

| Left Panel | Center Panel | Right Panel |
|------------|--------------|-------------|
| Available Processes | Recipe Steps | Process Configuration |
| | | Vision Result |

## Available Processes (Left Panel)

### Search
Type in search box to filter processes by name.

### Categories
- **Motion Control** - Move, position, velocity commands
- **I/O & Pneumatics** - Digital I/O, pneumatic slides
- **TEC & Laser** - Temperature and laser control
- **Power Supply** - Power supply commands
- **SMU Control** - Source measure unit operations
- **Data** - Data capture and storage
- **System** - Delays, prompts, logging
- **Control Flow** - Loops, conditionals
- **Calibration** - Calibration routines
- **Vision** - Camera capture, image processing
- **Dispensing** - Dispense operations
- **Aux Window** - Auxiliary display control
- **Comment** - Recipe documentation

### Adding a Process
1. Expand category
2. Double-click process or drag to Recipe Steps

## Recipe Steps (Center Panel)

### Header Controls

| Control | Function |
|---------|----------|
| **Name** | Recipe display name |
| **Run Recipe** (orange) | Execute entire recipe |
| **Run Selected** (blue) | Execute selected steps only |
| **Step** (green) | Execute one step at a time |
| **Continue** | Resume after step mode |

### Menu Bar

| Menu | Options |
|------|---------|
| **File** | New, Open, Save, Save As |
| **Edit** | Cut, Copy, Paste, Delete, Undo |
| **Select** | All, None, Invert, Enable/Disable |

### Process List

| Column | Description |
|--------|-------------|
| **Checkbox** | Select for batch operations |
| **#** | Step number |
| **>** | Execution indicator |
| **Process Instance** | Step name (editable) |
| **Run** | Enable/disable (green = enabled) |

### Step Operations
- **Click** - Select step for configuration
- **Double-click name** - Rename step
- **Drag** - Reorder steps
- **Right-click** - Context menu

## Process Configuration (Right Panel)

Displays parameters for selected step.

### Property Table

| Column | Description |
|--------|-------------|
| **Property** | Parameter name |
| **Value** | Editable value |

### Value Types
- **Dropdown** - Select from list
- **Text field** - Enter value
- **Checkbox** - Toggle on/off
- **Browse button (...)** - Open selector dialog

### Dependent Dropdowns

Parameters auto-refresh based on parent selection:

| When Changed | Updates |
|--------------|---------|
| Motion Device | Graph Name, Node lists |
| Graph Name | End Node list |
| IO Device | Pin Name list |

## Vision Result (Bottom Right)

Displays captured images from vision processes.

## Process Categories

### Motion Control
- Move to Position
- Move Relative
- Move to Graph Node
- Set Velocity

### Aux Window
- Enable Crosshair
- Enable Click-to-Move
- Set Chart Channel
- Set Chart Target
- Clear Chart Target

### I/O Control
- Set Output
- Wait for Input

### System
- Delay
- Prompt User

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Ctrl+B | Quick add process |
| Ctrl+S | Save recipe |
| Ctrl+Z | Undo |
| Delete | Remove selected step |
| Ctrl+A | Select all steps |

## Recipe File Format

Recipes saved as JSON in `recipes/` folder.
