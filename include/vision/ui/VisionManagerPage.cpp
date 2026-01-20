// VisionManagerPage.cpp
#include "VisionManagerPage.h"
#include "VisionToolbarWidget.h"
#include "VisionLeftPanelWidget.h"
#include "VisionImageWidget.h"
#include "VisionRightPanelWidget.h"
#include "../plugins/PatternMatchingPlugin.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "CameraManager.h"
#include "LiveVideoSubscriber.h"
#include "product_position_manager.h"
#include "../VisionManager.h"
#include "../VisionROI.h"  // Includes VisionSearchBox
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
#ifdef HAS_HALCON
#include "../plugins/HalconCircleDetectionPlugin.h"
#include "../plugins/HalconCircleDetectionRobustPlugin.h"
#include "../plugins/HalconLineDetectionPlugin.h"
#include "../plugins/HalconPatternMatchingEdgePlugin.h"
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
#include <QTimer>
#include <QFileInfo>
#include <QCoreApplication>
#include <QThread>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <fstream>
#include <iostream>

VisionManagerPage::VisionManagerPage(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_toolbar(nullptr)
    , m_leftPanel(nullptr)
    , m_imageWidget(nullptr)
    , m_rightPanel(nullptr)
    , m_mainSplitter(nullptr)
    , m_backButton(nullptr)
{
    setupUI();
    connectSignals();
    registerPlugins();
    updatePluginList();
    setupWorker();

    // Initialize camera selection after signals are connected
    m_rightPanel->selectFirstCamera();

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Vision Manager page initialized", "VisionManagerPage");
    }
}

VisionManagerPage::~VisionManagerPage()
{
    // Stop worker thread
    if (m_worker) {
        m_worker->stopWorkerThread();
        delete m_worker;
        m_worker = nullptr;
        std::cout << "[VisionManagerPage] Worker thread stopped" << std::endl;
    }

    // Unsubscribe from camera manager before destroying subscriber
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

    // Toolbar row
    m_toolbar = new VisionToolbarWidget(this);
    mainLayout->addWidget(m_toolbar);

    // Main content: 3-column splitter
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);

    // Left panel - plugin list and results
    m_leftPanel = new VisionLeftPanelWidget(this);
    m_mainSplitter->addWidget(m_leftPanel);

    // Middle - image display
    m_imageWidget = new VisionImageWidget(this);
    m_mainSplitter->addWidget(m_imageWidget);

    // Right panel - settings and parameters
    m_rightPanel = new VisionRightPanelWidget(m_context, this);
    m_mainSplitter->addWidget(m_rightPanel);

    // Set splitter sizes (left: 250px fixed, middle: flexible, right: 300px fixed)
    m_mainSplitter->setStretchFactor(0, 0);  // Left - fixed
    m_mainSplitter->setStretchFactor(1, 1);  // Middle - stretch
    m_mainSplitter->setStretchFactor(2, 0);  // Right - fixed

    mainLayout->addWidget(m_mainSplitter, 1);

    std::cout << "[VisionManagerPage] UI setup complete" << std::endl;
}

void VisionManagerPage::connectSignals()
{
    // Toolbar signals
    connect(m_toolbar, &VisionToolbarWidget::newRecipeClicked,
            this, &VisionManagerPage::onNewRecipe);
    connect(m_toolbar, &VisionToolbarWidget::loadRecipeClicked,
            this, &VisionManagerPage::onLoadRecipe);
    connect(m_toolbar, &VisionToolbarWidget::saveRecipeClicked,
            this, &VisionManagerPage::onSaveRecipe);
    connect(m_toolbar, &VisionToolbarWidget::saveAsRecipeClicked,
            this, &VisionManagerPage::onSaveAsRecipe);
    connect(m_toolbar, &VisionToolbarWidget::runRecipeClicked,
            this, &VisionManagerPage::onRunRecipe);
    connect(m_toolbar, &VisionToolbarWidget::stopClicked,
            this, &VisionManagerPage::onStopRecipe);

    // Left panel signals
    connect(m_leftPanel, &VisionLeftPanelWidget::addToNodeClicked,
            this, &VisionManagerPage::onAddToNode);
    connect(m_leftPanel, &VisionLeftPanelWidget::pluginSelected,
            this, &VisionManagerPage::onPluginSelected);

    // Image widget signals
    connect(m_imageWidget, &VisionImageWidget::captureClicked,
            this, &VisionManagerPage::onCaptureImage);
    connect(m_imageWidget, &VisionImageWidget::clearClicked,
            this, &VisionManagerPage::onClearImage);
    connect(m_imageWidget, &VisionImageWidget::roiDragged,
            this, &VisionManagerPage::onROIDragged);
    connect(m_imageWidget, &VisionImageWidget::searchBoxDragged,
            this, &VisionManagerPage::onSearchBoxDragged);
    connect(m_imageWidget, &VisionImageWidget::templateSelectionDragged,
            this, &VisionManagerPage::onTemplateSelectionDragged);

    // Right panel signals
    connect(m_rightPanel, &VisionRightPanelWidget::applyPluginParamsClicked,
            this, &VisionManagerPage::onApplyParams);
    connect(m_rightPanel, &VisionRightPanelWidget::executeVisionClicked,
            this, &VisionManagerPage::onApplyParams);
    connect(m_rightPanel, &VisionRightPanelWidget::cameraSelected,
            this, &VisionManagerPage::onCameraSelected);
    connect(m_rightPanel, &VisionRightPanelWidget::cameraSettingsChanged,
            this, &VisionManagerPage::onCameraSettingsChanged);
    connect(m_rightPanel, &VisionRightPanelWidget::calibrationChanged,
            this, &VisionManagerPage::onCalibrationChanged);
    connect(m_rightPanel, &VisionRightPanelWidget::roiChanged,
            this, &VisionManagerPage::onROIChanged);
    connect(m_rightPanel, &VisionRightPanelWidget::roiEnabledChanged,
            this, &VisionManagerPage::onROIEnabledChanged);
    connect(m_rightPanel, &VisionRightPanelWidget::searchBoxChanged,
            this, &VisionManagerPage::onSearchBoxChanged);
    connect(m_rightPanel, &VisionRightPanelWidget::searchBoxEnabledChanged,
            this, &VisionManagerPage::onSearchBoxEnabledChanged);

    std::cout << "[VisionManagerPage] Signals connected" << std::endl;
}

// ========== Toolbar Actions ==========
void VisionManagerPage::onNewRecipe()
{
    std::cout << "[VisionManagerPage] New Recipe clicked" << std::endl;
    VisionManager::instance().newRecipe();

    // Clear current profile/recipe paths
    m_currentProfilePath.clear();
    m_currentRecipePath.clear();
    m_profileModified = false;
    updateProfileDisplay();

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("New vision recipe created", "VisionManagerPage");
    }
    m_leftPanel->displayResult("New recipe created\nReady to add plugins...\n\nSteps: 0");
}

