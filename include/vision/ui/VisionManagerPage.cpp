// VisionManagerPage.cpp
// Wizard-style vision profile configuration page
#include "VisionManagerPage.h"
#include "VisionNavWidget.h"
#include "CameraLightingStepWidget.h"
#include "CaptureROIStepWidget.h"
#include "PluginConfigStepWidget.h"
#include "ExecuteTuneStepWidget.h"
#include "VisionImageWidget.h"
#include "../plugins/PatternMatchingPlugin.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "CameraManager.h"
#include "LiveVideoSubscriber.h"
#include "product_position_manager.h"
#include "../VisionManager.h"
#include "../VisionROI.h"
#include "../VisionProcessingWorker.h"
#include "../plugins/CircleDetectionPlugin.h"
#include "../plugins/EdgeDetectionPlugin.h"
#include "../plugins/FiducialDetectorPlugin.h"
#include "../plugins/LineDetectorPlugin.h"
#include "../plugins/CornerDetectorPlugin.h"
#include "../plugins/PatternMatchingPlugin.h"
#include "../plugins/PatternMatchingEdgePlugin.h"
#include "../plugins/ImageProcessingPlugin.h"
#include "../plugins/CalibrationPlatePlugin.h"
#include "VisionReliabilityTestDialog.h"
#ifdef HAS_HALCON
#include "../plugins/HalconCircleDetectionPlugin.h"
#include "../plugins/HalconCircleDetectionRobustPlugin.h"
#include "../plugins/HalconLineDetectionPlugin.h"
#include "../plugins/HalconPatternMatchingEdgePlugin.h"
#include "../plugins/HalconPatternMatchingNCCPlugin.h"
#include "../plugins/HalconPatternMatchingDescriptorPlugin.h"
#include "../plugins/HalconPatternMatchingGrayPlugin.h"
#include "../plugins/HalconBeamDetectionPlugin.h"
#include "../plugins/HalconD4SigmaBeamDetectionPlugin.h"
#endif
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QImage>
#include <QPixmap>
#include <QInputDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QThread>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <fstream>
#include <iostream>

VisionManagerPage::VisionManagerPage(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_mainSplitter(nullptr)
    , m_backButton(nullptr)
    , m_navWidget(nullptr)
    , m_contentStack(nullptr)
    , m_welcomeWidget(nullptr)
    , m_cameraStep(nullptr)
    , m_captureStep(nullptr)
    , m_pluginStep(nullptr)
    , m_executeStep(nullptr)
    , m_imageWidget(nullptr)
{
    setupUI();
    connectSignals();
    registerPlugins();
    updatePluginList();
    setupWorker();

    // Initialize camera selection after signals are connected
    m_cameraStep->selectFirstCamera();

    // Initialize vision directory for profile list
    updateVisionDirectory();

    // Auto-capture first frame after a short delay (let camera connect)
    QTimer::singleShot(500, this, [this]() {
        if (m_frameSubscriber && m_frameSubscriber->IsCameraConnected() && m_frameSubscriber->IsCameraGrabbing()) {
            onCaptureClicked();
            std::cout << "[VisionManagerPage] Auto-captured initial frame" << std::endl;
        }
    });

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Vision Manager page initialized (wizard mode)", "VisionManagerPage");
    }
}

VisionManagerPage::~VisionManagerPage()
{
    // Stop live timer
    if (m_liveTimer) {
        m_liveTimer->stop();
    }

    // Stop worker thread
    if (m_worker) {
        m_worker->stopWorkerThread();
        delete m_worker;
        m_worker = nullptr;
        std::cout << "[VisionManagerPage] Worker thread stopped" << std::endl;
    }

    // Unsubscribe from camera manager
    if (m_frameSubscriber && m_context && m_context->cameraManager) {
        m_context->cameraManager->UnsubscribeFromFrames(m_frameSubscriber->GetSubscriberId());
        std::cout << "[VisionManagerPage] Unsubscribed from camera" << std::endl;
    }
}

void VisionManagerPage::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Top section: Back button and title
    QHBoxLayout* topLayout = new QHBoxLayout();

    m_backButton = new QPushButton("Back", this);
    m_backButton->setToolTip("Return to main menu");
    m_backButton->setMinimumWidth(80);
    connect(m_backButton, &QPushButton::clicked, this, &VisionManagerPage::backToMainMenu);

    QLabel* titleLabel = new QLabel("Vision Manager", this);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");

    topLayout->addWidget(m_backButton);
    topLayout->addWidget(titleLabel);
    topLayout->addStretch();
    mainLayout->addLayout(topLayout);

    // Main content: 2-panel splitter. Column 1 (nav) is locked at 300 px
    // and the splitter handle is hidden so the user cannot drag it.
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);

    // Left panel: Navigation — fixed 300 px, non-resizable
    m_navWidget = new VisionNavWidget(m_context, this);
    m_navWidget->setFixedWidth(300);
    m_mainSplitter->addWidget(m_navWidget);

    // Right panel: Content stack
    m_contentStack = new QStackedWidget(this);

    // Page 0: Welcome
    m_welcomeWidget = new QWidget();
    QVBoxLayout* welcomeLayout = new QVBoxLayout(m_welcomeWidget);
    welcomeLayout->setAlignment(Qt::AlignCenter);
    QLabel* welcomeLabel = new QLabel(
        "Welcome to Vision Manager\n\n"
        "1. Plugin Selection - Choose which vision algorithm to use\n"
        "2. Camera & Lighting - Configure camera and lighting settings\n"
        "3. Plugin Parameters - Configure the selected plugin\n"
        "4. Execute & Tune - Run vision processing and view results\n\n"
        "Select a step from the left panel, or load a profile."
    );
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLabel->setStyleSheet("color: #aaa; font-size: 14px;");
    welcomeLayout->addWidget(welcomeLabel);
    m_contentStack->addWidget(m_welcomeWidget);  // index 0

    // Page 1: Plugin Selection (select which algorithm to use)
    m_pluginStep = new PluginConfigStepWidget(m_context, this);
    m_contentStack->addWidget(m_pluginStep);  // index 1

    // Page 2: Camera & Lighting
    m_cameraStep = new CameraLightingStepWidget(m_context, this);
    m_contentStack->addWidget(m_cameraStep);  // index 2

    // Page 3: Plugin Parameters (configure ROI + plugin-specific params)
    m_captureStep = new CaptureROIStepWidget(m_context, this);
    m_contentStack->addWidget(m_captureStep);  // index 3

    // Page 4: Execute & Tune
    m_executeStep = new ExecuteTuneStepWidget(m_context, this);
    m_contentStack->addWidget(m_executeStep);  // index 4

    m_mainSplitter->addWidget(m_contentStack);

    // Nav pane is fixed width; content pane takes all remaining space.
    // Handle width 0 hides the drag grip so the layout is truly locked.
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setHandleWidth(0);
    m_mainSplitter->setCollapsible(0, false);
    m_mainSplitter->setCollapsible(1, false);

    mainLayout->addWidget(m_mainSplitter, 1);

    // Create shared image widget (initially unparented from any step)
    m_imageWidget = new VisionImageWidget(this);
    m_imageWidget->setVisible(false);  // Hidden until a step embeds it

    // Live mode timer
    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(100);  // 10 fps live update
    connect(m_liveTimer, &QTimer::timeout, this, &VisionManagerPage::onLiveTimerTick);

    // Set shared state pointers on step widgets
    m_cameraStep->setState(&m_state);
    m_captureStep->setState(&m_state);
    m_pluginStep->setState(&m_state);
    m_executeStep->setState(&m_state);

    std::cout << "[VisionManagerPage] UI setup complete (wizard mode)" << std::endl;
}

