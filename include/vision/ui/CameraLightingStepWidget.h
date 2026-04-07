// CameraLightingStepWidget.h
// Step 1: Camera & Lighting configuration for vision wizard
#pragma once

#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QGroupBox>
#include <QTimer>
#include <QVBoxLayout>
#include <map>
#include <vector>
#include <string>

struct ApplicationContext;
struct VisionWizardState;
class VisionImageWidget;

namespace Lighting {
    class LightingManager;
    struct ChannelConfig;
}

class CameraLightingStepWidget : public QWidget {
    Q_OBJECT

public:
    explicit CameraLightingStepWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void setState(VisionWizardState* state);

    // Image widget embedding (shared image reparented here)
    void embedImageWidget(VisionImageWidget* imgWidget);
    void removeImageWidget();

    // Camera management
    void loadCameraList();
    void applyCameraSettings();
    void loadCurrentCameraSettings();
    QString getSelectedCameraId() const;
    void selectFirstCamera();

    // Settings get/set (for profile load/save)
    struct CameraSettings {
        QString cameraId;
        int exposureTime    = 1000;
        double gain         = 1.0;
        bool autoExposure   = false;
        bool autoGain       = false;
        double frameRate    = 8.0;
    };
    CameraSettings getCameraSettings() const;
    void setCameraSettings(const CameraSettings& settings);

    // Lighting
    struct LightingSettings {
        bool enabled = false;
        QString controllerId;
        std::map<int, int>  channelIntensities;
        std::map<int, bool> channelStates;
    };
    LightingSettings getLightingSettings() const;
    void setLightingSettings(const LightingSettings& settings);
    void applyLightingSettings();
    bool isLightingEnabled() const;

    // Calibration (read-only, loaded from config)
    double getPixelToMmX() const { return m_pixelToMmX; }
    double getPixelToMmY() const { return m_pixelToMmY; }

signals:
    void cameraSelected(const QString& cameraId);
    void cameraSettingsChanged();
    void lightingSettingsChanged();

private slots:
    void onCameraSelectionChanged(int index);
    void onAutoExposureChanged(Qt::CheckState state);
    void onAutoGainChanged(Qt::CheckState state);
    void onApplyCameraSettingsBtnClicked();
    void onLightingEnabledChanged(Qt::CheckState state);
    void onLightingChannelIntensityChanged(int channel, int intensity);
    void onLightingChannelToggleChanged(int channel, bool on);

private:
    void setupUI();
    void setupCameraSection(QVBoxLayout* col);
    void setupCalibrationSection(QVBoxLayout* col);
    void setupLightingSection(QVBoxLayout* col);
    void loadLightingChannels();
    void loadCalibrationFromConfig();
    void updateExposureLabel(int microseconds);
    void updateGainLabel(double gain);
    void updateFrameRateLabel(double fps);

    ApplicationContext* m_context;
    VisionWizardState*  m_state = nullptr;

    // Image widget placeholder (right side)
    QWidget*     m_imagePlaceholder = nullptr;
    QVBoxLayout* m_imagePlaceholderLayout = nullptr;

    // Camera controls
    QComboBox*      m_cameraSelector       = nullptr;
    QCheckBox*      m_autoExposureCheck    = nullptr;
    QCheckBox*      m_autoGainCheck        = nullptr;
    QSpinBox*       m_exposureSpinBox      = nullptr;
    QLabel*         m_exposureValueLabel   = nullptr;
    QDoubleSpinBox* m_gainSpinBox          = nullptr;
    QLabel*         m_gainValueLabel       = nullptr;
    QDoubleSpinBox* m_frameRateSpinBox     = nullptr;
    QLabel*         m_frameRateValueLabel  = nullptr;
    QPushButton*    m_applyCameraSettingsBtn = nullptr;

    // Calibration (read-only display)
    QLabel* m_pixelToMmXLabel = nullptr;
    QLabel* m_pixelToMmYLabel = nullptr;
    double  m_pixelToMmX = 0.00248;
    double  m_pixelToMmY = 0.00248;

    // Lighting
    QCheckBox*   m_lightingEnabledCheck       = nullptr;
    QWidget*     m_lightingChannelsContainer  = nullptr;
    QVBoxLayout* m_lightingChannelsLayout     = nullptr;
    std::vector<QSlider*>      m_lightingSliders;
    std::vector<QLabel*>       m_lightingValueLabels;
    std::vector<QPushButton*>  m_lightingToggleButtons;
    std::vector<int>           m_lightingChannelNumbers;
    std::string                m_lightingControllerId;
};
