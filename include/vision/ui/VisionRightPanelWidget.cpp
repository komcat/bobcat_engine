// VisionRightPanelWidget.cpp
#include "VisionRightPanelWidget.h"
#include "ApplicationContext.h"
#include "CameraManager.h"
#include "ICameraHardware.h"
#include "lighting/LightingManager.h"
#include "lighting/LightingTypes.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCoreApplication>
#include <QFile>
#include <QFrame>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

// Helper function to load UI throttle delay from config
static int loadUIThrottleDelay() {
    const int DEFAULT_THROTTLE_MS = 350;
    try {
        QFile file("config/SYSTEM/systemdelay.json");
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            nlohmann::json config = nlohmann::json::parse(data.constData());
            if (config.contains("ui_throttle_ms")) {
                return config["ui_throttle_ms"].get<int>();
            }
        }
    } catch (...) {}
    return DEFAULT_THROTTLE_MS;
}

VisionRightPanelWidget::VisionRightPanelWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_executeBtn(nullptr)
    , m_tabWidget(nullptr)
    , m_cameraSelector(nullptr)
    , m_autoExposureCheck(nullptr)
    , m_autoGainCheck(nullptr)
    , m_exposureSpinBox(nullptr)
    , m_exposureValueLabel(nullptr)
    , m_gainSpinBox(nullptr)
    , m_gainValueLabel(nullptr)
    , m_frameRateSpinBox(nullptr)
    , m_frameRateValueLabel(nullptr)
    , m_applyCameraSettingsBtn(nullptr)
    , m_pixelToMmXLabel(nullptr)
    , m_pixelToMmYLabel(nullptr)
    , m_pluginScrollArea(nullptr)
    , m_pluginParamsLayout(nullptr)
    , m_currentPluginConfig(nullptr)
    , m_applyParamsBtn(nullptr)
    , m_roiEnabledCheck(nullptr)
    , m_roiStartXSpin(nullptr)
    , m_roiStartYSpin(nullptr)
    , m_roiEndXSpin(nullptr)
    , m_roiEndYSpin(nullptr)
    , m_roiSizeLabel(nullptr)
    , m_lightingEnabledCheck(nullptr)
    , m_lightingChannelsContainer(nullptr)
    , m_lightingChannelsLayout(nullptr)
{
    setupUI();

    // Setup throttle timer for settings apply (500ms single-shot)
    m_applySettingsTimer = new QTimer(this);
    m_applySettingsTimer->setSingleShot(true);
    connect(m_applySettingsTimer, &QTimer::timeout, this, &VisionRightPanelWidget::onApplySettingsTimer);

    loadCalibrationFromConfig();  // Load calibration values from JSON
    loadCameraList();
}

void VisionRightPanelWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 0);
    mainLayout->setSpacing(5);

    // Execute Vision Profile button at the top (above tabs)
    m_executeBtn = new QPushButton("Execute Vision Profile", this);
    m_executeBtn->setToolTip("Execute selected plugin with current settings");
    m_executeBtn->setStyleSheet(
        "QPushButton { "
        "   background-color: #2E7D32; "
        "   color: white; "
        "   font-weight: bold; "
        "   font-size: 14px; "
        "   padding: 10px; "
        "   border-radius: 5px; "
        "} "
        "QPushButton:hover { background-color: #388E3C; } "
        "QPushButton:pressed { background-color: #1B5E20; }"
    );
    connect(m_executeBtn, &QPushButton::clicked, this, &VisionRightPanelWidget::executeVisionClicked);
    mainLayout->addWidget(m_executeBtn);

    // Create main tab widget
    m_tabWidget = new QTabWidget(this);

    // Tab 1: ROI Settings (before Camera)
    QWidget* roiTab = new QWidget();
    setupROITab(roiTab);
    m_tabWidget->addTab(roiTab, "ROI");

    // Tab 2: Search Box Settings
    QWidget* searchBoxTab = new QWidget();
    setupSearchBoxTab(searchBoxTab);
    m_tabWidget->addTab(searchBoxTab, "Search");

    // Tab 3: Camera Settings
    QWidget* cameraTab = new QWidget();
    setupCameraTab(cameraTab);
    m_tabWidget->addTab(cameraTab, "Camera");

    // Tab 4: Plugin Config
    QWidget* pluginTab = new QWidget();
    setupPluginTab(pluginTab);
    m_tabWidget->addTab(pluginTab, "Plugin Config");

    mainLayout->addWidget(m_tabWidget);

    // Set minimum width (allows expansion)
    setMinimumWidth(300);
}

void VisionRightPanelWidget::setupROITab(QWidget* roiTab)
{
    QVBoxLayout* tabLayout = new QVBoxLayout(roiTab);
    tabLayout->setContentsMargins(10, 10, 10, 10);
    tabLayout->setSpacing(10);

    // ========== ROI Enable Group ==========
    QGroupBox* enableGroup = new QGroupBox("Region of Interest", roiTab);
    QVBoxLayout* enableLayout = new QVBoxLayout(enableGroup);

    m_roiEnabledCheck = new QCheckBox("Enable ROI");
    m_roiEnabledCheck->setToolTip("Enable cropping to Region of Interest before processing");
    connect(m_roiEnabledCheck, &QCheckBox::checkStateChanged,
            this, &VisionRightPanelWidget::onROIEnabledChanged);
    enableLayout->addWidget(m_roiEnabledCheck);

    tabLayout->addWidget(enableGroup);

    // ========== ROI Coordinates Group ==========
    QGroupBox* coordGroup = new QGroupBox("ROI Coordinates (pixels)", roiTab);
    QFormLayout* coordLayout = new QFormLayout(coordGroup);
    coordLayout->setSpacing(8);

    // Start Corner
    QLabel* startLabel = new QLabel("<b>Start Corner:</b>");
    coordLayout->addRow(startLabel);

    m_roiStartXSpin = new QSpinBox();
    m_roiStartXSpin->setRange(0, 10000);
    m_roiStartXSpin->setSingleStep(10);
    m_roiStartXSpin->setValue(0);
    m_roiStartXSpin->setToolTip("X coordinate of start corner");
    connect(m_roiStartXSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &VisionRightPanelWidget::onROIValueChanged);
    coordLayout->addRow("Start X:", m_roiStartXSpin);

    m_roiStartYSpin = new QSpinBox();
    m_roiStartYSpin->setRange(0, 10000);
    m_roiStartYSpin->setSingleStep(10);
    m_roiStartYSpin->setValue(0);
    m_roiStartYSpin->setToolTip("Y coordinate of start corner");
    connect(m_roiStartYSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &VisionRightPanelWidget::onROIValueChanged);
    coordLayout->addRow("Start Y:", m_roiStartYSpin);

    // End Corner
    QLabel* endLabel = new QLabel("<b>End Corner:</b>");
    coordLayout->addRow(endLabel);

    m_roiEndXSpin = new QSpinBox();
    m_roiEndXSpin->setRange(0, 10000);
    m_roiEndXSpin->setSingleStep(10);
    m_roiEndXSpin->setValue(640);
    m_roiEndXSpin->setToolTip("X coordinate of end corner");
    connect(m_roiEndXSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &VisionRightPanelWidget::onROIValueChanged);
    coordLayout->addRow("End X:", m_roiEndXSpin);

    m_roiEndYSpin = new QSpinBox();
    m_roiEndYSpin->setRange(0, 10000);
    m_roiEndYSpin->setSingleStep(10);
    m_roiEndYSpin->setValue(480);
    m_roiEndYSpin->setToolTip("Y coordinate of end corner");
    connect(m_roiEndYSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &VisionRightPanelWidget::onROIValueChanged);
    coordLayout->addRow("End Y:", m_roiEndYSpin);

    tabLayout->addWidget(coordGroup);

    // ========== ROI Info Group ==========
    QGroupBox* infoGroup = new QGroupBox("ROI Information", roiTab);
    QVBoxLayout* infoLayout = new QVBoxLayout(infoGroup);

    m_roiSizeLabel = new QLabel("Size: 640 x 480 pixels\nArea: 307,200 pixels");
    m_roiSizeLabel->setStyleSheet("font-family: 'Consolas', monospace;");
    infoLayout->addWidget(m_roiSizeLabel);

    tabLayout->addWidget(infoGroup);

    tabLayout->addStretch();

    // Update size label
    updateROISizeLabel();
}