void VisionManagerPage::connectSignals()
{
    // Navigation signals
    connect(m_navWidget, &VisionNavWidget::navItemChanged,
            this, &VisionManagerPage::onNavItemChanged);

    // Profile management signals
    connect(m_navWidget, &VisionNavWidget::newProfileClicked,
            this, &VisionManagerPage::onNewProfile);
    connect(m_navWidget, &VisionNavWidget::loadProfileClicked,
            this, &VisionManagerPage::onLoadProfile);
    connect(m_navWidget, &VisionNavWidget::saveProfileClicked,
            this, &VisionManagerPage::onSaveProfile);
    connect(m_navWidget, &VisionNavWidget::saveAsProfileClicked,
            this, &VisionManagerPage::onSaveAsProfile);
    connect(m_navWidget, &VisionNavWidget::deleteProfileClicked,
            this, &VisionManagerPage::onDeleteProfile);
    connect(m_navWidget, &VisionNavWidget::profileLoadRequested,
            this, &VisionManagerPage::onProfileLoadRequested);
    connect(m_navWidget, &VisionNavWidget::profileDeleted,
            this, &VisionManagerPage::onProfileDeleted);
    connect(m_navWidget, &VisionNavWidget::profileRenamed,
            this, &VisionManagerPage::onProfileRenamed);

    // Image mode
    connect(m_navWidget, &VisionNavWidget::imageModeChanged,
            this, &VisionManagerPage::onImageModeChanged);

    // Plugin selection from nav panel
    connect(m_navWidget, &VisionNavWidget::pluginSelected,
            this, &VisionManagerPage::onPluginSelected);

    // Step 1: Camera & Lighting signals
    connect(m_cameraStep, &CameraLightingStepWidget::cameraSelected,
            this, &VisionManagerPage::onCameraSelected);
    connect(m_cameraStep, &CameraLightingStepWidget::cameraSettingsChanged,
            this, &VisionManagerPage::onCameraSettingsChanged);
    connect(m_cameraStep, &CameraLightingStepWidget::lightingSettingsChanged,
            this, &VisionManagerPage::onLightingSettingsChanged);

    // Step 3: Plugin Parameters + ROI signals
    connect(m_captureStep, &CaptureROIStepWidget::captureClicked,
            this, &VisionManagerPage::onExecuteRequested);
    connect(m_captureStep, &CaptureROIStepWidget::roiChanged,
            this, &VisionManagerPage::onROIChanged);
    connect(m_captureStep, &CaptureROIStepWidget::roiEnabledChanged,
            this, &VisionManagerPage::onROIEnabledChanged);
    connect(m_captureStep, &CaptureROIStepWidget::searchBoxChanged,
            this, &VisionManagerPage::onSearchBoxChanged);
    connect(m_captureStep, &CaptureROIStepWidget::searchBoxEnabledChanged,
            this, &VisionManagerPage::onSearchBoxEnabledChanged);

    // Step 3: Plugin config signals (plugin selection is via nav dialog)
    connect(m_pluginStep, &PluginConfigStepWidget::applyParamsClicked,
            this, &VisionManagerPage::onExecuteRequested);
    connect(m_pluginStep, &PluginConfigStepWidget::liveExecuteRequested,
            this, &VisionManagerPage::onExecuteRequested);

    // Step 4: Execute signals
    connect(m_executeStep, &ExecuteTuneStepWidget::executeClicked,
            this, &VisionManagerPage::onExecuteRequested);
    connect(m_executeStep, &ExecuteTuneStepWidget::stopClicked,
            this, &VisionManagerPage::onStopRequested);
    connect(m_executeStep, &ExecuteTuneStepWidget::reliabilityTestClicked,
            this, &VisionManagerPage::onReliabilityTestRequested);

    // Image widget signals (ROI/SearchBox/Template drag events)
    connect(m_imageWidget, &VisionImageWidget::roiDragged,
            this, [this](int sx, int sy, int ex, int ey) {
        m_captureStep->setROI(sx, sy, ex, ey);
        VisionROI roi(sx, sy, ex, ey);
        roi.setEnabled(m_captureStep->isROIEnabled());
        VisionManager::instance().setROI(roi);
        if (m_state.selectedPluginName == "PatternMatching") {
            updatePatternMatchingSourceImage();
        }
    });
    connect(m_imageWidget, &VisionImageWidget::searchBoxDragged,
            this, [this](int sx, int sy, int ex, int ey) {
        m_captureStep->setSearchBox(sx, sy, ex, ey);
    });
    connect(m_imageWidget, &VisionImageWidget::templateSelectionDragged,
            this, &VisionManagerPage::onTemplateSelectionDragged);

    // Test-mode injection signals
    connect(m_imageWidget, &VisionImageWidget::injectionModeToggled,
            this, &VisionManagerPage::onInjectionModeToggled);
    connect(m_imageWidget, &VisionImageWidget::loadInjectedImageRequested,
            this, &VisionManagerPage::onLoadInjectedImageRequested);

    std::cout << "[VisionManagerPage] Signals connected" << std::endl;
}

// ========== Navigation ==========

void VisionManagerPage::onNavItemChanged(int step)
{
    if (step < 0 || step > 3) {
        m_contentStack->setCurrentIndex(0);
        return;
    }

    // Reparent image widget to the new step
    reparentImageWidget(step);

    // Switch stacked widget (step + 1 because index 0 is welcome)
    m_contentStack->setCurrentIndex(step + 1);
    m_currentStep = step;

    std::cout << "[VisionManagerPage] Switched to step " << (step + 1) << std::endl;
}

void VisionManagerPage::reparentImageWidget(int targetStep)
{
    if (!m_imageWidget) return;

    // Remove from current step
    // Step order: 0=Plugin Selection, 1=Camera&Lighting, 2=Plugin Params, 3=Execute
    switch (m_currentStep) {
    case 0: m_pluginStep->removeImageWidget(); break;
    case 1: m_cameraStep->removeImageWidget(); break;
    case 2: m_captureStep->removeImageWidget(); break;
    case 3: m_executeStep->removeImageWidget(); break;
    default: break;
    }

    // Embed into new step (all 4 steps show the image)
    switch (targetStep) {
    case 0:  // Plugin Selection
        m_pluginStep->embedImageWidget(m_imageWidget);
        break;
    case 1:  // Camera & Lighting
        m_cameraStep->embedImageWidget(m_imageWidget);
        break;
    case 2:  // Plugin Parameters
        m_captureStep->embedImageWidget(m_imageWidget);
        break;
    case 3:  // Execute & Tune
        m_executeStep->embedImageWidget(m_imageWidget);
        break;
    default:
        m_imageWidget->setParent(this);
        m_imageWidget->setVisible(false);
        break;
    }
}

// ========== Profile Management ==========

void VisionManagerPage::onNewProfile()
{
    std::cout << "[VisionManagerPage] New Profile clicked" << std::endl;
    VisionManager::instance().newRecipe();

    m_state.currentProfilePath.clear();
    m_state.currentRecipePath.clear();
    m_state.profileModified = false;
    m_state.selectedPluginName.clear();
    m_navWidget->setProfileName("(new)");
    refreshCautionIcons();

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("New vision profile created", "VisionManagerPage");
    }
}

void VisionManagerPage::onLoadProfile()
{
    std::cout << "[VisionManagerPage] Load Profile clicked" << std::endl;

    QString visionDir;
    if (m_context && m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
        QString productFilePath = QString::fromStdString(m_context->productPositionManager->GetCurrentProductPath());
        QString productDir = QFileInfo(productFilePath).path();
        visionDir = productDir + "/vision";
    } else {
        visionDir = "config/vision";
    }

    ProfileSelectionDialog dialog(visionDir, this);
    if (dialog.exec() == QDialog::Accepted) {
        QString filePath = dialog.getSelectedFilePath();
        if (!filePath.isEmpty()) {
            loadProfileFromFile(filePath);
        }
    }
}

void VisionManagerPage::onSaveProfile()
{
    std::cout << "[VisionManagerPage] Save Profile clicked" << std::endl;

    if (!m_state.currentProfilePath.isEmpty()) {
        saveProfileToPath(m_state.currentProfilePath);
        return;
    }

    if (!m_state.currentRecipePath.isEmpty()) {
        saveRecipeToPath(m_state.currentRecipePath);
        return;
    }

    onSaveAsProfile();
}

void VisionManagerPage::onSaveAsProfile()
{
    std::cout << "[VisionManagerPage] Save As Profile clicked" << std::endl;

    if (!m_context || !m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
        QMessageBox::warning(this, "No Product Loaded",
            "Please load a product first before saving vision profiles.\n\n"
            "Vision profiles are saved to: config/[product_name]/vision/");
        return;
    }

    QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
    QString productFilePath = QString::fromStdString(m_context->productPositionManager->GetCurrentProductPath());
    QString productDir = QFileInfo(productFilePath).path();
    QString visionDir = productDir + "/vision";

    QDir dir;
    if (!dir.exists(visionDir)) {
        if (!dir.mkpath(visionDir)) {
            QMessageBox::critical(this, "Error", "Failed to create vision directory:\n" + visionDir);
            return;
        }
    }

    bool ok = false;
    QString profileName = QInputDialog::getText(this, "Save Vision Profile",
        "Enter profile name:\n(Will be saved to: " + visionDir + ")",
        QLineEdit::Normal, "vision_profile", &ok);

    if (!ok || profileName.isEmpty()) {
        return;
    }

    profileName.replace(" ", "_");
    profileName.replace("/", "_");
    profileName.replace("\\", "_");

    QString filePath = visionDir + "/" + profileName + ".json";

    if (QFile::exists(filePath)) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "Overwrite?",
            "File already exists:\n" + filePath + "\n\nDo you want to overwrite?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    saveProfileToPath(filePath);
}