void VisionManagerPage::onLoadRecipe()
{
    std::cout << "[VisionManagerPage] Load Recipe clicked" << std::endl;

    // Ask what to load
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Load Vision Data");
    msgBox.setText("What would you like to load?");
    msgBox.setInformativeText("Profile: Single plugin + camera settings\n"
                              "Recipe: Multiple processing steps");
    QPushButton* profileBtn = msgBox.addButton("Load Profile", QMessageBox::ActionRole);
    QPushButton* recipeBtn = msgBox.addButton("Load Recipe", QMessageBox::ActionRole);
    msgBox.addButton(QMessageBox::Cancel);

    msgBox.exec();

    if (msgBox.clickedButton() == recipeBtn) {
        loadVisionRecipeFile();
        return;
    } else if (msgBox.clickedButton() != profileBtn) {
        return;  // Cancelled
    }

    // Load vision profile (single plugin settings)

    // Get the vision profiles directory from current product
    QString visionDir;
    if (m_context && m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
        QString productFilePath = QString::fromStdString(m_context->productPositionManager->GetCurrentProductPath());
        // Extract directory from file path (GetCurrentProductPath returns path to .json file)
        QString productDir = QFileInfo(productFilePath).path();
        visionDir = productDir + "/vision";
    } else {
        // Fallback to config/vision if no product loaded
        visionDir = "config/vision";
    }

    QString filePath = QFileDialog::getOpenFileName(this, "Load Vision Profile", visionDir, "JSON Files (*.json);;All Files (*)");
    if (!filePath.isEmpty()) {
        try {
            std::ifstream file(filePath.toStdString());
            if (!file.is_open()) {
                m_leftPanel->displayResult("Error: Cannot open file\n" + filePath);
                return;
            }

            nlohmann::json j;
            file >> j;
            file.close();

            QString resultText = "Vision Profile Loaded:\n" + filePath + "\n\n";

            // Show profile metadata if available
            if (j.contains("name")) {
                resultText += "Profile: " + QString::fromStdString(j["name"].get<std::string>()) + "\n";
            }
            if (j.contains("product")) {
                resultText += "Product: " + QString::fromStdString(j["product"].get<std::string>()) + "\n";
            }
            if (j.contains("version")) {
                resultText += "Version: " + QString::fromStdString(j["version"].get<std::string>()) + "\n";
            }
            resultText += "\n";

            // Load ROI settings
            if (j.contains("roi")) {
                auto roiJson = j["roi"];
                int startX = roiJson.value("startX", 0);
                int startY = roiJson.value("startY", 0);
                int endX = roiJson.value("endX", 640);
                int endY = roiJson.value("endY", 480);
                bool enabled = roiJson.value("enabled", false);

                m_rightPanel->setROI(startX, startY, endX, endY);
                m_rightPanel->setROIEnabled(enabled);
                m_imageWidget->setROI(startX, startY, endX, endY);
                m_imageWidget->setROIEnabled(enabled);

                VisionROI roi(startX, startY, endX, endY);
                roi.setEnabled(enabled);
                VisionManager::instance().setROI(roi);

                resultText += "ROI: (" + QString::number(startX) + "," + QString::number(startY) + ") to ("
                           + QString::number(endX) + "," + QString::number(endY) + ")\n";
                resultText += "ROI Enabled: " + QString(enabled ? "Yes" : "No") + "\n\n";
            }

            // Load Search Box settings
            if (j.contains("searchBox")) {
                auto searchJson = j["searchBox"];
                int startX = searchJson.value("startX", 0);
                int startY = searchJson.value("startY", 0);
                int endX = searchJson.value("endX", 800);
                int endY = searchJson.value("endY", 600);
                bool enabled = searchJson.value("enabled", false);

                m_rightPanel->setSearchBox(startX, startY, endX, endY);
                m_rightPanel->setSearchBoxEnabled(enabled);
                m_imageWidget->setSearchBox(startX, startY, endX, endY);
                m_imageWidget->setSearchBoxEnabled(enabled);

                resultText += "Search Box: (" + QString::number(startX) + "," + QString::number(startY) + ") to ("
                           + QString::number(endX) + "," + QString::number(endY) + ")\n";
                resultText += "Search Box Enabled: " + QString(enabled ? "Yes" : "No") + "\n\n";
            }

            // Load camera settings
            if (j.contains("camera")) {
                auto camJson = j["camera"];
                VisionCameraSettings camSettings;
                camSettings.cameraId = QString::fromStdString(camJson.value("cameraId", ""));
                camSettings.exposureTime = camJson.value("exposureTime", 1000);
                camSettings.gain = camJson.value("gain", 1.0);
                camSettings.autoExposure = camJson.value("autoExposure", false);
                camSettings.autoGain = camJson.value("autoGain", false);
                camSettings.pixelToMmX = camJson.value("pixelToMmX", 0.00248);
                camSettings.pixelToMmY = camJson.value("pixelToMmY", 0.00248);

                m_rightPanel->setCameraSettings(camSettings);
                VisionManager::instance().setPixelToMm(camSettings.pixelToMmX, camSettings.pixelToMmY);

                // Apply camera settings to hardware immediately
                m_rightPanel->applyCameraSettings();

                resultText += "Camera: " + camSettings.cameraId + "\n";
                resultText += "Exposure: " + QString::number(camSettings.exposureTime) + " us\n";
                resultText += "Gain: " + QString::number(camSettings.gain, 'f', 1) + "\n";
                resultText += "Calibration: " + QString::number(camSettings.pixelToMmX, 'f', 6) + " mm/px\n\n";
            }

            // Load lighting settings
            if (j.contains("lighting")) {
                auto lightJson = j["lighting"];
                VisionLightingSettings lightSettings;
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

                m_rightPanel->setLightingSettings(lightSettings);

                // Apply lighting settings to hardware immediately
                m_rightPanel->applyLightingSettings();

                resultText += "Lighting: " + QString(lightSettings.enabled ? "Enabled" : "Disabled") + "\n";
                if (lightSettings.enabled) {
                    resultText += "  Channels: " + QString::number(lightSettings.channelIntensities.size()) + "\n";
                }
                resultText += "\n";
            }

            // Load plugin and parameters
            if (j.contains("plugin")) {
                auto pluginJson = j["plugin"];
                QString pluginName = QString::fromStdString(pluginJson.value("name", ""));

                if (!pluginName.isEmpty()) {
                    auto plugin = VisionManager::instance().getPlugin(pluginName);
                    if (plugin && pluginJson.contains("parameters")) {
                        plugin->setParameters(pluginJson["parameters"]);
                        m_selectedPluginName = pluginName;
                        loadPluginConfig(pluginName);
                        resultText += "Plugin: " + pluginName + "\nParameters loaded\n";
                    }
                }
            }

            if (m_context && m_context->logManager) {
                m_context->logManager->Info("Loaded vision profile: " + filePath.toStdString(), "VisionManagerPage");
            }

            // Store the current profile path
            m_currentProfilePath = filePath;
            m_currentRecipePath.clear();
            m_profileModified = false;
            updateProfileDisplay();

            m_leftPanel->displayResult(resultText);

        } catch (const std::exception& e) {
            m_leftPanel->displayResult("Error loading profile:\n" + QString::fromStdString(e.what()));
        }
    }
}

void VisionManagerPage::onSaveRecipe()
{
    std::cout << "[VisionManagerPage] Save Recipe clicked" << std::endl;

    // If we have a current profile path, save directly without asking
    if (!m_currentProfilePath.isEmpty()) {
        saveProfileToPath(m_currentProfilePath);
        return;
    }

    // If we have a current recipe path, save directly
    if (!m_currentRecipePath.isEmpty()) {
        saveRecipeToPath(m_currentRecipePath);
        return;
    }

    // No current path, use Save As
    onSaveAsRecipe();
}

void VisionManagerPage::onSaveAsRecipe()
{
    std::cout << "[VisionManagerPage] Save As Recipe clicked" << std::endl;

    // Check if there's a recipe with multiple steps
    int stepCount = VisionManager::instance().getRecipeStepCount();
    if (stepCount > 0) {
        // Ask what to save
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Save Vision Data");
        msgBox.setText("What would you like to save?");
        msgBox.setInformativeText("Recipe: " + QString::number(stepCount) + " step(s)\n"
                                  "Profile: Current plugin settings");
        QPushButton* recipeBtn = msgBox.addButton("Save Recipe", QMessageBox::ActionRole);
        QPushButton* profileBtn = msgBox.addButton("Save Profile", QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Cancel);

        msgBox.exec();

        if (msgBox.clickedButton() == recipeBtn) {
            saveVisionRecipeFile();
            return;
        } else if (msgBox.clickedButton() == profileBtn) {
            // Continue to save profile
        } else {
            return;  // Cancelled
        }
    }

    // Save vision profile (single plugin settings)

    // Check if a product is loaded
    if (!m_context || !m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
        QMessageBox::warning(this, "No Product Loaded",
            "Please load a product first before saving vision profiles.\n\n"
            "Vision profiles are saved to: config/[product_name]/vision/");
        return;
    }

    QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
    QString productFilePath = QString::fromStdString(m_context->productPositionManager->GetCurrentProductPath());
    // Extract directory from file path (GetCurrentProductPath returns path to .json file)
    QString productDir = QFileInfo(productFilePath).path();
    QString visionDir = productDir + "/vision";

    // Create vision directory if it doesn't exist
    QDir dir;
    if (!dir.exists(visionDir)) {
        if (!dir.mkpath(visionDir)) {
            QMessageBox::critical(this, "Error", "Failed to create vision directory:\n" + visionDir);
            return;
        }
        std::cout << "[VisionManagerPage] Created vision directory: " << visionDir.toStdString() << std::endl;
    }

    // Get file name from user
    bool ok;
    QString profileName = QInputDialog::getText(this, "Save Vision Profile",
        "Enter profile name:\n(Will be saved to: " + visionDir + ")",
        QLineEdit::Normal, "vision_profile", &ok);

    if (!ok || profileName.isEmpty()) {
        return;
    }

    // Clean up the name (remove invalid characters)
    profileName.replace(" ", "_");
    profileName.replace("/", "_");
    profileName.replace("\\", "_");

    // Construct full path
    QString filePath = visionDir + "/" + profileName + ".json";

    // Check if file exists
    if (QFile::exists(filePath)) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "Overwrite?",
            "File already exists:\n" + filePath + "\n\nDo you want to overwrite?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    // Save to the specified path
    saveProfileToPath(filePath);
}

void VisionManagerPage::onRunRecipe()
{
    std::cout << "[VisionManagerPage] Run Recipe clicked" << std::endl;

    int stepCount = VisionManager::instance().getRecipeStepCount();
    if (stepCount == 0) {
        // No recipe loaded, ask user to load one
        QMessageBox::StandardButton reply = QMessageBox::question(this, "No Recipe Loaded",
            "No vision recipe is currently loaded.\n\nWould you like to load a recipe file?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            loadVisionRecipeFile();
            stepCount = VisionManager::instance().getRecipeStepCount();
            if (stepCount == 0) {
                return;  // User cancelled or load failed
            }
        } else {
            return;
        }
    }

    // Check if we have an input image
    if (!VisionManager::instance().hasValidImage()) {
        m_leftPanel->displayResult("Error: No image captured\n\nPlease capture an image first before running recipe.");
        return;
    }

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Running vision recipe with " + std::to_string(stepCount) + " steps", "VisionManagerPage");
    }

    auto results = VisionManager::instance().runRecipe();
    QString summary = "Recipe Execution Results\n";
    summary += "========================\n";
    summary += "Total Steps: " + QString::number(results.size()) + "\n\n";

    int successCount = 0;
    double totalTime = 0;

    for (size_t i = 0; i < results.size(); ++i) {
        summary += "Step " + QString::number(i+1) + ": " + QString::fromStdString(results[i].pluginName) + "\n";
        summary += results[i].success ? "  Status: SUCCESS\n" : "  Status: FAILED\n";
        summary += "  Time: " + QString::number(results[i].processingTimeMs, 'f', 2) + " ms\n";

        if (results[i].success) {
            successCount++;
            // Show key measurements if any
            if (!results[i].measurements.empty()) {
                auto it = results[i].measurements.begin();
                summary += "  Result: " + QString::fromStdString(it->first) + " = "
                        + QString::number(it->second, 'f', 4) + "\n";
            }
        } else {
            summary += "  Error: " + QString::fromStdString(results[i].errorMessage) + "\n";
        }

        totalTime += results[i].processingTimeMs;
        summary += "\n";
    }

    summary += "------------------------\n";
    summary += "Success: " + QString::number(successCount) + "/" + QString::number(results.size()) + "\n";
    summary += "Total Time: " + QString::number(totalTime, 'f', 2) + " ms\n";

    m_leftPanel->displayResult(summary);
}

