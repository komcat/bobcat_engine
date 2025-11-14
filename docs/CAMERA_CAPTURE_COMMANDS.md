# Camera Capture Commands - Implementation Summary

## Overview

Added 4 new camera capture commands to the sequence system under the **"Vision"** category:

1. **CaptureToFile** - Save to default product folder
2. **CaptureToContext** - Save to temp folder + store in memory
3. **CaptureToDUT** - Save to DUT's image folder
4. **CaptureAndConfirm** - Show image to user with confirm/cancel

---

## ✅ **Completed Implementation**

### Files Created:
1. `include/AppUI/RunProgramPage/CaptureConfirmWidget.h` - Image confirmation UI widget
2. `include/AppUI/RunProgramPage/CaptureConfirmWidget.cpp` - Implementation
3. `docs/CAMERA_CAPTURE_COMMANDS.md` - This documentation

### Files Modified:
1. `include/sequence/SequenceContext.h` - Added frame storage support
2. `include/sequence/SequenceContext.cpp` - Implemented frame save/load + temp file management
3. `include/sequence/CameraCommandHandler.cpp` - Implemented all 4 camera commands
4. `include/dut/DeviceUnderTest.h` - Added `getImageFolderPath()` helper
5. `src/dut/DeviceUnderTest.cpp` - Implemented image folder path helpers
6. `CMakeLists.txt` - Added CaptureConfirmWidget to build

---

## 📸 **Command Reference**

### 1. **CaptureToFile**

**Purpose:** Capture frame and save to default product folder

**Parameters:**
- `product_name` (required) - Product name for folder structure
- `filename` (optional) - Custom filename (auto-generated if not provided)
- `captureAs` (optional) - Store filepath in context

**Output Path:**
```
data/[product_name]/captured_images/[YYYY-MM-DD]/[camera_name]_[HH-MM-SS].png
```

**Example:**
```cpp
Command: "CaptureToFile"
Device: "camera_1"
Parameters:
  product_name: "SIPHOG888"
  filename: "inspection.png"  // optional
  captureAs: "saved_path"     // optional

Result: data/SIPHOG888/captured_images/2025-01-14/camera_1_21-30-45.png
Context["saved_path"] = "data/SIPHOG888/captured_images/2025-01-14/camera_1_21-30-45.png"
```

---

### 2. **CaptureToContext**

**Purpose:** Capture frame, save to temp folder, and store in memory for later processing

**Parameters:**
- `context_key` (required) - Key to store frame data in context

**Storage:**
- **Memory:** CameraFrameData stored in `SequenceContext`
- **Temp File:** `temp/[YYYY-MM-DD]/[timestamp].png`

**Example:**
```cpp
Command: "CaptureToContext"
Device: "camera_1"
Parameters:
  context_key: "before_image"

Result:
  Context["before_image"] = CameraFrameData (in memory)
  Temp file: temp/2025-01-14/1705271445123.png
```

**Use Case:**
```
1. CaptureToContext ("before_image")
2. [Perform some operation]
3. CaptureToContext ("after_image")
4. CompareImages("before_image", "after_image")  // Future command
```

---

### 3. **CaptureToDUT**

**Purpose:** Capture frame and save to DUT's image folder

**Parameters:**
- `product_name` (required) - Product name
- `serial_number` (required) - DUT serial number
- `filename` (optional) - Custom filename (auto-generated if not provided)
- `captureAs` (optional) - Store filepath in context

**Output Path:**
```
data/[product_name]/[serial_number]/images/[camera_name]_[HH-MM-SS].png
```

**Example:**
```cpp
Command: "CaptureToDUT"
Device: "camera_1"
Parameters:
  product_name: "SIPHOG888"
  serial_number: "SN-12345"
  filename: "final_inspection.png"  // optional

Result: data/SIPHOG888/SN-12345/images/final_inspection.png
```

**Folder Structure:**
```
data/
  SIPHOG888/
    SN-12345/
      SN-12345.json          (DUT data file)
      images/                (NEW folder for captures)
        camera_1_14-30-00.png
        camera_2_14-30-15.png
        final_inspection.png
```

---

### 4. **CaptureAndConfirm**

**Purpose:** Capture frame, show to user, wait for confirmation

**Parameters:**
- `message` (required) - Message to show user (e.g., "Check lens alignment")
- `save_on_confirm` (optional, default: false) - Save image if user clicks Continue

**Behavior:**
1. Capture frame from camera's live feed
2. Display image in execution panel (640x480 preview)
3. Show message with "Continue" / "Cancel" buttons
4. Wait for user response (blocks sequence)
5. If user clicks Cancel, sequence stops
6. If user clicks Continue, sequence proceeds

**UI Widget:** `CaptureConfirmWidget` (similar to `ManualAdjustWidget`)

**Example:**
```cpp
Command: "CaptureAndConfirm"
Device: "camera_1"
Parameters:
  message: "Check lens alignment - Continue?"
  save_on_confirm: "true"
```

**Status:** ⚠️ **Needs UI Integration**
The command handler is implemented, but RunProgramPage needs to be updated to show the CaptureConfirmWidget (similar to how ManualAdjustWidget is integrated).