void VisionManagerPage::onDeleteProfile()
{
    std::cout << "[VisionManagerPage] Delete Profile clicked" << std::endl;

    if (m_state.currentProfilePath.isEmpty()) {
        QMessageBox::information(this, "No Profile", "No profile is currently loaded to delete.");
        return;
    }

    QFileInfo fileInfo(m_state.currentProfilePath);
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Delete Profile",
        QString("Are you sure you want to delete?\n\n%1").arg(fileInfo.fileName()),
        QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    QFile file(m_state.currentProfilePath);
    if (file.remove()) {
        m_state.currentProfilePath.clear();
        m_navWidget->setProfileName("(none)");
        refreshProfileList();
    } else {
        QMessageBox::critical(this, "Error", "Failed to delete profile.");
    }
}

void VisionManagerPage::onProfileLoadRequested(const QString& filePath)
{
    loadProfileFromFile(filePath);
}

void VisionManagerPage::onProfileDeleted(const QString& filePath)
{
    if (m_state.currentProfilePath == filePath) {
        m_state.currentProfilePath.clear();
        m_state.profileModified = false;
        m_navWidget->setProfileName("(none)");
    }
}

void VisionManagerPage::onProfileRenamed(const QString& oldPath, const QString& newPath)
{
    if (m_state.currentProfilePath == oldPath) {
        m_state.currentProfilePath = newPath;
        m_navWidget->setProfileName(QFileInfo(newPath).baseName());
    }
}

void VisionManagerPage::loadProfileFromFile(const QString& filePath)
{
    std::cout << "[VisionManagerPage] Loading profile: " << filePath.toStdString() << std::endl;

    if (filePath.isEmpty()) return;

    try {
        std::ifstream file(filePath.toStdString());
        if (!file.is_open()) {
            m_executeStep->displayResult("Error: Cannot open file\n" + filePath);
            return;
        }

        nlohmann::json j;
        file >> j;
        file.close();

        // Load ROI settings
        if (j.contains("roi")) {
            auto roiJson = j["roi"];
            int startX = roiJson.value("startX", 0);
            int startY = roiJson.value("startY", 0);
            int endX = roiJson.value("endX", 640);
            int endY = roiJson.value("endY", 480);
            bool enabled = roiJson.value("enabled", false);

            m_captureStep->setROI(startX, startY, endX, endY);
            m_captureStep->setROIEnabled(enabled);
            m_imageWidget->setROI(startX, startY, endX, endY);
            m_imageWidget->setROIEnabled(enabled);

            VisionROI roi(startX, startY, endX, endY);
            roi.setEnabled(enabled);
            VisionManager::instance().setROI(roi);
            m_state.roiEnabled = enabled;
        }

        // Load Search Box settings
        if (j.contains("searchBox")) {
            auto searchJson = j["searchBox"];
            int startX = searchJson.value("startX", 0);
            int startY = searchJson.value("startY", 0);
            int endX = searchJson.value("endX", 800);
            int endY = searchJson.value("endY", 600);
            bool enabled = searchJson.value("enabled", false);

            m_captureStep->setSearchBox(startX, startY, endX, endY);
            m_captureStep->setSearchBoxEnabled(enabled);
            m_imageWidget->setSearchBox(startX, startY, endX, endY);
            m_imageWidget->setSearchBoxEnabled(enabled);
            m_state.searchBoxEnabled = enabled;
            m_state.searchBoxStartX = startX;
            m_state.searchBoxStartY = startY;
            m_state.searchBoxEndX = endX;
            m_state.searchBoxEndY = endY;
        }

        // Load camera settings
        if (j.contains("camera")) {
            auto camJson = j["camera"];
            CameraLightingStepWidget::CameraSettings camSettings;
            camSettings.cameraId = QString::fromStdString(camJson.value("cameraId", ""));
            camSettings.exposureTime = camJson.value("exposureTime", 1000);
            camSettings.gain = camJson.value("gain", 1.0);
            camSettings.autoExposure = camJson.value("autoExposure", false);
            camSettings.autoGain = camJson.value("autoGain", false);

            m_cameraStep->setCameraSettings(camSettings);

            double pixelToMmX = camJson.value("pixelToMmX", 0.00248);
            double pixelToMmY = camJson.value("pixelToMmY", 0.00248);
            VisionManager::instance().setPixelToMm(pixelToMmX, pixelToMmY);

            m_cameraStep->applyCameraSettings();
            m_state.selectedCameraId = camSettings.cameraId;
        }

        // Load lighting settings
        if (j.contains("lighting")) {
            auto lightJson = j["lighting"];
            CameraLightingStepWidget::LightingSettings lightSettings;
            lightSettings.enabled = lightJson.value("enabled", false);
            lightSettings.controllerId = QString::fromStdString(lightJson.value("controllerId", ""));

            if (lightJson.contains("channels") && lightJson["channels"].is_array()) {
                for (const auto& chJson : lightJson["channels"]) {
                    int channel = chJson.value("channel", 0);
                    int intensity = chJson.value("intensity", 0);
                    bool on = chJson.value("on", false);
                    lightSettings.channelIntensities[channel] = intensity;
                    lightSettings.channelStates[channel] = on;
                }
            }

            m_cameraStep->setLightingSettings(lightSettings);
            m_cameraStep->applyLightingSettings();
        }

        // Load plugin and parameters
        if (j.contains("plugin")) {
            auto pluginJson = j["plugin"];
            QString pluginName = QString::fromStdString(pluginJson.value("name", ""));

            if (!pluginName.isEmpty()) {
                auto plugin = VisionManager::instance().getPlugin(pluginName);
                if (plugin) {
                    m_state.selectedPluginName = pluginName;
                    m_navWidget->setSelectedPluginName(pluginName);
                    // Create config widget FIRST (so UI elements exist)
                    loadPluginConfig(pluginName);
                    // THEN apply saved parameters (updates the UI elements)
                    if (pluginJson.contains("parameters")) {
                        plugin->setParameters(pluginJson["parameters"]);
                    }
                }
            }
        }

        // Store profile path
        m_state.currentProfilePath = filePath;
        m_state.currentRecipePath.clear();
        m_state.profileModified = false;
        m_navWidget->setProfileName(QFileInfo(filePath).baseName());

        // Navigate to Execute & Tune step
        m_navWidget->setCurrentStep(3);

        // Update caution icons
        refreshCautionIcons();

        if (m_context && m_context->logManager) {
            m_context->logManager->Info("Loaded vision profile: " + filePath.toStdString(), "VisionManagerPage");
        }

        m_executeStep->displayResult("Profile loaded: " + QFileInfo(filePath).baseName() +
                                      "\nReady to execute.");

    } catch (const std::exception& e) {
        m_executeStep->displayResult("Error loading profile:\n" + QString::fromStdString(e.what()));
    }
}

// ========== Image Mode ==========

void VisionManagerPage::onImageModeChanged(bool isLive)
{
    if (isLive) {
        m_liveTimer->start();
        std::cout << "[VisionManagerPage] Live mode started" << std::endl;
    } else {
        m_liveTimer->stop();
        std::cout << "[VisionManagerPage] Frame mode (live stopped)" << std::endl;
    }
}

void VisionManagerPage::onLiveTimerTick()
{
    if (m_useInjectedImage) {
        return;  // Test mode: never overwrite injected image with live frame
    }

    if (!m_frameSubscriber || !m_frameSubscriber->IsCameraConnected() || !m_frameSubscriber->IsCameraGrabbing()) {
        return;
    }

    CameraFrameData frame = m_frameSubscriber->GetLatestFrame();
    if (!frame.IsValid()) return;

    QImage::Format format = (frame.channels == 3) ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
    QImage qImage(frame.imageData.data(), frame.width, frame.height,
                  frame.width * frame.channels, format);
    m_imageWidget->setImage(QPixmap::fromImage(qImage));
}

// ========== Test-mode Image Injection ==========