void VisionRightPanelWidget::setupSearchBoxTab(QWidget* searchBoxTab)
{
    QVBoxLayout* tabLayout = new QVBoxLayout(searchBoxTab);
    tabLayout->setContentsMargins(10, 10, 10, 10);
    tabLayout->setSpacing(10);

    // ========== Search Box Enable Group ==========
    QGroupBox* searchEnableGroup = new QGroupBox("Search Box (3x3 Grid)", searchBoxTab);
    QVBoxLayout* searchEnableLayout = new QVBoxLayout(searchEnableGroup);

    m_searchBoxEnabledCheck = new QCheckBox("Enable Search Box");
    m_searchBoxEnabledCheck->setToolTip("Enable 3x3 grid search pattern within search area");
    connect(m_searchBoxEnabledCheck, &QCheckBox::checkStateChanged,
            this, &VisionRightPanelWidget::onSearchBoxEnabledChanged);
    searchEnableLayout->addWidget(m_searchBoxEnabledCheck);

    QLabel* searchInfo = new QLabel("When enabled, Apply Parameters will iterate\nROI through 9 grid positions (11-33) and\nreturn the best result found.");
    searchInfo->setStyleSheet("color: #FFA500; font-size: 10px;");  // Orange info text
    searchEnableLayout->addWidget(searchInfo);

    tabLayout->addWidget(searchEnableGroup);

    // ========== Search Box Coordinates Group ==========
    QGroupBox* searchCoordGroup = new QGroupBox("Search Box Coordinates (pixels)", searchBoxTab);
    QFormLayout* searchCoordLayout = new QFormLayout(searchCoordGroup);
    searchCoordLayout->setSpacing(8);

    // Start Corner
    QLabel* searchStartLabel = new QLabel("<b>Start Corner:</b>");
    searchCoordLayout->addRow(searchStartLabel);

    m_searchBoxStartXSpin = new QSpinBox();
    m_searchBoxStartXSpin->setRange(0, 10000);
    m_searchBoxStartXSpin->setSingleStep(10);
    m_searchBoxStartXSpin->setValue(0);
    m_searchBoxStartXSpin->setToolTip("X coordinate of search box start corner");
    connect(m_searchBoxStartXSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &VisionRightPanelWidget::onSearchBoxValueChanged);
    searchCoordLayout->addRow("Start X:", m_searchBoxStartXSpin);

    m_searchBoxStartYSpin = new QSpinBox();
    m_searchBoxStartYSpin->setRange(0, 10000);
    m_searchBoxStartYSpin->setSingleStep(10);
    m_searchBoxStartYSpin->setValue(0);
    m_searchBoxStartYSpin->setToolTip("Y coordinate of search box start corner");
    connect(m_searchBoxStartYSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &VisionRightPanelWidget::onSearchBoxValueChanged);
    searchCoordLayout->addRow("Start Y:", m_searchBoxStartYSpin);

    // End Corner
    QLabel* searchEndLabel = new QLabel("<b>End Corner:</b>");
    searchCoordLayout->addRow(searchEndLabel);

    m_searchBoxEndXSpin = new QSpinBox();
    m_searchBoxEndXSpin->setRange(0, 10000);
    m_searchBoxEndXSpin->setSingleStep(10);
    m_searchBoxEndXSpin->setValue(800);
    m_searchBoxEndXSpin->setToolTip("X coordinate of search box end corner");
    connect(m_searchBoxEndXSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &VisionRightPanelWidget::onSearchBoxValueChanged);
    searchCoordLayout->addRow("End X:", m_searchBoxEndXSpin);

    m_searchBoxEndYSpin = new QSpinBox();
    m_searchBoxEndYSpin->setRange(0, 10000);
    m_searchBoxEndYSpin->setSingleStep(10);
    m_searchBoxEndYSpin->setValue(600);
    m_searchBoxEndYSpin->setToolTip("Y coordinate of search box end corner");
    connect(m_searchBoxEndYSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &VisionRightPanelWidget::onSearchBoxValueChanged);
    searchCoordLayout->addRow("End Y:", m_searchBoxEndYSpin);

    tabLayout->addWidget(searchCoordGroup);

    // ========== Search Box Info Group ==========
    QGroupBox* searchInfoGroup = new QGroupBox("Search Box Information", searchBoxTab);
    QVBoxLayout* searchInfoLayout = new QVBoxLayout(searchInfoGroup);

    m_searchBoxSizeLabel = new QLabel("Size: 800 x 600 pixels\nArea: 480,000 pixels");
    m_searchBoxSizeLabel->setStyleSheet("font-family: 'Consolas', monospace; color: #FFA500;");
    searchInfoLayout->addWidget(m_searchBoxSizeLabel);

    tabLayout->addWidget(searchInfoGroup);

    tabLayout->addStretch();

    // Update size label
    updateSearchBoxSizeLabel();
}

