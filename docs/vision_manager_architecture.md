# Vision Manager Architecture Plan

**Version:** 1.0
**Date:** 2025-11-15
**Status:** Design Phase

---

## Overview

Vision Manager is a modular image processing system with plugin architecture, designed for industrial machine vision tasks. It provides pattern recognition, measurement, and calibration capabilities.

---

## Phase 1: Core Backend Architecture

### 1.1 Plugin System

```cpp
// include/vision/IVisionPlugin.h
class IVisionPlugin {
public:
    virtual ~IVisionPlugin() = default;

    // Identity
    virtual std::string getName() const = 0;
    virtual std::string getCategory() const = 0;  // "PR", "Measurement", "Filter", "Calibration"
    virtual std::string getDescription() const = 0;

    // Execution
    virtual bool execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) = 0;
    virtual bool validate() const = 0;  // Check if parameters are valid

    // Configuration
    virtual QWidget* createConfigWidget(QWidget* parent) = 0;
    virtual nlohmann::json getParameters() const = 0;
    virtual void setParameters(const nlohmann::json& params) = 0;
    virtual nlohmann::json getDefaultParameters() const = 0;
};
```

### 1.2 Result Structures

```cpp
// include/vision/VisionResult.h

struct Point2DResult {
    std::string name;
    cv::Point2d pixelCoord;
    cv::Point2d mmCoord;
};

struct LineResult {
    std::string name;
    cv::Point2d startPx, endPx;
    cv::Point2d startMm, endMm;
    double angleDeg;
    double lengthPx, lengthMm;
};

struct CircleResult {
    std::string name;
    cv::Point2d centerPx, centerMm;
    double radiusPx, radiusMm;
    double diameterPx, diameterMm;
};

struct RectangleResult {
    std::string name;
    cv::Rect2d boundingBoxPx;
    cv::Rect2d boundingBoxMm;
    double angleDeg;
    double areaPx, areaMm;
};

struct VisionResult {
    bool success = false;
    std::string pluginName;
    std::string errorMessage;
    double processingTimeMs = 0.0;

    // Results by type
    std::vector<Point2DResult> points;
    std::vector<LineResult> lines;
    std::vector<CircleResult> circles;
    std::vector<RectangleResult> rectangles;

    // Generic measurements
    std::map<std::string, double> measurements;  // "diameter_mm", "angle_deg", etc.
    std::map<std::string, std::string> metadata; // "algorithm", "confidence", etc.

    // Overlay commands for visualization
    std::vector<OverlayCommand> overlays;
};

struct OverlayCommand {
    enum Type { Point, Line, Circle, Rectangle, Text };
    Type type;
    cv::Scalar color;
    int thickness;
    // Union of draw parameters
    cv::Point2d p1, p2;
    double radius;
    std::string text;
};
```

### 1.3 Vision Manager Core

```cpp
// include/vision/VisionManager.h
class VisionManager : public QObject {
    Q_OBJECT

public:
    static VisionManager& instance();

    // Plugin Registry
    void registerPlugin(std::shared_ptr<IVisionPlugin> plugin);
    void unregisterPlugin(const QString& name);
    std::shared_ptr<IVisionPlugin> getPlugin(const QString& name);
    QStringList getAllPluginNames() const;
    QStringList getPluginsByCategory(const QString& category) const;
    QStringList getCategories() const;

    // Image Management
    void setInputImage(const cv::Mat& image);
    cv::Mat getInputImage() const;
    cv::Mat getOutputImage() const;
    void clearImages();

    // Execution
    VisionResult executePlugin(const QString& pluginName);
    VisionResult executePlugin(const QString& pluginName, const nlohmann::json& params);

    // Calibration
    void setPixelToMm(double x, double y);
    double getPixelToMmX() const;
    double getPixelToMmY() const;
    cv::Point2d convertPixelToMm(const cv::Point2d& pixelPt) const;
    cv::Point2d convertMmToPixel(const cv::Point2d& mmPt) const;

    // Recipe Management
    void newRecipe();
    bool loadRecipe(const QString& filePath);
    bool saveRecipe(const QString& filePath);
    bool saveRecipeAs(const QString& filePath);
    void addRecipeStep(const QString& pluginName, const nlohmann::json& params);
    void removeRecipeStep(int index);
    void clearRecipe();
    int getRecipeStepCount() const;
    std::vector<VisionResult> runRecipe();

signals:
    void pluginRegistered(const QString& name);
    void imageUpdated();
    void resultReady(const VisionResult& result);
    void recipeChanged();
    void calibrationChanged();

private:
    VisionManager();
    ~VisionManager() = default;

    std::map<QString, std::shared_ptr<IVisionPlugin>> m_plugins;
    cv::Mat m_inputImage;
    cv::Mat m_outputImage;
    double m_pixelToMmX = 0.00248;  // Default from camera settings
    double m_pixelToMmY = 0.00248;

    struct RecipeStep {
        QString pluginName;
        nlohmann::json parameters;
    };
    std::vector<RecipeStep> m_recipe;
    QString m_currentRecipePath;
};
```