void VisionManagerPage::onInjectionModeToggled(bool enabled)
{
    m_useInjectedImage = enabled;

    if (enabled) {
        // Stop the live camera stream so it cannot overwrite the injected image
        if (m_liveTimer && m_liveTimer->isActive()) {
            m_liveTimer->stop();
        }

        // If we already have a previously loaded injected image, re-display it
        if (!m_injectedImage.empty()) {
            VisionManager::instance().setInputImage(m_injectedImage);
            m_state.hasValidImage = true;
            refreshCautionIcons();
        }

        if (m_context && m_context->logManager) {
            m_context->logManager->Info("Vision test-mode injection ENABLED", "VisionManagerPage");
        }
    } else {
        // Drop the injected image and clear the display
        m_injectedImage.release();
        m_injectedImagePath.clear();
        m_imageWidget->setInjectedFileName(QString());
        m_imageWidget->clearImage();
        m_imageWidget->clearResultMarkers();
        m_imageWidget->clearOverlays();
        VisionManager::instance().clearImages();
        m_state.hasValidImage = false;
        refreshCautionIcons();

        if (m_context && m_context->logManager) {
            m_context->logManager->Info("Vision test-mode injection DISABLED", "VisionManagerPage");
        }
    }
}

void VisionManagerPage::onLoadInjectedImageRequested()
{
    QString path = QFileDialog::getOpenFileName(
        this,
        "Select Test Image",
        QString(),
        "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)");

    if (path.isEmpty()) {
        return;
    }

#ifdef HAS_OPENCV
    cv::Mat loaded = cv::imread(path.toStdString(), cv::IMREAD_UNCHANGED);
    if (loaded.empty()) {
        m_executeStep->displayResult("Failed to load image:\n" + path);
        if (m_context && m_context->logManager) {
            m_context->logManager->Error("Failed to load injected image: " + path.toStdString(), "VisionManagerPage");
        }
        return;
    }

    // Normalize to formats vision plugins accept (1 or 3 channels).
    // Drop alpha from RGBA images so plugins like CircleDetect (which only
    // supports 1/3-channel input) can process injected PNGs with transparency.
    if (loaded.channels() == 4) {
        cv::Mat bgr;
        cv::cvtColor(loaded, bgr, cv::COLOR_BGRA2BGR);
        loaded = bgr;
    } else if (loaded.channels() != 1 && loaded.channels() != 3) {
        m_executeStep->displayResult("Unsupported image channel count: " +
                                     QString::number(loaded.channels()));
        return;
    }

    m_injectedImage = loaded;
    m_injectedImagePath = path;

    // Build a QImage for display
    QImage qImage;
    if (loaded.channels() == 1) {
        qImage = QImage(loaded.data, loaded.cols, loaded.rows,
                        static_cast<int>(loaded.step), QImage::Format_Grayscale8).copy();
    } else {  // 3 channels (BGR)
        cv::Mat rgb;
        cv::cvtColor(loaded, rgb, cv::COLOR_BGR2RGB);
        qImage = QImage(rgb.data, rgb.cols, rgb.rows,
                        static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
    }

    m_imageWidget->setImage(QPixmap::fromImage(qImage));
    m_imageWidget->clearResultMarkers();
    m_imageWidget->clearOverlays();
    m_imageWidget->setInjectedFileName(QFileInfo(path).fileName());

    // Push to VisionManager (BGR/grayscale format, matching onCaptureClicked behavior)
    VisionManager::instance().setInputImage(m_injectedImage);
    m_state.hasValidImage = true;
    refreshCautionIcons();

    if (m_state.selectedPluginName == "PatternMatching") {
        updatePatternMatchingSourceImage();
    }

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Loaded injected test image: " + path.toStdString() +
                                    " (" + std::to_string(loaded.cols) + "x" +
                                    std::to_string(loaded.rows) + ", " +
                                    std::to_string(loaded.channels()) + " channels)",
                                    "VisionManagerPage");
    }
#else
    m_executeStep->displayResult("OpenCV not available - cannot load injected image.");
#endif
}

// ========== Step 1: Camera & Lighting ==========

void VisionManagerPage::onCameraSelected(const QString& cameraId)
{
    std::cout << "[VisionManagerPage] Camera selected: " << cameraId.toStdString() << std::endl;

    m_state.selectedCameraId = cameraId;

    // Unsubscribe old subscriber
    if (m_frameSubscriber && m_context && m_context->cameraManager) {
        m_context->cameraManager->UnsubscribeFromFrames(m_frameSubscriber->GetSubscriberId());
    }

    if (m_context && m_context->cameraManager) {
        std::string cameraIdStr = cameraId.toStdString();

        auto cameraStatus = m_context->cameraManager->GetCameraStatus(cameraIdStr);

        // Auto-connect if not connected
        if (!cameraStatus.connected) {
            m_context->cameraManager->ConnectCamera(cameraIdStr);
            cameraStatus = m_context->cameraManager->GetCameraStatus(cameraIdStr);
        }

        // Auto-start grabbing if connected but not grabbing
        if (cameraStatus.connected && !cameraStatus.grabbing) {
            m_context->cameraManager->StartGrabbingWithBroadcast(cameraIdStr);
            cameraStatus = m_context->cameraManager->GetCameraStatus(cameraIdStr);
        }

        m_frameSubscriber = std::make_shared<LiveVideoSubscriber>(cameraIdStr, "VisionManager");
        m_context->cameraManager->SubscribeToFrames(m_frameSubscriber);
        m_frameSubscriber->InitializeStatusFromManager(m_context->cameraManager);

        m_state.cameraConnected = m_frameSubscriber->IsCameraConnected();
        m_state.cameraGrabbing = m_frameSubscriber->IsCameraGrabbing();

        if (m_context->logManager) {
            m_context->logManager->Info("Subscribed to camera: " + cameraIdStr, "VisionManagerPage");
        }
    }

    refreshCautionIcons();
}

void VisionManagerPage::onCameraSettingsChanged()
{
    std::cout << "[VisionManagerPage] Camera settings changed" << std::endl;

    if (m_frameSubscriber && m_frameSubscriber->IsCameraConnected() && m_frameSubscriber->IsCameraGrabbing()) {
        QTimer::singleShot(100, this, [this]() {
            onCaptureClicked();
        });
    }
}

void VisionManagerPage::onLightingSettingsChanged()
{
    std::cout << "[VisionManagerPage] Lighting settings changed" << std::endl;
}

// ========== Step 2: Capture & ROI ==========

void VisionManagerPage::onCaptureClicked()
{
    std::cout << "[VisionManagerPage] Capture clicked" << std::endl;

    if (m_useInjectedImage) {
        if (m_injectedImage.empty()) {
            m_executeStep->displayResult("Test Mode: no image loaded.\n\nClick 'Load Image...' above the view.");
            return;
        }
        // Re-push the injected image into VisionManager (in case it was cleared)
        VisionManager::instance().setInputImage(m_injectedImage);
        m_state.hasValidImage = true;
        refreshCautionIcons();
        if (m_state.selectedPluginName == "PatternMatching") {
            updatePatternMatchingSourceImage();
        }
        return;
    }

    if (!m_frameSubscriber) {
        m_executeStep->displayResult("Error: No camera selected");
        return;
    }

    if (!m_frameSubscriber->IsCameraConnected() || !m_frameSubscriber->IsCameraGrabbing()) {
        m_executeStep->displayResult("Error: Camera not connected or not grabbing");
        return;
    }

    CameraFrameData frame = m_frameSubscriber->GetLatestFrame();
    if (!frame.IsValid()) {
        m_executeStep->displayResult("Error: No valid frame available");
        return;
    }

    // Convert to QImage for display
    QImage::Format format = (frame.channels == 3) ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
    QImage qImage(frame.imageData.data(), frame.width, frame.height,
                  frame.width * frame.channels, format);
    m_imageWidget->setImage(QPixmap::fromImage(qImage));
    m_imageWidget->clearResultMarkers();
    m_imageWidget->clearOverlays();

    // Convert to cv::Mat for VisionManager
    cv::Mat cvImage;
    if (frame.channels == 3) {
        cvImage = cv::Mat(frame.height, frame.width, CV_8UC3, frame.imageData.data()).clone();
        cv::cvtColor(cvImage, cvImage, cv::COLOR_RGB2BGR);
    } else {
        cvImage = cv::Mat(frame.height, frame.width, CV_8UC1, frame.imageData.data()).clone();
    }
    VisionManager::instance().setInputImage(cvImage);

    m_state.hasValidImage = true;
    refreshCautionIcons();

    if (m_state.selectedPluginName == "PatternMatching") {
        updatePatternMatchingSourceImage();
    }

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Image captured from " + m_state.selectedCameraId.toStdString(), "VisionManagerPage");
    }
}

