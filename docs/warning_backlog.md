# /W4 Warning Backlog

Build output from x64-debug-hardware, 2026-02-06. ~223 unique application warnings deduplicated from ~1603 raw lines.

---

## Priority 1: C4834 - Discarding `[[nodiscard]]` return (41 locations)

Safety-critical return values being silently ignored. These are real bugs.

| File | Line(s) |
|------|---------|
| `Application.cpp` | 913, 1319 |
| `MotionCommandHandler.cpp` | 2427, 2503, 2583, 2605, 2737, 2844, 2967, 4300, 4396, 4453, 4498 |
| `CalibrationCommandHandler.cpp` | 958 |
| `CoordinateAlignCommandHandler.cpp` | 1007 |
| `VisionCommandHandler.cpp` | 1279 |
| `AdaptiveScanController.cpp` | 158 |
| `MotionPage.cpp` | 1252 |
| `GraphWidget.cpp` | 2193, 2194, 2195 |
| `RecipePage.cpp` | 3524 |
| `AlignmentWizardDialog.cpp` | 902, 940 |
| `CameraClickToMoveWidget.cpp` | 969, 1018 |
| `ManualAdjustWidget.cpp` | 429, 430, 431 |
| `RetraceEditWidget.cpp` | 431, 432, 433, 434, 435, 436 |
| `ManualAlignPointWidget.cpp` | 236 |
| `pi_controller.cpp` | 253 |
| `acs_controller.cpp` | 261 |
| `unified_motion_manager.cpp` | 79, 1873 |
| `QuickOutputManager.cpp` | 137 |

---

## Priority 2: C4099 - class/struct mismatch (9 locations)

Single root-cause fix: `ApplicationContext` is `struct` but forward-declared as `class` in multiple headers. Same for `CameraFrameData`.

| File | Line | Type |
|------|------|------|
| `ApplicationContext.h` | 31 | `ApplicationContext` |
| `MainMenuPage.h` | 9 | `ApplicationContext` |
| `CalibrationCommandHandler.h` | 7 | `ApplicationContext` |
| `CameraCommandHandler.h` | 7 | `ApplicationContext` |
| `VisionCommandHandler.h` | 11 | `ApplicationContext` |
| `ManualAlignPointWidget.h` | 11 | `ApplicationContext` |
| `CameraClickToMoveWidget.h` | 16 | `CameraFrameData` |
| `CameraClickToMoveWidget.h` | 19 | `ApplicationContext` |
| `CameraFrameData.h` | 9 | `CameraFrameData` |

**Fix:** Change all `class ApplicationContext;` forward declarations to `struct ApplicationContext;`. Same for `CameraFrameData`.

---

## Priority 3: C4244/C4267 - Narrowing conversions (25 locations)

Potential data loss on 64-bit builds.

### C4267 (size_t to int)
| File | Line(s) |
|------|---------|
| `MotionControllerPageBase.cpp` | 234, 285, 299 |
| `QuickOutputWidget.cpp` | 305 |
| `product_json_loader.cpp` | 343, 347, 354, 358 |
| `CLD101xAdapter.cpp` | 373 |
| `CLD101xDirectAdapter.cpp` | 446, 474 |
| `TED4015DirectAdapter.cpp` | 369, 397 |
| `ProgramWorker.cpp` | 160, 332, 800 |

### C4244 (int to smaller type)
| File | Line(s) | Conversion |
|------|---------|------------|
| `CLD101xAdapter.cpp` | 54 | int -> u_short |
| `KeysightE36103B.cpp` | 92 | int -> u_short |
| `KeysightE36441A.cpp` | 93 | int -> u_short |
| `WagoIODevice.cpp` | 106 | SOCKET -> int |
| `WagoIODevice.cpp` | 269, 411, 478, 550, 593 | int -> uint8_t/uint16_t |

---

## Priority 4: C4996 - Deprecated/unsafe functions (22 locations)

### CRT unsafe functions
| File | Line(s) | Function | Replace with |
|------|---------|----------|-------------|
| `bobcat_engine.cpp` | 36 | `getenv` | `_dupenv_s` |
| `Application.cpp` | 167 | `getenv` | `_dupenv_s` |
| `DeviceUnderTest.cpp` | 316 | `gmtime` | `gmtime_s` |
| `SequenceContext.cpp` | 557, 558 | `localtime` | `localtime_s` |
| `DeviceLinkManager.cpp` | 86 | `localtime` | `localtime_s` |
| `RotationScanController.cpp` | 73, 362, 363 | `localtime`/`ctime` | `localtime_s`/`ctime_s` |
| `SMUPollingManager.cpp` | 329 | `localtime` | `localtime_s` |
| `KeithleySMUAdapter.cpp` | 514, 532 | `localtime` | `localtime_s` |
| `Keithley6482Adapter.cpp` | 497, 1072 | `localtime` | `localtime_s` |