void VisionRightPanelWidget::setupCameraTab(QWidget* cameraTab)
{
    QVBoxLayout* tabLayout = new QVBoxLayout(cameraTab);
    tabLayout->setContentsMargins(10, 10, 10, 10);
    tabLayout->setSpacing(10);

    // ========== Camera Selection Group ==========
    QGroupBox* selectGroup = new QGroupBox("Camera Selection", cameraTab);
    QVBoxLayout* selectLayout = new QVBoxLayout(selectGroup);
    selectLayout->setSpacing(8);

    m_cameraSelector = new QComboBox();
    m_cameraSelector->setToolTip("Select camera to control");
    connect(m_cameraSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VisionRightPanelWidget::onCameraSelectionChanged);
    selectLayout->addWidget(m_cameraSelector);

    tabLayout->addWidget(selectGroup);

    // ========== Camera Settings Group ==========
    QGroupBox* cameraGroup = new QGroupBox("Camera Settings", cameraTab);
    QVBoxLayout* settingsLayout = new QVBoxLayout(cameraGroup);
    settingsLayout->setSpacing(8);

    // Auto Exposure checkbox
    m_autoExposureCheck = new QCheckBox("Auto Exposure");
    m_autoExposureCheck->setToolTip("Enable automatic exposure control");
    connect(m_autoExposureCheck, &QCheckBox::checkStateChanged,
            this, &VisionRightPanelWidget::onAutoExposureChanged);
    settingsLayout->addWidget(m_autoExposureCheck);

    // Auto Gain checkbox
    m_autoGainCheck = new QCheckBox("Auto Gain");
    m_autoGainCheck->setToolTip("Enable automatic gain control");
    connect(m_autoGainCheck, &QCheckBox::checkStateChanged,
            this, &VisionRightPanelWidget::onAutoGainChanged);
    settingsLayout->addWidget(m_autoGainCheck);

    settingsLayout->addSpacing(10);

    // Exposure control
    QLabel* expLabel = new QLabel("Exposure:");
    expLabel->setStyleSheet("QLabel { font-weight: bold; }");
    settingsLayout->addWidget(expLabel);

    m_exposureSpinBox = new QSpinBox();
    m_exposureSpinBox->setMinimum(1);
    m_exposureSpinBox->setMaximum(100000);
    m_exposureSpinBox->setSingleStep(50);
    m_exposureSpinBox->setValue(1000);
    m_exposureSpinBox->setSuffix(" us");
    m_exposureSpinBox->setToolTip("Enter exposure time in microseconds");
    connect(m_exposureSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &VisionRightPanelWidget::onExposureSpinBoxChanged);
    settingsLayout->addWidget(m_exposureSpinBox);

    m_exposureValueLabel = new QLabel("1000 us = 1.00 ms");
    m_exposureValueLabel->setAlignment(Qt::AlignCenter);
    m_exposureValueLabel->setStyleSheet("color: #888888; font-size: 11px;");
    settingsLayout->addWidget(m_exposureValueLabel);

    settingsLayout->addSpacing(10);

    // Gain control
    QLabel* gainLabel = new QLabel("Gain:");
    gainLabel->setStyleSheet("QLabel { font-weight: bold; }");
    settingsLayout->addWidget(gainLabel);

    m_gainSpinBox = new QDoubleSpinBox();
    m_gainSpinBox->setMinimum(0.0);
    m_gainSpinBox->setMaximum(24.0);
    m_gainSpinBox->setValue(1.0);
    m_gainSpinBox->setSingleStep(0.1);
    m_gainSpinBox->setDecimals(1);
    m_gainSpinBox->setToolTip("Enter gain value (0.0 - 24.0)");
    connect(m_gainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &VisionRightPanelWidget::onGainSpinBoxChanged);
    settingsLayout->addWidget(m_gainSpinBox);

    m_gainValueLabel = new QLabel("Gain: 1.0");
    m_gainValueLabel->setAlignment(Qt::AlignCenter);
    m_gainValueLabel->setStyleSheet("color: #888888; font-size: 11px;");
    settingsLayout->addWidget(m_gainValueLabel);

    settingsLayout->addSpacing(10);

    // Frame Rate control
    QLabel* fpsLabel = new QLabel("Frame Rate:");
    fpsLabel->setStyleSheet("QLabel { font-weight: bold; }");
    settingsLayout->addWidget(fpsLabel);

    m_frameRateSpinBox = new QDoubleSpinBox();
    m_frameRateSpinBox->setMinimum(0.1);
    m_frameRateSpinBox->setMaximum(30.0);
    m_frameRateSpinBox->setValue(8.0);
    m_frameRateSpinBox->setSingleStep(0.5);
    m_frameRateSpinBox->setDecimals(1);
    m_frameRateSpinBox->setSuffix(" fps");
    m_frameRateSpinBox->setToolTip("Enter frame rate (0.1 - 30.0 fps)");
    connect(m_frameRateSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &VisionRightPanelWidget::onFrameRateSpinBoxChanged);
    settingsLayout->addWidget(m_frameRateSpinBox);

    m_frameRateValueLabel = new QLabel("8.0 fps");
    m_frameRateValueLabel->setAlignment(Qt::AlignCenter);
    m_frameRateValueLabel->setStyleSheet("color: #888888; font-size: 11px;");
    settingsLayout->addWidget(m_frameRateValueLabel);

    settingsLayout->addSpacing(10);

    // Apply Camera Settings button
    m_applyCameraSettingsBtn = new QPushButton("Apply Camera Settings");
    m_applyCameraSettingsBtn->setToolTip("Apply exposure, gain, and frame rate to the camera");
    m_applyCameraSettingsBtn->setStyleSheet(
        "QPushButton { background-color: #1565C0; color: white; font-weight: bold; padding: 8px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #1976D2; }"
        "QPushButton:pressed { background-color: #0D47A1; }"
    );
    connect(m_applyCameraSettingsBtn, &QPushButton::clicked,
            this, &VisionRightPanelWidget::onApplyCameraSettingsBtnClicked);
    settingsLayout->addWidget(m_applyCameraSettingsBtn);

    tabLayout->addWidget(cameraGroup);

    // ========== Calibration Group ==========
    QGroupBox* calibrationGroup = new QGroupBox("Pixel-to-mm Calibration", cameraTab);
    QFormLayout* calibLayout = new QFormLayout(calibrationGroup);
    calibLayout->setSpacing(8);

    // Display-only labels (values loaded from config/SYSTEM/val_pixelmm.json)
    m_pixelToMmXLabel = new QLabel("0.00248 mm/px");
    m_pixelToMmXLabel->setToolTip("X-axis calibration (from config/SYSTEM/val_pixelmm.json)");
    m_pixelToMmXLabel->setStyleSheet("color: #d4d4d4; font-family: monospace;");
    calibLayout->addRow("X Scale:", m_pixelToMmXLabel);

    m_pixelToMmYLabel = new QLabel("0.00248 mm/px");
    m_pixelToMmYLabel->setToolTip("Y-axis calibration (from config/SYSTEM/val_pixelmm.json)");
    m_pixelToMmYLabel->setStyleSheet("color: #d4d4d4; font-family: monospace;");
    calibLayout->addRow("Y Scale:", m_pixelToMmYLabel);

    QLabel* configNote = new QLabel("Edit config/SYSTEM/val_pixelmm.json to change");
    configNote->setStyleSheet("color: #888888; font-size: 10px; font-style: italic;");
    calibLayout->addRow("", configNote);

    tabLayout->addWidget(calibrationGroup);

    // ========== Lighting Section ==========
    setupLightingSection(tabLayout);

    tabLayout->addStretch();

    // No calibration change signal - values are read-only from JSON file
}


