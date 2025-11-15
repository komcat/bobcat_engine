# Manual Align Point - Vision Command Documentation

## Overview

**Manual Align Point** is an interactive vision command that allows operators to manually align the gantry to specific points using camera click-to-move functionality. The command captures reference images, stores position data, and provides visual feedback for precise manual alignment tasks.

**Category:** Vision
**Command Type:** Interactive (requires user input)
**Added:** January 2025

---

## ✅ **Key Features**

1. **Visual Reference Display** - Shows 200x200px reference image from previous alignments
2. **Click-to-Move Integration** - Enables camera click-to-move on Auxiliary Window
3. **Crosshair Overlay** - Visual alignment aid on camera view
4. **Real-time Position Display** - Updates gantry position every 500ms
5. **Image Capture on Save** - Automatically captures and saves reference image
6. **Context Storage** - Stores XYZ position AND captured image in context
7. **Smart File Management** - Timestamped files prevent overwriting

---

## 📸 **Command Reference**

### **ManualAlignPoint**

**Purpose:** Manually align gantry to a point using camera, capture reference image, and save position to context

**Parameters:**
- `point_name` (required) - Unique identifier for the alignment point (e.g., "fiducial_1", "corner_mark")
- `message` (optional) - Instruction message shown to operator (default: "Align the crosshair to the target point")
- `enable_x` (optional) - Enable X-axis adjustment (default: true)
- `enable_y` (optional) - Enable Y-axis adjustment (default: true)
- `enable_z` (optional) - Enable Z-axis adjustment (default: true)

**Storage Locations:**

1. **Context (XYZ Position):**
   ```
   context["{point_name}_X"] = gantry X position (float)
   context["{point_name}_Y"] = gantry Y position (float)
   context["{point_name}_Z"] = gantry Z position (float)
   ```

2. **Context (Captured Image):**
   ```
   context["{point_name}_image"] = CameraFrameData (full frame)
   ```

3. **Reference Image File:**
   ```
   data/{product_name}/referenceimages/{point_name}.png
   data/{product_name}/referenceimages/{point_name}_YYYYMMDD_HHMMSS.png  (if file exists)
   ```

**Example:**
```json
{
  "type": "Manual Align Point",
  "parameters": {
    "point_name": "fiducial_1",
    "message": "Align crosshair to top-left fiducial mark",
    "enable_x": "true",
    "enable_y": "true",
    "enable_z": "true"
  }
}
```

**Result:**
```
Context Storage:
  context["fiducial_1_X"] = 123.456
  context["fiducial_1_Y"] = 78.901
  context["fiducial_1_Z"] = 23.456
  context["fiducial_1_image"] = <CameraFrameData>

File Storage:
  data/SIPHOG888/referenceimages/fiducial_1.png
```

---

## 🎯 **User Interface Flow**

### 1. **Widget Display**

When the command executes, the Recipe Page shows:

```
┌─────────────────────────────────────────────────────┐
│ Select Manual Alignment Point: fiducial_1          │
│                                                     │
│ Align crosshair to top-left fiducial mark          │
│                                                     │
│ 📷 Use Click-to-Move on the Auxiliary Camera       │
│    Window to align the gantry.                     │
│    The crosshair shows the target alignment        │
│    position.                                        │
│                                                     │
│ Position: X: 123.456 | Y: 78.901 | Z: 23.456 mm   │
│                                                     │
│ Reference Image:                                    │
│ ┌──────────────────┐                               │
│ │   [200x200px]    │  ← Previous alignment image   │
│ │   image preview  │     or "No reference"         │
│ └──────────────────┘                               │
│                                                     │
│                           [Cancel] [Continue]       │
└─────────────────────────────────────────────────────┘
```

### 2. **Auxiliary Window**

The Auxiliary Camera Window shows:
- ✅ **Crosshair** at center of view (alignment target)
- ✅ **Click-to-Move** enabled (click anywhere to move gantry)
- ✅ Live camera feed

### 3. **Operator Workflow**