void VisionManagerPage::onClearClicked()
{
    std::cout << "[VisionManagerPage] Clear clicked" << std::endl;
    m_imageWidget->clearImage();
    m_imageWidget->clearResultMarkers();
    m_imageWidget->clearOverlays();
    VisionManager::instance().clearImages();
    m_state.hasValidImage = false;
    refreshCautionIcons();
}

void VisionManagerPage::onROIChanged(int startX, int startY, int endX, int endY)
{
    VisionROI roi(startX, startY, endX, endY);
    roi.setEnabled(m_captureStep->isROIEnabled());
    VisionManager::instance().setROI(roi);
    m_imageWidget->setROI(startX, startY, endX, endY);

    if (m_state.selectedPluginName == "PatternMatching") {
        updatePatternMatchingSourceImage();
    }
}

void VisionManagerPage::onROIEnabledChanged(bool enabled)
{
    VisionManager::instance().setROIEnabled(enabled);
    m_imageWidget->setROIEnabled(enabled);
    m_state.roiEnabled = enabled;
}

void VisionManagerPage::onSearchBoxChanged(int startX, int startY, int endX, int endY)
{
    m_imageWidget->setSearchBox(startX, startY, endX, endY);
    m_state.searchBoxStartX = startX;
    m_state.searchBoxStartY = startY;
    m_state.searchBoxEndX = endX;
    m_state.searchBoxEndY = endY;
}

void VisionManagerPage::onSearchBoxEnabledChanged(bool enabled)
{
    m_imageWidget->setSearchBoxEnabled(enabled);
    m_state.searchBoxEnabled = enabled;
}

// ========== Step 3: Plugin ==========

void VisionManagerPage::onPluginSelected(const QString& pluginName)
{
    std::cout << "[VisionManagerPage] Plugin selected: " << pluginName.toStdString() << std::endl;
    m_state.selectedPluginName = pluginName;
    m_state.pluginHasValidParams = true;
    m_navWidget->setSelectedPluginName(pluginName);
    loadPluginConfig(pluginName);
    refreshCautionIcons();
}

// ========== Step 4: Execute ==========

void VisionManagerPage::onExecuteRequested()
{
    std::cout << "[VisionManagerPage] Execute requested" << std::endl;

    if (m_state.selectedPluginName.isEmpty()) {
        m_executeStep->displayResult("Error: No plugin selected\n\nPlease select a plugin in Step 3.");
        return;
    }

#ifdef HAS_OPENCV
    if (m_worker && m_worker->isProcessing()) {
        m_executeStep->displayResult("Processing already in progress...");
        return;
    }

    // Auto-recapture fresh image before execution
    cv::Mat inputImage;
    if (m_useInjectedImage) {
        if (m_injectedImage.empty()) {
            m_executeStep->displayResult("Test Mode: no image loaded.\n\nClick 'Load Image...' above the view.");
            return;
        }
        inputImage = m_injectedImage.clone();
        VisionManager::instance().setInputImage(inputImage);
        m_state.hasValidImage = true;
    } else if (m_frameSubscriber && m_frameSubscriber->IsCameraConnected() && m_frameSubscriber->IsCameraGrabbing()) {
        QThread::msleep(100);

        CameraFrameData frame = m_frameSubscriber->GetLatestFrame();
        if (frame.IsValid()) {
            QImage::Format format = (frame.channels == 3) ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
            QImage qImage(frame.imageData.data(), frame.width, frame.height,
                          frame.width * frame.channels, format);
            m_imageWidget->setImage(QPixmap::fromImage(qImage));
            m_imageWidget->clearResultMarkers();
            m_imageWidget->clearOverlays();

            if (frame.channels == 3) {
                inputImage = cv::Mat(frame.height, frame.width, CV_8UC3, frame.imageData.data()).clone();
                cv::cvtColor(inputImage, inputImage, cv::COLOR_RGB2BGR);
            } else {
                inputImage = cv::Mat(frame.height, frame.width, CV_8UC1, frame.imageData.data()).clone();
            }
            VisionManager::instance().setInputImage(inputImage);
            m_state.hasValidImage = true;
        } else {
            inputImage = VisionManager::instance().getInputImage();
        }
    } else {
        inputImage = VisionManager::instance().getInputImage();
    }

    if (inputImage.empty()) {
        m_executeStep->displayResult("Error: No image captured\n\nPlease capture an image in Step 2.");
        return;
    }

    VisionROI currentROI = VisionManager::instance().getROI();

    if (!m_worker) {
        m_executeStep->displayResult("Error: Vision processing worker not available.");
        return;
    }

    bool useSearchBox = m_state.searchBoxEnabled;
    int searchStartX = m_state.searchBoxStartX;
    int searchStartY = m_state.searchBoxStartY;
    int searchEndX = m_state.searchBoxEndX;
    int searchEndY = m_state.searchBoxEndY;

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Starting execution of plugin: " + m_state.selectedPluginName.toStdString(), "VisionManagerPage");
    }

    m_worker->executePlugin(m_state.selectedPluginName, inputImage, currentROI, useSearchBox,
                           searchStartX, searchStartY, searchEndX, searchEndY);
#else
    m_executeStep->displayResult("Error: OpenCV not available");
#endif
}

void VisionManagerPage::onStopRequested()
{
    std::cout << "[VisionManagerPage] Stop requested" << std::endl;

#ifdef HAS_OPENCV
    if (m_worker && m_worker->isProcessing()) {
        m_worker->cancelProcessing();
        m_executeStep->displayResult("Cancelling processing...");
        return;
    }
#endif
}

void VisionManagerPage::onReliabilityTestRequested()
{
    std::cout << "[VisionManagerPage] Reliability Test requested" << std::endl;

    if (m_state.selectedPluginName.isEmpty()) {
        QMessageBox::warning(this, "No Plugin Selected",
                             "Please select a vision plugin before running a reliability test.");
        return;
    }

#ifdef HAS_OPENCV
    VisionReliabilityTestDialog dialog(
        m_context,
        m_state.selectedPluginName,
        m_frameSubscriber,
        m_state.searchBoxEnabled,
        m_state.searchBoxStartX, m_state.searchBoxStartY,
        m_state.searchBoxEndX,   m_state.searchBoxEndY,
        this);
    dialog.exec();
#else
    QMessageBox::warning(this, "OpenCV Not Available",
                         "Vision reliability testing requires OpenCV.");
#endif
}

// ========== Template Selection ==========

void VisionManagerPage::onTemplateSelectionDragged(int /*startX*/, int /*startY*/, int /*endX*/, int /*endY*/)
{
    // Just update display - no other action needed while dragging
}

void VisionManagerPage::onTemplateSelectionConfirmed()
{
    if (m_state.templateSelectionMode && m_state.templateSelectionOnConfirm) {
        int startX, startY, endX, endY;
        m_imageWidget->getTemplateSelection(startX, startY, endX, endY);
        m_state.templateSelectionOnConfirm(startX, startY, endX, endY);
        stopTemplateSelectionMode();
    }
}

void VisionManagerPage::onTemplateSelectionCancelled()
{
    if (m_state.templateSelectionMode) {
        if (m_state.templateSelectionOnCancel) {
            m_state.templateSelectionOnCancel();
        }
        stopTemplateSelectionMode();
    }
}

void VisionManagerPage::startTemplateSelectionMode(std::function<void(int, int, int, int)> onConfirm,
                                                   std::function<void()> onCancel)
{
    m_state.templateSelectionMode = true;
    m_state.templateSelectionOnConfirm = onConfirm;
    m_state.templateSelectionOnCancel = onCancel;
    m_imageWidget->setTemplateSelectionEnabled(true);

    std::cout << "[VisionManagerPage] Template selection mode started" << std::endl;
}

void VisionManagerPage::stopTemplateSelectionMode()
{
    m_state.templateSelectionMode = false;
    m_state.templateSelectionOnConfirm = nullptr;
    m_state.templateSelectionOnCancel = nullptr;
    m_imageWidget->setTemplateSelectionEnabled(false);

    std::cout << "[VisionManagerPage] Template selection mode stopped" << std::endl;
}

// ========== Plugin Registration (preserved) ==========

