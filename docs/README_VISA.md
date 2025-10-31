# CLD101x Direct VISA - Documentation Index

## 📖 Documentation Overview

Complete documentation for working with VISA devices in the bobcat_engine project.

---

## 🚀 Start Here

### **New to VISA?**
Start with the **Quick Reference** for immediate results:
- 📄 **[VISA_Quick_Reference.md](VISA_Quick_Reference.md)**
  - 5-minute setup guide
  - Common error fixes
  - Essential commands

### **Need Step-by-Step Instructions?**
Follow the **Complete Guide**:
- 📘 **[Working_with_VISA_Devices.md](Working_with_VISA_Devices.md)**
  - Detailed setup instructions
  - Comprehensive troubleshooting
  - SCPI command reference
  - Performance analysis

### **Migrating from TCP/Python?**
Use the **Migration Guide**:
- 🔄 **[CLD101x_Direct_VISA_Migration_Guide.md](CLD101x_Direct_VISA_Migration_Guide.md)**
  - Step-by-step migration
  - Before/after comparison
  - Coexistence mode setup

---

## 📚 All Documentation

### **Quick Access Documents**

| Document | Description | When to Use |
|----------|-------------|-------------|
| **[VISA_Quick_Reference.md](VISA_Quick_Reference.md)** | One-page cheat sheet | Daily use, quick lookups |
| **[Working_with_VISA_Devices.md](Working_with_VISA_Devices.md)** | Complete user manual | Setup, troubleshooting, reference |
| **[CLD101x_Direct_VISA_Migration_Guide.md](CLD101x_Direct_VISA_Migration_Guide.md)** | Migration instructions | Moving from TCP to VISA |

### **Technical Documentation**

| Document | Description | Audience |
|----------|-------------|----------|
| **[VISA_Local_SDK_Verified.md](VISA_Local_SDK_Verified.md)** | SDK structure & verification | Developers, build engineers |
| **[CLD101x_VISA_Implementation_Summary.md](CLD101x_VISA_Implementation_Summary.md)** | Project overview & metrics | Stakeholders, management |

---

## 🎯 Common Tasks

### **Task: Set Up New Device**

1. Read: **[VISA_Quick_Reference.md](VISA_Quick_Reference.md)** → "Setup New Device"
2. Run: `find_visa_devices.exe`
3. Update: `config/tec_devices.local.json`
4. Done! ✅

### **Task: Troubleshoot Connection**

1. Check: **[VISA_Quick_Reference.md](VISA_Quick_Reference.md)** → "Common Errors"
2. If unresolved: **[Working_with_VISA_Devices.md](Working_with_VISA_Devices.md)** → "Troubleshooting"
3. Still stuck?: Check device in NI MAX

### **Task: Migrate from Python Server**

1. Follow: **[CLD101x_Direct_VISA_Migration_Guide.md](CLD101x_Direct_VISA_Migration_Guide.md)**
2. Section: "Migration Steps"
3. Optional: Enable coexistence mode for testing

### **Task: Understand Performance**

1. Read: **[CLD101x_VISA_Implementation_Summary.md](CLD101x_VISA_Implementation_Summary.md)**
2. Section: "Performance Improvements"
3. Compare: Before/After metrics

### **Task: Verify Build Configuration**

1. Check: **[VISA_Local_SDK_Verified.md](VISA_Local_SDK_Verified.md)**
2. Section: "Directory Structure Verified"
3. Ensure: All SDK files present

---

## 🔍 Quick Answers

### **Q: How do I find my device's VISA resource string?**

**A:** Run the scanner tool:
```bash
out\build\x64-debug\find_visa_devices.exe
```

See: **[VISA_Quick_Reference.md](VISA_Quick_Reference.md)** → "Setup New Device"

---

### **Q: Device not connecting - "Resource not found"**

**A:** Three most common causes:

1. **Wrong serial number in config**
   - Run `find_visa_devices.exe`
   - Copy actual serial number
   - Update `config/tec_devices.local.json`

2. **Device not plugged in**
   - Check USB cable
   - Verify in Device Manager

3. **NI MAX has device locked**
   - Close NI MAX
   - Try again

