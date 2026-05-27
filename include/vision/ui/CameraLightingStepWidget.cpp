// CameraLightingStepWidget.cpp
// Step 1: Camera & Lighting configuration for vision wizard
#include "CameraLightingStepWidget.h"
#include "VisionImageWidget.h"
#include "VisionWizardState.h"
#include "ApplicationContext.h"
#include "CameraManager.h"
#include "ICameraHardware.h"
#include "lighting/LightingManager.h"
#include "lighting/LightingTypes.h"
#include <QScrollArea>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QFrame>
#include <QCoreApplication>
#include <QFile>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CameraLightingStepWidget::CameraLightingStepWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
{
    setupUI();
    loadCalibrationFromConfig();
    loadCameraList();
}

void CameraLightingStepWidget::setState(VisionWizardState* state)
{
    m_state = state;
}

// ---------------------------------------------------------------------------
// Image widget embedding
// ---------------------------------------------------------------------------

void CameraLightingStepWidget::embedImageWidget(VisionImageWidget* imgWidget)
{
    if (!imgWidget || !m_imagePlaceholderLayout) return;
    m_imagePlaceholderLayout->addWidget(imgWidget);
    imgWidget->show();
}

void CameraLightingStepWidget::removeImageWidget()
{
    if (!m_imagePlaceholderLayout) return;
    while (m_imagePlaceholderLayout->count() > 0) {
        QLayoutItem* item = m_imagePlaceholderLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->setParent(nullptr);
        }
        delete item;
    }
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void CameraLightingStepWidget::setupUI()
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ---------- Left config panel (scrollable, ~300px) ----------
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFixedWidth(480);

    QWidget* scrollContent = new QWidget();
    QVBoxLayout* configCol = new QVBoxLayout(scrollContent);
    configCol->setContentsMargins(6, 6, 6, 6);
    configCol->setSpacing(8);

    setupCameraSection(configCol);
    setupCalibrationSection(configCol);
    setupLightingSection(configCol);
    configCol->addStretch();

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea);

    // ---------- Right image placeholder (flex) ----------
    m_imagePlaceholder = new QWidget(this);
    m_imagePlaceholderLayout = new QVBoxLayout(m_imagePlaceholder);
    m_imagePlaceholderLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* hint = new QLabel("Camera preview will appear here\nwhen Image Mode is set to Live");
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet("color: #666; font-size: 13px; font-style: italic;");
    m_imagePlaceholderLayout->addWidget(hint);

    mainLayout->addWidget(m_imagePlaceholder, 1);
}