1. **View Reference** - Operator sees previous alignment image (if available)
2. **Click to Move** - Operator clicks on camera view to move gantry
3. **Fine Adjustment** - Operator adjusts position until crosshair is aligned
4. **Monitor Position** - Real-time position display updates every 500ms
5. **Continue** - Click Continue button when aligned
6. **Auto-Capture** - System automatically captures current camera frame
7. **Save** - System saves:
   - Reference image to file
   - Image to context for vision processing
   - XYZ position to context

### 4. **Cancel Behavior**

If operator clicks **Cancel**:
- No image is captured
- No position is saved
- Sequence execution stops with error
- Returns to recipe execution panel

---

## 💾 **Reference Image Management**

### File Naming Strategy

**First Alignment:**
```
data/SIPHOG888/referenceimages/fiducial_1.png
```

**Subsequent Alignments (file exists):**
```
data/SIPHOG888/referenceimages/fiducial_1_20250114_143022.png
data/SIPHOG888/referenceimages/fiducial_1_20250114_150230.png  ← Latest
```

### Loading Reference Image

When widget opens:
1. Searches `data/{product}/referenceimages/` for files matching `{point_name}*.png`
2. Sorts by file modification time (most recent first)
3. Loads and displays latest image scaled to 200x200px
4. Shows "No reference image available" if none found

### Image Capture on Continue

When operator clicks Continue:
1. Captures current camera frame from live feed
2. Checks if `{point_name}.png` exists
3. If exists: adds timestamp → `{point_name}_YYYYMMDD_HHMMSS.png`
4. If not exists: saves as → `{point_name}.png`
5. Creates directory structure if needed
6. Stores frame in context as `{point_name}_image`

**Benefits:**
- ✅ Never overwrites existing references
- ✅ Maintains history of all alignments
- ✅ Easy to compare alignment drift over time
- ✅ Product-specific image organization

---

## 🔧 **Use Cases**

### 1. **Single Point Alignment**

Align to one reference point and use position for subsequent operations:

```
Recipe Steps:
1. Manual Align Point "fiducial_1"
   → Operator aligns to fiducial mark
   → Saves: fiducial_1_X, fiducial_1_Y, fiducial_1_Z, fiducial_1_image

2. Move to Position
   → X = context["fiducial_1_X"] + offset
   → Y = context["fiducial_1_Y"] + offset
   → Moves relative to aligned point
```

### 2. **Multi-Point Alignment**

Align to multiple points for coordinate transformation:

```
Recipe Steps:
1. Manual Align Point "top_left"
   → Saves: top_left_X, top_left_Y, top_left_Z

2. Manual Align Point "top_right"
   → Saves: top_right_X, top_right_Y, top_right_Z

3. Manual Align Point "bottom_left"
   → Saves: bottom_left_X, bottom_left_Y, bottom_left_Z

4. Calculate Transform Matrix
   → Uses 3 points to calculate rotation/translation
   → Apply to all subsequent moves
```

### 3. **Alignment with Vision Processing**

Capture alignment image and process for verification:

```
Recipe Steps:
1. Manual Align Point "corner_mark"
   → Saves: corner_mark_X/Y/Z, corner_mark_image

2. Crop ROI
   → Source: "corner_mark_image"
   → Output: "corner_cropped"
   → Width: 50%, Height: 50%

3. [Vision Processing on cropped image]
   → Template matching
   → Edge detection
   → Quality verification
```

### 4. **Reference Image Comparison**

Compare current alignment to previous reference:

```
Recipe Steps:
1. Manual Align Point "fiducial_1"
   → Shows previous reference image
   → Operator aligns to match reference
   → Captures new image

2. Compare Images (future command)
   → Load: data/.../fiducial_1.png (reference)
   → Compare with: context["fiducial_1_image"] (current)
   → Calculate alignment error
```

### 5. **Teaching Mode for Automated Alignment**

Teach system where to look for features:

```
Teaching Recipe:
1. Manual Align Point "feature_1"
   → Operator manually aligns
   → System captures position + image

2. Manual Align Point "feature_2"
   → Repeat for all features

Production Recipe:
1. Load Position "feature_1"
   → Load: feature_1_X/Y/Z from database
   → Move to position automatically

2. Vision Verification
   → Capture image at position
   → Compare with reference image
   → Auto-adjust if needed
```

---

## 🔌 **Implementation Details**

### Files Created:
1. `include/AppUI/RunProgramPage/ManualAlignPointWidget.h` - Widget header
2. `include/AppUI/RunProgramPage/ManualAlignPointWidget.cpp` - Widget implementation

### Files Modified:
1. `include/recipe/processes/CameraProcesses.cpp` - Added ManualAlignPointPlugin
2. `include/sequence/SystemCommandHandler.h` - Added ManualAlignPointCallback
3. `include/sequence/SystemCommandHandler.cpp` - Added ManualAlignPoint handler
4. `include/AppUI/RecipePage/RecipePage.h` - Added widget and callbacks
5. `include/AppUI/RecipePage/RecipePage.cpp` - Wired widget integration

### Dependencies:
- **UnifiedMotionManager** - For position reading/control
- **CameraManager** - For frame capture
- **AuxiliaryWindowManager** - For click-to-move and crosshair control
- **ProductPositionManager** - For product name lookup
- **SequenceContext** - For data storage

---

## 📊 **Data Stored in Context**

After manual alignment, the following data is available:

### Position Data (XYZ)
```cpp
// Access position values
float x = context->GetNumeric("fiducial_1_X");
float y = context->GetNumeric("fiducial_1_Y");
float z = context->GetNumeric("fiducial_1_Z");
```

### Image Data
```cpp
// Access captured image
auto frameData = context->GetFrameData("fiducial_1_image");

// Frame data includes:
frameData->cameraId       // Camera name (e.g., "camera_1")
frameData->width          // Image width in pixels
frameData->height         // Image height in pixels
frameData->channels       // Number of channels (3 = RGB)
frameData->imageData      // Raw pixel data (vector<uint8_t>)
frameData->timestamp      // Capture timestamp
frameData->exposureTime   // Camera exposure setting
frameData->gain           // Camera gain setting
```

### Temp File Path
```cpp
// Get temp file path for captured image
std::string tempPath = context->GetFrameTempFilePath("fiducial_1_image");
// Returns: "temp/2025-01-14/1763161484312.png"
```

---

## ⚙️ **Configuration**

### Recipe Configuration UI

When configuring in Recipe Editor:

```
Process Name: Manual Align Point

Parameters:
┌─────────────────────────────────────────────────┐
│ Point Name: [fiducial_1                     ]  │
│                                                 │
│ Instruction Message:                            │
│ [Align crosshair to top-left fiducial mark  ]  │
│                                                 │
│ Enable X Axis: [true ▼]                        │
│ Enable Y Axis: [true ▼]                        │
│ Enable Z Axis: [true ▼]                        │
└─────────────────────────────────────────────────┘
```

### Default Values:
- **Point Name:** "alignment_point_1"
- **Message:** "Align the crosshair to the target point"
- **Enable X/Y/Z:** true

---

## 🚨 **Error Handling**

### Possible Errors:

1. **No Camera Available**
   ```
   Error: CameraManager not available for image capture
   Action: Check camera connections
   ```

2. **Invalid Frame Capture**
   ```
   Error: Captured frame is invalid
   Action: Check camera is grabbing frames
   ```

3. **No Product Loaded**
   ```
   Error: No product name available
   Action: Load a product before running alignment
   ```

4. **Motion Manager Not Available**
   ```
   Error: Failed to get position from device: gantry-main
   Action: Check gantry-main connection
   ```

5. **User Cancelled**
   ```
   Warning: User cancelled manual alignment
   Action: Sequence stops, no data saved
   ```

---

## 🎓 **Best Practices**

### Naming Conventions

Use descriptive, consistent names for alignment points:

