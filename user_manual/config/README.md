# Configuration Files

All hardware and system configuration files are stored in the `config/` folder as JSON files.

## Local Override System

Most config files support a `.local.json` override pattern for machine-specific settings.

### How It Works

On application startup, the system loads configuration in this order:

1. **First**, look for `*.local.json` file (e.g., `camera_config.local.json`)
2. **If not found**, fall back to base `*.json` file (e.g., `camera_config.json`)

### File Priority

| Priority | File Pattern | Purpose |
|----------|--------------|---------|
| 1 (High) | `camera_config.local.json` | Machine-specific settings (not in git) |
| 2 (Low) | `camera_config.json` | Default/shared settings (in git) |

### Supported Local Overrides

| Base File | Local Override |
|-----------|----------------|
| `camera_config.json` | `camera_config.local.json` |
| `motor_devices.json` | `motor_devices.local.json` |
| `io_devices.json` | `io_devices.local.json` |
| `pneumatic_slides.json` | `pneumatic_slides.local.json` |
| `tcp_clients.json` | `tcp_clients.local.json` |
| `tec_devices.json` | `tec_devices.local.json` |
| `power_supply_config.json` | `power_supply_config.local.json` |
| `quick_outputs.json` | `quick_outputs.local.json` |

### Usage Guidelines

- **Base files** (`*.json`): Contain default or shared configuration. Checked into version control.
- **Local files** (`*.local.json`): Machine-specific overrides. Ignored by git (`.gitignore`).
- Local file **completely replaces** the base file (not merged).
- Copy base file to create local: `copy camera_config.json camera_config.local.json`
- Example files provided: `*.example.json` (reference for creating local files)

---

## camera_config.json

Camera device configuration.

| Key | Type | Description |
|-----|------|-------------|
| `cameras` | array | List of camera devices |

### Camera Object

| Key | Type | Description |
|-----|------|-------------|
| `id` | string | Unique camera identifier |
| `enabled` | boolean | Enable/disable camera |
| `camera_type` | string | Camera SDK type: `PYLON`, `IDS` |
| `connection_type` | string | Connection method: `ip`, `usb`, `serial` |
| `ip_address` | string | Camera IP address (for IP connection) |
| `display_name` | string | Friendly name shown in UI |
| `auto_connect` | boolean | Connect on application startup |

**Example:**
```json
{
  "cameras": [
    {
      "id": "main_camera",
      "enabled": true,
      "camera_type": "PYLON",
      "connection_type": "ip",
      "ip_address": "192.168.0.68",
      "display_name": "Basler Camera",
      "auto_connect": true
    }
  ]
}
```

---

## motor_devices.json

Motion controller and named position configuration.

### Top Level

| Key | Type | Description |
|-----|------|-------------|
| `motion_devices` | array | List of motion controllers |
| `named_positions` | object | Saved positions per device |

### Motion Device Object

| Key | Type | Description |
|-----|------|-------------|
| `id` | string | Unique device identifier |
| `name` | string | Display name |
| `device_id` | number | Controller device ID |
| `ip_address` | string | Controller IP address |
| `port` | number | TCP port |
| `enabled` | boolean | Enable/disable device |
| `controller_type` | string | Controller type: `ACS`, `PI` |
| `axes` | string | Space-separated axis names (e.g., `X Y Z U V W`) |

### Named Positions

Positions stored per device with coordinates:

| Key | Type | Description |
|-----|------|-------------|
| `x`, `y`, `z` | number | Linear position (mm) |
| `u`, `v`, `w` | number | Rotational position (degrees) |

**Example:**
```json
{
  "motion_devices": [
    {
      "id": "gantry-main",
      "name": "gantry-main",
      "ip_address": "192.168.0.50",
      "port": 701,
      "enabled": true,
      "controller_type": "ACS",
      "axes": "X Y Z"
    }
  ],
  "named_positions": {
    "gantry-main": {
      "home": { "x": 3.0, "y": 3.0, "z": 12.0, "u": 0, "v": 0, "w": 0 },
      "safe": { "x": 143.19, "y": 75.7, "z": 8.24, "u": 0, "v": 0, "w": 0 }
    }
  }
}
```