void CameraLightingStepWidget::setupCameraSection(QVBoxLayout* col)
{
    static const char* gs =
        "QGroupBox { color: #CCCCCC; border: 1px solid #444; border-radius: 3px; "
        "margin-top: 8px; padding: 4px 4px 2px 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 6px; font-weight: bold; font-size: 11px; }";

    // ---- Camera selector ----
    QGroupBox* selGroup = new QGroupBox("Camera");
    selGroup->setStyleSheet(gs);
    QVBoxLayout* selLayout = new QVBoxLayout(selGroup);
    selLayout->setContentsMargins(4, 2, 4, 2);
    selLayout->setSpacing(2);

    m_cameraSelector = new QComboBox();
    m_cameraSelector->setToolTip("Select camera to control");
    connect(m_cameraSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraLightingStepWidget::onCameraSelectionChanged);
    selLayout->addWidget(m_cameraSelector);
    col->addWidget(selGroup);

    // ---- Camera settings (compact grid) ----
    QGroupBox* camGroup = new QGroupBox("Settings");
    camGroup->setStyleSheet(gs);
    QVBoxLayout* camLayout = new QVBoxLayout(camGroup);
    camLayout->setContentsMargins(4, 2, 4, 2);
    camLayout->setSpacing(3);

    // Auto checkboxes in a row
    QHBoxLayout* autoRow = new QHBoxLayout();
    autoRow->setSpacing(10);
    m_autoExposureCheck = new QCheckBox("Auto Exp");
    connect(m_autoExposureCheck, &QCheckBox::checkStateChanged,
            this, &CameraLightingStepWidget::onAutoExposureChanged);
    m_autoGainCheck = new QCheckBox("Auto Gain");
    connect(m_autoGainCheck, &QCheckBox::checkStateChanged,
            this, &CameraLightingStepWidget::onAutoGainChanged);
    autoRow->addWidget(m_autoExposureCheck);
    autoRow->addWidget(m_autoGainCheck);
    autoRow->addStretch();
    camLayout->addLayout(autoRow);

    // Grid: Exposure | Gain | FPS in two columns
    QGridLayout* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(3);

    // Row 0: Exposure
    grid->addWidget(new QLabel("Exp:"), 0, 0);
    m_exposureSpinBox = new QSpinBox();
    m_exposureSpinBox->setRange(1, 1000000);
    m_exposureSpinBox->setSingleStep(50);
    m_exposureSpinBox->setValue(1000);
    m_exposureSpinBox->setSuffix(" us");
    m_exposureSpinBox->setMaximumWidth(100);
    connect(m_exposureSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) { updateExposureLabel(v); });
    grid->addWidget(m_exposureSpinBox, 0, 1);
    m_exposureValueLabel = new QLabel("1.00 ms");
    m_exposureValueLabel->setStyleSheet("color: #888; font-size: 10px;");
    m_exposureValueLabel->setMaximumWidth(50);
    grid->addWidget(m_exposureValueLabel, 0, 2);

    // Row 1: Gain
    grid->addWidget(new QLabel("Gain:"), 1, 0);
    m_gainSpinBox = new QDoubleSpinBox();
    m_gainSpinBox->setRange(0.0, 24.0);
    m_gainSpinBox->setValue(1.0);
    m_gainSpinBox->setSingleStep(0.1);
    m_gainSpinBox->setDecimals(1);
    m_gainSpinBox->setMaximumWidth(100);
    connect(m_gainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { updateGainLabel(v); });
    grid->addWidget(m_gainSpinBox, 1, 1);
    m_gainValueLabel = new QLabel("1.0");
    m_gainValueLabel->setStyleSheet("color: #888; font-size: 10px;");
    m_gainValueLabel->setMaximumWidth(50);
    grid->addWidget(m_gainValueLabel, 1, 2);

    // Row 2: Frame Rate
    grid->addWidget(new QLabel("FPS:"), 2, 0);
    m_frameRateSpinBox = new QDoubleSpinBox();
    m_frameRateSpinBox->setRange(0.1, 30.0);
    m_frameRateSpinBox->setValue(8.0);
    m_frameRateSpinBox->setSingleStep(0.5);
    m_frameRateSpinBox->setDecimals(1);
    m_frameRateSpinBox->setSuffix(" fps");
    m_frameRateSpinBox->setMaximumWidth(100);
    connect(m_frameRateSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { updateFrameRateLabel(v); });
    grid->addWidget(m_frameRateSpinBox, 2, 1);
    m_frameRateValueLabel = new QLabel("8.0");
    m_frameRateValueLabel->setStyleSheet("color: #888; font-size: 10px;");
    m_frameRateValueLabel->setMaximumWidth(50);
    grid->addWidget(m_frameRateValueLabel, 2, 2);

    camLayout->addLayout(grid);

    // Apply button
    m_applyCameraSettingsBtn = new QPushButton("Apply Camera Settings");
    m_applyCameraSettingsBtn->setStyleSheet(
        "QPushButton { background-color: #1565C0; color: white; font-weight: bold; padding: 6px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #1976D2; }"
        "QPushButton:pressed { background-color: #0D47A1; }"
    );
    connect(m_applyCameraSettingsBtn, &QPushButton::clicked,
            this, &CameraLightingStepWidget::onApplyCameraSettingsBtnClicked);
    camLayout->addWidget(m_applyCameraSettingsBtn);

    col->addWidget(camGroup);
}

void CameraLightingStepWidget::setupCalibrationSection(QVBoxLayout* col)
{
    static const char* gs =
        "QGroupBox { color: #CCCCCC; border: 1px solid #444; border-radius: 3px; "
        "margin-top: 8px; padding: 4px 4px 2px 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 6px; font-weight: bold; font-size: 11px; }";

    QGroupBox* calGroup = new QGroupBox("Calibration");
    calGroup->setStyleSheet(gs);
    QGridLayout* calGrid = new QGridLayout(calGroup);
    calGrid->setContentsMargins(4, 2, 4, 2);
    calGrid->setSpacing(2);

    calGrid->addWidget(new QLabel("X:"), 0, 0);
    m_pixelToMmXLabel = new QLabel("0.00248 mm/px");
    m_pixelToMmXLabel->setStyleSheet("color: #d4d4d4; font-family: monospace; font-size: 10px;");
    calGrid->addWidget(m_pixelToMmXLabel, 0, 1);

    calGrid->addWidget(new QLabel("Y:"), 1, 0);
    m_pixelToMmYLabel = new QLabel("0.00248 mm/px");
    m_pixelToMmYLabel->setStyleSheet("color: #d4d4d4; font-family: monospace; font-size: 10px;");
    calGrid->addWidget(m_pixelToMmYLabel, 1, 1);

    col->addWidget(calGroup);
}