### 1.4 Initial Plugins (Examples)

| Plugin Name | Category | Description |
|------------|----------|-------------|
| CircleDetection | PR | Detect circles using HoughCircles |
| EdgeDetection | Filter | Canny edge detection |
| TemplateMatching | PR | Pattern recognition via template |
| MeasureDistance | Measurement | Distance between two points |
| BlobDetection | PR | Find blobs/contours |
| ThresholdBinary | Filter | Binary thresholding |

---

## Phase 2: UI Architecture

### 2.1 Widget Hierarchy

```
VisionManagerPage (QWidget)
├── VisionToolbarWidget (Top row)
├── QSplitter (Horizontal, main content)
│   ├── VisionLeftPanelWidget (Left column)
│   │   ├── Add to Node Button
│   │   ├── Plugin Selection Tabs
│   │   └── Results Display Widget
│   │
│   ├── VisionImageWidget (Middle column)
│   │   ├── Image Display (with overlays)
│   │   └── Control buttons (Capture, Clear)
│   │
│   └── VisionRightPanelWidget (Right column)
│       ├── Camera Settings Section
│       ├── Calibration Section
│       └── Plugin Parameters Section (dynamic)
```

### 2.2 Widget Specifications

#### 2.2.1 VisionToolbarWidget
**File:** `include/vision/ui/VisionToolbarWidget.h/.cpp`

```cpp
class VisionToolbarWidget : public QWidget {
    Q_OBJECT
public:
    explicit VisionToolbarWidget(QWidget* parent = nullptr);

signals:
    void newRecipeClicked();
    void loadRecipeClicked();
    void saveRecipeClicked();
    void saveAsRecipeClicked();
    void runRecipeClicked();
    void stopClicked();

private:
    QPushButton* m_newBtn;
    QPushButton* m_loadBtn;
    QPushButton* m_saveBtn;
    QPushButton* m_saveAsBtn;
    QPushButton* m_runBtn;
    QPushButton* m_stopBtn;
};
```

**Layout:** Horizontal row with icon buttons
- Height: ~40px fixed
- Buttons: New | Load | Save | Save As | --- | Run Recipe | Stop

---

#### 2.2.2 VisionLeftPanelWidget
**File:** `include/vision/ui/VisionLeftPanelWidget.h/.cpp`

```cpp
class VisionLeftPanelWidget : public QWidget {
    Q_OBJECT
public:
    explicit VisionLeftPanelWidget(QWidget* parent = nullptr);

    void updatePluginList(const QStringList& plugins);
    void displayResult(const VisionResult& result);
    QString getSelectedPlugin() const;

signals:
    void addToNodeClicked();
    void pluginSelected(const QString& pluginName);

private:
    QPushButton* m_addToNodeBtn;
    QTabWidget* m_pluginTabs;      // Tabs for PR, Measurement, Filter
    QListWidget* m_prList;
    QListWidget* m_measureList;
    QListWidget* m_filterList;
    QTextEdit* m_resultsDisplay;   // Read-only results text
};
```

**Layout:** Vertical stack
- Width: 250px fixed
- Top: "Add to Recipe Node" button
- Middle: Tabbed plugin list (PR | Measure | Filter)
- Bottom: Results text box (scrollable, monospace font)

---

#### 2.2.3 VisionImageWidget
**File:** `include/vision/ui/VisionImageWidget.h/.cpp`

