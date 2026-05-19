# Power Supply Configuration Guide

## Overview

The `power_supply_config.json` file configures power supply devices for the Bobcat Engine.

Two control modes are supported:
1. **Direct Control** (Recommended) - C++ directly controls PSU via TCP/IP
2. **Server-based Control** (Legacy) - C++ communicates through Python server

---

## Configuration Format

### Option 1: Direct Control (Recommended)

**File:** `config/power_supply_config.json`

```json
{
    "devices": [
      {
        "name": "PSU1",
        "type": "keysight_e36441a_direct",
        "ip": "192.168.0.32",
        "port": 5025,
        "channels": 4
      },
      {
        "name": "PSU2",
        "type": "keysight_e36103b_direct",
        "ip": "192.168.0.33",
        "port": 5025,
        "channels": 1
      }
    ]
  }
```

**Advantages:**
- ✅ No Python server required
- ✅ Faster response time
- ✅ Simpler architecture
- ✅ Fewer dependencies

**When to use:**
- Direct LAN connection to PSU
- Single-machine deployment
- Real-time control requirements

---

### Option 2: Server-based Control (Legacy)

**File:** `config/power_supply_config.json`

```json
{
  "server": {
    "host": "127.0.0.60",
    "port": 5000
  },
  "devices": [
    {
      "name": "PSU1",
      "type": "keysight_e36103b",
      "ip": "192.168.0.60",
      "port": 5025
    }
  ]
}
```

**When to use:**
- Existing Python server infrastructure
- Multi-client scenarios
- Remote PSU control through server proxy

---

## Supported Device Types

| Type | Description | Control Mode |
|------|-------------|--------------|
| `keysight_e36103b_direct` | Keysight E36103B Direct Control (1-channel) | Direct TCP/IP |
| `keysight_direct` | Alias for above | Direct TCP/IP |
| `keysight_e36441a_direct` | Keysight E36441A Direct Control (4-channel) | Direct TCP/IP |
| `unit_t_udp3305s` | UNI-T UDP3305S-E (3-channel) | USB / NI-VISA |
| `keysight_e36103b` | Keysight E36103B via Server | Python Server |
| `keysight` | Alias for above | Python Server |

### UNI-T UDP3305S-E (USB / VISA)

Unlike the Keysight LAN PSUs, the UDP3305S-E is reached over USB via NI-VISA.
Use `visa_resource` instead of `ip`/`port`. Get the resource string by running
the `find_visa_devices` utility (built when NI-VISA is detected) or with
`docs/UDP3305S/check_connection.py`.

```json
{
  "devices": [
    {
      "name": "PSU3",
      "type": "unit_t_udp3305s",
      "visa_resource": "USB0::0x0483::0x5740::<serial>::INSTR",
      "channels": 3
    }
  ]
}
```

The serial number is unique per unit, so this entry belongs in
`power_supply_config.local.json` (gitignored), not the checked-in
`power_supply_config.json`. UNI-T enumerates under the STMicro USB VID
(`0x0483`) — that's expected, not a wrong device.

See `docs/UDP3305S/HANDOFF.md` for the verified SCPI command set, hardware
test scripts, and integration notes.

---

## Configuration Parameters

### Device Configuration

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | Yes | - | Device identifier (e.g., "PSU1") |
| `type` | string | Yes | - | Device type (see table above) |
| `ip` | string | Cond. | - | IP address of the PSU (required for TCP types) |
| `port` | int | No | 5025 | TCP port (5025 is SCPI standard) |
| `visa_resource` | string | Cond. | - | VISA resource string (required for USB/VISA types like `unit_t_udp3305s`) |
| `channels` | int | No | 1 | Number of independent output channels |

### Server Configuration (Server-based mode only)

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `host` | string | Yes | - | Python server IP address |
| `port` | int | Yes | - | Python server TCP port |

---

## Multiple Device Example