---

## io_devices.json

Digital I/O device configuration (EziIO controllers).

| Key | Type | Description |
|-----|------|-------------|
| `eziio` | array | List of EziIO devices |

### EziIO Device Object

| Key | Type | Description |
|-----|------|-------------|
| `deviceId` | number | Device ID |
| `name` | string | Device name for reference |
| `ip` | string | Device IP address |
| `inputCount` | number | Number of input pins |
| `outputCount` | number | Number of output pins |
| `enabled` | boolean | Enable/disable device |
| `ioConfig` | object | Pin naming configuration |

### IO Config

| Key | Type | Description |
|-----|------|-------------|
| `inputs` | array | Input pin definitions |
| `outputs` | array | Output pin definitions |

### Pin Definition

| Key | Type | Description |
|-----|------|-------------|
| `name` | string | Pin display name |
| `pin` | number | Physical pin number (0-indexed) |

**Example:**
```json
{
  "eziio": [
    {
      "deviceId": 0,
      "name": "IOBottom",
      "ip": "192.168.0.3",
      "inputCount": 16,
      "outputCount": 16,
      "enabled": true,
      "ioConfig": {
        "inputs": [],
        "outputs": [
          { "name": "L_Gripper", "pin": 0 },
          { "name": "R_Gripper", "pin": 2 }
        ]
      }
    }
  ]
}
```

---

## pneumatic_slides.json

Pneumatic slide configuration linking I/O pins to slide control.

| Key | Type | Description |
|-----|------|-------------|
| `pneumaticSlides` | array | List of pneumatic slides |

### Pneumatic Slide Object

| Key | Type | Description |
|-----|------|-------------|
| `name` | string | Slide name |
| `output` | object | Output pin reference |
| `extendedInput` | object | Extended position sensor |
| `retractedInput` | object | Retracted position sensor |
| `timeoutMs` | number | Movement timeout (milliseconds) |

### Pin Reference

| Key | Type | Description |
|-----|------|-------------|
| `deviceName` | string | IO device name |
| `pinName` | string | Pin name from ioConfig |

**Example:**
```json
{
  "pneumaticSlides": [
    {
      "name": "UV_Head",
      "output": { "deviceName": "IOBottom", "pinName": "UV_Head" },
      "extendedInput": { "deviceName": "IOTop", "pinName": "UV_Head_Down" },
      "retractedInput": { "deviceName": "IOTop", "pinName": "UV_Head_Up" },
      "timeoutMs": 5000
    }
  ]
}
```

---

## tcp_clients.json

TCP client connections for external data sources.

| Key | Type | Description |
|-----|------|-------------|
| `tcp_clients` | array | List of TCP clients |

### TCP Client Object

| Key | Type | Description |
|-----|------|-------------|
| `id` | string | Unique client identifier |
| `name` | string | Display name |
| `host` | string | Server IP address |
| `port` | number | Server port |
| `protocol_type` | string | Protocol: `single` or `multi` |
| `data_channel` | string | DataStore channel (single mode) |
| `data_channels` | array | DataStore channels (multi mode) |
| `auto_connect` | boolean | Connect on startup |
| `auto_reconnect` | boolean | Auto-reconnect on disconnect |
| `description` | string | Client description |

**Example:**
```json
{
  "tcp_clients": [
    {
      "id": "siphog_01",
      "name": "SiPhOG Multi-Channel Monitor",
      "host": "127.0.0.63",
      "port": 65432,
      "protocol_type": "multi",
      "data_channels": [
        "SLED_Current_mA",
        "Photo_Current_uA",
        "SLED_Temp_C"
      ],
      "auto_connect": false
    }
  ]
}
```

---

## tec_devices.json

TEC (Thermoelectric Cooler) and laser controller configuration.

