# Calibration Files

This directory contains calibration data for the needle dispensing system.

## File Structure

```
config/calibration/
├── needleXYoffsetCamera.json         ← SYSTEM: XY offset (factory default included)
├── needle_z_calibration.json         ← GLOBAL: Latest needle Z height (created after first calibration)
├── navigation_*.json                 ← PRODUCT: Camera positions (created per product)
└── README.md                         ← This file
```

## Factory Default Files

### needleXYoffsetCamera.json
- **Status**: Included out-of-the-box with factory default values
- **Purpose**: XY offset from camera center to needle tip
- **Default Values**: X=50.0mm, Y=10.0mm
- **⚠️ Important**: These are approximate values! For precise operation, run the **"Calibrate Camera-to-Needle Offset"** process

## Files Created During Operation

### needle_z_calibration.json
- **Created When**: First time you run "Calibrate Needle Z"
- **Purpose**: Global needle Z height (shared across all products)
- **Updated**: Every time any product runs needle Z calibration
- **Contains**: Current Z height, previous Z height, delta tracking

### navigation_{UID}.json
- **Created When**: First time a specific process/product runs calibration
- **Purpose**: Product-specific camera position for auto-navigation
- **Example**: `navigation_calibrate_needle_z_001.json`
- **Contains**: Camera XYZ position for auto-return to calibration spot
- **Benefit**: Subsequent calibrations auto-navigate to saved position

## Calibration Workflow

### 🔧 Initial Setup (One-Time, System-Level)

**Process**: "Calibrate Camera-to-Needle Offset"

**When**:
- After machine assembly
- After camera or dispenser head replacement
- Periodically to verify accuracy

**Result**: Updates `needleXYoffsetCamera.json` with accurate XY offset

### 🎯 Product Calibration (Per Product, Repeatable)

**Process**: "Calibrate Needle Z"

**When**:
- Before dispensing on a new product type
- When needle Z height changes (wear, replacement)
- Periodically to monitor drift

**First Run**:
- Creates `navigation_{UID}.json` for this product
- Updates global `needle_z_calibration.json`

**Subsequent Runs**:
- Auto-navigates to saved camera position
- User only fine-tunes Z height
- Shows delta from previous calibration

## File Ownership

| File | Scope | Updated By |
|------|-------|------------|
| `needleXYoffsetCamera.json` | System (global, static) | "Calibrate Camera-to-Needle Offset" |
| `needle_z_calibration.json` | Global (shared) | Any "Calibrate Needle Z" run |
| `navigation_{UID}.json` | Product-specific | Each product's "Calibrate Needle Z" |

## Backup and Maintenance

### Recommended Backup
- **Before recalibration**: Backup existing files
- **After major changes**: Save calibration history
- **Version control**: Track changes to detect drift

### File Locations
- **Source**: `config/calibration/` (development)
- **Runtime**: `out/build/x64-debug/config/calibration/` (copied during build)

### Troubleshooting

**Q: Needle not positioning correctly during Z calibration?**
- A: Run "Calibrate Camera-to-Needle Offset" to update XY offset

**Q: Want to recalibrate for a different product?**
- A: Just run "Calibrate Needle Z" - it will create a new navigation file with unique UID

**Q: Delta showing large change in Z calibration?**
- A: Normal if needle wear occurred. Document delta. If > 0.1mm, investigate cause.

**Q: Lost calibration files?**
- A: Factory default XY offset still works. Recalibrate for best accuracy.

## Technical Notes

- All measurements in millimeters (mm)
- Coordinates relative to machine home position
- Timestamps in local system time
- JSON files use 2-space indentation for readability

---

**Document Version**: 1.0
**Last Updated**: 2025-10-27