void VisionManagerPage::registerPlugins()
{
    std::cout << "[VisionManagerPage] Registering vision plugins..." << std::endl;

    auto circlePlugin = std::make_shared<CircleDetectionPlugin>();
    VisionManager::instance().registerPlugin(circlePlugin);

    auto edgePlugin = std::make_shared<EdgeDetectionPlugin>();
    VisionManager::instance().registerPlugin(edgePlugin);

    auto fiducialPlugin = std::make_shared<FiducialDetectorPlugin>();
    VisionManager::instance().registerPlugin(fiducialPlugin);

    auto linePlugin = std::make_shared<LineDetectorPlugin>();
    VisionManager::instance().registerPlugin(linePlugin);

    auto cornerPlugin = std::make_shared<CornerDetectorPlugin>();
    VisionManager::instance().registerPlugin(cornerPlugin);

    auto patternPlugin = std::make_shared<PatternMatchingPlugin>();
    VisionManager::instance().registerPlugin(patternPlugin);

    auto patternEdgePlugin = std::make_shared<PatternMatchingEdgePlugin>();
    VisionManager::instance().registerPlugin(patternEdgePlugin);

    auto imageProcessingPlugin = std::make_shared<ImageProcessingPlugin>();
    VisionManager::instance().registerPlugin(imageProcessingPlugin);

    auto calibrationPlatePlugin = std::make_shared<CalibrationPlatePlugin>();
    VisionManager::instance().registerPlugin(calibrationPlatePlugin);

#ifdef HAS_HALCON
    auto halconCirclePlugin = std::make_shared<HalconCircleDetectionPlugin>();
    VisionManager::instance().registerPlugin(halconCirclePlugin);

    auto halconCircleRobustPlugin = std::make_shared<HalconCircleDetectionRobustPlugin>();
    VisionManager::instance().registerPlugin(halconCircleRobustPlugin);

    auto halconLinePlugin = std::make_shared<HalconLineDetectionPlugin>();
    VisionManager::instance().registerPlugin(halconLinePlugin);

    auto halconPatternEdgePlugin = std::make_shared<HalconPatternMatchingEdgePlugin>();
    halconPatternEdgePlugin->setVisionManagerPage(this);
    VisionManager::instance().registerPlugin(halconPatternEdgePlugin);

    auto halconPatternNCCPlugin = std::make_shared<HalconPatternMatchingNCCPlugin>();
    halconPatternNCCPlugin->setVisionManagerPage(this);
    VisionManager::instance().registerPlugin(halconPatternNCCPlugin);

    // PatternMatchDescriptor is NOT registered — the plugin has unresolved
    // issues producing bogus matches with score=1.0 at nonsense coordinates,
    // and was failing to locate templates that NCC/Edge match correctly on
    // the same images. See comment block at the top of
    // HalconPatternMatchingDescriptorPlugin.h for details. The source files
    // are kept in the build so the work isn't lost; re-enable by
    // uncommenting this block once the match/homography issue is resolved.
    // auto halconPatternDescriptorPlugin = std::make_shared<HalconPatternMatchingDescriptorPlugin>();
    // halconPatternDescriptorPlugin->setVisionManagerPage(this);
    // VisionManager::instance().registerPlugin(halconPatternDescriptorPlugin);

    auto halconPatternGrayPlugin = std::make_shared<HalconPatternMatchingGrayPlugin>();
    halconPatternGrayPlugin->setVisionManagerPage(this);
    VisionManager::instance().registerPlugin(halconPatternGrayPlugin);

    auto halconBeamPlugin = std::make_shared<HalconBeamDetectionPlugin>();
    VisionManager::instance().registerPlugin(halconBeamPlugin);

    auto halconD4SigmaBeamPlugin = std::make_shared<HalconD4SigmaBeamDetectionPlugin>();
    VisionManager::instance().registerPlugin(halconD4SigmaBeamPlugin);
#endif

    if (m_context && m_context->logManager) {
        int count = VisionManager::instance().getPluginCount();
        m_context->logManager->Info("Registered " + std::to_string(count) + " vision plugins", "VisionManagerPage");
    }
}

void VisionManagerPage::updatePluginList()
{
    auto& vm = VisionManager::instance();
    QStringList prPlugins = vm.getPluginsByCategory("PR");
    QStringList measurePlugins = vm.getPluginsByCategory("Measurement");
    QStringList filterPlugins = vm.getPluginsByCategory("Filter");
    QStringList calibrationPlugins = vm.getPluginsByCategory("Calibration");

    m_navWidget->updatePluginsByCategory(prPlugins, measurePlugins, filterPlugins, calibrationPlugins);
}

void VisionManagerPage::loadPluginConfig(const QString& pluginName)
{
    std::cout << "[VisionManagerPage] Loading config for: " << pluginName.toStdString() << std::endl;

    m_pluginStep->clearPluginConfigWidget();

    auto plugin = VisionManager::instance().getPlugin(pluginName);
    if (!plugin) {
        std::cerr << "[VisionManagerPage] Plugin not found: " << pluginName.toStdString() << std::endl;
        return;
    }

    QWidget* configWidget = plugin->createConfigWidget();
    if (configWidget) {
        m_pluginStep->setPluginConfigWidget(configWidget);
    }

    // Helper: resolve template directory and profile name
    auto resolveTemplateDir = [this](std::string& outDir, std::string& outProfileName) {
        if (!m_state.currentProfilePath.isEmpty()) {
            QFileInfo profileInfo(m_state.currentProfilePath);
            QString productDir = profileInfo.absolutePath() + "/..";
            QString prDir = QDir::cleanPath(productDir + "/PRMatchingTemplate");
            outDir = prDir.toStdString();
            outProfileName = profileInfo.baseName().toStdString();
        } else if (m_context && m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
            QString productFilePath = QString::fromStdString(m_context->productPositionManager->GetCurrentProductPath());
            QString productDir = QFileInfo(productFilePath).path();
            QString prDir = productDir + "/PRMatchingTemplate";
            outDir = prDir.toStdString();
            outProfileName = "untitled";
        }
    };

    if (pluginName == "PatternMatching") {
        updatePatternMatchingSourceImage();
        auto pmPlugin = std::dynamic_pointer_cast<PatternMatchingPlugin>(plugin);
        if (pmPlugin) {
            pmPlugin->setVisionManagerPage(this);
        }
    }

    if (pluginName == "PatternMatchingEdge") {
        auto pmePlugin = std::dynamic_pointer_cast<PatternMatchingEdgePlugin>(plugin);
        if (pmePlugin) {
            pmePlugin->setVisionManagerPage(this);
            std::string templateDir, profileName;
            resolveTemplateDir(templateDir, profileName);
            if (!templateDir.empty()) {
                pmePlugin->setTemplateDirectory(templateDir);
                pmePlugin->setProfileName(profileName);
            }
        }
    }

#ifdef HAS_HALCON
    if (pluginName == "PatternMatchEdge (Halcon)") {
        auto halconPlugin = std::dynamic_pointer_cast<HalconPatternMatchingEdgePlugin>(plugin);
        if (halconPlugin) {
            halconPlugin->setVisionManagerPage(this);
            std::string templateDir, profileName;
            resolveTemplateDir(templateDir, profileName);
            if (!templateDir.empty()) {
                halconPlugin->setTemplateDirectory(templateDir);
                halconPlugin->setProfileName(profileName);
            }
        }
    }
    if (pluginName == "PatternMatchNCC (Halcon)") {
        auto halconPlugin = std::dynamic_pointer_cast<HalconPatternMatchingNCCPlugin>(plugin);
        if (halconPlugin) {
            halconPlugin->setVisionManagerPage(this);
            std::string templateDir, profileName;
            resolveTemplateDir(templateDir, profileName);
            if (!templateDir.empty()) {
                halconPlugin->setTemplateDirectory(templateDir);
                halconPlugin->setProfileName(profileName);
            }
        }
    }
    // PatternMatchDescriptor is disabled — see comment in plugin registration
    // block above. Template-dir resolution kept commented out to match.
    // if (pluginName == "PatternMatchDescriptor (Halcon)") {
    //     auto halconPlugin = std::dynamic_pointer_cast<HalconPatternMatchingDescriptorPlugin>(plugin);
    //     if (halconPlugin) {
    //         halconPlugin->setVisionManagerPage(this);
    //         std::string templateDir, profileName;
    //         resolveTemplateDir(templateDir, profileName);
    //         if (!templateDir.empty()) {
    //             halconPlugin->setTemplateDirectory(templateDir);
    //             halconPlugin->setProfileName(profileName);
    //         }
    //     }
    // }
    if (pluginName == "PatternMatchGray (Halcon)") {
        auto halconPlugin = std::dynamic_pointer_cast<HalconPatternMatchingGrayPlugin>(plugin);
        if (halconPlugin) {
            halconPlugin->setVisionManagerPage(this);
            std::string templateDir, profileName;
            resolveTemplateDir(templateDir, profileName);
            if (!templateDir.empty()) {
                halconPlugin->setTemplateDirectory(templateDir);
                halconPlugin->setProfileName(profileName);
            }
        }
    }
#endif
}