void CameraLightingStepWidget::setupLightingSection(QVBoxLayout* col)
{
    static const char* gs =
        "QGroupBox { color: #CCCCCC; border: 1px solid #444; border-radius: 3px; "
        "margin-top: 8px; padding: 4px 4px 2px 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 6px; font-weight: bold; font-size: 11px; }";

    QGroupBox* lightingGroup = new QGroupBox("Lighting");
    lightingGroup->setStyleSheet(gs);
    QVBoxLayout* lightingLayout = new QVBoxLayout(lightingGroup);
    lightingLayout->setContentsMargins(4, 2, 4, 2);
    lightingLayout->setSpacing(3);

    m_lightingEnabledCheck = new QCheckBox("Enable Lighting for this Profile");
    m_lightingEnabledCheck->setToolTip(
        "When enabled, lighting settings will be saved with the vision profile "
        "and applied when executing");
    connect(m_lightingEnabledCheck, &QCheckBox::checkStateChanged,
            this, &CameraLightingStepWidget::onLightingEnabledChanged);
    lightingLayout->addWidget(m_lightingEnabledCheck);

    m_lightingChannelsContainer = new QWidget();
    m_lightingChannelsLayout = new QVBoxLayout(m_lightingChannelsContainer);
    m_lightingChannelsLayout->setContentsMargins(0, 5, 0, 0);
    m_lightingChannelsLayout->setSpacing(5);

    loadLightingChannels();

    lightingLayout->addWidget(m_lightingChannelsContainer);
    col->addWidget(lightingGroup);
}

// ---------------------------------------------------------------------------
// Camera list & settings
// ---------------------------------------------------------------------------

void CameraLightingStepWidget::loadCameraList()
{
    if (!m_context || !m_context->cameraManager) {
        std::cerr << "[CameraLightingStepWidget] CameraManager not available" << std::endl;
        if (m_cameraSelector) {
            m_cameraSelector->addItem("(No cameras available)");
        }
        return;
    }

    m_cameraSelector->blockSignals(true);
    m_cameraSelector->clear();

    auto cameraIds = m_context->cameraManager->GetCameraIds();

    if (cameraIds.empty()) {
        m_cameraSelector->addItem("(No cameras configured)");
    } else {
        for (const auto& id : cameraIds) {
            m_cameraSelector->addItem(QString::fromStdString(id));
        }
    }

    m_cameraSelector->blockSignals(false);

    if (!cameraIds.empty()) {
        loadCurrentCameraSettings();
        // NOTE: Don't emit signal here — too early (signals may not be connected yet).
        // Caller should invoke selectFirstCamera() after connecting signals.
    }

    std::cout << "[CameraLightingStepWidget] Loaded " << cameraIds.size() << " cameras" << std::endl;
}

void CameraLightingStepWidget::onCameraSelectionChanged(int index)
{
    if (index < 0) return;

    QString cameraId = m_cameraSelector->currentText();
    if (cameraId.startsWith("(")) return;  // Placeholder text

    std::cout << "[CameraLightingStepWidget] Camera selected: " << cameraId.toStdString() << std::endl;

    loadCurrentCameraSettings();
    emit cameraSelected(cameraId);
}