void VisionManagerPage::loadVisionRecipeFile()
{
    std::cout << "[VisionManagerPage] Loading vision recipe file" << std::endl;

    // Get the vision directory from current product
    QString visionDir;
    if (m_context && m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
        QString productFilePath = QString::fromStdString(m_context->productPositionManager->GetCurrentProductPath());
        // Extract directory from file path (GetCurrentProductPath returns path to .json file)
        QString productDir = QFileInfo(productFilePath).path();
        visionDir = productDir + "/vision";
    } else {
        visionDir = "config/vision";
    }

    QString filePath = QFileDialog::getOpenFileName(this, "Load Vision Recipe",
        visionDir, "Recipe JSON (*.recipe.json);;JSON Files (*.json);;All Files (*)");

    if (filePath.isEmpty()) {
        return;
    }

    try {
        std::ifstream file(filePath.toStdString());
        if (!file.is_open()) {
            m_leftPanel->displayResult("Error: Cannot open recipe file\n" + filePath);
            return;
        }

        nlohmann::json j;
        file >> j;
        file.close();

        // Clear existing recipe
        VisionManager::instance().newRecipe();

        QString resultText = "Vision Recipe Loaded:\n" + filePath + "\n\n";

        // Show metadata
        if (j.contains("name")) {
            resultText += "Recipe: " + QString::fromStdString(j["name"].get<std::string>()) + "\n";
        }
        if (j.contains("product")) {
            resultText += "Product: " + QString::fromStdString(j["product"].get<std::string>()) + "\n";
        }
        if (j.contains("description")) {
            resultText += "Description: " + QString::fromStdString(j["description"].get<std::string>()) + "\n";
        }
        resultText += "\n";

        // Load steps
        if (j.contains("steps") && j["steps"].is_array()) {
            auto stepsArray = j["steps"];
            resultText += "Steps (" + QString::number(stepsArray.size()) + "):\n";

            for (size_t i = 0; i < stepsArray.size(); ++i) {
                auto step = stepsArray[i];
                QString pluginName = QString::fromStdString(step.value("plugin", ""));
                nlohmann::json params = step.value("parameters", nlohmann::json::object());
                bool enabled = step.value("enabled", true);

                if (!pluginName.isEmpty()) {
                    // Check if plugin exists
                    auto plugin = VisionManager::instance().getPlugin(pluginName);
                    if (plugin) {
                        VisionManager::instance().addRecipeStep(pluginName, params);
                        resultText += "  " + QString::number(i+1) + ". " + pluginName;
                        if (!enabled) {
                            resultText += " (disabled)";
                        }
                        resultText += "\n";
                    } else {
                        resultText += "  " + QString::number(i+1) + ". " + pluginName + " [NOT FOUND]\n";
                    }
                }
            }
        }

        // Load ROI settings if present
        if (j.contains("roi")) {
            auto roiJson = j["roi"];
            int startX = roiJson.value("startX", 0);
            int startY = roiJson.value("startY", 0);
            int endX = roiJson.value("endX", 640);
            int endY = roiJson.value("endY", 480);
            bool enabled = roiJson.value("enabled", false);

            m_rightPanel->setROI(startX, startY, endX, endY);
            m_rightPanel->setROIEnabled(enabled);
            m_imageWidget->setROI(startX, startY, endX, endY);
            m_imageWidget->setROIEnabled(enabled);

            VisionROI roi(startX, startY, endX, endY);
            roi.setEnabled(enabled);
            VisionManager::instance().setROI(roi);

            resultText += "\nROI: " + QString(enabled ? "Enabled" : "Disabled") + "\n";
        }

        // Load Search Box settings if present
        if (j.contains("searchBox")) {
            auto searchJson = j["searchBox"];
            int startX = searchJson.value("startX", 0);
            int startY = searchJson.value("startY", 0);
            int endX = searchJson.value("endX", 800);
            int endY = searchJson.value("endY", 600);
            bool enabled = searchJson.value("enabled", false);

            m_rightPanel->setSearchBox(startX, startY, endX, endY);
            m_rightPanel->setSearchBoxEnabled(enabled);
            m_imageWidget->setSearchBox(startX, startY, endX, endY);
            m_imageWidget->setSearchBoxEnabled(enabled);

            resultText += "Search Box: " + QString(enabled ? "Enabled" : "Disabled") + "\n";
        }

        // Load camera settings if present
        if (j.contains("camera")) {
            auto camJson = j["camera"];
            VisionCameraSettings camSettings;
            camSettings.cameraId = QString::fromStdString(camJson.value("cameraId", ""));
            camSettings.exposureTime = camJson.value("exposureTime", 1000);
            camSettings.gain = camJson.value("gain", 1.0);
            camSettings.autoExposure = camJson.value("autoExposure", false);
            camSettings.autoGain = camJson.value("autoGain", false);
            camSettings.pixelToMmX = camJson.value("pixelToMmX", 0.00248);
            camSettings.pixelToMmY = camJson.value("pixelToMmY", 0.00248);

            m_rightPanel->setCameraSettings(camSettings);
            VisionManager::instance().setPixelToMm(camSettings.pixelToMmX, camSettings.pixelToMmY);

            // Apply camera settings to hardware immediately
            m_rightPanel->applyCameraSettings();

            resultText += "Camera: " + camSettings.cameraId + "\n";
        }

        resultText += "\nRecipe ready. Click 'Run Recipe' to execute.";

        if (m_context && m_context->logManager) {
            m_context->logManager->Info("Loaded vision recipe: " + filePath.toStdString(), "VisionManagerPage");
        }

        // Store the current recipe path
        m_currentRecipePath = filePath;
        m_currentProfilePath.clear();
        m_profileModified = false;
        updateProfileDisplay();

        m_leftPanel->displayResult(resultText);

    } catch (const std::exception& e) {
        m_leftPanel->displayResult("Error loading recipe:\n" + QString::fromStdString(e.what()));
    }
}