void VisionManagerPage::updatePatternMatchingSourceImage()
{
    auto plugin = VisionManager::instance().getPlugin("PatternMatching");
    if (!plugin) return;

    auto patternPlugin = std::dynamic_pointer_cast<PatternMatchingPlugin>(plugin);
    if (!patternPlugin) return;

    if (!m_state.currentProfilePath.isEmpty()) {
        QFileInfo profileInfo(m_state.currentProfilePath);
        QString productDir = QDir::cleanPath(profileInfo.absolutePath() + "/..");
        QString prDir = productDir + "/PRMatchingTemplate";
        patternPlugin->setTemplateDirectory(prDir.toStdString());
        patternPlugin->setProfileName(profileInfo.baseName().toStdString());
    } else if (m_context && m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
        QString productFilePath = QString::fromStdString(m_context->productPositionManager->GetCurrentProductPath());
        QString productDir = QFileInfo(productFilePath).path();
        QString prDir = productDir + "/PRMatchingTemplate";
        patternPlugin->setTemplateDirectory(prDir.toStdString());
        patternPlugin->setProfileName("untitled");
    }

    cv::Mat inputImage = VisionManager::instance().getInputImage();
    if (inputImage.empty()) return;

    VisionROI roi = VisionManager::instance().getROI();
    if (!roi.isValid()) {
        patternPlugin->setSourceImage(inputImage);
        return;
    }

    int startX = std::max(0, std::min(roi.getStartX(), inputImage.cols - 1));
    int startY = std::max(0, std::min(roi.getStartY(), inputImage.rows - 1));
    int endX = std::max(startX + 1, std::min(roi.getEndX(), inputImage.cols));
    int endY = std::max(startY + 1, std::min(roi.getEndY(), inputImage.rows));

    cv::Mat roiImage = inputImage(cv::Rect(startX, startY, endX - startX, endY - startY)).clone();
    patternPlugin->setSourceImage(roiImage);
}

// ========== Profile Save (preserved) ==========

void VisionManagerPage::saveProfileToPath(const QString& filePath)
{
    std::cout << "[VisionManagerPage] Saving profile to: " << filePath.toStdString() << std::endl;

    try {
        nlohmann::json j;

        j["version"] = "1.0";
        j["name"] = QFileInfo(filePath).baseName().toStdString();

        if (m_context && m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
            j["product"] = m_context->productPositionManager->GetCurrentProductName();
        }

        // ROI
        VisionROI currentROI = VisionManager::instance().getROI();
        j["roi"]["startX"] = currentROI.getStartX();
        j["roi"]["startY"] = currentROI.getStartY();
        j["roi"]["endX"] = currentROI.getEndX();
        j["roi"]["endY"] = currentROI.getEndY();
        j["roi"]["enabled"] = currentROI.isEnabled();

        // Search Box
        int searchStartX, searchStartY, searchEndX, searchEndY;
        m_captureStep->getSearchBox(searchStartX, searchStartY, searchEndX, searchEndY);
        j["searchBox"]["startX"] = searchStartX;
        j["searchBox"]["startY"] = searchStartY;
        j["searchBox"]["endX"] = searchEndX;
        j["searchBox"]["endY"] = searchEndY;
        j["searchBox"]["enabled"] = m_captureStep->isSearchBoxEnabled();

        // Camera
        auto camSettings = m_cameraStep->getCameraSettings();
        j["camera"]["cameraId"] = camSettings.cameraId.toStdString();
        j["camera"]["exposureTime"] = camSettings.exposureTime;
        j["camera"]["gain"] = camSettings.gain;
        j["camera"]["autoExposure"] = camSettings.autoExposure;
        j["camera"]["autoGain"] = camSettings.autoGain;
        j["camera"]["pixelToMmX"] = m_cameraStep->getPixelToMmX();
        j["camera"]["pixelToMmY"] = m_cameraStep->getPixelToMmY();

        // Lighting
        auto lightingSettings = m_cameraStep->getLightingSettings();
        j["lighting"]["enabled"] = lightingSettings.enabled;
        j["lighting"]["controllerId"] = lightingSettings.controllerId.toStdString();
        nlohmann::json channelsJson = nlohmann::json::array();
        for (const auto& [channel, intensity] : lightingSettings.channelIntensities) {
            nlohmann::json chJson;
            chJson["channel"] = channel;
            chJson["intensity"] = intensity;
            auto stateIt = lightingSettings.channelStates.find(channel);
            chJson["on"] = (stateIt != lightingSettings.channelStates.end()) ? stateIt->second : false;
            channelsJson.push_back(chJson);
        }
        j["lighting"]["channels"] = channelsJson;

        // Plugin
        if (!m_state.selectedPluginName.isEmpty()) {
            auto plugin = VisionManager::instance().getPlugin(m_state.selectedPluginName);
            if (plugin) {
                j["plugin"]["name"] = m_state.selectedPluginName.toStdString();
                j["plugin"]["category"] = plugin->getCategory();
                j["plugin"]["parameters"] = plugin->getParameters();
            }
        }

        // Write
        std::ofstream file(filePath.toStdString());
        if (!file.is_open()) {
            m_executeStep->displayResult("Error: Cannot create file\n" + filePath);
            return;
        }

        file << j.dump(4);
        file.close();

        m_state.currentProfilePath = filePath;
        m_state.currentRecipePath.clear();
        m_state.profileModified = false;
        m_navWidget->setProfileName(QFileInfo(filePath).baseName());
        refreshProfileList();

        if (m_context && m_context->logManager) {
            m_context->logManager->Info("Vision profile saved: " + filePath.toStdString(), "VisionManagerPage");
        }

        m_executeStep->displayResult("Profile saved: " + QFileInfo(filePath).fileName());

    } catch (const std::exception& e) {
        m_executeStep->displayResult("Error saving profile:\n" + QString::fromStdString(e.what()));
    }
}

void VisionManagerPage::saveRecipeToPath(const QString& filePath)
{
    // Preserved from original - simplified for now
    std::cout << "[VisionManagerPage] Saving recipe to: " << filePath.toStdString() << std::endl;

    try {
        nlohmann::json j;
        j["version"] = "1.0";
        j["type"] = "vision_recipe";
        j["name"] = QFileInfo(filePath).baseName().toStdString();

        if (m_context && m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
            j["product"] = m_context->productPositionManager->GetCurrentProductName();
        }

        // ROI
        VisionROI currentROI = VisionManager::instance().getROI();
        j["roi"]["startX"] = currentROI.getStartX();
        j["roi"]["startY"] = currentROI.getStartY();
        j["roi"]["endX"] = currentROI.getEndX();
        j["roi"]["endY"] = currentROI.getEndY();
        j["roi"]["enabled"] = currentROI.isEnabled();

        // Camera
        auto camSettings = m_cameraStep->getCameraSettings();
        j["camera"]["cameraId"] = camSettings.cameraId.toStdString();
        j["camera"]["exposureTime"] = camSettings.exposureTime;
        j["camera"]["gain"] = camSettings.gain;
        j["camera"]["pixelToMmX"] = m_cameraStep->getPixelToMmX();
        j["camera"]["pixelToMmY"] = m_cameraStep->getPixelToMmY();

        // Steps
        j["steps"] = nlohmann::json::array();
        int stepCount = VisionManager::instance().getRecipeStepCount();
        for (int i = 0; i < stepCount; ++i) {
            auto step = VisionManager::instance().getRecipeStep(i);
            nlohmann::json stepJson;
            stepJson["plugin"] = step.pluginName.toStdString();
            stepJson["parameters"] = step.parameters;
            stepJson["enabled"] = step.enabled;
            j["steps"].push_back(stepJson);
        }

        std::ofstream file(filePath.toStdString());
        if (!file.is_open()) {
            m_executeStep->displayResult("Error: Cannot create file\n" + filePath);
            return;
        }

        file << j.dump(4);
        file.close();

        m_state.currentRecipePath = filePath;
        m_state.currentProfilePath.clear();
        m_state.profileModified = false;
        m_navWidget->setProfileName(QFileInfo(filePath).baseName());

        if (m_context && m_context->logManager) {
            m_context->logManager->Info("Vision recipe saved: " + filePath.toStdString(), "VisionManagerPage");
        }

        m_executeStep->displayResult("Recipe saved: " + QFileInfo(filePath).fileName());

    } catch (const std::exception& e) {
        m_executeStep->displayResult("Error saving recipe:\n" + QString::fromStdString(e.what()));
    }
}