| Key | Type | Description |
|-----|------|-------------|
| `tec_devices` | array | List of TEC/Laser devices |

### TEC Device Object

| Key | Type | Description |
|-----|------|-------------|
| `name` | string | Device name |
| `controller_type` | string | Controller type (see below) |
| `visa_resource` | string | VISA resource string (for direct) |
| `ip_address` | string | IP address (for TCP) |
| `port` | number | TCP port (for TCP) |
| `enabled` | boolean | Enable/disable device |
| `default_temperature` | number | Default temperature setpoint (°C) |
| `default_laser_current` | number | Default laser current (mA) |

### Controller Types

| Type | Description |
|------|-------------|
| `CLD101x_Direct` | Thorlabs CLD101x via VISA (laser + TEC) |
| `CLD101x` | Thorlabs CLD101x via TCP adapter |
| `TED4015_Direct` | Thorlabs TED4015 via VISA (TEC only) |

**Example:**
```json
{
  "tec_devices": [
    {
      "name": "laser-main",
      "controller_type": "CLD101x_Direct",
      "visa_resource": "USB0::0x1313::0x804F::M00930341::INSTR",
      "enabled": true,
      "default_temperature": 25.0,
      "default_laser_current": 50.0
    }
  ]
}
```

---

## smu_devices.json

Source Measure Unit configuration.

| Key | Type | Description |
|-----|------|-------------|
| `devices` | array | List of SMU devices |

### SMU Device Object

| Key | Type | Description |
|-----|------|-------------|
| `name` | string | Device name |
| `type` | string | SMU type: `keithley_6482`, `keithley_2400` |
| `visa_resource` | string | VISA resource string |
| `enabled` | boolean | Enable/disable device |
| `channel_count` | number | Number of channels |
| `enabled_channels` | array | List of enabled channel numbers |
| `measurement_nplc` | number | NPLC (integration time) |
| `auto_range` | boolean | Enable auto-range |
| `fixed_range` | number | Fixed measurement range (if auto_range=false) |

### Keithley 2400 Additional Keys

| Key | Type | Description |
|-----|------|-------------|
| `default_voltage` | number | Default output voltage |
| `default_current` | number | Default output current |
| `default_compliance` | number | Default compliance limit |

**Example:**
```json
{
  "devices": [
    {
      "name": "Keithley_6482",
      "type": "keithley_6482",
      "visa_resource": "GPIB0::1::INSTR",
      "enabled": true,
      "channel_count": 2,
      "enabled_channels": [1, 2],
      "measurement_nplc": 5,
      "auto_range": true
    }
  ]
}
```

---

## power_supply_config.json

Power supply device configuration.

| Key | Type | Description |
|-----|------|-------------|
| `devices` | array | List of power supply devices |

### Power Supply Device Object

| Key | Type | Description |
|-----|------|-------------|
| `name` | string | Device name |
| `type` | string | PSU type (see below) |
| `ip` | string | Device IP address |
| `port` | number | SCPI port (typically 5025) |
| `channels` | number | Number of output channels |

### Power Supply Types

| Type | Description |
|------|-------------|
| `keysight_e36441a_direct` | Keysight E36441A (4-channel) |
| `keysight_e36103b_direct` | Keysight E36103B (1-channel) |

**Example:**
```json
{
  "devices": [
    {
      "name": "PSU1",
      "type": "keysight_e36441a_direct",
      "ip": "192.168.0.32",
      "port": 5025,
      "channels": 4
    }
  ]
}
```

---

## transformation_matrix.json

Coordinate transformation matrices for motion devices.

Array of transformation objects:

| Key | Type | Description |
|-----|------|-------------|
| `DeviceId` | string | Motion device ID |
| `Matrix` | object | 3x3 transformation matrix |

### Matrix Object

| Key | Description |
|-----|-------------|
| `M11`, `M12`, `M13` | Row 1 |
| `M21`, `M22`, `M23` | Row 2 |
| `M31`, `M32`, `M33` | Row 3 |