---

## 🔧 **Technical Implementation**

### Frame Capture Method

All commands use `LiveVideoSubscriber` to pull frames from the camera's live feed:

```cpp
// Create temporary subscriber
auto subscriber = std::make_unique<LiveVideoSubscriber>(cameraName);
cameraManager->AddSubscriber(subscriber.get());

// Wait briefly for frame (camera already grabbing)
std::this_thread::sleep_for(std::chrono::milliseconds(150));

// Get latest frame
CameraFrameData frame = subscriber->GetLatestFrame();

// Clean up
cameraManager->RemoveSubscriber(subscriber.get());
```

**Benefits:**
- ✅ No need to trigger camera grab (already running)
- ✅ Fast (<150ms to get frame)
- ✅ Thread-safe
- ✅ No interference with existing subscribers

### PNG Saving

Uses Qt's `QImage::save()`:

```cpp
QImage image(
    frameData.imageData.data(),
    frameData.width,
    frameData.height,
    frameData.width * frameData.channels,
    QImage::Format_RGB888
);

image.save(QString::fromStdString(filepath), "PNG");
```

### Context Storage

`SequenceContext` now supports storing `CameraFrameData`:

```cpp
// Store frame in context with temp file
context->SetFrameData("my_key", frameData, saveToTempFile=true);

// Retrieve frame
std::shared_ptr<CameraFrameData> frame = context->GetFrameData("my_key");

// Get temp file path
std::string tempPath = context->GetFrameTempFilePath("my_key");
```

---

## ⚠️ **Pending Work**

### CaptureAndConfirm UI Integration

The command handler stores the frame in context, but RunProgramPage needs to:

1. Check for `__capture_confirm_frame__` in context
2. Show `CaptureConfirmWidget` with the frame
3. Wait for user response via signal/slot
4. Continue or cancel sequence based on response

**Pattern to follow:** See `ManualAdjustWidget` integration in `RunProgramPage`

**Location:** `include/AppUI/RunProgramPage/RunProgramPage.cpp`

**Required changes:**
```cpp
// In RunProgramPage.h
CaptureConfirmWidget* m_captureConfirmWidget;

// In RunProgramPage.cpp constructor
m_captureConfirmWidget = new CaptureConfirmWidget(this);
connect(m_captureConfirmWidget, &CaptureConfirmWidget::responseReceived,
        this, &RunProgramPage::onCaptureConfirmResponse);

// Add to execution panel layout
m_executionPanelLayout->addWidget(m_captureConfirmWidget);

// In ProgramWorker/RecipeWorker
// When CaptureAndConfirm command executes:
if (context->HasValue("__capture_confirm_frame__")) {
    auto frame = context->GetFrameData("__capture_confirm_frame__");
    QString message = QString::fromStdString(
        context->GetString("__capture_confirm_message__")
    );

    emit showCaptureConfirm(*frame, message);
    // Block and wait for response...
}
```

---

## 🧪 **Testing**

### Test Sequence Example:

```json
{
  "steps": [
    {
      "type": "action",
      "device": "camera_1",
      "method": "CaptureToFile",
      "params": {
        "product_name": "SIPHOG888",
        "captureAs": "image_path"
      }
    },
    {
      "type": "action",
      "device": "camera_1",
      "method": "CaptureToContext",
      "params": {
        "context_key": "inspection_image"
      }
    },
    {
      "type": "action",
      "device": "camera_1",
      "method": "CaptureToDUT",
      "params": {
        "product_name": "SIPHOG888",
        "serial_number": "SN-12345",
        "filename": "final.png"
      }
    },
    {
      "type": "action",
      "device": "camera_1",
      "method": "CaptureAndConfirm",
      "params": {
        "message": "Check alignment - OK to continue?"
      }
    }
  ]
}
```

### Verification:

1. **CaptureToFile:** Check `data/SIPHOG888/captured_images/[date]/` for PNG files
2. **CaptureToContext:** Check `temp/[date]/` for PNG files
3. **CaptureToDUT:** Check `data/SIPHOG888/SN-12345/images/` for PNG files
4. **CaptureAndConfirm:** Currently auto-continues (UI integration needed)

---

## 📋 **Summary**

| Command | Status | Output Location | Use Case |
|---------|--------|----------------|----------|
| CaptureToFile | ✅ Ready | `data/[product]/captured_images/[date]/` | General inspection images |
| CaptureToContext | ✅ Ready | `temp/[date]/` + Memory | Image processing pipelines |
| CaptureToDUT | ✅ Ready | `data/[product]/[SN]/images/` | Per-device records |
| CaptureAndConfirm | ⚠️ Needs UI | N/A | Manual inspection steps |

**Next Steps:**
1. Build and test first 3 commands
2. Integrate CaptureConfirmWidget into RunProgramPage
3. Test CaptureAndConfirm with UI
4. Add to recipe process plugins (optional)

---

**Status: READY TO BUILD AND TEST** 🎉

*Note: CaptureAndConfirm requires UI integration before it's fully functional.*