void VisionManagerPage::loadVisionRecipeFile()
{
    // Kept for future use but not exposed in wizard UI
    std::cout << "[VisionManagerPage] loadVisionRecipeFile - not yet wired in wizard mode" << std::endl;
}

void VisionManagerPage::saveVisionRecipeFile()
{
    // Kept for future use but not exposed in wizard UI
    std::cout << "[VisionManagerPage] saveVisionRecipeFile - not yet wired in wizard mode" << std::endl;
}

// ========== Worker Thread (preserved) ==========

void VisionManagerPage::setupWorker()
{
#ifdef HAS_OPENCV
    std::cout << "[VisionManagerPage] Setting up vision processing worker..." << std::endl;

    m_worker = new VisionProcessingWorker();

    QObject::connect(m_worker, &VisionProcessingWorker::processingStarted,
            this, &VisionManagerPage::onProcessingStarted);
    QObject::connect(m_worker, &VisionProcessingWorker::processingProgress,
            this, &VisionManagerPage::onProcessingProgress);
    QObject::connect(m_worker, &VisionProcessingWorker::processingFinished,
            this, &VisionManagerPage::onProcessingFinished);
    QObject::connect(m_worker, &VisionProcessingWorker::processingError,
            this, &VisionManagerPage::onProcessingError);
    QObject::connect(m_worker, &VisionProcessingWorker::processingCancelled,
            this, &VisionManagerPage::onProcessingCancelled);

    m_worker->startWorkerThread();

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Vision processing worker initialized", "VisionManagerPage");
    }
#else
    std::cout << "[VisionManagerPage] OpenCV not available, worker not created" << std::endl;
#endif
}

void VisionManagerPage::setUIProcessingState(bool isProcessing)
{
    m_navWidget->setEnabled(!isProcessing);
    m_backButton->setEnabled(!isProcessing);
    m_executeStep->setProcessingState(isProcessing);

    if (isProcessing) {
        setCursor(Qt::WaitCursor);
    } else {
        unsetCursor();
    }
}

void VisionManagerPage::onProcessingStarted(const QString& pluginName)
{
    std::cout << "[VisionManagerPage] Processing started: " << pluginName.toStdString() << std::endl;
    setUIProcessingState(true);
    m_executeStep->displayResult("Processing: " + pluginName + "\nStatus: Running...");
}

void VisionManagerPage::onProcessingProgress(int percent, const QString& message)
{
    m_executeStep->setProgress(percent, message);
}

void VisionManagerPage::onProcessingFinished(const VisionResult& result, const QString& pluginName)
{
    std::cout << "[VisionManagerPage] Processing finished: " << pluginName.toStdString() << std::endl;

    setUIProcessingState(false);

    QString resultText = "Plugin: " + pluginName + "\n";
    resultText += "Status: " + QString(result.success ? "SUCCESS" : "FAILED") + "\n";
    resultText += "Processing Time: " + QString::number(result.processingTimeMs, 'f', 2) + " ms\n";
    resultText += QString(50, '=') + "\n\n";

    if (result.success) {
        if (!result.measurements.empty()) {
            resultText += "Measurements:\n";
            for (const auto& kv : result.measurements) {
                resultText += "  " + QString::fromStdString(kv.first) + ": "
                           + QString::number(kv.second, 'f', 4) + "\n";
            }
            resultText += "\n";
        }

        if (!result.circles.empty()) {
            resultText += "Detected Circles: " + QString::number(result.circles.size()) + "\n";
            int count = 0;
            for (const auto& circle : result.circles) {
                if (count >= 10) {
                    resultText += "  ... and " + QString::number(result.circles.size() - 10) + " more\n";
                    break;
                }
                resultText += "  " + QString::number(count+1) + ". Circle at ("
                           + QString::number(circle.centerPx.x, 'f', 1) + ", "
                           + QString::number(circle.centerPx.y, 'f', 1) + ") r="
                           + QString::number(circle.radiusPx, 'f', 1) + " px\n";
                count++;
            }
            resultText += "\n";
        }

        if (!result.lines.empty()) {
            resultText += "Detected Lines: " + QString::number(result.lines.size()) + "\n";
            for (size_t i = 0; i < std::min(result.lines.size(), size_t(5)); ++i) {
                const auto& line = result.lines[i];
                resultText += "  " + QString::number(i+1) + ". Line from ("
                           + QString::number(line.startPx.x, 'f', 1) + ", "
                           + QString::number(line.startPx.y, 'f', 1) + ") to ("
                           + QString::number(line.endPx.x, 'f', 1) + ", "
                           + QString::number(line.endPx.y, 'f', 1) + ")\n";
            }
            resultText += "\n";
        }

        if (!result.points.empty()) {
            resultText += "Detected Points: " + QString::number(result.points.size()) + "\n";
            for (size_t i = 0; i < std::min(result.points.size(), size_t(10)); ++i) {
                const auto& point = result.points[i];
                resultText += "  " + QString::number(i+1) + ". Point at ("
                           + QString::number(point.pixelCoord.x, 'f', 1) + ", "
                           + QString::number(point.pixelCoord.y, 'f', 1) + ")\n";
            }
            resultText += "\n";
        }

        if (!result.metadata.empty()) {
            resultText += "Metadata:\n";
            for (const auto& kv : result.metadata) {
                resultText += "  " + QString::fromStdString(kv.first) + ": "
                           + QString::fromStdString(kv.second) + "\n";
            }
        }

        // Result markers for detected circles
        std::vector<VisionImageWidget::ResultMarker> markers;
        int labelNum = 1;
        for (const auto& circle : result.circles) {
            VisionImageWidget::ResultMarker marker;
            marker.x = circle.centerPx.x;
            marker.y = circle.centerPx.y;
            marker.radius = circle.radiusPx;
            marker.label = labelNum++;
            markers.push_back(marker);
        }
        m_imageWidget->setResultMarkers(markers);
        m_imageWidget->setOverlays(result.overlays);

    } else {
        resultText += "Error: " + QString::fromStdString(result.errorMessage) + "\n";
        m_imageWidget->clearResultMarkers();
        m_imageWidget->clearOverlays();
    }

    m_executeStep->displayResult(resultText);
}

void VisionManagerPage::onProcessingError(const QString& errorMessage)
{
    std::cout << "[VisionManagerPage] Processing error: " << errorMessage.toStdString() << std::endl;
    setUIProcessingState(false);
    m_executeStep->displayResult("Processing Error!\n\nPlugin: " + m_state.selectedPluginName +
                                  "\nError: " + errorMessage);
    m_imageWidget->clearResultMarkers();
    m_imageWidget->clearOverlays();
}

void VisionManagerPage::onProcessingCancelled()
{
    std::cout << "[VisionManagerPage] Processing cancelled" << std::endl;
    setUIProcessingState(false);
    m_executeStep->displayResult("Processing Cancelled\n\nPlugin: " + m_state.selectedPluginName);
}

// ========== State Management ==========

void VisionManagerPage::refreshCautionIcons()
{
    m_navWidget->updateCautionIcons(m_state);

    // Update status text
    QString status;
    if (!m_state.currentProfilePath.isEmpty()) {
        status = "Profile: " + QFileInfo(m_state.currentProfilePath).baseName();
    } else {
        status = "No profile loaded";
    }
    status += "\nCamera: " + (m_state.selectedCameraId.isEmpty() ? "(none)" : m_state.selectedCameraId);
    status += "\nPlugin: " + (m_state.selectedPluginName.isEmpty() ? "(none)" : m_state.selectedPluginName);
    status += "\nImage: " + QString(m_state.hasValidImage ? "Captured" : "Not captured");
    m_navWidget->setStatusText(status);
}

void VisionManagerPage::updateVisionDirectory()
{
    QString visionDir;
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        QString productFilePath = QString::fromStdString(
            m_context->productPositionManager->GetCurrentProductPath());
        QString productDir = QFileInfo(productFilePath).path();
        visionDir = productDir + "/vision";
    } else {
        visionDir = "config/vision";
    }

    m_navWidget->setVisionDirectory(visionDir);
}

void VisionManagerPage::refreshProfileList()
{
    if (m_navWidget) {
        m_navWidget->refreshProfileList();
    }
}
