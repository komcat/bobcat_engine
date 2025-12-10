# TODO: Refactor IO Bit Format to Standard Layout

## Problem

The `IOControllerPage` UI expects output status in a non-standard bit format:
- 8-pin devices: outputs in bits 8-15
- 16-pin devices: outputs in bits 16-31

This was done to match the Fastech EziIO SDK's quirky `FAS_GetOutput()` return format.

## Current State

| Device | Raw Output Bits | Adapter Needed |
|--------|-----------------|----------------|
| EziIO (Fastech) | 8-15 (weird) | No - UI matches EziIO format |
| WAGO (Modbus) | 0-7 (standard) | Yes - shifts to 8-15 |

The `WagoIODevice` has an adapter layer that shifts WAGO's standard bits 0-7 to match EziIO's format.

## Proposed Fix

1. **Normalize to standard format (bits 0-7 for outputs)**
2. **Update `EziIODevice`** to shift bits from 8-15 down to 0-7
3. **Update `IOControllerPage`** to read outputs from bits 0-7 (remove `bitOffset` logic)
4. **Remove adapter layer from `WagoIODevice`**

## Files to Modify

- `include/IO/EziIODevice.cpp` - Add bit shifting in `ReadOutputs()` and `SetOutputs()`
- `include/AppUI/IOPage/IOControllerPage.cpp` - Remove `bitOffset` calculation (lines 546-548, 554)
- `include/IO/WagoIODevice.cpp` - Remove `WagoToUiBits()` / `UiToWagoBits()` calls
- `include/IO/WagoIODevice.h` - Remove adapter helper methods (optional cleanup)

## Benefits

- Simpler code - no bit shifting adapters needed
- Standard Modbus devices work out of the box
- Easier to add new IO device types

## Reference

Commit that added the bitOffset fix for EziIO: `3825466`
