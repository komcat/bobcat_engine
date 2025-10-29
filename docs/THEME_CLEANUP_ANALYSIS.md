# Theme Cleanup Analysis - AppUI Directory

**Date:** 2025-10-29
**Total Files:** 54 .cpp files
**Total setStyleSheet Calls:** 690

## Overview

All files in `/include/AppUI` have been analyzed for theme-related code. Files are categorized by cleanup priority based on styling complexity and impact.

---

## 🔴 HIGH PRIORITY - Dialogs with Light Theme (BROKEN)

These dialogs explicitly set light theme and will have **broken visibility** (white text on white background).

| File | Calls | Issue | Impact |
|------|-------|-------|--------|
| `Common/FileSelectorDialog.cpp` | 9 | Full light theme override | 🔴 **CRITICAL** - Unreadable text |
| `Common/CreateProductDialog.cpp` | 15 | Full light theme override | 🔴 **CRITICAL** - Unreadable text |
| `Common/RecipeSaveAsDialog.cpp` | 16 | Full light theme override | 🔴 **CRITICAL** - Unreadable text |
| `Common/ProductSelectorDialog.cpp` | 5 | Full light theme override | 🔴 **CRITICAL** - Unreadable text |
| `RecipePage/PneumaticSlideSelectorDialog.cpp` | 8 | Likely light theme | 🔴 **CRITICAL** - Unreadable text |
| `MotionPages/OffsetMoveDialog.cpp` | 5 | Likely light theme | 🟡 **HIGH** - May be broken |
| `RunProgramPage/UserPromptWidget.cpp` | 5 | Light theme styling | 🟡 **HIGH** - User interaction broken |

**Cleanup Action:** Remove ALL `setStyleSheet` calls for standard widgets. Keep only semantic button colors.

---

## 🟡 MEDIUM PRIORITY - Pages with Heavy Styling

Pages with significant custom styling that can benefit from global theme.

| File | Calls | Issue | Priority |
|------|-------|-------|----------|
| `RecipePage/ProcessConfigWidget.cpp` | 14 | Heavy widget styling | 🟡 MEDIUM |
| `RecipePage/RecipePage.cpp` | 46 | Extensive styling | 🟡 MEDIUM |
| `TECPage/TECControlPage.cpp` | 40 | Heavy page styling | 🟡 MEDIUM |
| `PowerSupplyControlPage/PowerSupplyControlPage.cpp` | 21 | Heavy page styling | 🟡 MEDIUM |
| `PneumaticPage/PneumaticControlPage.cpp` | 47 | Heavy page styling | 🟡 MEDIUM |
| `IOPage/IOControllerPage.cpp` | 15 | Moderate styling | 🟡 MEDIUM |
| `RunProgramPage/RunProgramPage.cpp` | 38 | Heavy page styling | 🟡 MEDIUM |
| `HardwareInstrumentPage.cpp` | 5 | Light styling | 🟡 MEDIUM |
| `MainMenuPage.cpp` | 35 | Heavy button styling | 🟡 MEDIUM |
| `ConfigEditor/ConfigEditorPage.cpp` | 10 | Moderate styling | 🟡 MEDIUM |

**Cleanup Action:** Review each, remove redundant theme code, keep only custom features.

---

## 🟢 LOW PRIORITY - Components with Minimal Styling

Components with light styling that work but can be simplified.

| File | Calls | Notes |
|------|-------|-------|
| `MotionPages/EditPositionDialog.cpp` | 19 | Already updated with dark theme |
| `MotionPages/WaypointEditorDialog.cpp` | 13 | Already updated with dark theme |
| `MotionPages/NewPositionDialog.cpp` | 12 | ✅ **Already cleaned up** |
| `MotionPages/MotionControlPanel.cpp` | 10 | Dark theme, can simplify |
| `MotionPages/ScanControlPanel.cpp` | 18 | Moderate styling |
| `MotionPages/ACSControllerPage.cpp` | 13 | Moderate styling |
| `MotionPages/PIControllerPage.cpp` | 9 | Light styling |
| `MotionPages/MotionPage.cpp` | 12 | Moderate styling |
| `MotionPages/MotionControllerPageBase.cpp` | 5 | Light styling |
| `MotionPages/DeviceButtonStack.cpp` | 2 | Minimal styling |
| `MotionPages/CollapsiblePanel.cpp` | 3 | Minimal styling |
| `JogWindow/JogPanelWindow.cpp` | 13 | Moderate styling |
| `JogWindow/QuickOutputWidget.cpp` | 14 | Moderate styling |
| `RunProgramPage/ManualAdjustWidget.cpp` | 15 | Moderate styling |
| `RunProgramPage/ScanControlWidget.cpp` | 8 | Light styling |
| `CoordinateSystem/CreateSystemWidget.cpp` | 51 | Heavy styling |
| `CoordinateSystem/TestSystemWidget.cpp` | 6 | Light styling |
| `RecipePage/DeviceSelectorWidgets.cpp` | 6 | Light styling |
| `RecipePage/RecipeProgressWidget.cpp` | 10 | Moderate styling |

**Cleanup Action:** Can be cleaned up opportunistically when touching these files.

---

## ✅ MINIMAL STYLING - Form Widgets

Form widgets with standard styling patterns.