void VisionManagerPage::saveVisionRecipeFile()
{
    std::cout << "[VisionManagerPage] Saving vision recipe file" << std::endl;

    int stepCount = VisionManager::instance().getRecipeStepCount();
    if (stepCount == 0) {
        QMessageBox::warning(this, "No Recipe",
            "No recipe steps to save.\n\nUse 'Add to Recipe Node' to add plugin steps first.");
        return;
    }

    // Check if a product is loaded
    if (!m_context || !m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
        QMessageBox::warning(this, "No Product Loaded",
            "Please load a product first before saving vision recipes.\n\n"
            "Vision recipes are saved to: config/[product_name]/vision/");
        return;
    }

    QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
    QString productFilePath = QString::fromStdString(m_context->productPositionManager->GetCurrentProductPath());
    // Extract directory from file path (GetCurrentProductPath returns path to .json file)
    QString productDir = QFileInfo(productFilePath).path();
    QString visionDir = productDir + "/vision";

    // Create vision directory if it doesn't exist
    QDir dir;
    if (!dir.exists(visionDir)) {
        dir.mkpath(visionDir);
    }

    // Get recipe name from user
    bool ok;
    QString recipeName = QInputDialog::getText(this, "Save Vision Recipe",
        "Enter recipe name:\n(Will be saved to: " + visionDir + ")",
        QLineEdit::Normal, "vision_recipe", &ok);

    if (!ok || recipeName.isEmpty()) {
        return;
    }

    // Clean up the name
    recipeName.replace(" ", "_");
    recipeName.replace("/", "_");
    recipeName.replace("\\", "_");

    // Get optional description
    QString description = QInputDialog::getText(this, "Recipe Description",
        "Enter recipe description (optional):", QLineEdit::Normal, "", &ok);

    // Construct full path
    QString filePath = visionDir + "/" + recipeName + ".recipe.json";

    // Check if file exists
    if (QFile::exists(filePath)) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "Overwrite?",
            "File already exists:\n" + filePath + "\n\nDo you want to overwrite?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    try {
        nlohmann::json j;

        // Save recipe metadata
        j["version"] = "1.0";
        j["type"] = "vision_recipe";
        j["name"] = recipeName.toStdString();
        j["product"] = productName.toStdString();
        j["description"] = description.toStdString();

        // Save ROI settings
        VisionROI currentROI = VisionManager::instance().getROI();
        j["roi"]["startX"] = currentROI.getStartX();
        j["roi"]["startY"] = currentROI.getStartY();
        j["roi"]["endX"] = currentROI.getEndX();
        j["roi"]["endY"] = currentROI.getEndY();
        j["roi"]["enabled"] = currentROI.isEnabled();

        // Save Search Box settings
        int searchStartX, searchStartY, searchEndX, searchEndY;
        m_rightPanel->getSearchBox(searchStartX, searchStartY, searchEndX, searchEndY);
        j["searchBox"]["startX"] = searchStartX;
        j["searchBox"]["startY"] = searchStartY;
        j["searchBox"]["endX"] = searchEndX;
        j["searchBox"]["endY"] = searchEndY;
        j["searchBox"]["enabled"] = m_rightPanel->isSearchBoxEnabled();

        // Save camera settings
        VisionCameraSettings camSettings = m_rightPanel->getCameraSettings();
        j["camera"]["cameraId"] = camSettings.cameraId.toStdString();
        j["camera"]["exposureTime"] = camSettings.exposureTime;
        j["camera"]["gain"] = camSettings.gain;
        j["camera"]["autoExposure"] = camSettings.autoExposure;
        j["camera"]["autoGain"] = camSettings.autoGain;
        j["camera"]["pixelToMmX"] = camSettings.pixelToMmX;
        j["camera"]["pixelToMmY"] = camSettings.pixelToMmY;

        // Save recipe steps
        nlohmann::json stepsArray = nlohmann::json::array();
        for (int i = 0; i < stepCount; ++i) {
            auto step = VisionManager::instance().getRecipeStep(i);
            nlohmann::json stepJson;
            stepJson["plugin"] = step.pluginName.toStdString();
            stepJson["parameters"] = step.parameters;
            stepJson["enabled"] = step.enabled;
            stepsArray.push_back(stepJson);
        }
        j["steps"] = stepsArray;

        // Write to file
        std::ofstream file(filePath.toStdString());
        if (!file.is_open()) {
            m_leftPanel->displayResult("Error: Cannot create file\n" + filePath);
            return;
        }

        file << j.dump(4);  // Pretty print
        file.close();

        if (m_context && m_context->logManager) {
            m_context->logManager->Info("Vision recipe saved: " + filePath.toStdString(), "VisionManagerPage");
        }

        // Store the current recipe path
        m_currentRecipePath = filePath;
        m_currentProfilePath.clear();
        m_profileModified = false;
        updateProfileDisplay();

        QString resultText = "Vision Recipe Saved!\n\n";
        resultText += "Product: " + productName + "\n";
        resultText += "Recipe: " + recipeName + "\n";
        resultText += "File: " + filePath + "\n\n";
        resultText += "Steps saved: " + QString::number(stepCount) + "\n";
        for (int i = 0; i < stepCount; ++i) {
            auto step = VisionManager::instance().getRecipeStep(i);
            resultText += "  " + QString::number(i+1) + ". " + step.pluginName + "\n";
        }

        m_leftPanel->displayResult(resultText);

    } catch (const std::exception& e) {
        m_leftPanel->displayResult("Error saving recipe:\n" + QString::fromStdString(e.what()));
    }
}

void VisionManagerPage::onStopRecipe()
{
    std::cout << "[VisionManagerPage] Stop Recipe clicked" << std::endl;

    // Cancel worker thread processing if active
#ifdef HAS_OPENCV
    if (m_worker && m_worker->isProcessing()) {
        std::cout << "[VisionManagerPage] Cancelling worker thread processing..." << std::endl;
        m_worker->cancelProcessing();

        if (m_context && m_context->logManager) {
            m_context->logManager->Warning("Vision processing cancelled by user", "VisionManagerPage");
        }
        m_leftPanel->displayResult("Cancelling processing...\n\nPlease wait for cancellation to complete.");
        return;
    }
#endif

    if (m_context && m_context->logManager) {
        m_context->logManager->Warning("Vision recipe execution stopped", "VisionManagerPage");
    }
    m_leftPanel->displayResult("Recipe execution stopped by user");
}

// ========== Left Panel Actions ==========
void VisionManagerPage::onAddToNode()
{
    QString selectedPlugin = m_leftPanel->getSelectedPlugin();
    std::cout << "[VisionManagerPage] Add to Node clicked (mockup): " << selectedPlugin.toStdString() << std::endl;

    // Mockup - functionality disabled
    QString info = "Add to Recipe Node\n";
    info += "==================\n\n";
    info += "This feature is planned for future release.\n\n";
    info += "It will allow building multi-step vision pipelines:\n";
    info += "- Chain multiple plugins in sequence\n";
    info += "- Filter -> Detect -> Measure workflows\n";
    info += "- Save and load complex recipes\n\n";
    info += "For now, use single plugin execution via 'Execute' button.";

    m_leftPanel->displayResult(info);

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Add to Recipe Node - mockup (not implemented)", "VisionManagerPage");
    }
}

void VisionManagerPage::onPluginSelected(const QString& pluginName)
{
    std::cout << "[VisionManagerPage] Plugin selected: " << pluginName.toStdString() << std::endl;
    m_selectedPluginName = pluginName;

    if (m_context && m_context->logManager) {
        m_context->logManager->Debug("Selected plugin: " + pluginName.toStdString(), "VisionManagerPage");
    }

    auto plugin = VisionManager::instance().getPlugin(pluginName);
    if (!plugin) {
        m_leftPanel->displayResult("Error: Plugin not found");
        return;
    }

    // Display plugin info
    QString info = "Plugin: " + pluginName + "\n";
    info += "Category: " + QString::fromStdString(plugin->getCategory()) + "\n";
    info += "Description: " + QString::fromStdString(plugin->getDescription()) + "\n\n";

    // Show parameter schema summary
    auto schema = plugin->getParameterSchema();
    info += "Parameters (" + QString::number(schema.parameters.size()) + "):\n";
    for (const auto& param : schema.parameters) {
        info += "  - " + QString::fromStdString(param.displayName);
        if (!param.unit.empty()) {
            info += " (" + QString::fromStdString(param.unit) + ")";
        }
        info += "\n";
    }

    m_leftPanel->displayResult(info);

    // Load plugin config widget
    loadPluginConfig(pluginName);
}

// ========== Image Widget Actions ==========
void VisionManagerPage::onCaptureImage()
{
    std::cout << "[VisionManagerPage] Capture Image clicked" << std::endl;

    if (!m_frameSubscriber) {
        m_leftPanel->displayResult("Error: No camera selected\n\nPlease select a camera from the right panel first.");
        return;
    }

    if (!m_frameSubscriber->IsCameraConnected()) {
        m_leftPanel->displayResult("Error: Camera not connected\n\nCamera: " + m_selectedCameraId);
        return;
    }

    if (!m_frameSubscriber->IsCameraGrabbing()) {
        m_leftPanel->displayResult("Error: Camera not grabbing\n\nPlease start live view for: " + m_selectedCameraId);
        return;
    }

    // Get the latest frame
    CameraFrameData frame = m_frameSubscriber->GetLatestFrame();
    if (!frame.IsValid()) {
        m_leftPanel->displayResult("Error: No valid frame available\n\nWaiting for camera to provide frames...");
        return;
    }

    // Convert to QImage for display
    QImage::Format format = (frame.channels == 3) ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
    QImage qImage(frame.imageData.data(), frame.width, frame.height,
                  frame.width * frame.channels, format);

    // Display in image widget
    m_imageWidget->setImage(QPixmap::fromImage(qImage));

    // Clear any previous result markers and overlays
    m_imageWidget->clearResultMarkers();
    m_imageWidget->clearOverlays();

    // Convert to cv::Mat for VisionManager
    cv::Mat cvImage;
    if (frame.channels == 3) {
        // RGB to BGR for OpenCV
        cvImage = cv::Mat(frame.height, frame.width, CV_8UC3, frame.imageData.data()).clone();
        cv::cvtColor(cvImage, cvImage, cv::COLOR_RGB2BGR);
    } else {
        cvImage = cv::Mat(frame.height, frame.width, CV_8UC1, frame.imageData.data()).clone();
    }

    // Set in VisionManager
    VisionManager::instance().setInputImage(cvImage);

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Image captured from " + m_selectedCameraId.toStdString(), "VisionManagerPage");
    }

    // Update PatternMatching source image if that plugin is selected
    if (m_selectedPluginName == "PatternMatching") {
        updatePatternMatchingSourceImage();
    }

    QString info = "Image captured successfully!\n\n";
    info += "Camera: " + m_selectedCameraId + "\n";
    info += "Size: " + QString::number(frame.width) + " x " + QString::number(frame.height) + "\n";
    info += "Channels: " + QString::number(frame.channels) + "\n";
    info += "Frame #: " + QString::number(frame.frameNumber) + "\n";
    info += "Exposure: " + QString::number(frame.exposureTime, 'f', 2) + " us\n";
    info += "Gain: " + QString::number(frame.gain, 'f', 2) + "\n\n";
    info += "Image is now ready for processing.";

    m_leftPanel->displayResult(info);
}

