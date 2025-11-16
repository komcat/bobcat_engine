# Vision Manager System

This document describes the Vision Manager subsystem, including plugin architecture, ROI (Region of Interest), and Search Box concepts.

## Overview

The Vision Manager provides an interactive UI for configuring and executing computer vision operations. It consists of:

- **Left Panel**: Plugin selection by category (PR, Measurement, Filter)
- **Center Panel**: Image display with interactive ROI/Search Box overlays
- **Right Panel**: Configuration tabs (ROI, Search, Camera, Plugin Config)
- **Toolbar**: Profile/Recipe management (New, Load, Save, Run)

## Core Components

### VisionManager (Singleton)
Central manager that holds:
- Registered plugins
- Current input image (cv::Mat)
- Current ROI settings
- Pixel-to-mm calibration values
- Recipe steps for multi-step processing

Location: `include/vision/VisionManager.h`

### VisionManagerPage
Qt widget that orchestrates the UI and connects all components.

Location: `include/vision/ui/VisionManagerPage.h`

---

## Plugin System

### Plugin Architecture

Plugins are the processing units that perform specific vision tasks. Each plugin:
- Inherits from `VisionPluginBase`
- Defines its own parameter schema
- Creates its own configuration widget
- Executes vision algorithms on input images

### Plugin Categories

- **PR (Pattern Recognition)**: Feature detection, fiducial finding
- **Measurement**: Distance, angle, dimensional measurements
- **Filter**: Image preprocessing, edge detection

### Creating a New Plugin

#### Step 1: Create Plugin Header

```cpp
// include/vision/plugins/MyNewPlugin.h
#pragma once
#include "../VisionPluginBase.h"

class MyNewPlugin : public VisionPluginBase {
public:
    MyNewPlugin();

    VisionResult execute(const cv::Mat& image) override;
    ParameterSchema getParameterSchema() const override;
    QWidget* createConfigWidget() override;
};
```

#### Step 2: Implement Plugin

```cpp
// include/vision/plugins/MyNewPlugin.cpp
#include "MyNewPlugin.h"
#include <QVBoxLayout>
#include <QSpinBox>
#include <QLabel>
#include <QGroupBox>

MyNewPlugin::MyNewPlugin()
    : VisionPluginBase("MyNewPlugin", "Measurement", "Description of what plugin does")
{
    // Set default parameters
    m_parameters["threshold"] = 50;
    m_parameters["sensitivity"] = 0.8;
}

ParameterSchema MyNewPlugin::getParameterSchema() const
{
    ParameterSchema schema;

    ParameterDef threshold;
    threshold.name = "threshold";
    threshold.displayName = "Detection Threshold";
    threshold.type = ParameterType::Int;
    threshold.minValue = 0;
    threshold.maxValue = 255;
    threshold.defaultValue = 50;
    threshold.description = "Minimum threshold for detection";
    schema.parameters.push_back(threshold);

    ParameterDef sensitivity;
    sensitivity.name = "sensitivity";
    sensitivity.displayName = "Sensitivity";
    sensitivity.type = ParameterType::Double;
    sensitivity.minValue = 0.0;
    sensitivity.maxValue = 1.0;
    sensitivity.defaultValue = 0.8;
    sensitivity.description = "Detection sensitivity";
    schema.parameters.push_back(sensitivity);

    return schema;
}

VisionResult MyNewPlugin::execute(const cv::Mat& image)
{
    VisionResult result;
    result.pluginName = getName();

    auto startTime = std::chrono::high_resolution_clock::now();

    try {
        // Get parameters
        int threshold = m_parameters.value("threshold", 50);
        double sensitivity = m_parameters.value("sensitivity", 0.8);

        // Perform vision processing here
        // ...

        // Store results
        result.measurements["value"] = 123.45;
        result.success = true;

    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = e.what();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.processingTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

QWidget* MyNewPlugin::createConfigWidget()
{
    QWidget* widget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(widget);

    QGroupBox* group = new QGroupBox("My Plugin Settings");
    QFormLayout* form = new QFormLayout(group);

    // Threshold spinner
    QSpinBox* thresholdSpin = new QSpinBox();
    thresholdSpin->setRange(0, 255);
    thresholdSpin->setValue(m_parameters.value("threshold", 50));
    connect(thresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_parameters["threshold"] = val; });
    form->addRow("Threshold:", thresholdSpin);

    // Sensitivity spinner
    QDoubleSpinBox* sensitivitySpin = new QDoubleSpinBox();
    sensitivitySpin->setRange(0.0, 1.0);
    sensitivitySpin->setSingleStep(0.1);
    sensitivitySpin->setValue(m_parameters.value("sensitivity", 0.8));
    connect(sensitivitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_parameters["sensitivity"] = val; });
    form->addRow("Sensitivity:", sensitivitySpin);

    layout->addWidget(group);
    layout->addStretch();

    return widget;
}
```