```json
{
  "devices": [
    {
      "name": "PSU_Main",
      "type": "keysight_e36103b_direct",
      "ip": "192.168.0.60",
      "port": 5025,
      "description": "Main power supply for DUT"
    },
    {
      "name": "PSU_Aux",
      "type": "keysight_e36103b_direct",
      "ip": "192.168.0.61",
      "port": 5025,
      "description": "Auxiliary supply for peripherals"
    }
  ]
}
```

**Note:** The `description` field is optional and not currently parsed.

---

## Network Configuration

### Keysight E36103B Default Settings

- **TCP Port:** 5025 (SCPI over TCP/IP)
- **Protocol:** SCPI (Standard Commands for Programmable Instruments)
- **Timeout:** 5 seconds (hardcoded in driver)

### Firewall Requirements

Ensure the following ports are open:

| Mode | Port | Direction | Purpose |
|------|------|-----------|---------|
| Direct | 5025 | Outbound | SCPI commands to PSU |
| Server | 5000 | Outbound | Commands to Python server |

---

## Troubleshooting

### Connection Failed

**Error:** `Connection failed with error: ...`

**Solutions:**
1. Verify PSU IP address is correct
2. Check PSU is powered on and network-enabled
3. Ping PSU: `ping 192.168.0.60`
4. Verify firewall allows port 5025
5. Check Ethernet cable connection

### Device Not Found

**Error:** `Device 'PSU1' missing IP address`

**Solutions:**
1. Check JSON syntax (commas, brackets)
2. Ensure `ip` field is present and not empty
3. Validate JSON using online validator

### Wrong Driver Type

**Error:** `Unknown PSU type: ...`

**Solutions:**
1. Use supported type: `keysight_e36103b_direct` or `keysight_e36103b`
2. Check for typos in `type` field
3. Ensure quotes around type value

---

## Migration from Server-based to Direct

**Old (Server-based):**
```json
{
  "server": {
    "host": "127.0.0.60",
    "port": 5000
  },
  "devices": [
    {
      "name": "PSU1",
      "type": "keysight_e36103b",
      "ip": "192.168.0.60",
      "port": 5025
    }
  ]
}
```

**New (Direct):**
```json
{
  "devices": [
    {
      "name": "PSU1",
      "type": "keysight_e36103b_direct",
      "ip": "192.168.0.60",
      "port": 5025
    }
  ]
}
```

**Changes:**
1. Remove `"server"` section
2. Change `"type"` from `"keysight_e36103b"` to `"keysight_e36103b_direct"`
3. Keep `"ip"` and `"port"` unchanged

**Note:** Python server no longer needed after migration!

---

## Testing Configuration

After updating `power_supply_config.json`:

1. **Restart Bobcat Engine** to reload configuration
2. **Check Console Output** for loading messages:
   ```
   [PowerSupplyJsonLoader] Creating DIRECT driver for PSU1
   [PowerSupplyJsonLoader] Successfully loaded 1 device(s)
   ```
3. **Navigate to Power Supply Control Page**
4. **Click "Connect"** button
5. **Verify Connection:**
   ```
   [KeysightE36103B] Connected to 192.168.0.60:5025
   ```

---

## Advanced Configuration

### Custom Timeout (Requires Code Modification)

Edit `KeysightE36103B.h`:
```cpp
static constexpr int RECV_TIMEOUT_MS = 10000;  // 10 seconds
static constexpr int SEND_TIMEOUT_MS = 10000;  // 10 seconds
```

### Adding New PSU Model

1. Create new class implementing `IPowerSupply` interface
2. Add to `PowerSupplyJsonLoader::createPowerSupply()`
3. Update configuration with new type identifier

---

## Related Files

- **Configuration:** `config/power_supply_config.json`
- **Driver Interface:** `include/PSU/IPowerSupply.h`
- **Direct Driver:** `include/PSU/KeysightE36103B.h`
- **Server Driver:** `include/PSU/KeysightPSUAdapter.h`
- **Loader:** `include/PSU/PowerSupplyJsonLoader.h`
- **Manager:** `include/PSU/PowerSupplyManager.h`