void CameraLightingStepWidget::applyCameraSettings()
{
    if (!m_context || !m_context->cameraManager) {
        std::cerr << "[CameraLightingStepWidget] CameraManager not available" << std::endl;
        return;
    }

    QString cameraId = getSelectedCameraId();
    if (cameraId.isEmpty()) {
        std::cerr << "[CameraLightingStepWidget] No camera selected" << std::endl;
        return;
    }

    ICameraHardware::ExposureSettings settings;
    settings.auto_exposure = m_autoExposureCheck->isChecked();
    settings.auto_gain     = m_autoGainCheck->isChecked();
    settings.exposure_time = m_exposureSpinBox  ? m_exposureSpinBox->value()  : 1000;
    settings.gain          = m_gainSpinBox      ? m_gainSpinBox->value()      : 1.0;
    settings.frame_rate    = m_frameRateSpinBox ? m_frameRateSpinBox->value() : 8.0;

    std::string cameraIdStr = cameraId.toStdString();

    if (m_context->cameraManager->ApplyExposureSettings(cameraIdStr, settings)) {
        std::cout << "[CameraLightingStepWidget] Camera settings applied: "
                  << "Exposure=" << settings.exposure_time << "us, "
                  << "Gain=" << settings.gain << ", "
                  << "FrameRate=" << settings.frame_rate << "fps" << std::endl;
    } else {
        std::cerr << "[CameraLightingStepWidget] Failed to apply camera settings" << std::endl;
    }
}

void CameraLightingStepWidget::loadCurrentCameraSettings()
{
    if (!m_context || !m_context->cameraManager) return;

    QString cameraId = getSelectedCameraId();
    if (cameraId.isEmpty()) return;

    std::string cameraIdStr = cameraId.toStdString();

    auto cameraStatus = m_context->cameraManager->GetCameraStatus(cameraIdStr);
    if (!cameraStatus.connected) {
        std::cout << "[CameraLightingStepWidget] Camera not connected, using default values" << std::endl;
        return;
    }

    auto settings = m_context->cameraManager->GetExposureSettings(cameraIdStr);

    // Block signals while loading
    m_autoExposureCheck->blockSignals(true);
    m_autoGainCheck->blockSignals(true);
    if (m_exposureSpinBox)  m_exposureSpinBox->blockSignals(true);
    if (m_gainSpinBox)      m_gainSpinBox->blockSignals(true);
    if (m_frameRateSpinBox) m_frameRateSpinBox->blockSignals(true);

    m_autoExposureCheck->setChecked(settings.auto_exposure);
    m_autoGainCheck->setChecked(settings.auto_gain);

    int exposureValue = static_cast<int>(settings.exposure_time);
    if (exposureValue < 1)       exposureValue = 1;
    if (exposureValue > 1000000) exposureValue = 1000000;

    if (m_exposureSpinBox)  m_exposureSpinBox->setValue(exposureValue);
    if (m_gainSpinBox)      m_gainSpinBox->setValue(settings.gain);
    if (m_frameRateSpinBox) m_frameRateSpinBox->setValue(settings.frame_rate);

    updateExposureLabel(exposureValue);
    updateGainLabel(settings.gain);
    updateFrameRateLabel(settings.frame_rate);

    if (m_exposureSpinBox) m_exposureSpinBox->setEnabled(!settings.auto_exposure);
    if (m_gainSpinBox)     m_gainSpinBox->setEnabled(!settings.auto_gain);

    // Unblock signals
    m_autoExposureCheck->blockSignals(false);
    m_autoGainCheck->blockSignals(false);
    if (m_exposureSpinBox)  m_exposureSpinBox->blockSignals(false);
    if (m_gainSpinBox)      m_gainSpinBox->blockSignals(false);
    if (m_frameRateSpinBox) m_frameRateSpinBox->blockSignals(false);

    std::cout << "[CameraLightingStepWidget] Loaded camera settings from " << cameraIdStr << std::endl;
}

QString CameraLightingStepWidget::getSelectedCameraId() const
{
    if (!m_cameraSelector || m_cameraSelector->currentIndex() < 0) {
        return QString();
    }

    QString text = m_cameraSelector->currentText();
    if (text.startsWith("(")) {
        return QString();  // Placeholder text
    }

    return text;
}

void CameraLightingStepWidget::selectFirstCamera()
{
    QString currentCamera = getSelectedCameraId();
    if (!currentCamera.isEmpty()) {
        std::cout << "[CameraLightingStepWidget] Selecting initial camera: "
                  << currentCamera.toStdString() << std::endl;
        emit cameraSelected(currentCamera);
    }
}

// ---------------------------------------------------------------------------
// Settings get/set
// ---------------------------------------------------------------------------