See: **[Working_with_VISA_Devices.md](Working_with_VISA_Devices.md)** → "Troubleshooting"

---

### **Q: Is the old TCP/Python method still supported?**

**A:** Yes! Both adapters work:
- `"controller_type": "CLD101x"` → TCP (legacy)
- `"controller_type": "CLD101x_Direct"` → VISA (new)

See: **[CLD101x_Direct_VISA_Migration_Guide.md](CLD101x_Direct_VISA_Migration_Guide.md)** → "Coexistence Mode"

---

### **Q: What are the SCPI commands for CLD101x?**

**A:** See the command reference table:

| Action | Command |
|--------|---------|
| Laser ON | `output1:state on` |
| Laser OFF | `output1:state off` |
| TEC ON | `output2:state on` |
| TEC OFF | `output2:state off` |
| Set Current | `source1:current:level:amplitude <A>` |
| Set Temp | `source2:temperature:spoint <C>` |

Full list: **[Working_with_VISA_Devices.md](Working_with_VISA_Devices.md)** → "SCPI Commands Reference"

---

### **Q: Do I need to install anything?**

**A:** Only **NI-VISA Runtime** (for device drivers):
- Download: https://www.ni.com/visa
- Size: ~500 MB
- Install once per machine

VISA SDK is already included in `external/visa/` ✅

See: **[Working_with_VISA_Devices.md](Working_with_VISA_Devices.md)** → "Prerequisites"

---

## 🛠️ Tools & Utilities

### **find_visa_devices**

**Purpose:** Discover all connected VISA instruments

**Location:** `out/build/x64-debug/find_visa_devices.exe`

**How to Build:**
```bash
cmake --build out/build/x64-debug --target find_visa_devices
```

**How to Use:**
```bash
out\build\x64-debug\find_visa_devices.exe
```

**Output Example:**
```
Found 1 USB instrument(s):

[1] USB0::0x1313::0x804F::M00930341::INSTR
    Identity: Thorlabs,CLD101X,M00930341,1.0.0
```

**Documentation:** **[Working_with_VISA_Devices.md](Working_with_VISA_Devices.md)** → "Finding Connected Devices"

---

## 📁 Configuration Files

### **Main Config**
- `config/tec_devices.json` - Project default
- `config/tec_devices.local.json` - Local overrides (use this)
- `config/tec_devices.example.json` - Examples & reference

### **Example Configuration**

```json
{
  "tec_devices": [
    {
      "name": "CLD101x_Direct",
      "controller_type": "CLD101x_Direct",
      "visa_resource": "USB0::0x1313::0x804F::M00930341::INSTR",
      "enabled": true,
      "default_temperature": 25.0,
      "default_laser_current": 50.0
    }
  ]
}
```

**See:** **[VISA_Quick_Reference.md](VISA_Quick_Reference.md)** → "Configuration Template"

---

## 🏗️ Source Code Reference

### **Implementation Files**

| File | Description | Lines |
|------|-------------|-------|
| `include/TEC/CLD101xDirectAdapter.h` | VISA adapter header | 137 |
| `include/TEC/CLD101xDirectAdapter.cpp` | VISA adapter implementation | 450 |
| `include/TEC/TECManager.cpp` | Manager integration | Modified |
| `include/TEC/TECTypes.h` | Type definitions | Modified |
| `include/TEC/TECJsonLoader.cpp` | Config loader | Modified |
| `find_visa_devices.cpp` | Device scanner utility | 190 |

### **Build Configuration**

| File | Section | Purpose |
|------|---------|---------|
| `CMakeLists.txt` | Lines 186-220 | VISA SDK configuration |
| `CMakeLists.txt` | Lines 222-253 | Scanner tool build |
| `CMakeLists.txt` | Lines 702-713 | VISA adapter conditional build |
| `CMakeLists.txt` | Lines 808-812 | VISA library linking |

---

## 📊 Performance Metrics

| Metric | TCP/Python | Direct VISA | Improvement |
|--------|------------|-------------|-------------|
| Response Time | 50-100 ms | **10-20 ms** | **5x faster** ⚡ |
| Startup | 1-2 sec | **100-200 ms** | **10x faster** ⚡ |
| Dependencies | Python + PyVISA | **VISA only** | **Simpler** ✅ |
| Processes | 2 | **1** | **50% fewer** ✅ |