#### Step 3: Register Plugin

In `VisionManagerPage::registerPlugins()`:

```cpp
#include "../plugins/MyNewPlugin.h"

void VisionManagerPage::registerPlugins()
{
    // ... existing plugins ...

    // Register your new plugin
    auto myPlugin = std::make_shared<MyNewPlugin>();
    VisionManager::instance().registerPlugin(myPlugin);
}
```

#### Step 4: Add to CMakeLists.txt

```cmake
set(VISION_SOURCES
    # ... existing sources ...
    include/vision/plugins/MyNewPlugin.cpp
)
```

---

## ROI (Region of Interest)

### Concept

ROI defines a rectangular region within the captured image where vision processing is focused. This:
- Reduces processing time by limiting analysis area
- Allows focusing on specific features
- Improves accuracy by excluding irrelevant areas

### ROI Class

```cpp
class VisionROI {
    int startX, startY;  // Top-left corner
    int endX, endY;      // Bottom-right corner
    bool enabled;        // Whether to use ROI
};
```

### UI Features

- **ROI Tab**: Configure coordinates via spinboxes
- **Enable Checkbox**: Toggle ROI processing on/off
- **Visual Overlay**: Cyan dashed rectangle on image
- **Interactive Dragging**: Resize ROI by dragging corners
- **Size Display**: Shows width x height and total area

### Usage Flow

1. Capture image from camera
2. Define ROI either:
   - Drag corners on image display
   - Enter coordinates in ROI tab
3. Enable ROI checkbox
4. Execute plugin - processing only occurs within ROI
5. Results displayed relative to ROI position

---

## Search Box (3x3 Grid Search)

### Concept

Search Box addresses part variation where the target feature may not be perfectly centered. It defines a larger search area that is divided into a 3x3 grid. The ROI is systematically moved through all 9 grid positions to find the best detection result.

### Grid Layout

```
+-------+-------+-------+
|  11   |  12   |  13   |
+-------+-------+-------+
|  21   |  22   |  23   |
+-------+-------+-------+
|  31   |  32   |  33   |
+-------+-------+-------+
```

Grid positions labeled as: `row + column` (11, 12, 13, 21, 22, 23, 31, 32, 33)

### VisionSearchBox Class

```cpp
class VisionSearchBox {
    cv::Point searchTL;  // Top-left of search area
    cv::Point searchBR;  // Bottom-right of search area
    int searchW, searchH; // Dimensions

    // Get ROI position for a specific grid cell
    VisionROI getROIForGridCell(int gridIndex, int roiWidth, int roiHeight) const;

    // Get all 9 grid positions
    std::vector<VisionROI> getAllGridROIs(int roiWidth, int roiHeight) const;

    // Get label for grid position (11, 12, etc.)
    static std::string getGridLabel(int gridIndex);
};
```

### UI Features

- **Search Tab**: Configure search box coordinates
- **Enable Checkbox**: Toggle grid search on/off
- **Visual Overlay**: Orange dashed rectangle with 3x3 grid lines
- **Interactive Dragging**: Resize search box by dragging corners
- **Info Text**: Explains that enabling will iterate through 9 positions

