# VISA Devices - Quick Reference Card

## 🚀 Setup New Device (5 minutes)

```bash
# 1. Build scanner
cmake --build out/build/x64-debug --target find_visa_devices

# 2. Connect device via USB

# 3. Find resource string
out\build\x64-debug\find_visa_devices.exe

# 4. Copy output (e.g., USB0::0x1313::0x804F::M00930341::INSTR)

# 5. Edit config/tec_devices.local.json
{
  "name": "CLD101x_Direct",
  "controller_type": "CLD101x_Direct",
  "visa_resource": "USB0::0x1313::0x804F::M00930341::INSTR",
  "enabled": true
}

# 6. Run application - Done!
```

---

## ✅ Success Indicators

**Console logs:**
```
[INFO] CLD101x_Direct: Connected to: Thorlabs,CLD101X,M00930341,1.0.0
[INFO] CLD101x_Direct: TEC turned ON
[INFO] CLD101x_Direct: Laser turned ON
```

**UI shows:**
- ✅ Device status: Connected
- ✅ Temperature readings update
- ✅ Laser current readings update
- ✅ Controls work (ON/OFF buttons)

---

## ❌ Common Errors & Fixes

### "Resource not found"
```
[ERROR] CLD101x_Direct: Failed to open instrument: Resource not found
```

**Fix:**
1. Run `find_visa_devices.exe` to get correct serial number
2. Update `visa_resource` in config with actual serial
3. Make sure device is plugged in

---

### "Failed to open VISA Resource Manager"
```
[ERROR] Failed to open VISA Resource Manager
```

**Fix:**
1. Install NI-VISA Runtime: https://www.ni.com/visa
2. Restart computer
3. Check: `where visa64.dll` → should show `C:\WINDOWS\system32\visa64.dll`

---

### "Timeout"
```
[ERROR] Query read failed: Timeout
```

**Fix:**
1. Close NI MAX if running
2. Unplug & replug device
3. Try different USB port

---

## 📋 Configuration Template

```json
{
  "tec_devices": [
    {
      "name": "YOUR_DEVICE_NAME",
      "controller_type": "CLD101x_Direct",
      "visa_resource": "USB0::0x1313::0x804F::YOUR_SERIAL::INSTR",
      "enabled": true,
      "default_temperature": 25.0,
      "default_laser_current": 50.0
    }
  ]
}
```

**Replace:**
- `YOUR_DEVICE_NAME` → e.g., `"CLD101x_Main"`
- `YOUR_SERIAL` → Run `find_visa_devices.exe` to get this

---

## 🔧 SCPI Commands (CLD101x)

| Action | Command | Response |
|--------|---------|----------|
| Get ID | `*IDN?` | `Thorlabs,CLD101X,M00930341,1.0.0` |
| Laser ON | `output1:state on` | - |
| Laser OFF | `output1:state off` | - |
| Check Laser | `output1:state?` | `1` (ON) or `0` (OFF) |
| Set Current | `source1:current:level:amplitude 0.05` | - |
| Read Current | `sense3:current:dc:data?` | `0.0485` (Amps) |
| TEC ON | `output2:state on` | - |
| TEC OFF | `output2:state off` | - |
| Check TEC | `output2:state?` | `1` (ON) or `0` (OFF) |
| Set Temp | `source2:temperature:spoint 25.0` | - |
| Read Temp | `SENSe2:temperature:data?` | `24.8` (°C) |

---

## 🆚 TCP vs Direct VISA

| Feature | TCP/Python | Direct VISA |
|---------|------------|-------------|
| Speed | 50-100 ms | **10-20 ms** ⚡ |
| Dependencies | Python + PyVISA | **VISA Runtime only** |
| Processes | 2 (App + Server) | **1 (App only)** |
| Setup | Complex | **Simple** ✅ |

---

## 📁 Files Reference

| File | Purpose |
|------|---------|
| `find_visa_devices.cpp` | Scanner tool source |
| `out/build/x64-debug/find_visa_devices.exe` | Scanner executable |
| `config/tec_devices.local.json` | Device configuration |
| `include/TEC/CLD101xDirectAdapter.cpp` | Adapter implementation |
| `docs/Working_with_VISA_Devices.md` | Full documentation |

---

## 🔗 Quick Links

- **NI-VISA Download:** https://www.ni.com/visa
- **Full Guide:** `docs/Working_with_VISA_Devices.md`
- **Migration Guide:** `docs/CLD101x_Direct_VISA_Migration_Guide.md`

---

**Need help?** See `docs/Working_with_VISA_Devices.md` for detailed troubleshooting.