**Good:**
```
fiducial_1, fiducial_2, fiducial_3
corner_top_left, corner_top_right
feature_a_center, feature_b_center
```

**Avoid:**
```
point1, p2, temp
alignment, align, align2
```

### Instruction Messages

Write clear, specific instructions for operators:

**Good:**
```
"Align crosshair to the top-left fiducial mark"
"Center crosshair on the circular alignment feature"
"Position crosshair at the corner of the lens edge"
```

**Avoid:**
```
"Align here"
"Move to position"
"Click continue when done"
```

### Reference Image Management

1. **First Run:** Capture reference image under ideal conditions
2. **Subsequent Runs:** System shows reference for comparison
3. **Periodic Review:** Check reference image folder for alignment drift
4. **Archive Old Images:** Timestamped files allow historical analysis

### Workflow Integration

**Sequential Alignment:**
```
1. Manual Align Point "datum_1"  (operator aligns)
2. Wait 1000ms                     (settle time)
3. Manual Align Point "datum_2"  (operator aligns)
4. Calculate offsets               (automated)
5. [Production moves]              (automated)
```

**Error Recovery:**
```
1. Manual Align Point "feature_1"
2. Vision Verification             (check alignment quality)
3. Conditional: if verification fails
   → Manual Align Point "feature_1"  (re-align)
4. Continue with process
```

---

## 📝 **Troubleshooting**

### Reference Image Not Showing

**Symptoms:** Widget shows "No reference image available"

**Solutions:**
1. Check product name is loaded correctly
2. Verify folder exists: `data/{product}/referenceimages/`
3. Check file naming: `{point_name}.png` or `{point_name}_*.png`
4. Verify file permissions

### Click-to-Move Not Working

**Symptoms:** Clicking on auxiliary window doesn't move gantry

**Solutions:**
1. Verify Auxiliary Window is open
2. Check camera transformation matrix is configured
3. Ensure gantry-main is connected
4. Verify motion axes are enabled in configuration

### Position Not Updating

**Symptoms:** Position display shows "0.000" or stale values

**Solutions:**
1. Check gantry-main connection status
2. Verify motion controller communication
3. Restart application if connection lost

### Image Capture Fails

**Symptoms:** Reference image file not created

**Solutions:**
1. Check camera is grabbing frames
2. Verify disk space available
3. Check folder permissions for `data/{product}/referenceimages/`
4. Ensure camera live feed is active

---

## 🔮 **Future Enhancements**

Potential improvements for future versions:

1. **Auto-Alignment Mode**
   - Use vision to automatically align to reference image
   - Operator just verifies instead of manually aligning

2. **Alignment Quality Metrics**
   - Calculate alignment score vs reference image
   - Warn if alignment quality is poor

3. **Multi-Camera Support**
   - Select which camera to use for alignment
   - Support different cameras for different points

4. **Coordinate System Integration**
   - Automatically apply alignment offsets to coordinate system
   - Update transformation matrices based on aligned points

5. **Historical Tracking**
   - Log all alignment attempts with timestamp
   - Show alignment drift over time
   - Alert if drift exceeds threshold

---

## 📚 **Related Commands**

This command works well with:

- **Capture to Context** - Additional image captures for comparison
- **Crop ROI** - Process captured alignment image
- **Move to Graph Node** - Move between aligned positions
- **DUT Create** - Track alignment data per device
- **Wait** - Allow settling time after alignment

---

## 📄 **Version History**

- **v1.0** (January 2025) - Initial implementation
  - Reference image display (200x200px)
  - Click-to-move integration
  - Crosshair overlay
  - Auto-capture on continue
  - Context storage (position + image)
  - Smart file management with timestamps

---

## 👥 **Support**

For questions or issues:
1. Check this documentation
2. Review example recipes in `config/SIPHOG888/recipes/`
3. Consult system logs in log viewer
4. Contact development team

---

**Last Updated:** January 2025
**Category:** Vision Commands
**Status:** Production Ready ✅