void VisionRightPanelWidget::setupPluginTab(QWidget* pluginTab)
{
    QVBoxLayout* tabLayout = new QVBoxLayout(pluginTab);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(0);

    // Create scroll area for plugin parameters
    m_pluginScrollArea = new QScrollArea(pluginTab);
    m_pluginScrollArea->setWidgetResizable(true);
    m_pluginScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget* scrollContent = new QWidget();
    m_pluginParamsLayout = new QVBoxLayout(scrollContent);
    m_pluginParamsLayout->setContentsMargins(10, 10, 10, 10);
    m_pluginParamsLayout->setSpacing(8);

    QLabel* noPluginLabel = new QLabel("Select a plugin to configure parameters");
    noPluginLabel->setStyleSheet("color: #808080; font-style: italic;");
    noPluginLabel->setAlignment(Qt::AlignCenter);
    noPluginLabel->setWordWrap(true);
    m_pluginParamsLayout->addWidget(noPluginLabel);

    m_pluginParamsLayout->addStretch();

    m_applyParamsBtn = new QPushButton("Apply Parameters", scrollContent);
    m_applyParamsBtn->setToolTip("Apply current parameter settings");
    m_applyParamsBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; padding: 8px; }");
    m_pluginParamsLayout->addWidget(m_applyParamsBtn);

    m_pluginScrollArea->setWidget(scrollContent);
    tabLayout->addWidget(m_pluginScrollArea);

    // Connect signals
    connect(m_applyParamsBtn, &QPushButton::clicked,
            this, &VisionRightPanelWidget::applyPluginParamsClicked);
}

void VisionRightPanelWidget::loadCameraList()
{
    if (!m_context || !m_context->cameraManager) {
        std::cerr << "[VisionRightPanelWidget] CameraManager not available" << std::endl;
        m_cameraSelector->addItem("(No cameras available)");
        return;
    }

    m_cameraSelector->blockSignals(true);
    m_cameraSelector->clear();

    auto cameraIds = m_context->cameraManager->GetCameraIds();

    if (cameraIds.empty()) {
        m_cameraSelector->addItem("(No cameras configured)");
    } else {
        for (const auto& cameraId : cameraIds) {
            m_cameraSelector->addItem(QString::fromStdString(cameraId));
        }
    }

    m_cameraSelector->blockSignals(false);

    // Load settings for first camera if available
    if (!cameraIds.empty()) {
        loadCurrentCameraSettings();
        // NOTE: Don't emit signal here - it's too early (signals not connected yet)
        // Parent will call selectFirstCamera() after connecting signals
    }

    std::cout << "[VisionRightPanelWidget] Loaded " << cameraIds.size() << " cameras" << std::endl;
}

void VisionRightPanelWidget::onCameraSelectionChanged(int index)
{
    if (index < 0) return;

    QString cameraId = m_cameraSelector->currentText();
    if (cameraId.startsWith("(")) return;  // Placeholder text

    std::cout << "[VisionRightPanelWidget] Camera selected: " << cameraId.toStdString() << std::endl;

    loadCurrentCameraSettings();
    emit cameraSelected(cameraId);
}

void VisionRightPanelWidget::applyCameraSettings()
{
    if (!m_context || !m_context->cameraManager) {
        std::cerr << "[VisionRightPanelWidget] CameraManager not available" << std::endl;
        return;
    }

    QString cameraId = getSelectedCameraId();
    if (cameraId.isEmpty()) {
        std::cerr << "[VisionRightPanelWidget] No camera selected" << std::endl;
        return;
    }

    ICameraHardware::ExposureSettings settings;
    settings.auto_exposure = m_autoExposureCheck->isChecked();
    settings.auto_gain = m_autoGainCheck->isChecked();
    settings.exposure_time = m_exposureSpinBox ? m_exposureSpinBox->value() : 1000;
    settings.gain = m_gainSpinBox ? m_gainSpinBox->value() : 1.0;
    settings.frame_rate = m_frameRateSpinBox ? m_frameRateSpinBox->value() : 8.0;

    std::string cameraIdStr = cameraId.toStdString();

    if (m_context->cameraManager->ApplyExposureSettings(cameraIdStr, settings)) {
        std::cout << "[VisionRightPanelWidget] Camera settings applied: "
                  << "Exposure=" << settings.exposure_time << "μs, "
                  << "Gain=" << settings.gain << ", "
                  << "FrameRate=" << settings.frame_rate << "fps" << std::endl;
    } else {
        std::cerr << "[VisionRightPanelWidget] Failed to apply camera settings" << std::endl;
    }
}

void VisionRightPanelWidget::loadCurrentCameraSettings()
{
    if (!m_context || !m_context->cameraManager) {
        return;
    }

    QString cameraId = getSelectedCameraId();
    if (cameraId.isEmpty()) {
        return;
    }

    std::string cameraIdStr = cameraId.toStdString();

    // Check if camera is connected
    auto cameraStatus = m_context->cameraManager->GetCameraStatus(cameraIdStr);
    if (!cameraStatus.connected) {
        std::cout << "[VisionRightPanelWidget] Camera not connected, using default values" << std::endl;
        return;
    }

    auto settings = m_context->cameraManager->GetExposureSettings(cameraIdStr);

    // Block signals during load
    m_autoExposureCheck->blockSignals(true);
    m_autoGainCheck->blockSignals(true);
    if (m_exposureSpinBox) m_exposureSpinBox->blockSignals(true);
    if (m_gainSpinBox) m_gainSpinBox->blockSignals(true);
    if (m_frameRateSpinBox) m_frameRateSpinBox->blockSignals(true);

    m_autoExposureCheck->setChecked(settings.auto_exposure);
    m_autoGainCheck->setChecked(settings.auto_gain);

    int exposureValue = static_cast<int>(settings.exposure_time);
    if (exposureValue < 1) exposureValue = 1;
    if (exposureValue > 100000) exposureValue = 100000;
    if (m_exposureSpinBox) m_exposureSpinBox->setValue(exposureValue);

    if (m_gainSpinBox) m_gainSpinBox->setValue(settings.gain);

    // Load frame rate
    if (m_frameRateSpinBox) m_frameRateSpinBox->setValue(settings.frame_rate);

    // Update labels
    updateExposureLabel(exposureValue);
    updateGainLabel(settings.gain);
    updateFrameRateLabel(settings.frame_rate);

    // Update spinbox enabled state
    if (m_exposureSpinBox) m_exposureSpinBox->setEnabled(!settings.auto_exposure);
    if (m_gainSpinBox) m_gainSpinBox->setEnabled(!settings.auto_gain);

    // Unblock signals
    m_autoExposureCheck->blockSignals(false);
    m_autoGainCheck->blockSignals(false);
    if (m_exposureSpinBox) m_exposureSpinBox->blockSignals(false);
    if (m_gainSpinBox) m_gainSpinBox->blockSignals(false);
    if (m_frameRateSpinBox) m_frameRateSpinBox->blockSignals(false);

    std::cout << "[VisionRightPanelWidget] Loaded camera settings from " << cameraIdStr << std::endl;
}

