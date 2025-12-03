# Hardware Instrument Page

Hardware device configuration and control.

## Layout

| Left Panel | Right Panel |
|------------|-------------|
| Device Categories | Device Configuration |

## Device Categories (Left Panel)

| Category | Description |
|----------|-------------|
| **Camera** | Camera devices and settings |
| **PI Hexapod** | PI hexapod controllers |
| **ACS** | ACS motion controllers |
| **IO** | Digital I/O devices |
| **Pneumatic** | Pneumatic slide control |
| **Data View** | Data channel monitoring |
| **TCP Client** | Network client connections |
| **Laser/TEC** | Laser and thermoelectric coolers |
| **Power Supply** | Power supply devices |
| **SMU** | Source measure units |

---

## Camera Tab

### Header
Shows camera summary:
```
Cameras: 2 Total | 2 Enabled | 2 Connected
```

### Sub-tabs
- **Camera** - Camera settings
- **Lighting** - Lighting control

### Camera Selection
Tabs for each camera (e.g., Overhead Camera, Side Camera)

### Status Bar
Shows connection status:
```
Camera: Overhead Camera - Connected (Pub-Sub)
```

### Performance Info
```
Processing: 5.06 ms/frame | Total: 72 | Displayed: 9
```

### Camera Settings

| Setting | Description |
|---------|-------------|
| **Auto Exposure** | Automatic exposure control |
| **Auto Gain** | Automatic gain control |
| **Exposure** | Manual exposure time (µs) |
| **Gain** | Manual gain value |
| **Save Image** (green) | Capture current frame |

### Digital Zoom

| Control | Function |
|---------|----------|
| **Zoom slider** | Adjust zoom level (100%+) |
| **Zoom dropdown** | Select preset zoom |
| **Reset** (blue) | Return to 100% |

### Live View
Displays real-time camera feed with FPS indicator.

### Connection Controls

| Button | Function |
|--------|----------|
| **Connect** | Connect selected camera |
| **Disconnect** (green) | Disconnect selected camera |
| **Connect Enabled Cameras** (green) | Connect all enabled cameras |
| **Connect All Cameras** (blue) | Connect every camera |
| **Disconnect All Cameras** (blue) | Disconnect all cameras |

---

## PI Hexapod Tab

Configure PI hexapod motion controllers.

### Device List
Shows configured hexapods (hex-left, hex-right)

### Connection Settings
- IP Address
- Port number
- Axis configuration

### Controls
- Connect / Disconnect
- Home axes
- Enable / Disable servo

---

## ACS Tab

Configure ACS gantry motion controllers.

### Device List
Shows ACS controllers (gantry-main)

### Connection Settings
- Communication port
- Axis mapping

### Controls
- Connect / Disconnect
- Home sequence
- Enable / Disable motors

---

## IO Tab

Digital I/O device configuration.

### Device List
Shows EziIO devices

### Pin Configuration
- Input pin names and states
- Output pin names and states

### Controls
- Toggle outputs
- Monitor inputs
- Refresh status

---

## Pneumatic Tab

Pneumatic slide configuration.

### Slide List
Shows configured pneumatic slides

### Settings
- Extend/Retract I/O pins
- Sensor pins
- Timing parameters

### Controls
- Extend / Retract
- Status monitoring

---

## Data View Tab

Real-time data channel monitoring.

### Channel List
Shows all DataStore channels

### Display
- Current value
- Historical chart
- Update rate

---

## TCP Client Tab

Network client connections.

### Client List
Shows configured TCP clients

### Settings
- Server address
- Port number
- Protocol type

### Controls
- Connect / Disconnect
- Send test message
- View received data

---

## Laser/TEC Tab

Laser and thermoelectric cooler control.

### TEC Devices
- Temperature setpoint
- Current temperature
- Enable / Disable

### Laser Devices
- Power control
- Enable / Disable
- Interlock status

---

## Power Supply Tab

Power supply device control.

### Device List
Shows configured power supplies

### Settings
- Voltage setpoint
- Current limit

### Controls
- Output On / Off
- Measure voltage/current

---

## SMU Tab

Source Measure Unit configuration.

### Device List
Shows SMU channels (e.g., Keithley 6482)

### Settings
- Source mode (Voltage/Current)
- Compliance limit
- Measurement range

### Controls
- Output On / Off
- Trigger measurement
- View readings

---

## Common Operations

### Connect All Hardware
1. Go to each device tab
2. Click Connect or use bulk connect buttons
3. Verify status indicators turn green

### Save Configuration
Configuration saved automatically to:
- `config/camera_config.json`
- `config/motor_devices.json`
- `config/io_devices.json`
- etc.