```cpp
class VisionImageWidget : public QWidget {
    Q_OBJECT
public:
    explicit VisionImageWidget(QWidget* parent = nullptr);

    void setImage(const cv::Mat& image);
    void setOverlays(const std::vector<OverlayCommand>& overlays);
    void clearOverlays();
    void setROI(const cv::Rect& roi);
    void clearROI();
    void enableCrosshair(bool enable);
    void enableROISelection(bool enable);

signals:
    void captureClicked();
    void clearClicked();
    void roiSelected(const cv::Rect& roi);
    void pointClicked(const cv::Point2d& point);

private:
    QLabel* m_imageLabel;          // Or custom QGraphicsView
    QScrollArea* m_scrollArea;
    QPushButton* m_captureBtn;
    QPushButton* m_clearBtn;
    QCheckBox* m_crosshairToggle;

    cv::Mat m_currentImage;
    std::vector<OverlayCommand> m_overlays;
    bool m_showCrosshair = false;
    bool m_roiSelectionMode = false;
    cv::Rect m_currentROI;
};
```

**Layout:** Vertical
- Main: Image display area (scrollable, maintains aspect ratio)
- Bottom row: [Capture] [Clear] [☑ Crosshair]

---

#### 2.2.4 VisionRightPanelWidget
**File:** `include/vision/ui/VisionRightPanelWidget.h/.cpp`

```cpp
class VisionRightPanelWidget : public QWidget {
    Q_OBJECT
public:
    explicit VisionRightPanelWidget(QWidget* parent = nullptr);

    void setPluginConfigWidget(QWidget* configWidget);
    void clearPluginConfigWidget();

    // Camera settings
    double getExposure() const;
    double getGain() const;
    bool isAutoExposure() const;
    bool isAutoGain() const;

    // Calibration
    double getPixelToMmX() const;
    double getPixelToMmY() const;

signals:
    void exposureChanged(double value);
    void gainChanged(double value);
    void autoExposureToggled(bool enabled);
    void autoGainToggled(bool enabled);
    void calibrationChanged(double xMmPerPx, double yMmPerPx);
    void applyPluginParamsClicked();

private:
    // Camera Settings Group
    QGroupBox* m_cameraGroup;
    QDoubleSpinBox* m_exposureSpin;
    QDoubleSpinBox* m_gainSpin;
    QCheckBox* m_autoExposureCheck;
    QCheckBox* m_autoGainCheck;

    // Calibration Group
    QGroupBox* m_calibrationGroup;
    QDoubleSpinBox* m_pixelToMmXSpin;
    QDoubleSpinBox* m_pixelToMmYSpin;

    // Plugin Parameters Group
    QGroupBox* m_pluginParamsGroup;
    QVBoxLayout* m_pluginParamsLayout;
    QWidget* m_currentPluginConfig;
    QPushButton* m_applyParamsBtn;
};
```

**Layout:** Vertical stack of group boxes
- Width: 300px fixed
- Camera Settings:
  - Exposure: QDoubleSpinBox (µs)
  - Gain: QDoubleSpinBox
  - Auto Exposure: QCheckBox
  - Auto Gain: QCheckBox
- Calibration:
  - Pixel/mm X: QDoubleSpinBox
  - Pixel/mm Y: QDoubleSpinBox
- Plugin Parameters:
  - Dynamic widget (swapped when plugin selected)
  - [Apply] button

---

#### 2.2.5 VisionManagerPage (Container)
**File:** `include/vision/ui/VisionManagerPage.h/.cpp`

```cpp
class VisionManagerPage : public QWidget {
    Q_OBJECT
public:
    explicit VisionManagerPage(ApplicationContext* context, QWidget* parent = nullptr);

private slots:
    void onNewRecipe();
    void onLoadRecipe();
    void onSaveRecipe();
    void onSaveAsRecipe();
    void onRunRecipe();
    void onStopRecipe();
    void onAddToNode();
    void onPluginSelected(const QString& pluginName);
    void onCaptureImage();
    void onClearImage();
    void onApplyParams();
    void onResultReady(const VisionResult& result);

private:
    void setupUI();
    void connectSignals();
    void updatePluginList();
    void loadPluginConfig(const QString& pluginName);

    ApplicationContext* m_context;
    VisionManager* m_visionManager;

    // Child widgets
    VisionToolbarWidget* m_toolbar;
    VisionLeftPanelWidget* m_leftPanel;
    VisionImageWidget* m_imageWidget;
    VisionRightPanelWidget* m_rightPanel;

    QSplitter* m_mainSplitter;
};
```

---

## File Structure