QString VisionRightPanelWidget::getSelectedCameraId() const
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

void VisionRightPanelWidget::selectFirstCamera()
{
    // Emit signal for the currently selected camera (first one after load)
    QString currentCamera = getSelectedCameraId();
    if (!currentCamera.isEmpty()) {
        std::cout << "[VisionRightPanelWidget] Selecting initial camera: " << currentCamera.toStdString() << std::endl;
        emit cameraSelected(currentCamera);
    }
}

VisionCameraSettings VisionRightPanelWidget::getCameraSettings() const
{
    VisionCameraSettings settings;
    settings.cameraId = getSelectedCameraId();
    settings.exposureTime = m_exposureSpinBox ? m_exposureSpinBox->value() : 1000;
    settings.gain = m_gainSpinBox ? m_gainSpinBox->value() : 1.0;
    settings.autoExposure = m_autoExposureCheck ? m_autoExposureCheck->isChecked() : false;
    settings.autoGain = m_autoGainCheck ? m_autoGainCheck->isChecked() : false;
    settings.frameRate = m_frameRateSpinBox ? m_frameRateSpinBox->value() : 8.0;
    settings.pixelToMmX = m_pixelToMmX;  // From config/SYSTEM/val_pixelmm.json
    settings.pixelToMmY = m_pixelToMmY;  // From config/SYSTEM/val_pixelmm.json
    return settings;
}

void VisionRightPanelWidget::setCameraSettings(const VisionCameraSettings& settings)
{
    // Block signals during set
    m_cameraSelector->blockSignals(true);
    m_autoExposureCheck->blockSignals(true);
    m_autoGainCheck->blockSignals(true);
    if (m_exposureSpinBox) m_exposureSpinBox->blockSignals(true);
    if (m_gainSpinBox) m_gainSpinBox->blockSignals(true);
    if (m_frameRateSpinBox) m_frameRateSpinBox->blockSignals(true);

    // Set camera
    int cameraIndex = m_cameraSelector->findText(settings.cameraId);
    if (cameraIndex >= 0) {
        m_cameraSelector->setCurrentIndex(cameraIndex);
    }

    // Set exposure/gain
    m_autoExposureCheck->setChecked(settings.autoExposure);
    m_autoGainCheck->setChecked(settings.autoGain);
    if (m_exposureSpinBox) m_exposureSpinBox->setValue(settings.exposureTime);
    if (m_gainSpinBox) m_gainSpinBox->setValue(settings.gain);
    updateExposureLabel(settings.exposureTime);
    updateGainLabel(settings.gain);

    // Set frame rate
    if (m_frameRateSpinBox) m_frameRateSpinBox->setValue(settings.frameRate);
    updateFrameRateLabel(settings.frameRate);

    // Update calibration display labels (read-only from config/SYSTEM/val_pixelmm.json)
    // Note: settings.pixelToMmX/Y are ignored - calibration comes from JSON file only
    if (m_pixelToMmXLabel) {
        m_pixelToMmXLabel->setText(QString("%1 mm/px").arg(m_pixelToMmX, 0, 'f', 5));
    }
    if (m_pixelToMmYLabel) {
        m_pixelToMmYLabel->setText(QString("%1 mm/px").arg(m_pixelToMmY, 0, 'f', 5));
    }

    // Update enabled states
    if (m_exposureSpinBox) m_exposureSpinBox->setEnabled(!settings.autoExposure);
    if (m_gainSpinBox) m_gainSpinBox->setEnabled(!settings.autoGain);

    // Unblock signals
    m_cameraSelector->blockSignals(false);
    m_autoExposureCheck->blockSignals(false);
    m_autoGainCheck->blockSignals(false);
    if (m_exposureSpinBox) m_exposureSpinBox->blockSignals(false);
    if (m_gainSpinBox) m_gainSpinBox->blockSignals(false);
    if (m_frameRateSpinBox) m_frameRateSpinBox->blockSignals(false);

    std::cout << "[VisionRightPanelWidget] Camera settings restored: "
              << "Camera=" << settings.cameraId.toStdString() << ", "
              << "Exposure=" << settings.exposureTime << "μs, "
              << "Gain=" << settings.gain << ", "
              << "FrameRate=" << settings.frameRate << "fps" << std::endl;
}

void VisionRightPanelWidget::onExposureSpinBoxChanged(int value)
{
    updateExposureLabel(value);
}

void VisionRightPanelWidget::onGainSpinBoxChanged(double value)
{
    updateGainLabel(value);
}

void VisionRightPanelWidget::onAutoExposureChanged(Qt::CheckState state)
{
    bool autoEnabled = (state == Qt::Checked);
    if (m_exposureSpinBox) {
        m_exposureSpinBox->setEnabled(!autoEnabled);
    }
    applyCameraSettings();
    emit cameraSettingsChanged();
}

void VisionRightPanelWidget::onAutoGainChanged(Qt::CheckState state)
{
    bool autoEnabled = (state == Qt::Checked);
    if (m_gainSpinBox) {
        m_gainSpinBox->setEnabled(!autoEnabled);
    }
    applyCameraSettings();
    emit cameraSettingsChanged();
}

void VisionRightPanelWidget::updateExposureLabel(int microseconds)
{
    if (microseconds >= 1000) {
        double ms = microseconds / 1000.0;
        m_exposureValueLabel->setText(QString("%1 us = %2 ms").arg(microseconds).arg(ms, 0, 'f', 2));
    } else {
        m_exposureValueLabel->setText(QString("%1 us").arg(microseconds));
    }
}

void VisionRightPanelWidget::updateGainLabel(double gain)
{
    m_gainValueLabel->setText(QString("Gain: %1").arg(gain, 0, 'f', 1));
}

void VisionRightPanelWidget::onFrameRateSpinBoxChanged(double value)
{
    updateFrameRateLabel(value);
}