CameraLightingStepWidget::CameraSettings CameraLightingStepWidget::getCameraSettings() const
{
    CameraSettings s;
    s.cameraId    = getSelectedCameraId();
    s.exposureTime = m_exposureSpinBox  ? m_exposureSpinBox->value()  : 1000;
    s.gain         = m_gainSpinBox      ? m_gainSpinBox->value()      : 1.0;
    s.autoExposure = m_autoExposureCheck ? m_autoExposureCheck->isChecked() : false;
    s.autoGain     = m_autoGainCheck     ? m_autoGainCheck->isChecked()     : false;
    s.frameRate    = m_frameRateSpinBox ? m_frameRateSpinBox->value() : 8.0;
    return s;
}

void CameraLightingStepWidget::setCameraSettings(const CameraSettings& settings)
{
    m_cameraSelector->blockSignals(true);
    m_autoExposureCheck->blockSignals(true);
    m_autoGainCheck->blockSignals(true);
    if (m_exposureSpinBox)  m_exposureSpinBox->blockSignals(true);
    if (m_gainSpinBox)      m_gainSpinBox->blockSignals(true);
    if (m_frameRateSpinBox) m_frameRateSpinBox->blockSignals(true);

    int cameraIndex = m_cameraSelector->findText(settings.cameraId);
    if (cameraIndex >= 0) {
        m_cameraSelector->setCurrentIndex(cameraIndex);
    }

    m_autoExposureCheck->setChecked(settings.autoExposure);
    m_autoGainCheck->setChecked(settings.autoGain);

    if (m_exposureSpinBox)  m_exposureSpinBox->setValue(settings.exposureTime);
    if (m_gainSpinBox)      m_gainSpinBox->setValue(settings.gain);
    if (m_frameRateSpinBox) m_frameRateSpinBox->setValue(settings.frameRate);

    updateExposureLabel(settings.exposureTime);
    updateGainLabel(settings.gain);
    updateFrameRateLabel(settings.frameRate);

    if (m_exposureSpinBox) m_exposureSpinBox->setEnabled(!settings.autoExposure);
    if (m_gainSpinBox)     m_gainSpinBox->setEnabled(!settings.autoGain);

    // Refresh calibration labels (values come from JSON, not from settings)
    if (m_pixelToMmXLabel) {
        m_pixelToMmXLabel->setText(QString("%1 mm/px").arg(m_pixelToMmX, 0, 'f', 5));
    }
    if (m_pixelToMmYLabel) {
        m_pixelToMmYLabel->setText(QString("%1 mm/px").arg(m_pixelToMmY, 0, 'f', 5));
    }

    m_cameraSelector->blockSignals(false);
    m_autoExposureCheck->blockSignals(false);
    m_autoGainCheck->blockSignals(false);
    if (m_exposureSpinBox)  m_exposureSpinBox->blockSignals(false);
    if (m_gainSpinBox)      m_gainSpinBox->blockSignals(false);
    if (m_frameRateSpinBox) m_frameRateSpinBox->blockSignals(false);

    std::cout << "[CameraLightingStepWidget] Camera settings restored: "
              << "Camera=" << settings.cameraId.toStdString() << ", "
              << "Exposure=" << settings.exposureTime << "us, "
              << "Gain=" << settings.gain << ", "
              << "FrameRate=" << settings.frameRate << "fps" << std::endl;
}

// ---------------------------------------------------------------------------
// Camera slot handlers
// ---------------------------------------------------------------------------

void CameraLightingStepWidget::onAutoExposureChanged(Qt::CheckState state)
{
    bool autoEnabled = (state == Qt::Checked);
    if (m_exposureSpinBox) {
        m_exposureSpinBox->setEnabled(!autoEnabled);
    }
    applyCameraSettings();
    emit cameraSettingsChanged();
}

void CameraLightingStepWidget::onAutoGainChanged(Qt::CheckState state)
{
    bool autoEnabled = (state == Qt::Checked);
    if (m_gainSpinBox) {
        m_gainSpinBox->setEnabled(!autoEnabled);
    }
    applyCameraSettings();
    emit cameraSettingsChanged();
}

void CameraLightingStepWidget::onApplyCameraSettingsBtnClicked()
{
    applyCameraSettings();
    emit cameraSettingsChanged();
    std::cout << "[CameraLightingStepWidget] Camera settings applied via button" << std::endl;
}

// ---------------------------------------------------------------------------
// Label helpers
// ---------------------------------------------------------------------------

void CameraLightingStepWidget::updateExposureLabel(int microseconds)
{
    if (!m_exposureValueLabel) return;
    if (microseconds >= 1000) {
        m_exposureValueLabel->setText(QString("= %1 ms").arg(microseconds / 1000.0, 0, 'f', 2));
    } else {
        m_exposureValueLabel->setText(QString("%1 us").arg(microseconds));
    }
}

