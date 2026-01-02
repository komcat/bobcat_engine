// VisionRightPanelWidget.h
#pragma once

#include <QWidget>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QTabWidget>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPointer>
#include <map>

struct ApplicationContext;
class VisionROI;

namespace Lighting {
    class LightingManager;
    struct ChannelConfig;
}

// Camera settings wrapper for saving/loading with vision recipes
struct VisionCameraSettings {
    QString cameraId;
    int exposureTime = 1000;      // microseconds
    double gain = 1.0;            // 0.0 - 10.0
    bool autoExposure = false;
    bool autoGain = false;
    double pixelToMmX = 0.00248;  // mm per pixel
    double pixelToMmY = 0.00248;
};

// Lighting settings wrapper for saving/loading with vision profiles
struct VisionLightingSettings {
    bool enabled = false;                          // Whether lighting is enabled for this profile
    QString controllerId;                          // Lighting controller ID
    std::map<int, int> channelIntensities;         // channel number -> intensity (0-255)
    std::map<int, bool> channelStates;             // channel number -> on/off state
};

class VisionRightPanelWidget : public QWidget {
    Q_OBJECT

public:
    explicit VisionRightPanelWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void setPluginConfigWidget(QWidget* configWidget);
    void clearPluginConfigWidget();

    // Camera settings
    void loadCameraList();
    void applyCameraSettings();
    void loadCurrentCameraSettings();
    QString getSelectedCameraId() const;
    VisionCameraSettings getCameraSettings() const;
    void setCameraSettings(const VisionCameraSettings& settings);
    void selectFirstCamera();  // Call after signals connected to initialize camera

    // Calibration getters
    double getPixelToMmX() const;
    double getPixelToMmY() const;

    // ROI management
    void setROI(int startX, int startY, int endX, int endY);
    void setROIEnabled(bool enabled);
    bool isROIEnabled() const;

    // Search Box management
    void setSearchBox(int startX, int startY, int endX, int endY);
    void setSearchBoxEnabled(bool enabled);
    bool isSearchBoxEnabled() const;
    void getSearchBox(int& startX, int& startY, int& endX, int& endY) const;

    // Lighting management
    VisionLightingSettings getLightingSettings() const;
    void setLightingSettings(const VisionLightingSettings& settings);
    void applyLightingSettings();
    bool isLightingEnabled() const;
    void setLightingEnabled(bool enabled);

signals:
    void cameraSelected(const QString& cameraId);
    void cameraSettingsChanged();
    void calibrationChanged(double xMmPerPx, double yMmPerPx);
    void applyPluginParamsClicked();
    void executeVisionClicked();
    void roiChanged(int startX, int startY, int endX, int endY);
    void roiEnabledChanged(bool enabled);
    void searchBoxChanged(int startX, int startY, int endX, int endY);
    void searchBoxEnabledChanged(bool enabled);
    void lightingSettingsChanged();

private slots:
    void onCameraSelectionChanged(int index);
    void onExposureSliderChanged(int value);
    void onGainSliderChanged(int value);
    void onAutoExposureChanged(int state);
    void onAutoGainChanged(int state);
    void onROIValueChanged();
    void onROIEnabledChanged(int state);
    void onSearchBoxValueChanged();
    void onSearchBoxEnabledChanged(int state);
    void onLightingEnabledChanged(int state);
    void onLightingChannelIntensityChanged(int channel, int intensity);
    void onLightingChannelToggleChanged(int channel, bool on);

private:
    void setupUI();
    void setupROITab(QWidget* roiTab);
    void setupSearchBoxTab(QWidget* searchBoxTab);
    void setupCameraTab(QWidget* cameraTab);
    void setupPluginTab(QWidget* pluginTab);
    void setupLightingSection(QVBoxLayout* parentLayout);
    void updateExposureLabel(int microseconds);
    void updateGainLabel(double gain);
    void updateROISizeLabel();
    void updateSearchBoxSizeLabel();
    void loadLightingChannels();
    void updateLightingIntensityLabel(int channel, int intensity);

    ApplicationContext* m_context;

    // Execute button (above tabs)
    QPushButton* m_executeBtn;

    // Main tab widget
    QTabWidget* m_tabWidget;

    // Camera Settings Tab
    QComboBox* m_cameraSelector;
    QCheckBox* m_autoExposureCheck;
    QCheckBox* m_autoGainCheck;
    QSlider* m_exposureSlider;
    QLabel* m_exposureValueLabel;
    QSlider* m_gainSlider;
    QLabel* m_gainValueLabel;
    QLabel* m_pixelToMmXLabel;  // Display-only (loaded from config/SYSTEM/val_pixelmm.json)
    QLabel* m_pixelToMmYLabel;  // Display-only (loaded from config/SYSTEM/val_pixelmm.json)

    // Plugin Parameters Tab
    QScrollArea* m_pluginScrollArea;
    QVBoxLayout* m_pluginParamsLayout;
    QPointer<QWidget> m_currentPluginConfig;  // ✅ Use QPointer to prevent use-after-free
    QPushButton* m_applyParamsBtn;

    // ROI Tab
    QCheckBox* m_roiEnabledCheck;
    QSpinBox* m_roiStartXSpin;
    QSpinBox* m_roiStartYSpin;
    QSpinBox* m_roiEndXSpin;
    QSpinBox* m_roiEndYSpin;
    QLabel* m_roiSizeLabel;

    // Search Box Tab
    QCheckBox* m_searchBoxEnabledCheck;
    QSpinBox* m_searchBoxStartXSpin;
    QSpinBox* m_searchBoxStartYSpin;
    QSpinBox* m_searchBoxEndXSpin;
    QSpinBox* m_searchBoxEndYSpin;
    QLabel* m_searchBoxSizeLabel;

    // Lighting Section (in Camera tab)
    QCheckBox* m_lightingEnabledCheck;
    QWidget* m_lightingChannelsContainer;
    QVBoxLayout* m_lightingChannelsLayout;
    std::vector<QSlider*> m_lightingSliders;
    std::vector<QLabel*> m_lightingValueLabels;
    std::vector<QPushButton*> m_lightingToggleButtons;
    std::vector<int> m_lightingChannelNumbers;  // Maps widget index to channel number
    std::string m_lightingControllerId;

    // Calibration values (loaded from config/SYSTEM/val_pixelmm.json)
    double m_pixelToMmX = 0.00248;
    double m_pixelToMmY = 0.00248;
    void loadCalibrationFromConfig();
};