```
include/vision/
├── VisionManager.h
├── VisionManager.cpp
├── IVisionPlugin.h
├── VisionResult.h
├── VisionRecipe.h
├── OpenCVTest.h              (existing)
├── OpenCVTest.cpp            (existing)
│
├── plugins/
│   ├── CircleDetectionPlugin.h/.cpp
│   ├── EdgeDetectionPlugin.h/.cpp
│   ├── TemplateMatchingPlugin.h/.cpp
│   ├── BlobDetectionPlugin.h/.cpp
│   ├── MeasureDistancePlugin.h/.cpp
│   └── ThresholdPlugin.h/.cpp
│
└── ui/
    ├── VisionManagerPage.h/.cpp
    ├── VisionToolbarWidget.h/.cpp
    ├── VisionLeftPanelWidget.h/.cpp
    ├── VisionImageWidget.h/.cpp
    └── VisionRightPanelWidget.h/.cpp
```

---

## CMakeLists.txt Updates

```cmake
# Add vision UI sources
if(OPENCV_FOUND)
    list(APPEND SOURCES
        include/vision/OpenCVTest.cpp
        include/vision/VisionManager.cpp
        include/vision/ui/VisionManagerPage.cpp
        include/vision/ui/VisionToolbarWidget.cpp
        include/vision/ui/VisionLeftPanelWidget.cpp
        include/vision/ui/VisionImageWidget.cpp
        include/vision/ui/VisionRightPanelWidget.cpp
        # Plugins
        include/vision/plugins/CircleDetectionPlugin.cpp
        include/vision/plugins/EdgeDetectionPlugin.cpp
    )
    list(APPEND HEADERS
        include/vision/OpenCVTest.h
        include/vision/VisionManager.h
        include/vision/IVisionPlugin.h
        include/vision/VisionResult.h
        include/vision/ui/VisionManagerPage.h
        include/vision/ui/VisionToolbarWidget.h
        include/vision/ui/VisionLeftPanelWidget.h
        include/vision/ui/VisionImageWidget.h
        include/vision/ui/VisionRightPanelWidget.h
    )
endif()
```

---

## Integration with Existing System

### 1. MainWindow Integration
- Replace "Future Update" button with "Vision Manager"
- Add VisionManagerPage to page stack

### 2. CameraManager Integration
- Capture images from connected cameras
- Apply camera settings (exposure, gain)

### 3. DataStore Integration
- Store vision results for use in sequences
- Key format: "vision.{plugin}.{result_name}"

### 4. SequenceEngine Integration (Future)
- VisionCommandHandler for vision operations in sequences
- Commands: CaptureImage, RunVisionPlugin, GetVisionResult

### 5. Recipe System Integration (Future)
- Vision recipes as process steps
- ProcessPluginRegistry for vision processes

---

## Implementation Order

### Phase 1: Backend (Week 1)
1. ✅ OpenCVTest (Done)
2. Create IVisionPlugin interface
3. Create VisionResult structures
4. Create VisionManager singleton
5. Implement 2 basic plugins (Circle, Edge)
6. Unit tests for core functionality

### Phase 2: UI Mockup (Week 2)
1. Create VisionToolbarWidget (buttons only, no wiring)
2. Create VisionLeftPanelWidget (placeholder lists)
3. Create VisionImageWidget (static image display)
4. Create VisionRightPanelWidget (spinboxes, no camera)
5. Create VisionManagerPage (layout assembly)
6. Wire to "Vision Manager" button in MainWindow

### Phase 3: Wiring (Week 3)
1. Connect toolbar to VisionManager
2. Connect plugin list to plugin registry
3. Connect image widget to camera capture
4. Connect settings to camera manager
5. Connect results display to VisionResult
6. Plugin parameter widget swapping

### Phase 4: Polish (Week 4)
1. ROI selection in image widget
2. Overlay drawing
3. Recipe save/load
4. Additional plugins
5. Error handling and validation

---

## Notes

- All widgets should follow existing dark theme (DarkTheme.h)
- Use consistent spacing: 10px margins, 8px spacing
- Font: Roboto Mono (already loaded)
- Icons: Use existing assets or placeholder text for now
- Error messages should appear in LogManager, not message boxes

---

## Questions for Review

1. Should VisionManager be a singleton or part of ApplicationContext?
2. Should recipes support conditional branching (if detection fails)?
3. Do we need undo/redo for recipe editing?
4. Should plugin config widgets be persistent or recreated each time?
5. Integration priority: SequenceEngine or Recipe system first?