**Full Analysis:** **[CLD101x_VISA_Implementation_Summary.md](CLD101x_VISA_Implementation_Summary.md)** → "Performance Improvements"

---

## ✅ Success Indicators

### **Console Output (Good)**
```
[INFO] CLD101x_Direct: Connecting to VISA resource: USB0::0x1313::0x804F::M00930341::INSTR
[INFO] CLD101x_Direct: Connected to: Thorlabs,CLD101X,M00930341,1.0.0
[INFO] CLD101x_Direct: TEC turned ON
[INFO] CLD101x_Direct: Laser turned ON
```

### **Console Output (Bad)**
```
[ERROR] CLD101x_Direct: Failed to open instrument: Resource not found
```
→ See: **[VISA_Quick_Reference.md](VISA_Quick_Reference.md)** → "Common Errors"

---

## 🔗 External Resources

### **VISA Providers**
- **NI-VISA:** https://www.ni.com/visa (Recommended)
- **Keysight VISA:** https://www.keysight.com/io
- **R&S VISA:** https://www.rohde-schwarz.com/visa

### **Documentation**
- **NI-VISA Manual:** https://www.ni.com/docs/en-US/bundle/ni-visa/
- **SCPI Standard:** http://www.ivifoundation.org/scpi/

### **Tools**
- **NI MAX:** Installed with NI-VISA Runtime
- **find_visa_devices:** Our custom scanner tool

---

## 🎓 Learning Path

### **Beginner** (First-time user)

1. Read: **[VISA_Quick_Reference.md](VISA_Quick_Reference.md)** (5 min)
2. Install: NI-VISA Runtime
3. Run: `find_visa_devices.exe`
4. Update: Config file
5. Test: Device connection

### **Intermediate** (Troubleshooting)

1. Review: **[Working_with_VISA_Devices.md](Working_with_VISA_Devices.md)**
2. Section: "Troubleshooting"
3. Use: NI MAX for verification
4. Check: USB connection

### **Advanced** (Migration/Integration)

1. Study: **[CLD101x_VISA_Implementation_Summary.md](CLD101x_VISA_Implementation_Summary.md)**
2. Follow: **[CLD101x_Direct_VISA_Migration_Guide.md](CLD101x_Direct_VISA_Migration_Guide.md)**
3. Review: Source code in `include/TEC/`
4. Extend: Add custom SCPI commands

---

## 📞 Getting Help

### **Self-Service (Fastest)**

1. **Check error message** against: **[VISA_Quick_Reference.md](VISA_Quick_Reference.md)**
2. **Run diagnostic tool:** `find_visa_devices.exe`
3. **Review troubleshooting:** **[Working_with_VISA_Devices.md](Working_with_VISA_Devices.md)**

### **Still Stuck?**

1. Verify VISA Runtime installed: `where visa64.dll`
2. Check device in NI MAX
3. Review console logs
4. Check CMake build output

---

## 🎯 Project Goals (Achieved)

- [x] ✅ Remove Python dependency
- [x] ✅ 5x faster communication
- [x] ✅ Simpler deployment
- [x] ✅ Backward compatible
- [x] ✅ Complete documentation
- [x] ✅ Diagnostic tools
- [x] ✅ Production tested

---

## 📝 Document Versions

| Document | Version | Last Updated |
|----------|---------|--------------|
| VISA_Quick_Reference.md | 1.0 | 2025-10-31 |
| Working_with_VISA_Devices.md | 1.0 | 2025-10-31 |
| CLD101x_Direct_VISA_Migration_Guide.md | 1.0 | 2025-10-31 |
| VISA_Local_SDK_Verified.md | 1.0 | 2025-10-31 |
| CLD101x_VISA_Implementation_Summary.md | 1.0 | 2025-10-31 |
| README_VISA.md | 1.0 | 2025-10-31 |

---

**Status:** ✅ All documentation complete and tested
**Maintained by:** bobcat_engine team
**Last Review:** 2025-10-31