void CameraLightingStepWidget::updateGainLabel(double gain)
{
    if (m_gainValueLabel) {
        m_gainValueLabel->setText(QString("%1").arg(gain, 0, 'f', 1));
    }
}

void CameraLightingStepWidget::updateFrameRateLabel(double fps)
{
    if (m_frameRateValueLabel) {
        m_frameRateValueLabel->setText(QString("%1").arg(fps, 0, 'f', 1));
    }
}

// ---------------------------------------------------------------------------
// Calibration loading
// ---------------------------------------------------------------------------

void CameraLightingStepWidget::loadCalibrationFromConfig()
{
    QString appDir    = QCoreApplication::applicationDirPath();
    QString configPath = appDir + "/config/SYSTEM/val_pixelmm.json";

    if (QFile::exists(configPath)) {
        try {
            std::ifstream file(configPath.toStdString());
            if (file.is_open()) {
                nlohmann::json j;
                file >> j;

                if (j.contains("calibration")) {
                    const auto& cal = j["calibration"];
                    if (cal.contains("pixel_to_mm_x")) {
                        m_pixelToMmX = cal["pixel_to_mm_x"].get<double>();
                    }
                    if (cal.contains("pixel_to_mm_y")) {
                        m_pixelToMmY = cal["pixel_to_mm_y"].get<double>();
                    }
                }

                file.close();
                std::cout << "[CameraLightingStepWidget] Loaded calibration: "
                          << "X=" << m_pixelToMmX << " mm/px, "
                          << "Y=" << m_pixelToMmY << " mm/px" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[CameraLightingStepWidget] Error loading calibration JSON: "
                      << e.what() << std::endl;
        }
    } else {
        std::cout << "[CameraLightingStepWidget] Calibration config not found at: "
                  << configPath.toStdString() << ", using defaults" << std::endl;
    }

    if (m_pixelToMmXLabel) {
        m_pixelToMmXLabel->setText(QString("%1 mm/px").arg(m_pixelToMmX, 0, 'f', 5));
    }
    if (m_pixelToMmYLabel) {
        m_pixelToMmYLabel->setText(QString("%1 mm/px").arg(m_pixelToMmY, 0, 'f', 5));
    }
}

// ---------------------------------------------------------------------------
// Lighting channels loading
// ---------------------------------------------------------------------------

void CameraLightingStepWidget::loadLightingChannels()
{
    // Clear existing widgets and tracking arrays
    m_lightingSliders.clear();
    m_lightingValueLabels.clear();
    m_lightingToggleButtons.clear();
    m_lightingChannelNumbers.clear();

    while (m_lightingChannelsLayout->count() > 0) {
        QLayoutItem* item = m_lightingChannelsLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    if (!m_context || !m_context->lightingManager) {
        QLabel* noLightingLabel = new QLabel("No lighting controller configured");
        noLightingLabel->setStyleSheet("color: #888888; font-style: italic;");
        m_lightingChannelsLayout->addWidget(noLightingLabel);
        return;
    }

    auto controllerIds = m_context->lightingManager->GetControllerIds();
    if (controllerIds.empty()) {
        QLabel* noLightingLabel = new QLabel("No lighting controller configured");
        noLightingLabel->setStyleSheet("color: #888888; font-style: italic;");
        m_lightingChannelsLayout->addWidget(noLightingLabel);
        return;
    }

    // Use the first controller
    m_lightingControllerId = controllerIds[0];
    auto channels = m_context->lightingManager->GetChannelConfigs(m_lightingControllerId);

    // Keep only enabled channels
    std::vector<Lighting::ChannelConfig> enabledChannels;
    for (const auto& ch : channels) {
        if (ch.enabled) {
            enabledChannels.push_back(ch);
        }
    }

    if (enabledChannels.empty()) {
        QLabel* noChannelsLabel = new QLabel("No enabled lighting channels");
        noChannelsLabel->setStyleSheet("color: #888888; font-style: italic;");
        m_lightingChannelsLayout->addWidget(noChannelsLabel);
        return;
    }

    for (size_t i = 0; i < enabledChannels.size(); ++i) {
        const auto& ch = enabledChannels[i];

        QWidget* channelWidget = new QWidget();
        QHBoxLayout* channelLayout = new QHBoxLayout(channelWidget);
        channelLayout->setContentsMargins(0, 1, 0, 1);
        channelLayout->setSpacing(2);

        // Channel label (compact: L1, L2, ...)
        QString labelText = QString("L%1").arg(ch.channelNumber + 1);
        QLabel* channelLabel = new QLabel(labelText);
        channelLabel->setFixedWidth(20);
        channelLabel->setStyleSheet("font-size: 10px;");
        channelLayout->addWidget(channelLabel);

        // Intensity slider (flex)
        QSlider* slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 255);
        slider->setValue(0);
        slider->setProperty("channelIndex", static_cast<int>(i));
        connect(slider, &QSlider::valueChanged, this, [this, i](int value) {
            if (i < m_lightingChannelNumbers.size()) {
                onLightingChannelIntensityChanged(m_lightingChannelNumbers[i], value);
            }
        });
        channelLayout->addWidget(slider, 1);
        m_lightingSliders.push_back(slider);

        // Intensity value label
        QLabel* valueLabel = new QLabel("0");
        valueLabel->setFixedWidth(22);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueLabel->setStyleSheet("font-size: 10px; color: #00aaff;");
        channelLayout->addWidget(valueLabel);
        m_lightingValueLabels.push_back(valueLabel);

        // Toggle button (ON / OFF)
        QPushButton* toggleBtn = new QPushButton("OFF");
        toggleBtn->setFixedSize(30, 18);
        toggleBtn->setCheckable(true);
        toggleBtn->setStyleSheet(
            "QPushButton { background-color: #555; color: #aaa; font-size: 9px; font-weight: bold; border: none; border-radius: 3px; }"
            "QPushButton:checked { background-color: #00aa00; color: white; }"
        );
        connect(toggleBtn, &QPushButton::clicked, this, [this, i](bool checked) {
            if (i < m_lightingChannelNumbers.size()) {
                onLightingChannelToggleChanged(m_lightingChannelNumbers[i], checked);
            }
        });
        channelLayout->addWidget(toggleBtn);
        m_lightingToggleButtons.push_back(toggleBtn);

        m_lightingChannelNumbers.push_back(ch.channelNumber);
        m_lightingChannelsLayout->addWidget(channelWidget);

        // Separator between channels (skip after last)
        if (i < enabledChannels.size() - 1) {
            QFrame* line = new QFrame();
            line->setFrameShape(QFrame::HLine);
            line->setStyleSheet("background-color: #3a3a3a;");
            line->setFixedHeight(1);
            m_lightingChannelsLayout->addWidget(line);
        }
    }

    std::cout << "[CameraLightingStepWidget] Loaded " << enabledChannels.size()
              << " lighting channels" << std::endl;
}

// ---------------------------------------------------------------------------
// Lighting slot handlers
// ---------------------------------------------------------------------------

void CameraLightingStepWidget::onLightingEnabledChanged(Qt::CheckState state)
{
    bool enabled = (state == Qt::Checked);
    if (m_lightingChannelsContainer) {
        m_lightingChannelsContainer->setEnabled(enabled);
    }
    std::cout << "[CameraLightingStepWidget] Lighting " << (enabled ? "enabled" : "disabled") << std::endl;
    emit lightingSettingsChanged();
}

void CameraLightingStepWidget::onLightingChannelIntensityChanged(int channel, int intensity)
{
    // Update value label
    for (size_t i = 0; i < m_lightingChannelNumbers.size(); ++i) {
        if (m_lightingChannelNumbers[i] == channel && i < m_lightingValueLabels.size()) {
            m_lightingValueLabels[i]->setText(QString::number(intensity));
            break;
        }
    }

    // Push to hardware if lighting is enabled
    if (m_lightingEnabledCheck && m_lightingEnabledCheck->isChecked()) {
        if (m_context && m_context->lightingManager && !m_lightingControllerId.empty()) {
            m_context->lightingManager->SetIntensity(m_lightingControllerId, channel, intensity);
        }
    }

    emit lightingSettingsChanged();
}

void CameraLightingStepWidget::onLightingChannelToggleChanged(int channel, bool on)
{
    // Update button text
    for (size_t i = 0; i < m_lightingChannelNumbers.size(); ++i) {
        if (m_lightingChannelNumbers[i] == channel && i < m_lightingToggleButtons.size()) {
            m_lightingToggleButtons[i]->setText(on ? "ON" : "OFF");
            break;
        }
    }

    // Push to hardware if lighting is enabled
    if (m_lightingEnabledCheck && m_lightingEnabledCheck->isChecked()) {
        if (m_context && m_context->lightingManager && !m_lightingControllerId.empty()) {
            m_context->lightingManager->SetOn(m_lightingControllerId, channel, on);
        }
    }

    emit lightingSettingsChanged();
}

// ---------------------------------------------------------------------------
// Lighting settings get/set/apply
// ---------------------------------------------------------------------------

CameraLightingStepWidget::LightingSettings CameraLightingStepWidget::getLightingSettings() const
{
    LightingSettings s;
    s.enabled      = m_lightingEnabledCheck ? m_lightingEnabledCheck->isChecked() : false;
    s.controllerId = QString::fromStdString(m_lightingControllerId);

    for (size_t i = 0; i < m_lightingChannelNumbers.size(); ++i) {
        int channel = m_lightingChannelNumbers[i];
        if (i < m_lightingSliders.size()) {
            s.channelIntensities[channel] = m_lightingSliders[i]->value();
        }
        if (i < m_lightingToggleButtons.size()) {
            s.channelStates[channel] = m_lightingToggleButtons[i]->isChecked();
        }
    }

    return s;
}

void CameraLightingStepWidget::setLightingSettings(const LightingSettings& settings)
{
    if (m_lightingEnabledCheck) {
        m_lightingEnabledCheck->blockSignals(true);
        m_lightingEnabledCheck->setChecked(settings.enabled);
        m_lightingEnabledCheck->blockSignals(false);
    }

    for (size_t i = 0; i < m_lightingChannelNumbers.size(); ++i) {
        int channel = m_lightingChannelNumbers[i];

        auto intensityIt = settings.channelIntensities.find(channel);
        if (intensityIt != settings.channelIntensities.end() && i < m_lightingSliders.size()) {
            m_lightingSliders[i]->blockSignals(true);
            m_lightingSliders[i]->setValue(intensityIt->second);
            m_lightingSliders[i]->blockSignals(false);
            if (i < m_lightingValueLabels.size()) {
                m_lightingValueLabels[i]->setText(QString::number(intensityIt->second));
            }
        }

        auto stateIt = settings.channelStates.find(channel);
        if (stateIt != settings.channelStates.end() && i < m_lightingToggleButtons.size()) {
            m_lightingToggleButtons[i]->blockSignals(true);
            m_lightingToggleButtons[i]->setChecked(stateIt->second);
            m_lightingToggleButtons[i]->setText(stateIt->second ? "ON" : "OFF");
            m_lightingToggleButtons[i]->blockSignals(false);
        }
    }

    if (m_lightingChannelsContainer) {
        m_lightingChannelsContainer->setEnabled(settings.enabled);
    }

    std::cout << "[CameraLightingStepWidget] Lighting settings restored: enabled=" << settings.enabled
              << ", channels=" << settings.channelIntensities.size() << std::endl;
}

void CameraLightingStepWidget::applyLightingSettings()
{
    if (!m_lightingEnabledCheck || !m_lightingEnabledCheck->isChecked()) {
        std::cout << "[CameraLightingStepWidget] Lighting not enabled, skipping apply" << std::endl;
        return;
    }

    if (!m_context || !m_context->lightingManager || m_lightingControllerId.empty()) {
        std::cerr << "[CameraLightingStepWidget] LightingManager not available" << std::endl;
        return;
    }

    for (size_t i = 0; i < m_lightingChannelNumbers.size(); ++i) {
        int channel = m_lightingChannelNumbers[i];

        if (i < m_lightingSliders.size()) {
            int intensity = m_lightingSliders[i]->value();
            m_context->lightingManager->SetIntensity(m_lightingControllerId, channel, intensity);
        }

        if (i < m_lightingToggleButtons.size()) {
            bool isOn = m_lightingToggleButtons[i]->isChecked();
            m_context->lightingManager->SetOn(m_lightingControllerId, channel, isOn);
        }
    }

    std::cout << "[CameraLightingStepWidget] Applied lighting settings to hardware" << std::endl;
}

bool CameraLightingStepWidget::isLightingEnabled() const
{
    return m_lightingEnabledCheck ? m_lightingEnabledCheck->isChecked() : false;
}
