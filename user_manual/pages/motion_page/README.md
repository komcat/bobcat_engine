# Motion Page

Motion device management, position control, and graph editing.

## Layout

| Left Panel | Center Panel | Right Panel |
|------------|--------------|-------------|
| Selected Position | Tab Content | Zoom / Graph Execution |
| Devices | |

## Header Bar

| Element | Function |
|---------|----------|
| **Back to Main Menu** | Return to home screen |
| **Product / File** | Current product and positions file |
| **Import Positions** (orange) | Load positions from file |
| **Save Positions** (green) | Save positions to file |

## Left Panel

### Selected Position
Displays coordinates for selected position:
- **X, Y, Z** - Linear axes (mm)
- **U, V, W** - Rotational axes (degrees)
- **Waypoints** - Associated waypoint positions

### Devices
List of available motion devices:
- **gantry-main** - Main gantry system
- **hex-left** - Left hexapod
- **hex-right** - Right hexapod

Click device to select and view its positions.

## Tabs

### Motion Control Tab

#### Device Header
Shows selected device name.

#### Sub-tabs
- **Actions** - Position operations
- **Graph** - Graph view for device

#### Action Buttons

| Button | Function |
|--------|----------|
| **Move to Position** | Move to selected position |
| **Move with Offset** | Move with XYZ offset dialog |
| **Edit Position** | Modify position coordinates |
| **Update Position** | Save current location to position |
| **Delete Position** | Remove selected position |
| **+ Create Position** (green) | Create new named position |
| **Auto Fit View** (blue) | Reset graph view |
| **Home** (blue) | Move device to home position |
| **STOP** (red) | Emergency stop motion |

#### Positions List
Scrollable list of named positions:
- Home, Mid Back, safe, SN view, etc.
- Click to select position
- Double-click to move to position

### Coordinate Systems Tab
Configure coordinate transformations between devices.

### Graph Tab

Visual graph editor for path planning.

#### Graph Controls

| Button | Color | Function |
|--------|-------|----------|
| **Graph** dropdown | - | Select graph to edit |
| **New Graph** | Blue | Create new graph |
| **Rename** | Blue | Rename current graph |
| **Delete Graph** | Red | Remove current graph |
| **Create Node** | Teal | Add node at position |
| **Create Edge** | Teal | Connect two nodes |

#### Graph View
- **Green circles** - Position nodes
- **Arrows** - Edges (allowed paths)
- **Drag** - Pan view
- **Scroll** - Zoom in/out

#### Device Legend
Shows color coding for each device's nodes.

#### Graph Execution Panel

| Element | Function |
|---------|----------|
| **Motion Speed** | Slider for movement speed (mm/s) |
| **Current Position** | Shows current node (highlighted) |
| **Goal Node** | Select destination node |
| **Calculate Path** (orange) | Find path to goal |
| **Execute Movement** (teal) | Run calculated path |

## Workflows

### Create New Position
1. Jog device to desired location
2. Click **+ Create Position**
3. Enter position name
4. Click OK

### Update Existing Position
1. Select position from list
2. Jog device to new location
3. Click **Update Position**
4. Confirm update

### Move Using Graph
1. Go to Graph tab
2. Select Goal Node from dropdown
3. Click **Calculate Path**
4. Review path in graph view
5. Click **Execute Movement**

### Create Graph Edge
1. Click **Create Edge**
2. Click first node in graph
3. Click second node
4. Edge appears with arrow

## Safety

- **STOP** button immediately halts all motion
- Always verify path before Execute Movement
- Use lower Motion Speed for testing