void VisionManagerPage::onClearImage()
{
    std::cout << "[VisionManagerPage] Clear Image clicked" << std::endl;

    // Clear display
    m_imageWidget->clearImage();

    // Clear result markers and overlays
    m_imageWidget->clearResultMarkers();
    m_imageWidget->clearOverlays();

    // Clear VisionManager images
    VisionManager::instance().clearImages();

    if (m_context && m_context->logManager) {
        m_context->logManager->Debug("Image cleared", "VisionManagerPage");
    }

    m_leftPanel->displayResult("Image cleared\n\nReady to capture new image.");
}

void VisionManagerPage::onROIDragged(int startX, int startY, int endX, int endY)
{
    // Debug log suppressed - spams during drag events
    // std::cout << "[VisionManagerPage] ROI dragged: (" << startX << "," << startY << ") to (" << endX << "," << endY << ")" << std::endl;

    // Update the right panel spinboxes (without triggering signals back)
    m_rightPanel->setROI(startX, startY, endX, endY);

    // Update VisionManager
    VisionROI roi(startX, startY, endX, endY);
    roi.setEnabled(m_rightPanel->isROIEnabled());
    VisionManager::instance().setROI(roi);

    // Update PatternMatching source image if that plugin is selected
    if (m_selectedPluginName == "PatternMatching") {
        updatePatternMatchingSourceImage();
    }

    // Debug log suppressed - spams during drag events
    // if (m_context && m_context->logManager) {
    //     m_context->logManager->Debug("ROI dragged to new position", "VisionManagerPage");
    // }
}

// ========== Right Panel Actions ==========
void VisionManagerPage::onApplyParams()
{
    std::cout << "[VisionManagerPage] Apply Parameters clicked" << std::endl;

    if (m_selectedPluginName.isEmpty()) {
        m_leftPanel->displayResult("Error: No plugin selected\n\nPlease select a plugin first.");
        return;
    }

#ifdef HAS_OPENCV
    // Check if worker is already processing
    if (m_worker && m_worker->isProcessing()) {
        m_leftPanel->displayResult("Processing already in progress...\n\nPlease wait for current operation to complete.");
        return;
    }

    // Auto-recapture fresh image before execution
    cv::Mat inputImage;
    if (m_frameSubscriber && m_frameSubscriber->IsCameraConnected() && m_frameSubscriber->IsCameraGrabbing()) {
        std::cout << "[VisionManagerPage] Auto-recapturing fresh image before execution..." << std::endl;

        // Wait 100ms for camera exposure settings to stabilize
        QThread::msleep(100);

        // Get the latest frame
        CameraFrameData frame = m_frameSubscriber->GetLatestFrame();
        if (frame.IsValid()) {
            // Convert to QImage for display
            QImage::Format format = (frame.channels == 3) ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
            QImage qImage(frame.imageData.data(), frame.width, frame.height,
                          frame.width * frame.channels, format);

            // Display in image widget
            m_imageWidget->setImage(QPixmap::fromImage(qImage));

            // Clear any previous result markers and overlays
            m_imageWidget->clearResultMarkers();
            m_imageWidget->clearOverlays();

            // Convert to cv::Mat for VisionManager
            if (frame.channels == 3) {
                inputImage = cv::Mat(frame.height, frame.width, CV_8UC3, frame.imageData.data()).clone();
                cv::cvtColor(inputImage, inputImage, cv::COLOR_RGB2BGR);
            } else {
                inputImage = cv::Mat(frame.height, frame.width, CV_8UC1, frame.imageData.data()).clone();
            }

            // Also set in VisionManager for consistency
            VisionManager::instance().setInputImage(inputImage);

            if (m_context && m_context->logManager) {
                m_context->logManager->Debug("Auto-recaptured fresh image before execution", "VisionManagerPage");
            }
        } else {
            std::cout << "[VisionManagerPage] Warning: Could not get fresh frame, using existing image" << std::endl;
            inputImage = VisionManager::instance().getInputImage();
        }
    } else {
        // Use existing image from VisionManager
        inputImage = VisionManager::instance().getInputImage();
    }

    // Check if we have an input image
    if (inputImage.empty()) {
        m_leftPanel->displayResult("Error: No image captured\n\nPlease capture an image first.");
        return;
    }

    // Get current ROI
    VisionROI currentROI = VisionManager::instance().getROI();

    // Check if worker is available
    if (!m_worker) {
        m_leftPanel->displayResult("Error: Vision processing worker not available.\n\nOpenCV may not be configured.");
        return;
    }

    // Get search box parameters
    bool useSearchBox = m_rightPanel->isSearchBoxEnabled();
    int searchStartX = 0, searchStartY = 0, searchEndX = 0, searchEndY = 0;
    if (useSearchBox) {
        m_rightPanel->getSearchBox(searchStartX, searchStartY, searchEndX, searchEndY);
    }

    if (m_context && m_context->logManager) {
        if (useSearchBox) {
            m_context->logManager->Info("Starting async grid search with plugin: " + m_selectedPluginName.toStdString(), "VisionManagerPage");
        } else {
            m_context->logManager->Info("Starting async execution of plugin: " + m_selectedPluginName.toStdString(), "VisionManagerPage");
        }
    }

    // Execute plugin asynchronously using worker thread
    m_worker->executePlugin(m_selectedPluginName, inputImage, currentROI, useSearchBox,
                           searchStartX, searchStartY, searchEndX, searchEndY);

#else
    m_leftPanel->displayResult("Error: OpenCV not available\n\nVision processing requires OpenCV.");
#endif
}

void VisionManagerPage::onCameraSelected(const QString& cameraId)
{
    std::cout << "[VisionManagerPage] Camera selected: " << cameraId.toStdString() << std::endl;

    m_selectedCameraId = cameraId;

    // Unsubscribe old subscriber if exists
    if (m_frameSubscriber && m_context && m_context->cameraManager) {
        m_context->cameraManager->UnsubscribeFromFrames(m_frameSubscriber->GetSubscriberId());
    }

    // Create or update the frame subscriber
    if (m_context && m_context->cameraManager) {
        std::string cameraIdStr = cameraId.toStdString();

        // Check camera status and auto-connect/start if needed
        auto cameraStatus = m_context->cameraManager->GetCameraStatus(cameraIdStr);

        QString statusInfo = "";

        // Auto-connect if not connected
        if (!cameraStatus.connected) {
            std::cout << "[VisionManagerPage] Camera not connected, attempting to connect..." << std::endl;
            if (m_context->cameraManager->ConnectCamera(cameraIdStr)) {
                statusInfo += "Camera connected successfully\n";
                cameraStatus = m_context->cameraManager->GetCameraStatus(cameraIdStr);
            } else {
                statusInfo += "Warning: Failed to connect camera\n";
            }
        }

        // Auto-start grabbing if connected but not grabbing
        if (cameraStatus.connected && !cameraStatus.grabbing) {
            std::cout << "[VisionManagerPage] Camera not grabbing, attempting to start..." << std::endl;
            if (m_context->cameraManager->StartGrabbingWithBroadcast(cameraIdStr)) {
                statusInfo += "Camera grabbing started\n";
                cameraStatus = m_context->cameraManager->GetCameraStatus(cameraIdStr);
            } else {
                statusInfo += "Warning: Failed to start grabbing\n";
            }
        }

        m_frameSubscriber = std::make_shared<LiveVideoSubscriber>(
            cameraIdStr, "VisionManager"
        );

        // Subscribe to camera manager
        m_context->cameraManager->SubscribeToFrames(m_frameSubscriber);

        // Initialize status from manager
        m_frameSubscriber->InitializeStatusFromManager(m_context->cameraManager);

        if (m_context->logManager) {
            m_context->logManager->Info("Subscribed to camera: " + cameraIdStr, "VisionManagerPage");
        }

        QString info = "Camera: " + cameraId + "\n\n";
        if (!statusInfo.isEmpty()) {
            info += statusInfo + "\n";
        }
        info += "Status: " + QString(m_frameSubscriber->IsCameraConnected() ? "Connected" : "Disconnected") + "\n";
        info += "Grabbing: " + QString(m_frameSubscriber->IsCameraGrabbing() ? "Yes" : "No") + "\n\n";

        if (m_frameSubscriber->IsCameraConnected() && m_frameSubscriber->IsCameraGrabbing()) {
            info += "Ready! Click 'Capture' to grab a frame.";
        } else if (m_frameSubscriber->IsCameraConnected()) {
            info += "Camera connected but not grabbing.\nStart live view to enable capture.";
        } else {
            info += "Camera not connected.\nCheck camera hardware connection.";
        }

        m_leftPanel->displayResult(info);
    } else {
        m_leftPanel->displayResult("Camera: " + cameraId + "\n\nError: CameraManager not available");
    }
}

void VisionManagerPage::onCameraSettingsChanged()
{
    std::cout << "[VisionManagerPage] Camera settings changed" << std::endl;

    if (m_context && m_context->logManager) {
        m_context->logManager->Debug("Camera settings updated", "VisionManagerPage");
    }

    // Auto-recapture image if we have an active subscriber
    if (m_frameSubscriber && m_frameSubscriber->IsCameraConnected() && m_frameSubscriber->IsCameraGrabbing()) {
        // Small delay to allow camera settings to take effect
        QTimer::singleShot(100, this, [this]() {
            onCaptureImage();
            std::cout << "[VisionManagerPage] Auto-recaptured image after settings change" << std::endl;
        });
    }
}