### Qt deprecated APIs
| File | Line(s) | Deprecated | Replace with |
|------|---------|-----------|-------------|
| `RecipePage.cpp` | 719, 720, 722, 723, 724, 726, 745 | `QMenu::addAction` | New overload with `(text, shortcut, object, slot)` |
| `ApplicationSettingsPage.cpp` | 470 | `QCheckBox::stateChanged` | `checkStateChanged()` |

---

## Priority 5: C4309 - Constant truncation (4 locations)

Modbus byte values assigned to signed char in `WagoIODevice.cpp`.

| File | Line(s) |
|------|---------|
| `WagoIODevice.cpp` | 30, 134, 141, 153 |

---

## Low Priority

### C4100 - Unreferenced parameter (68 locations)

Most are in virtual interface default implementations or callback signatures. Fix with `(void)param;` or omit parameter name.

**High-frequency files:**
- `KeysightE36103B.cpp` - 9 locations (all `channel` param, single-channel device)
- `KeysightPSUAdapter.cpp` - 9 locations (same)
- `CoarseToFineStrategy.cpp` - 8 locations
- `RecipeWorker.cpp` - 5 locations
- `CameraFrameData.h` - 3 locations (header, repeated in every TU)
- `ICameraHardware.h` - 3 locations (header, repeated in every TU)
- `RunProgramPage.cpp` - 7 locations

### C4189 - Unused initialized local variable (24 locations)
| File | Line(s) | Variable |
|------|---------|----------|
| `CameraWorker.cpp` | 346, 347 | `captured`, `displayed` |
| `PneumaticSlide.cpp` | 52, 82, 160 | `oldPosition` |
| `MotionGraphicsView.cpp` | 264 | `viewportHeight` |
| `RecipePage.cpp` | 1651, 2316, 2342, 2378, 2404, 2440, 2467, 2522, 2649, 2675, 2926, 2952, 3841, 3884, 4356 | `totalSteps`, `oldWidget`, `stepCount`, `cancelButton` |
| `DependencyGraphWindow.cpp` | 421 | `widget` |
| `OpenCVTest.cpp` | 189 | `originalEdge` |
| `KeithleySMUAdapter.cpp` | 832 | `retCount` |

### C4458/C4457/C4456 - Variable shadowing (21 locations)
| Warning | File | Line(s) | Variable |
|---------|------|---------|----------|
| C4458 (hides member) | `ConfigEditorPage.cpp` | 275 | `data` |
| C4458 | `MotionPage.cpp` | 1454 | `data` |
| C4458 | `RecipePage.cpp` | 1222, 1434 | `data` |
| C4458 | `RecipeSaveAsDialog.cpp` | 210 | `data` |
| C4458 | `ManualAlignPointWidget.cpp` | 627 | `data` |
| C4457 (hides param) | `MotionCommandHandler.cpp` | 2736 | `deviceName` |
| C4457 | `SystemCommandHandler.cpp` | 313 | `deviceName` |
| C4456 (hides local) | `MotionPage.cpp` | 1042-1210 | `msgBox` (7x) |
| C4456 | `RecipePage.cpp` | 1014 | `productName` |
| C4456 | `SystemCommandHandler.cpp` | 585 | `captureAsIt` |
| C4456 | `TECCommandHandler.cpp` | 110, 221 | `captureAsIt` |
| C4456 | `RecipeWorker.cpp` | 428 | `logger` |
| C4456 | `PIAlignExecutor.cpp` | 254 | `piController` |

### C4101 - Unreferenced local variable (7 locations)
| File | Line(s) | Variable |
|------|---------|----------|
| `GraphWidget.cpp` | 881, 1027, 1071 | `ok` |
| `AuxWindowCommandHandler.cpp` | 138 | `e` |
| `Keithley6482Adapter.cpp` | 302 | `e` |
| `HalconLineDetectionPlugin.cpp` | 137 | `e` |
| `unified_motion_manager.cpp` | 1950 | `e` |

### C4505 - Unreferenced static function (1 location)
- `CameraView.cpp(20)`: `loadUIThrottleDelay`

### C4702 - Unreachable code (1 location)
- `CalibrationProcesses.cpp(176)`

---

## External Warning Suppression

These come from vendor SDKs and should be suppressed, not fixed.

| Warning | Source | Count | Suggestion |
|---------|--------|-------|-----------|
| C4201 | `external/eziio/*.h` | ~56 unique locations | `#pragma warning(disable: 4201)` in EziIO includes |
| C4127 | `external/opencv/core/types.hpp` | repeated per TU | Use `/external:I` + `/external:W0` or pragma push/pop |

**CMake approach:**
```cmake
# Suppress external SDK warnings
if(MSVC)
    target_compile_options(bobcat_engine PRIVATE /external:anglebrackets /external:W0)
endif()
```