void VisionRightPanelWidget::onApplyCameraSettingsBtnClicked()
{
    applyCameraSettings();
    emit cameraSettingsChanged();
    std::cout << "[VisionRightPanelWidget] Camera settings applied via button" << std::endl;
}

void VisionRightPanelWidget::updateFrameRateLabel(double fps)
{
    if (m_frameRateValueLabel) {
        m_frameRateValueLabel->setText(QString("%1 fps").arg(fps, 0, 'f', 1));
    }
}

void VisionRightPanelWidget::setPluginConfigWidget(QWidget* configWidget)
{
    clearPluginConfigWidget();

    if (configWidget) {
        // Remove placeholder text (all items except the last stretch and apply button)
        while (m_pluginParamsLayout->count() > 2) {
            QLayoutItem* item = m_pluginParamsLayout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }

        // Insert new config widget at the beginning
        m_pluginParamsLayout->insertWidget(0, configWidget);
        m_currentPluginConfig = configWidget;

        // Switch to plugin config tab
        m_tabWidget->setCurrentIndex(1);
    }
}

void VisionRightPanelWidget::clearPluginConfigWidget()
{
    // ✅ FIX: Use QPointer to safely handle widget lifecycle
    // QPointer automatically becomes null if widget is deleted elsewhere
    if (m_currentPluginConfig) {
        m_pluginParamsLayout->removeWidget(m_currentPluginConfig);
        m_currentPluginConfig->deleteLater();
        m_currentPluginConfig = nullptr;
    }

    // Restore placeholder if needed (check if only stretch and button remain)
    if (m_pluginParamsLayout->count() <= 2) {
        QLabel* noPluginLabel = new QLabel("Select a plugin to configure parameters");
        noPluginLabel->setStyleSheet("color: #808080; font-style: italic;");
        noPluginLabel->setAlignment(Qt::AlignCenter);
        noPluginLabel->setWordWrap(true);
        m_pluginParamsLayout->insertWidget(0, noPluginLabel);
    }
}

double VisionRightPanelWidget::getPixelToMmX() const
{
    return m_pixelToMmX;  // From config/SYSTEM/val_pixelmm.json
}

double VisionRightPanelWidget::getPixelToMmY() const
{
    return m_pixelToMmY;  // From config/SYSTEM/val_pixelmm.json
}

// ========== ROI Management ==========

void VisionRightPanelWidget::setROI(int startX, int startY, int endX, int endY)
{
    m_roiStartXSpin->blockSignals(true);
    m_roiStartYSpin->blockSignals(true);
    m_roiEndXSpin->blockSignals(true);
    m_roiEndYSpin->blockSignals(true);

    m_roiStartXSpin->setValue(startX);
    m_roiStartYSpin->setValue(startY);
    m_roiEndXSpin->setValue(endX);
    m_roiEndYSpin->setValue(endY);

    m_roiStartXSpin->blockSignals(false);
    m_roiStartYSpin->blockSignals(false);
    m_roiEndXSpin->blockSignals(false);
    m_roiEndYSpin->blockSignals(false);

    updateROISizeLabel();
}

void VisionRightPanelWidget::setROIEnabled(bool enabled)
{
    m_roiEnabledCheck->blockSignals(true);
    m_roiEnabledCheck->setChecked(enabled);
    m_roiEnabledCheck->blockSignals(false);
}

bool VisionRightPanelWidget::isROIEnabled() const
{
    return m_roiEnabledCheck ? m_roiEnabledCheck->isChecked() : false;
}

void VisionRightPanelWidget::onROIValueChanged()
{
    updateROISizeLabel();

    int startX = m_roiStartXSpin->value();
    int startY = m_roiStartYSpin->value();
    int endX = m_roiEndXSpin->value();
    int endY = m_roiEndYSpin->value();

    std::cout << "[VisionRightPanelWidget] ROI changed: ("
              << startX << "," << startY << ") to ("
              << endX << "," << endY << ")" << std::endl;

    emit roiChanged(startX, startY, endX, endY);
}

void VisionRightPanelWidget::onROIEnabledChanged(Qt::CheckState state)
{
    bool enabled = (state == Qt::Checked);
    std::cout << "[VisionRightPanelWidget] ROI " << (enabled ? "enabled" : "disabled") << std::endl;
    emit roiEnabledChanged(enabled);
}

void VisionRightPanelWidget::updateROISizeLabel()
{
    if (!m_roiSizeLabel) return;

    int startX = m_roiStartXSpin->value();
    int startY = m_roiStartYSpin->value();
    int endX = m_roiEndXSpin->value();
    int endY = m_roiEndYSpin->value();

    int width = std::abs(endX - startX);
    int height = std::abs(endY - startY);
    int area = width * height;

    QString info = QString("Size: %1 x %2 pixels\n").arg(width).arg(height);
    info += QString("Area: %L1 pixels").arg(area);

    m_roiSizeLabel->setText(info);
}

void VisionRightPanelWidget::updateSearchBoxSizeLabel()
{
    if (!m_searchBoxSizeLabel) return;

    int startX = m_searchBoxStartXSpin->value();
    int startY = m_searchBoxStartYSpin->value();
    int endX = m_searchBoxEndXSpin->value();
    int endY = m_searchBoxEndYSpin->value();

    int width = std::abs(endX - startX);
    int height = std::abs(endY - startY);
    int area = width * height;

    QString info = QString("Size: %1 x %2 pixels\n").arg(width).arg(height);
    info += QString("Area: %L1 pixels").arg(area);

    m_searchBoxSizeLabel->setText(info);
}

// ========== Search Box Methods ==========

void VisionRightPanelWidget::setSearchBox(int startX, int startY, int endX, int endY)
{
    // Block signals to avoid recursive updates
    m_searchBoxStartXSpin->blockSignals(true);
    m_searchBoxStartYSpin->blockSignals(true);
    m_searchBoxEndXSpin->blockSignals(true);
    m_searchBoxEndYSpin->blockSignals(true);

    m_searchBoxStartXSpin->setValue(startX);
    m_searchBoxStartYSpin->setValue(startY);
    m_searchBoxEndXSpin->setValue(endX);
    m_searchBoxEndYSpin->setValue(endY);

    m_searchBoxStartXSpin->blockSignals(false);
    m_searchBoxStartYSpin->blockSignals(false);
    m_searchBoxEndXSpin->blockSignals(false);
    m_searchBoxEndYSpin->blockSignals(false);

    updateSearchBoxSizeLabel();
}

void VisionRightPanelWidget::setSearchBoxEnabled(bool enabled)
{
    m_searchBoxEnabledCheck->blockSignals(true);
    m_searchBoxEnabledCheck->setChecked(enabled);
    m_searchBoxEnabledCheck->blockSignals(false);
}