void VisionManagerPage::onCalibrationChanged(double xMmPerPx, double yMmPerPx)
{
    std::cout << "[VisionManagerPage] Calibration changed: X=" << xMmPerPx << " Y=" << yMmPerPx << " mm/px" << std::endl;

    VisionManager::instance().setPixelToMm(xMmPerPx, yMmPerPx);

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Pixel-to-mm calibration updated", "VisionManagerPage");
    }
}

void VisionManagerPage::onROIChanged(int startX, int startY, int endX, int endY)
{
    std::cout << "[VisionManagerPage] ROI changed: (" << startX << "," << startY << ") to (" << endX << "," << endY << ")" << std::endl;

    VisionROI roi(startX, startY, endX, endY);
    roi.setEnabled(m_rightPanel->isROIEnabled());
    VisionManager::instance().setROI(roi);

    // Update image widget overlay
    m_imageWidget->setROI(startX, startY, endX, endY);

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("ROI updated", "VisionManagerPage");
    }
}

void VisionManagerPage::onROIEnabledChanged(bool enabled)
{
    std::cout << "[VisionManagerPage] ROI " << (enabled ? "enabled" : "disabled") << std::endl;

    VisionManager::instance().setROIEnabled(enabled);

    // Update image widget overlay
    m_imageWidget->setROIEnabled(enabled);

    if (m_context && m_context->logManager) {
        m_context->logManager->Info(enabled ? "ROI enabled" : "ROI disabled", "VisionManagerPage");
    }
}

void VisionManagerPage::onSearchBoxDragged(int startX, int startY, int endX, int endY)
{
    // Debug log suppressed - spams during drag events
    // std::cout << "[VisionManagerPage] Search Box dragged: (" << startX << "," << startY << ") to (" << endX << "," << endY << ")" << std::endl;

    // Update the right panel spinboxes (without triggering signals back)
    m_rightPanel->setSearchBox(startX, startY, endX, endY);

    // Debug log suppressed - spams during drag events
    // if (m_context && m_context->logManager) {
    //     m_context->logManager->Debug("Search Box dragged to new position", "VisionManagerPage");
    // }
}

void VisionManagerPage::onSearchBoxChanged(int startX, int startY, int endX, int endY)
{
    std::cout << "[VisionManagerPage] Search Box changed: (" << startX << "," << startY << ") to (" << endX << "," << endY << ")" << std::endl;

    // Update image widget overlay
    m_imageWidget->setSearchBox(startX, startY, endX, endY);

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Search Box updated", "VisionManagerPage");
    }
}

void VisionManagerPage::onSearchBoxEnabledChanged(bool enabled)
{
    std::cout << "[VisionManagerPage] Search Box " << (enabled ? "enabled" : "disabled") << std::endl;

    // Update image widget overlay
    m_imageWidget->setSearchBoxEnabled(enabled);

    if (m_context && m_context->logManager) {
        m_context->logManager->Info(enabled ? "Search Box enabled" : "Search Box disabled", "VisionManagerPage");
    }
}

// ========== Template Selection Actions ==========

void VisionManagerPage::onTemplateSelectionDragged(int startX, int startY, int endX, int endY)
{
    std::cout << "[VisionManagerPage] Template Selection dragged: (" << startX << "," << startY << ") to (" << endX << "," << endY << ")" << std::endl;
    // Just update display - no other action needed while dragging
}

void VisionManagerPage::onTemplateSelectionConfirmed()
{
    if (m_templateSelectionMode && m_templateSelectionOnConfirm) {
        int startX, startY, endX, endY;
        m_imageWidget->getTemplateSelection(startX, startY, endX, endY);

        std::cout << "[VisionManagerPage] Template selection confirmed: ("
                  << startX << "," << startY << ") to (" << endX << "," << endY << ")" << std::endl;

        // Call the callback
        m_templateSelectionOnConfirm(startX, startY, endX, endY);

        // Stop template selection mode
        stopTemplateSelectionMode();
    }
}

void VisionManagerPage::onTemplateSelectionCancelled()
{
    if (m_templateSelectionMode) {
        std::cout << "[VisionManagerPage] Template selection cancelled" << std::endl;

        if (m_templateSelectionOnCancel) {
            m_templateSelectionOnCancel();
        }

        // Stop template selection mode
        stopTemplateSelectionMode();
    }
}

void VisionManagerPage::startTemplateSelectionMode(std::function<void(int, int, int, int)> onConfirm,
                                                   std::function<void()> onCancel)
{
    m_templateSelectionMode = true;
    m_templateSelectionOnConfirm = onConfirm;
    m_templateSelectionOnCancel = onCancel;

    // Enable template selection in image widget
    m_imageWidget->setTemplateSelectionEnabled(true);

    std::cout << "[VisionManagerPage] Template selection mode started" << std::endl;

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Template selection mode activated", "VisionManagerPage");
    }
}

void VisionManagerPage::stopTemplateSelectionMode()
{
    m_templateSelectionMode = false;
    m_templateSelectionOnConfirm = nullptr;
    m_templateSelectionOnCancel = nullptr;

    // Disable template selection in image widget
    m_imageWidget->setTemplateSelectionEnabled(false);

    std::cout << "[VisionManagerPage] Template selection mode stopped" << std::endl;

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Template selection mode deactivated", "VisionManagerPage");
    }
}

void VisionManagerPage::registerPlugins()
{
    std::cout << "[VisionManagerPage] Registering vision plugins..." << std::endl;

    // Register Circle Detection Plugin
    auto circlePlugin = std::make_shared<CircleDetectionPlugin>();
    VisionManager::instance().registerPlugin(circlePlugin);

    // Register Edge Detection Plugin
    auto edgePlugin = std::make_shared<EdgeDetectionPlugin>();
    VisionManager::instance().registerPlugin(edgePlugin);

    // Register Fiducial Detector Plugin
    auto fiducialPlugin = std::make_shared<FiducialDetectorPlugin>();
    VisionManager::instance().registerPlugin(fiducialPlugin);

    // Register Line Detector Plugin
    auto linePlugin = std::make_shared<LineDetectorPlugin>();
    VisionManager::instance().registerPlugin(linePlugin);

    // Register Corner Detector Plugin
    auto cornerPlugin = std::make_shared<CornerDetectorPlugin>();
    VisionManager::instance().registerPlugin(cornerPlugin);

    // Register Pattern Matching Plugin
    auto patternPlugin = std::make_shared<PatternMatchingPlugin>();
    VisionManager::instance().registerPlugin(patternPlugin);

    // Register Pattern Matching Edge Plugin
    auto patternEdgePlugin = std::make_shared<PatternMatchingEdgePlugin>();
    VisionManager::instance().registerPlugin(patternEdgePlugin);

    // Register Image Processing Plugin (rotate, flip, grayscale, binary, gaussian)
    auto imageProcessingPlugin = std::make_shared<ImageProcessingPlugin>();
    VisionManager::instance().registerPlugin(imageProcessingPlugin);

    // Register Calibration Plate Plugin (MVTec calibration plate mm/pixel calculation)
    auto calibrationPlatePlugin = std::make_shared<CalibrationPlatePlugin>();
    VisionManager::instance().registerPlugin(calibrationPlatePlugin);

#ifdef HAS_HALCON
    // Register Halcon Circle Detection Plugin
    auto halconCirclePlugin = std::make_shared<HalconCircleDetectionPlugin>();
    VisionManager::instance().registerPlugin(halconCirclePlugin);
    std::cout << "[VisionManagerPage] Registered Halcon Circle Detection plugin" << std::endl;

    // Register Halcon Circle Detection Robust Plugin
    auto halconCircleRobustPlugin = std::make_shared<HalconCircleDetectionRobustPlugin>();
    VisionManager::instance().registerPlugin(halconCircleRobustPlugin);
    std::cout << "[VisionManagerPage] Registered Halcon Circle Detection Robust plugin" << std::endl;

    // Register Halcon Line Detection Plugin
    auto halconLinePlugin = std::make_shared<HalconLineDetectionPlugin>();
    VisionManager::instance().registerPlugin(halconLinePlugin);
    std::cout << "[VisionManagerPage] Registered Halcon Line Detection plugin" << std::endl;

    // Register Halcon Pattern Matching Edge Plugin
    auto halconPatternEdgePlugin = std::make_shared<HalconPatternMatchingEdgePlugin>();
    halconPatternEdgePlugin->setVisionManagerPage(this);
    VisionManager::instance().registerPlugin(halconPatternEdgePlugin);
    std::cout << "[VisionManagerPage] Registered Halcon Pattern Matching Edge plugin" << std::endl;
#endif

    if (m_context && m_context->logManager) {
        int count = VisionManager::instance().getPluginCount();
        m_context->logManager->Info("Registered " + std::to_string(count) + " vision plugins", "VisionManagerPage");
    }
}

void VisionManagerPage::updatePluginList()
{
    std::cout << "[VisionManagerPage] Updating plugin list from VisionManager" << std::endl;

    auto& vm = VisionManager::instance();

    // Get plugins by category
    QStringList prPlugins = vm.getPluginsByCategory("PR");
    QStringList measurePlugins = vm.getPluginsByCategory("Measurement");
    QStringList filterPlugins = vm.getPluginsByCategory("Filter");
    QStringList calibrationPlugins = vm.getPluginsByCategory("Calibration");

    // Update the left panel
    m_leftPanel->updatePluginsByCategory(prPlugins, measurePlugins, filterPlugins, calibrationPlugins);

    std::cout << "[VisionManagerPage] Plugin list: PR=" << prPlugins.size()
              << ", Measure=" << measurePlugins.size()
              << ", Filter=" << filterPlugins.size()
              << ", Calibration=" << calibrationPlugins.size() << std::endl;
}