### Execution Flow

When Search Box is enabled and "Execute Vision Profile" is clicked:

1. Current ROI dimensions are preserved (e.g., 453x268)
2. Search Box area is divided into 3x3 grid
3. ROI is positioned at each of 9 grid cells:
   - Grid 11 (top-left)
   - Grid 12 (top-center)
   - Grid 13 (top-right)
   - Grid 21 (middle-left)
   - Grid 22 (center)
   - Grid 23 (middle-right)
   - Grid 31 (bottom-left)
   - Grid 32 (bottom-center)
   - Grid 33 (bottom-right)
4. Plugin is executed at each position
5. Results are scored:
   - Circles found: +10 points each
   - Lines found: +5 points each
   - Circularity bonus: value * 100
   - Confidence bonus: value * 50
6. Best result (highest score) is returned
7. ROI is moved to best position for visualization

### Scoring Example

```
Grid Search Results:
  Grid 11: Found 0 circles, 0 lines (score: 0.0)
  Grid 12: Found 1 circles, 0 lines (score: 10.0)
  Grid 13: Found 0 circles, 0 lines (score: 0.0)
  Grid 21: Found 1 circles, 0 lines (score: 10.0)
  Grid 22: Found 1 circles, 0 lines (score: 10.0)
  Grid 23: Found 1 circles, 0 lines (score: 10.0)
  Grid 31: Found 0 circles, 0 lines (score: 0.0)
  Grid 32: Found 1 circles, 0 lines (score: 10.0)
  Grid 33: Found 1 circles, 0 lines (score: 10.0)

Best Result: Grid 12 (score: 10.0)
Total Search Time: 384.52 ms
```

---

## Profile and Recipe Management

### Vision Profile

Single plugin configuration including:
- ROI settings (coordinates, enabled state)
- Search Box settings (coordinates, enabled state)
- Camera settings (exposure, gain, calibration)
- Selected plugin and its parameters

Saved as JSON file: `config/[product]/vision/profile_name.json`

### Vision Recipe

Multi-step processing pipeline:
- Multiple plugin steps in sequence
- Each step has its own parameters
- Shared ROI, Search Box, and camera settings

Saved as JSON file: `config/[product]/vision/recipe_name.recipe.json`

### Save/Load Operations

- **Save**: Click toolbar save button (saves to current path if loaded, otherwise prompts)
- **Save As**: Always prompts for new file name
- **Load**: Choose between Profile or Recipe, then select file
- **New**: Clears current settings and paths

---

## Key Files

- `include/vision/VisionManager.h` - Core manager singleton
- `include/vision/VisionPluginBase.h` - Base class for plugins
- `include/vision/VisionROI.h` - ROI and SearchBox classes
- `include/vision/VisionResult.h` - Result data structure
- `include/vision/ui/VisionManagerPage.h` - Main UI page
- `include/vision/ui/VisionImageWidget.h` - Image display with overlays
- `include/vision/ui/VisionRightPanelWidget.h` - Settings panel
- `include/vision/ui/VisionLeftPanelWidget.h` - Plugin list and results
- `include/vision/plugins/*.h` - Individual plugin implementations

---

## Best Practices

1. **Plugin Design**
   - Keep parameters minimal and well-documented
   - Provide sensible defaults
   - Handle edge cases gracefully
   - Return meaningful error messages

2. **ROI Usage**
   - Always validate ROI bounds against image size
   - Use ROI to reduce processing time for large images
   - Consider feature size when setting ROI dimensions

3. **Search Box**
   - Use when part positioning has variation
   - Ensure Search Box is larger than ROI
   - Consider processing time (9x single execution time)
   - Review grid results to optimize search area size

4. **Profile Management**
   - Save profiles per product for consistency
   - Document profile purpose in file name
   - Test profiles with production variation samples