| File | Calls | Notes |
|------|-------|-------|
| `Forms/CameraFormWidget.cpp` | 11 | Standard form styling |
| `Forms/MotorDeviceFormWidget.cpp` | 13 | Standard form styling |
| `Forms/TcpClientFormWidget.cpp` | 13 | Standard form styling |
| `Forms/SavedPositionsFormWidget.cpp` | 14 | Standard form styling |
| `Forms/PneumaticSlidesFormWidget.cpp` | 12 | Standard form styling |
| `Forms/IODevicesFormWidget.cpp` | 23 | Heavy form styling |
| `Forms/PowerSupplyFormWidget.cpp` | 12 | Standard form styling |
| `Forms/TECDevicesFormWidget.cpp` | 12 | Standard form styling |

**Cleanup Action:** Low priority, stable code.

---

## ⚪ NO STYLING - Graphics Components

These have minimal or no theme concerns (graphics rendering).

| File | Notes |
|------|-------|
| `MotionPages/PositionDot.cpp` | Graphics primitive |
| `MotionPages/DevicePositionMarker.cpp` | Graphics primitive |
| `MotionPages/PositionAnchor.cpp` | Graphics primitive |
| `MotionPages/PositionLabel.cpp` | Graphics primitive |
| `MotionPages/PathPreview.cpp` | Graphics primitive |
| `MotionPages/MotionGraphicsView.cpp` | Graphics view |
| `PlaceholderPage.cpp` | Simple placeholder |
| `JogWindow/JogKeyBindingHandler.cpp` | Logic only |

**Cleanup Action:** None needed.

---

## 🏁 Already Done

| File | Status |
|------|--------|
| `MainWindow.cpp` | ✅ Uses `DarkTheme::getStyleSheet()` |
| `DarkTheme.cpp` | ✅ Central theme definition |
| `MotionPages/NewPositionDialog.cpp` | ✅ Cleaned up and simplified |

---

## Cleanup Approach

### Phase 1: Critical Fixes (HIGH PRIORITY)
**Files:** 7 dialogs with light theme
**Impact:** Fix broken visibility immediately
**Time:** ~2 hours

```cpp
// Pattern: Remove ALL of this
setStyleSheet(
  "QDialog { background-color: white; }"
  "QLabel { color: #2c3e50; }"
  "QLineEdit { background-color: white; color: #2c3e50; }"
  ...
);

// Keep ONLY semantic button colors
deleteBtn->setObjectName("deleteButton");
deleteBtn->setStyleSheet("QPushButton#deleteButton { background-color: #e74c3c; }");
```

### Phase 2: Medium Priority Cleanup (MEDIUM PRIORITY)
**Files:** 10 pages with heavy styling
**Impact:** Improve consistency and maintainability
**Time:** ~4 hours

Review each file:
1. Remove global theme overrides
2. Remove redundant widget styling
3. Keep only custom UI features (animations, special layouts)
4. Keep semantic button colors

### Phase 3: Low Priority Optimization (LOW PRIORITY)
**Files:** 19 components
**Impact:** Code quality improvement
**Time:** Opportunistic (as files are touched)

Clean up when making other changes to these files.

---

## Estimated Code Reduction

| Metric | Before | After | Savings |
|--------|--------|-------|---------|
| setStyleSheet calls | 690 | ~150 | 78% reduction |
| Lines of style code | ~3000 | ~800 | 73% reduction |
| Maintenance burden | High | Low | Centralized |
| Theme consistency | Poor | Excellent | Single source |

---

## Testing Plan

### For Each Cleaned File:

1. **Visual Test:** Open dialog/page, verify readable text on dark background
2. **Interaction Test:** Verify all buttons respond to hover/click
3. **Color Test:** Verify semantic colors (green=create, red=delete, etc.)
4. **Input Test:** Verify text inputs have visible text and cursor

### Critical Test Cases:

- FileSelectorDialog: Can see and select files
- CreateProductDialog: Can fill form and create product
- RecipeSaveAsDialog: Can enter name and save
- UserPromptWidget: Can read prompt and respond
- All navigation buttons work

---

## Priority Execution Plan

### Week 1: Critical Fixes
- [ ] FileSelectorDialog.cpp
- [ ] CreateProductDialog.cpp
- [ ] RecipeSaveAsDialog.cpp
- [ ] ProductSelectorDialog.cpp
- [ ] PneumaticSlideSelectorDialog.cpp
- [ ] OffsetMoveDialog.cpp
- [ ] UserPromptWidget.cpp

### Week 2-3: Medium Priority
- [ ] RecipePage.cpp
- [ ] ProcessConfigWidget.cpp
- [ ] TECControlPage.cpp
- [ ] PowerSupplyControlPage.cpp
- [ ] PneumaticControlPage.cpp
- [ ] IOControllerPage.cpp
- [ ] RunProgramPage.cpp
- [ ] MainMenuPage.cpp
- [ ] HardwareInstrumentPage.cpp
- [ ] ConfigEditorPage.cpp

### Ongoing: Low Priority
- Clean up as files are modified for other reasons

---

## Success Metrics

- ✅ Zero white-background dialogs
- ✅ All text readable on dark background
- ✅ Consistent color scheme across app
- ✅ 70%+ reduction in setStyleSheet calls
- ✅ Single source of truth for theme (DarkTheme.cpp)

---

## Next Steps

1. **Start with HIGH priority dialogs** - Fix broken visibility issues
2. **Test each dialog** before moving to next
3. **Document any custom styling** that needs to remain
4. **Create before/after screenshots** for validation

---

## Notes

- Keep semantic button colors (green, red, orange) using object names
- Keep custom font sizes for headers/titles
- Keep special animations or transitions
- Remove ALL standard widget styling (let global theme handle it)
- Test in actual application, not just visually