void VisionManagerPage::loadPluginConfig(const QString& pluginName)
{
    std::cout << "[VisionManagerPage] Loading config for: " << pluginName.toStdString() << std::endl;

    m_rightPanel->clearPluginConfigWidget();

    auto plugin = VisionManager::instance().getPlugin(pluginName);
    if (!plugin) {
        std::cerr << "[VisionManagerPage] Plugin not found: " << pluginName.toStdString() << std::endl;
        return;
    }

    // Create the plugin's config widget
    QWidget* configWidget = plugin->createConfigWidget();
    if (configWidget) {
        m_rightPanel->setPluginConfigWidget(configWidget);
        std::cout << "[VisionManagerPage] Config widget loaded for: " << pluginName.toStdString() << std::endl;
    } else {
        std::cerr << "[VisionManagerPage] Failed to create config widget for: " << pluginName.toStdString() << std::endl;
    }

    // If it's a PatternMatchingPlugin, pass the current ROI image for teaching and set VisionManagerPage reference
    if (pluginName == "PatternMatching") {
        updatePatternMatchingSourceImage();

        // Give plugin access to VisionManagerPage for template selection mode
        auto pmPlugin = std::dynamic_pointer_cast<PatternMatchingPlugin>(plugin);
        if (pmPlugin) {
            pmPlugin->setVisionManagerPage(this);
        }
    }

    // If it's a PatternMatchingEdgePlugin, set VisionManagerPage reference for template selection
    if (pluginName == "PatternMatchingEdge") {
        auto pmePlugin = std::dynamic_pointer_cast<PatternMatchingEdgePlugin>(plugin);
        if (pmePlugin) {
            pmePlugin->setVisionManagerPage(this);

            // Set template directory and profile name for auto-save
            if (!m_currentProfilePath.isEmpty()) {
                QFileInfo profileInfo(m_currentProfilePath);
                QString visionDir = profileInfo.absolutePath();
                pmePlugin->setTemplateDirectory(visionDir.toStdString());
                pmePlugin->setProfileName(profileInfo.baseName().toStdString());
            } else if (m_context && m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
                QString configPath = QCoreApplication::applicationDirPath() + "/config/"
                                    + QString::fromStdString(m_context->productPositionManager->GetCurrentProductName()) + "/vision";
                pmePlugin->setTemplateDirectory(configPath.toStdString());
                pmePlugin->setProfileName("untitled");
            }
        }
    }
}

void VisionManagerPage::updatePatternMatchingSourceImage()
{
    auto plugin = VisionManager::instance().getPlugin("PatternMatching");
    if (!plugin) return;

    auto patternPlugin = std::dynamic_pointer_cast<PatternMatchingPlugin>(plugin);
    if (!patternPlugin) return;

    // Set template directory and profile name for auto-save
    if (!m_currentProfilePath.isEmpty()) {
        QFileInfo profileInfo(m_currentProfilePath);
        QString visionDir = profileInfo.absolutePath();
        patternPlugin->setTemplateDirectory(visionDir.toStdString());
        patternPlugin->setProfileName(profileInfo.baseName().toStdString());
        std::cout << "[VisionManagerPage] Set template directory: " << visionDir.toStdString()
                  << ", profile: " << profileInfo.baseName().toStdString() << std::endl;
    } else if (m_context && m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
        // Use product-based path if no profile path set
        QString configPath = QCoreApplication::applicationDirPath() + "/config/"
                            + QString::fromStdString(m_context->productPositionManager->GetCurrentProductName()) + "/vision";
        patternPlugin->setTemplateDirectory(configPath.toStdString());
        patternPlugin->setProfileName("untitled");
        std::cout << "[VisionManagerPage] Set default template directory: " << configPath.toStdString() << std::endl;
    }

    // Get the current input image and ROI
    cv::Mat inputImage = VisionManager::instance().getInputImage();
    if (inputImage.empty()) {
        std::cout << "[VisionManagerPage] No input image available for PatternMatching teaching" << std::endl;
        return;
    }

    // Get current ROI
    VisionROI roi = VisionManager::instance().getROI();
    if (!roi.isValid()) {
        // No ROI set, use entire image
        patternPlugin->setSourceImage(inputImage);
        std::cout << "[VisionManagerPage] Set full image for PatternMatching teaching" << std::endl;
        return;
    }

    // Crop image to ROI
    int startX = roi.getStartX();
    int startY = roi.getStartY();
    int endX = roi.getEndX();
    int endY = roi.getEndY();

    // Clamp to image bounds
    startX = std::max(0, std::min(startX, inputImage.cols - 1));
    startY = std::max(0, std::min(startY, inputImage.rows - 1));
    endX = std::max(startX + 1, std::min(endX, inputImage.cols));
    endY = std::max(startY + 1, std::min(endY, inputImage.rows));

    cv::Mat roiImage = inputImage(cv::Rect(startX, startY, endX - startX, endY - startY)).clone();

    patternPlugin->setSourceImage(roiImage);
    std::cout << "[VisionManagerPage] Set ROI image (" << roiImage.cols << "x" << roiImage.rows
              << ") for PatternMatching teaching" << std::endl;
}

void VisionManagerPage::saveProfileToPath(const QString& filePath)
{
    std::cout << "[VisionManagerPage] Saving profile to: " << filePath.toStdString() << std::endl;

    try {
        nlohmann::json j;

        // Save profile metadata
        j["version"] = "1.0";
        j["name"] = QFileInfo(filePath).baseName().toStdString();

        if (m_context && m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
            j["product"] = m_context->productPositionManager->GetCurrentProductName();
        }

        // Get current ROI from VisionManager
        VisionROI currentROI = VisionManager::instance().getROI();

        // Save ROI settings
        j["roi"]["startX"] = currentROI.getStartX();
        j["roi"]["startY"] = currentROI.getStartY();
        j["roi"]["endX"] = currentROI.getEndX();
        j["roi"]["endY"] = currentROI.getEndY();
        j["roi"]["enabled"] = currentROI.isEnabled();

        // Save Search Box settings
        int searchStartX, searchStartY, searchEndX, searchEndY;
        m_rightPanel->getSearchBox(searchStartX, searchStartY, searchEndX, searchEndY);
        j["searchBox"]["startX"] = searchStartX;
        j["searchBox"]["startY"] = searchStartY;
        j["searchBox"]["endX"] = searchEndX;
        j["searchBox"]["endY"] = searchEndY;
        j["searchBox"]["enabled"] = m_rightPanel->isSearchBoxEnabled();

        // Save camera settings
        VisionCameraSettings camSettings = m_rightPanel->getCameraSettings();
        j["camera"]["cameraId"] = camSettings.cameraId.toStdString();
        j["camera"]["exposureTime"] = camSettings.exposureTime;
        j["camera"]["gain"] = camSettings.gain;
        j["camera"]["autoExposure"] = camSettings.autoExposure;
        j["camera"]["autoGain"] = camSettings.autoGain;
        j["camera"]["pixelToMmX"] = camSettings.pixelToMmX;
        j["camera"]["pixelToMmY"] = camSettings.pixelToMmY;

        // Save lighting settings
        VisionLightingSettings lightingSettings = m_rightPanel->getLightingSettings();
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

        // Save plugin and parameters
        if (!m_selectedPluginName.isEmpty()) {
            auto plugin = VisionManager::instance().getPlugin(m_selectedPluginName);
            if (plugin) {
                j["plugin"]["name"] = m_selectedPluginName.toStdString();
                j["plugin"]["category"] = plugin->getCategory();
                j["plugin"]["parameters"] = plugin->getParameters();
            }
        }

        // Write to file
        std::ofstream file(filePath.toStdString());
        if (!file.is_open()) {
            m_leftPanel->displayResult("Error: Cannot create file\n" + filePath);
            return;
        }

        file << j.dump(4);  // Pretty print with 4-space indent
        file.close();

        // Store the current profile path
        m_currentProfilePath = filePath;
        m_currentRecipePath.clear();  // Clear recipe path when saving profile
        m_profileModified = false;

        // Update UI to show current profile
        updateProfileDisplay();

        if (m_context && m_context->logManager) {
            m_context->logManager->Info("Vision profile saved: " + filePath.toStdString(), "VisionManagerPage");
        }

        QString resultText = "Vision Profile Saved!\n\n";
        resultText += "File: " + QFileInfo(filePath).fileName() + "\n";
        resultText += "Path: " + filePath + "\n\n";
        resultText += "Saved:\n";
        resultText += "- ROI settings\n";
        resultText += "- Camera: " + camSettings.cameraId + "\n";
        resultText += "- Lighting: " + QString(lightingSettings.enabled ? "Enabled" : "Disabled") + "\n";
        resultText += "- Plugin: " + m_selectedPluginName + "\n";
        resultText += "- All plugin parameters\n";

        m_leftPanel->displayResult(resultText);

    } catch (const std::exception& e) {
        m_leftPanel->displayResult("Error saving profile:\n" + QString::fromStdString(e.what()));
    }
}