bool VisionRightPanelWidget::isSearchBoxEnabled() const
{
    return m_searchBoxEnabledCheck->isChecked();
}

void VisionRightPanelWidget::getSearchBox(int& startX, int& startY, int& endX, int& endY) const
{
    startX = m_searchBoxStartXSpin->value();
    startY = m_searchBoxStartYSpin->value();
    endX = m_searchBoxEndXSpin->value();
    endY = m_searchBoxEndYSpin->value();
}

void VisionRightPanelWidget::onSearchBoxValueChanged()
{
    updateSearchBoxSizeLabel();
    emit searchBoxChanged(
        m_searchBoxStartXSpin->value(),
        m_searchBoxStartYSpin->value(),
        m_searchBoxEndXSpin->value(),
        m_searchBoxEndYSpin->value()
    );
}

void VisionRightPanelWidget::onSearchBoxEnabledChanged(Qt::CheckState state)
{
    emit searchBoxEnabledChanged(state == Qt::Checked);
}

void VisionRightPanelWidget::loadCalibrationFromConfig()
{
    // Load calibration values from config/SYSTEM/val_pixelmm.json
    QString appDir = QCoreApplication::applicationDirPath();
    QString configPath = appDir + "/config/SYSTEM/val_pixelmm.json";

    if (QFile::exists(configPath)) {
        try {
            std::ifstream file(configPath.toStdString());
            if (file.is_open()) {
                nlohmann::json j;
                file >> j;

                if (j.contains("calibration")) {
                    auto& cal = j["calibration"];
                    if (cal.contains("pixel_to_mm_x")) {
                        m_pixelToMmX = cal["pixel_to_mm_x"].get<double>();
                    }
                    if (cal.contains("pixel_to_mm_y")) {
                        m_pixelToMmY = cal["pixel_to_mm_y"].get<double>();
                    }
                }

                file.close();
                std::cout << "[VisionRightPanelWidget] Loaded calibration from JSON: "
                          << "X=" << m_pixelToMmX << " mm/px, "
                          << "Y=" << m_pixelToMmY << " mm/px" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[VisionRightPanelWidget] Error loading calibration JSON: "
                      << e.what() << std::endl;
        }
    } else {
        std::cout << "[VisionRightPanelWidget] Calibration config not found at: "
                  << configPath.toStdString() << ", using defaults" << std::endl;
    }

    // Update the display labels with loaded values
    if (m_pixelToMmXLabel) {
        m_pixelToMmXLabel->setText(QString("%1 mm/px").arg(m_pixelToMmX, 0, 'f', 5));
    }
    if (m_pixelToMmYLabel) {
        m_pixelToMmYLabel->setText(QString("%1 mm/px").arg(m_pixelToMmY, 0, 'f', 5));
    }
}

// ========== Lighting Section Implementation ==========

void VisionRightPanelWidget::setupLightingSection(QVBoxLayout* parentLayout)
{
    QGroupBox* lightingGroup = new QGroupBox("Lighting Control", this);
    QVBoxLayout* lightingLayout = new QVBoxLayout(lightingGroup);
    lightingLayout->setSpacing(8);

    // Enable checkbox
    m_lightingEnabledCheck = new QCheckBox("Enable Lighting for this Profile");
    m_lightingEnabledCheck->setToolTip("When enabled, lighting settings will be saved with the vision profile and applied when executing");
    connect(m_lightingEnabledCheck, &QCheckBox::checkStateChanged,
            this, &VisionRightPanelWidget::onLightingEnabledChanged);
    lightingLayout->addWidget(m_lightingEnabledCheck);

    // Container for channel widgets
    m_lightingChannelsContainer = new QWidget();
    m_lightingChannelsLayout = new QVBoxLayout(m_lightingChannelsContainer);
    m_lightingChannelsLayout->setContentsMargins(0, 5, 0, 0);
    m_lightingChannelsLayout->setSpacing(5);

    // Load lighting channels from LightingManager
    loadLightingChannels();

    lightingLayout->addWidget(m_lightingChannelsContainer);
    parentLayout->addWidget(lightingGroup);
}

void VisionRightPanelWidget::loadLightingChannels()
{
    // Clear existing widgets
    m_lightingSliders.clear();
    m_lightingValueLabels.clear();
    m_lightingToggleButtons.clear();
    m_lightingChannelNumbers.clear();

    // Clear layout
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

    // Use first controller
    m_lightingControllerId = controllerIds[0];
    auto channels = m_context->lightingManager->GetChannelConfigs(m_lightingControllerId);

    // Filter to only enabled channels
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

    // Create compact channel widgets
    for (size_t i = 0; i < enabledChannels.size(); ++i) {
        const auto& ch = enabledChannels[i];

        QWidget* channelWidget = new QWidget();
        QHBoxLayout* channelLayout = new QHBoxLayout(channelWidget);
        channelLayout->setContentsMargins(0, 2, 0, 2);
        channelLayout->setSpacing(8);

        // Channel label (L1, L2, etc. + name)
        QString labelText = QString("L%1").arg(ch.channelNumber + 1);
        if (!ch.name.empty()) {
            labelText += " " + QString::fromStdString(ch.name);
        }
        QLabel* channelLabel = new QLabel(labelText);
        channelLabel->setFixedWidth(80);
        channelLabel->setStyleSheet("font-size: 11px;");
        channelLayout->addWidget(channelLabel);

        // Intensity slider
        QSlider* slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 255);
        slider->setValue(0);
        slider->setMinimumWidth(100);
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
        valueLabel->setFixedWidth(30);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueLabel->setStyleSheet("font-size: 11px; color: #00aaff;");
        channelLayout->addWidget(valueLabel);
        m_lightingValueLabels.push_back(valueLabel);

        // Toggle button
        QPushButton* toggleBtn = new QPushButton("OFF");
        toggleBtn->setFixedSize(45, 22);
        toggleBtn->setCheckable(true);
        toggleBtn->setStyleSheet(
            "QPushButton { background-color: #555; color: #aaa; font-size: 10px; font-weight: bold; border: none; border-radius: 3px; }"
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

        // Add separator (except for last)
        if (i < enabledChannels.size() - 1) {
            QFrame* line = new QFrame();
            line->setFrameShape(QFrame::HLine);
            line->setStyleSheet("background-color: #3a3a3a;");
            line->setFixedHeight(1);
            m_lightingChannelsLayout->addWidget(line);
        }
    }

    std::cout << "[VisionRightPanelWidget] Loaded " << enabledChannels.size() << " lighting channels" << std::endl;
}

void VisionRightPanelWidget::onLightingEnabledChanged(Qt::CheckState state)
{
    bool enabled = (state == Qt::Checked);
    m_lightingChannelsContainer->setEnabled(enabled);

    std::cout << "[VisionRightPanelWidget] Lighting " << (enabled ? "enabled" : "disabled") << std::endl;
    emit lightingSettingsChanged();
}

void VisionRightPanelWidget::onLightingChannelIntensityChanged(int channel, int intensity)
{
    // Update label
    for (size_t i = 0; i < m_lightingChannelNumbers.size(); ++i) {
        if (m_lightingChannelNumbers[i] == channel && i < m_lightingValueLabels.size()) {
            m_lightingValueLabels[i]->setText(QString::number(intensity));
            break;
        }
    }

    // Apply to hardware if enabled
    if (m_lightingEnabledCheck && m_lightingEnabledCheck->isChecked()) {
        if (m_context && m_context->lightingManager && !m_lightingControllerId.empty()) {
            m_context->lightingManager->SetIntensity(m_lightingControllerId, channel, intensity);
        }
    }

    emit lightingSettingsChanged();
}

void VisionRightPanelWidget::onLightingChannelToggleChanged(int channel, bool on)
{
    // Update button text
    for (size_t i = 0; i < m_lightingChannelNumbers.size(); ++i) {
        if (m_lightingChannelNumbers[i] == channel && i < m_lightingToggleButtons.size()) {
            m_lightingToggleButtons[i]->setText(on ? "ON" : "OFF");
            break;
        }
    }

    // Apply to hardware if enabled
    if (m_lightingEnabledCheck && m_lightingEnabledCheck->isChecked()) {
        if (m_context && m_context->lightingManager && !m_lightingControllerId.empty()) {
            m_context->lightingManager->SetOn(m_lightingControllerId, channel, on);
        }
    }

    emit lightingSettingsChanged();
}

void VisionRightPanelWidget::updateLightingIntensityLabel(int channel, int intensity)
{
    for (size_t i = 0; i < m_lightingChannelNumbers.size(); ++i) {
        if (m_lightingChannelNumbers[i] == channel && i < m_lightingValueLabels.size()) {
            m_lightingValueLabels[i]->setText(QString::number(intensity));
            break;
        }
    }
}

VisionLightingSettings VisionRightPanelWidget::getLightingSettings() const
{
    VisionLightingSettings settings;
    settings.enabled = m_lightingEnabledCheck ? m_lightingEnabledCheck->isChecked() : false;
    settings.controllerId = QString::fromStdString(m_lightingControllerId);

    // Collect channel intensities and states
    for (size_t i = 0; i < m_lightingChannelNumbers.size(); ++i) {
        int channel = m_lightingChannelNumbers[i];
        if (i < m_lightingSliders.size()) {
            settings.channelIntensities[channel] = m_lightingSliders[i]->value();
        }
        if (i < m_lightingToggleButtons.size()) {
            settings.channelStates[channel] = m_lightingToggleButtons[i]->isChecked();
        }
    }

    return settings;
}

void VisionRightPanelWidget::setLightingSettings(const VisionLightingSettings& settings)
{
    // Block signals during restore
    if (m_lightingEnabledCheck) {
        m_lightingEnabledCheck->blockSignals(true);
        m_lightingEnabledCheck->setChecked(settings.enabled);
        m_lightingEnabledCheck->blockSignals(false);
    }

    // Set channel values
    for (size_t i = 0; i < m_lightingChannelNumbers.size(); ++i) {
        int channel = m_lightingChannelNumbers[i];

        // Set intensity
        auto intensityIt = settings.channelIntensities.find(channel);
        if (intensityIt != settings.channelIntensities.end() && i < m_lightingSliders.size()) {
            m_lightingSliders[i]->blockSignals(true);
            m_lightingSliders[i]->setValue(intensityIt->second);
            m_lightingSliders[i]->blockSignals(false);
            if (i < m_lightingValueLabels.size()) {
                m_lightingValueLabels[i]->setText(QString::number(intensityIt->second));
            }
        }

        // Set on/off state
        auto stateIt = settings.channelStates.find(channel);
        if (stateIt != settings.channelStates.end() && i < m_lightingToggleButtons.size()) {
            m_lightingToggleButtons[i]->blockSignals(true);
            m_lightingToggleButtons[i]->setChecked(stateIt->second);
            m_lightingToggleButtons[i]->setText(stateIt->second ? "ON" : "OFF");
            m_lightingToggleButtons[i]->blockSignals(false);
        }
    }

    // Update container enabled state
    if (m_lightingChannelsContainer) {
        m_lightingChannelsContainer->setEnabled(settings.enabled);
    }

    std::cout << "[VisionRightPanelWidget] Lighting settings restored: enabled=" << settings.enabled
              << ", channels=" << settings.channelIntensities.size() << std::endl;
}

void VisionRightPanelWidget::applyLightingSettings()
{
    if (!m_lightingEnabledCheck || !m_lightingEnabledCheck->isChecked()) {
        std::cout << "[VisionRightPanelWidget] Lighting not enabled, skipping apply" << std::endl;
        return;
    }

    if (!m_context || !m_context->lightingManager || m_lightingControllerId.empty()) {
        std::cerr << "[VisionRightPanelWidget] LightingManager not available" << std::endl;
        return;
    }

    // Apply all channel settings to hardware
    for (size_t i = 0; i < m_lightingChannelNumbers.size(); ++i) {
        int channel = m_lightingChannelNumbers[i];

        // Apply intensity
        if (i < m_lightingSliders.size()) {
            int intensity = m_lightingSliders[i]->value();
            m_context->lightingManager->SetIntensity(m_lightingControllerId, channel, intensity);
        }

        // Apply on/off state
        if (i < m_lightingToggleButtons.size()) {
            bool isOn = m_lightingToggleButtons[i]->isChecked();
            m_context->lightingManager->SetOn(m_lightingControllerId, channel, isOn);
        }
    }

    std::cout << "[VisionRightPanelWidget] Applied lighting settings to hardware" << std::endl;
}

bool VisionRightPanelWidget::isLightingEnabled() const
{
    return m_lightingEnabledCheck ? m_lightingEnabledCheck->isChecked() : false;
}

void VisionRightPanelWidget::setLightingEnabled(bool enabled)
{
    if (m_lightingEnabledCheck) {
        m_lightingEnabledCheck->blockSignals(true);
        m_lightingEnabledCheck->setChecked(enabled);
        m_lightingEnabledCheck->blockSignals(false);
    }
    if (m_lightingChannelsContainer) {
        m_lightingChannelsContainer->setEnabled(enabled);
    }
}

void VisionRightPanelWidget::onApplySettingsTimer()
{
    // Throttled apply - only called 500ms after last slider change
    if (m_settingsPending) {
        m_settingsPending = false;
        applyCameraSettings();
        emit cameraSettingsChanged();
        std::cout << "[VisionRightPanelWidget] Applied throttled camera settings" << std::endl;
    }
}