Used to transform coordinates from camera/vision system to motion device coordinates.

**Example:**
```json
[
  {
    "DeviceId": "gantry-main",
    "Matrix": {
      "M11": 1.0, "M12": 0.0, "M13": 0.0,
      "M21": 0.0, "M22": 1.0, "M23": 0.0,
      "M31": 0.0, "M32": 0.0, "M33": -1.0
    }
  }
]
```

---

## lighting_config.json

LED lighting controller configuration.

| Key | Type | Description |
|-----|------|-------------|
| `lighting_controllers` | array | List of lighting controllers |

### Lighting Controller Object

| Key | Type | Description |
|-----|------|-------------|
| `device_id` | string | Unique device ID |
| `device_name` | string | Display name |
| `controller_type` | string | Controller type: `CCS_PD3` |
| `ip_address` | string | Controller IP address |
| `port` | number | TCP port |
| `enabled` | boolean | Enable/disable controller |
| `channels` | array | Channel configurations |

### Channel Object

| Key | Type | Description |
|-----|------|-------------|
| `channel` | number | Channel number (0-indexed) |
| `name` | string | Channel display name |
| `color` | string | LED color (for UI) |
| `default_intensity` | number | Default brightness (0-255) |
| `enabled` | boolean | Enable/disable channel |

**Example:**
```json
{
  "lighting_controllers": [
    {
      "device_id": "led-controller-1",
      "device_name": "Main LED Controller",
      "controller_type": "CCS_PD3",
      "ip_address": "192.168.0.2",
      "port": 40001,
      "enabled": true,
      "channels": [
        { "channel": 0, "name": "Collimate", "color": "white", "default_intensity": 0, "enabled": true }
      ]
    }
  ]
}
```

---

## pi_pivot_config.json

PI hexapod pivot point configuration.

| Key | Type | Description |
|-----|------|-------------|
| `pi_pivot_points` | array | List of pivot configurations |

### Pivot Point Object

| Key | Type | Description |
|-----|------|-------------|
| `device_name` | string | Motion device ID |
| `pivot_x` | number | X pivot offset (mm) |
| `pivot_y` | number | Y pivot offset (mm) |
| `pivot_z` | number | Z pivot offset (mm) |

Pivot points define the rotation center for hexapod U/V/W rotations.

**Example:**
```json
{
  "pi_pivot_points": [
    {
      "device_name": "hex-left",
      "pivot_x": -12.950,
      "pivot_y": 0.0,
      "pivot_z": 109.750
    }
  ]
}
```

---

## position_labels.json

Visual label offsets for named positions in graph view.

Structure: `{ "device_id": { "position_name": { "offsetX": n, "offsetY": n } } }`

| Key | Type | Description |
|-----|------|-------------|
| `offsetX` | number | Label X offset from node center |
| `offsetY` | number | Label Y offset from node center |

Used to position text labels in the Motion Page graph view to avoid overlapping.

**Example:**
```json
{
  "gantry-main": {
    "home": { "offsetX": 9.99, "offsetY": -11.38 },
    "safe": { "offsetX": -4.06, "offsetY": -1.52 }
  }
}
```

---

## quick_outputs.json

Quick output buttons for Jog Window.

| Key | Type | Description |
|-----|------|-------------|
| `quick_outputs` | array | List of quick output buttons |

### Quick Output Object

| Key | Type | Description |
|-----|------|-------------|
| `device_name` | string | IO device name |
| `pin_number` | number | Output pin number |
| `display_name` | string | Button label |

Supports local override with `quick_outputs.local.json`.

**Example:**
```json
{
  "quick_outputs": [
    { "device_name": "IOBottom", "pin_number": 0, "display_name": "L Gripper" },
    { "device_name": "IOBottom", "pin_number": 2, "display_name": "R Gripper" }
  ]
}
```

---

## SYSTEM Subfolder

The `config/SYSTEM/` folder contains system-level configuration that should not be modified:
- Graph definitions
- System calibration data
- Factory defaults