void VisionManagerPage::saveRecipeToPath(const QString& filePath)
{
    std::cout << "[VisionManagerPage] Saving recipe to: " << filePath.toStdString() << std::endl;

    try {
        nlohmann::json j;

        // Metadata
        j["version"] = "1.0";
        j["type"] = "vision_recipe";
        j["name"] = QFileInfo(filePath).baseName().toStdString();

        if (m_context && m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
            j["product"] = m_context->productPositionManager->GetCurrentProductName();
        }

        // ROI settings
        VisionROI currentROI = VisionManager::instance().getROI();
        j["roi"]["startX"] = currentROI.getStartX();
        j["roi"]["startY"] = currentROI.getStartY();
        j["roi"]["endX"] = currentROI.getEndX();
        j["roi"]["endY"] = currentROI.getEndY();
        j["roi"]["enabled"] = currentROI.isEnabled();

        // Search Box settings
        int searchStartX, searchStartY, searchEndX, searchEndY;
        m_rightPanel->getSearchBox(searchStartX, searchStartY, searchEndX, searchEndY);
        j["searchBox"]["startX"] = searchStartX;
        j["searchBox"]["startY"] = searchStartY;
        j["searchBox"]["endX"] = searchEndX;
        j["searchBox"]["endY"] = searchEndY;
        j["searchBox"]["enabled"] = m_rightPanel->isSearchBoxEnabled();

        // Camera settings
        VisionCameraSettings camSettings = m_rightPanel->getCameraSettings();
        j["camera"]["cameraId"] = camSettings.cameraId.toStdString();
        j["camera"]["exposureTime"] = camSettings.exposureTime;
        j["camera"]["gain"] = camSettings.gain;
        j["camera"]["pixelToMmX"] = camSettings.pixelToMmX;
        j["camera"]["pixelToMmY"] = camSettings.pixelToMmY;

        // Recipe steps
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

        // Write
        std::ofstream file(filePath.toStdString());
        if (!file.is_open()) {
            m_leftPanel->displayResult("Error: Cannot create file\n" + filePath);
            return;
        }

        file << j.dump(4);
        file.close();

        // Store the current recipe path
        m_currentRecipePath = filePath;
        m_currentProfilePath.clear();  // Clear profile path when saving recipe
        m_profileModified = false;

        // Update UI
        updateProfileDisplay();

        if (m_context && m_context->logManager) {
            m_context->logManager->Info("Vision recipe saved: " + filePath.toStdString(), "VisionManagerPage");
        }

        QString resultText = "Vision Recipe Saved!\n\n";
        resultText += "File: " + QFileInfo(filePath).fileName() + "\n";
        resultText += "Steps: " + QString::number(stepCount) + "\n";
        resultText += "Path: " + filePath + "\n";

        m_leftPanel->displayResult(resultText);

    } catch (const std::exception& e) {
        m_leftPanel->displayResult("Error saving recipe:\n" + QString::fromStdString(e.what()));
    }
}

void VisionManagerPage::updateProfileDisplay()
{
    QString title = "Vision Manager";
    if (!m_currentProfilePath.isEmpty()) {
        title += " - " + QFileInfo(m_currentProfilePath).fileName();
    } else if (!m_currentRecipePath.isEmpty()) {
        title += " - " + QFileInfo(m_currentRecipePath).fileName();
    }

    // Update toolbar title or window title
    m_toolbar->setTitle(title);
}

// ========== Worker Thread Management ==========
void VisionManagerPage::setupWorker()
{
#ifdef HAS_OPENCV
    std::cout << "[VisionManagerPage] Setting up vision processing worker..." << std::endl;

    m_worker = new VisionProcessingWorker();

    // Connect worker signals to UI slots
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

    // Start the worker thread
    m_worker->startWorkerThread();

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Vision processing worker initialized", "VisionManagerPage");
    }

    std::cout << "[VisionManagerPage] Worker setup complete" << std::endl;
#else
    std::cout << "[VisionManagerPage] OpenCV not available, worker not created" << std::endl;
#endif
}

void VisionManagerPage::setUIProcessingState(bool isProcessing)
{
    // Disable/enable UI elements during processing
    m_toolbar->setEnabled(!isProcessing);
    m_leftPanel->setEnabled(!isProcessing);
    m_rightPanel->setEnabled(!isProcessing);
    m_imageWidget->setEnabled(!isProcessing);
    m_backButton->setEnabled(!isProcessing);

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

    QString info = "Processing: " + pluginName + "\n\n";
    info += "Status: Running...\n";
    info += "Progress: 0%";
    m_leftPanel->displayResult(info);

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Started processing: " + pluginName.toStdString(), "VisionManagerPage");
    }
}

void VisionManagerPage::onProcessingProgress(int percent, const QString& message)
{
    std::cout << "[VisionManagerPage] Processing progress: " << percent << "% - " << message.toStdString() << std::endl;

    QString info = "Processing: " + m_selectedPluginName + "\n\n";
    info += "Status: " + message + "\n";
    info += "Progress: " + QString::number(percent) + "%\n";

    // Add a simple progress bar
    int barWidth = 30;
    int filled = (percent * barWidth) / 100;
    QString bar = "[";
    for (int i = 0; i < barWidth; ++i) {
        if (i < filled) {
            bar += "=";
        } else if (i == filled) {
            bar += ">";
        } else {
            bar += " ";
        }
    }
    bar += "]";
    info += bar;

    m_leftPanel->displayResult(info);
}

void VisionManagerPage::onProcessingFinished(const VisionResult& result, const QString& pluginName)
{
    std::cout << "[VisionManagerPage] Processing finished: " << pluginName.toStdString() << std::endl;

    setUIProcessingState(false);

    // Display results (similar to original onApplyParams logic)
    QString resultText = "Plugin: " + pluginName + "\n";
    resultText += "Status: " + QString(result.success ? "SUCCESS" : "FAILED") + "\n";
    resultText += "Processing Time: " + QString::number(result.processingTimeMs, 'f', 2) + " ms\n";
    resultText += QString(50, '=') + "\n\n";

    if (result.success) {
        // Show numeric results
        if (!result.measurements.empty()) {
            resultText += "Measurements:\n";
            for (const auto& kv : result.measurements) {
                resultText += "  " + QString::fromStdString(kv.first) + ": "
                           + QString::number(kv.second, 'f', 4) + "\n";
            }
            resultText += "\n";
        }

        // Show detected circles
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

        // Show detected lines
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
            if (result.lines.size() > 5) {
                resultText += "  ... and " + QString::number(result.lines.size() - 5) + " more\n";
            }
            resultText += "\n";
        }

        // Show detected points
        if (!result.points.empty()) {
            resultText += "Detected Points: " + QString::number(result.points.size()) + "\n";
            for (size_t i = 0; i < std::min(result.points.size(), size_t(10)); ++i) {
                const auto& point = result.points[i];
                resultText += "  " + QString::number(i+1) + ". Point at ("
                           + QString::number(point.pixelCoord.x, 'f', 1) + ", "
                           + QString::number(point.pixelCoord.y, 'f', 1) + ")\n";
            }
            if (result.points.size() > 10) {
                resultText += "  ... and " + QString::number(result.points.size() - 10) + " more\n";
            }
            resultText += "\n";
        }

        // Show metadata
        if (!result.metadata.empty()) {
            resultText += "Metadata:\n";
            for (const auto& kv : result.metadata) {
                resultText += "  " + QString::fromStdString(kv.first) + ": "
                           + QString::fromStdString(kv.second) + "\n";
            }
        }

        // Create result markers for detected circles
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

        // Set markers on image widget
        m_imageWidget->setResultMarkers(markers);

        // Set overlay commands (contours, lines, etc.)
        m_imageWidget->setOverlays(result.overlays);

    } else {
        resultText += "Error: " + QString::fromStdString(result.errorMessage) + "\n";

        // Clear markers on failure
        m_imageWidget->clearResultMarkers();
        m_imageWidget->clearOverlays();
    }

    m_leftPanel->displayResult(resultText);

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Processing completed: " +
            std::string(result.success ? "success" : "failed"), "VisionManagerPage");
    }
}

void VisionManagerPage::onProcessingError(const QString& errorMessage)
{
    std::cout << "[VisionManagerPage] Processing error: " << errorMessage.toStdString() << std::endl;

    setUIProcessingState(false);

    QString info = "Processing Error!\n\n";
    info += "Plugin: " + m_selectedPluginName + "\n";
    info += "Error: " + errorMessage + "\n\n";
    info += "Please check the parameters and try again.";

    m_leftPanel->displayResult(info);

    // Clear markers on error
    m_imageWidget->clearResultMarkers();
    m_imageWidget->clearOverlays();

    if (m_context && m_context->logManager) {
        m_context->logManager->Error("Processing error: " + errorMessage.toStdString(), "VisionManagerPage");
    }
}

void VisionManagerPage::onProcessingCancelled()
{
    std::cout << "[VisionManagerPage] Processing cancelled" << std::endl;

    setUIProcessingState(false);

    QString info = "Processing Cancelled\n\n";
    info += "Plugin: " + m_selectedPluginName + "\n";
    info += "The operation was cancelled by user.";

    m_leftPanel->displayResult(info);

    if (m_context && m_context->logManager) {
        m_context->logManager->Warning("Processing cancelled", "VisionManagerPage");
    }
}
