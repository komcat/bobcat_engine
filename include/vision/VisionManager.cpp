// VisionManager.cpp
#ifdef HAS_OPENCV

#include "VisionManager.h"
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <fstream>

// QSettings keys for calibration (shared with CameraClickToMoveWidget)
static constexpr const char* SETTINGS_KEY_PIXEL_TO_MM_X = "CameraClickToMove/PixelToMmFactorX";
static constexpr const char* SETTINGS_KEY_PIXEL_TO_MM_Y = "CameraClickToMove/PixelToMmFactorY";

// JSON config file for calibration (priority over QSettings)
static constexpr const char* CALIBRATION_CONFIG_FILE = "config/SYSTEM/val_pixelmm.json";

VisionManager& VisionManager::instance()
{
    static VisionManager instance;
    return instance;
}

VisionManager::VisionManager()
    : QObject(nullptr)
{
    // Load calibration from JSON config file first, fallback to QSettings
    loadCalibrationFromSettings();
    std::cout << "[VisionManager] Initialized with calibration: "
              << m_pixelToMmX << " mm/px (X), " << m_pixelToMmY << " mm/px (Y)" << std::endl;
}

// ========== Plugin Registry ==========

void VisionManager::registerPlugin(std::shared_ptr<IVisionPlugin> plugin)
{
    if (!plugin) {
        std::cerr << "[VisionManager] Cannot register null plugin" << std::endl;
        return;
    }

    QString name = QString::fromStdString(plugin->getName());

    if (m_plugins.find(name) != m_plugins.end()) {
        std::cerr << "[VisionManager] Plugin '" << name.toStdString()
                  << "' already registered, replacing" << std::endl;
    }

    m_plugins[name] = plugin;
    std::cout << "[VisionManager] Registered plugin: " << name.toStdString()
              << " (Category: " << plugin->getCategory() << ")" << std::endl;

    emit pluginRegistered(name);
}

void VisionManager::unregisterPlugin(const QString& name)
{
    auto it = m_plugins.find(name);
    if (it != m_plugins.end()) {
        m_plugins.erase(it);
        std::cout << "[VisionManager] Unregistered plugin: " << name.toStdString() << std::endl;
        emit pluginUnregistered(name);
    }
}

std::shared_ptr<IVisionPlugin> VisionManager::getPlugin(const QString& name)
{
    auto it = m_plugins.find(name);
    if (it != m_plugins.end()) {
        return it->second;
    }
    return nullptr;
}

QStringList VisionManager::getAllPluginNames() const
{
    QStringList names;
    for (const auto& [name, plugin] : m_plugins) {
        names.append(name);
    }
    names.sort();
    return names;
}

QStringList VisionManager::getPluginsByCategory(const QString& category) const
{
    QStringList names;
    for (const auto& [name, plugin] : m_plugins) {
        if (QString::fromStdString(plugin->getCategory()) == category) {
            names.append(name);
        }
    }
    names.sort();
    return names;
}

QStringList VisionManager::getCategories() const
{
    QStringList categories;
    for (const auto& [name, plugin] : m_plugins) {
        QString cat = QString::fromStdString(plugin->getCategory());
        if (!categories.contains(cat)) {
            categories.append(cat);
        }
    }
    categories.sort();
    return categories;
}

int VisionManager::getPluginCount() const
{
    return static_cast<int>(m_plugins.size());
}

// ========== Image Management ==========

void VisionManager::setInputImage(const cv::Mat& image)
{
    if (image.empty()) {
        std::cerr << "[VisionManager] Cannot set empty image" << std::endl;
        return;
    }

    m_inputImage = image.clone();
    m_outputImage = cv::Mat();  // Clear output
    std::cout << "[VisionManager] Input image set: " << image.cols << "x" << image.rows
              << " channels=" << image.channels() << std::endl;

    emit imageUpdated();
}

cv::Mat VisionManager::getInputImage() const
{
    return m_inputImage;
}

cv::Mat VisionManager::getOutputImage() const
{
    return m_outputImage;
}

bool VisionManager::hasValidImage() const
{
    return !m_inputImage.empty();
}

void VisionManager::clearImages()
{
    m_inputImage = cv::Mat();
    m_outputImage = cv::Mat();
    emit imageUpdated();
}

// ========== Plugin Execution ==========

VisionResult VisionManager::executePlugin(const QString& pluginName)
{
    auto plugin = getPlugin(pluginName);
    if (!plugin) {
        VisionResult result;
        result.success = false;
        result.pluginName = pluginName.toStdString();
        result.errorMessage = "Plugin not found: " + pluginName.toStdString();
        std::cerr << "[VisionManager] " << result.errorMessage << std::endl;
        return result;
    }

    return executePlugin(pluginName, plugin->getParameters());
}

VisionResult VisionManager::executePlugin(const QString& pluginName, const nlohmann::json& params)
{
    VisionResult result;
    result.pluginName = pluginName.toStdString();

    auto plugin = getPlugin(pluginName);
    if (!plugin) {
        result.success = false;
        result.errorMessage = "Plugin not found: " + pluginName.toStdString();
        std::cerr << "[VisionManager] " << result.errorMessage << std::endl;
        return result;
    }

    if (!hasValidImage()) {
        result.success = false;
        result.errorMessage = "No input image available";
        std::cerr << "[VisionManager] " << result.errorMessage << std::endl;
        return result;
    }

    // Set parameters
    plugin->setParameters(params);

    // Validate
    if (!plugin->validate()) {
        result.success = false;
        result.errorMessage = "Invalid parameters: " + plugin->getLastError();
        std::cerr << "[VisionManager] " << result.errorMessage << std::endl;
        return result;
    }

    std::cout << "[VisionManager] Executing plugin: " << pluginName.toStdString() << std::endl;
    emit executionStarted(pluginName);

    // Time the execution
    auto startTime = std::chrono::high_resolution_clock::now();

    // Apply ROI if enabled
    cv::Mat processingImage = m_inputImage;
    cv::Point2d roiOffset(0, 0);
    bool usingROI = false;

    if (m_roi.isEnabled() && m_roi.isValid()) {
        processingImage = m_roi.cropImage(m_inputImage);
        if (processingImage.empty()) {
            result.success = false;
            result.errorMessage = "ROI crop failed - invalid ROI bounds";
            std::cerr << "[VisionManager] " << result.errorMessage << std::endl;
            return result;
        }
        roiOffset = m_roi.getOffset();
        usingROI = true;
        std::cout << "[VisionManager] Using ROI: " << m_roi.toString() << std::endl;
        result.addMetadata("roi_used", "true");
        result.addMetadata("roi_offset_x", std::to_string(roiOffset.x));
        result.addMetadata("roi_offset_y", std::to_string(roiOffset.y));
    }

    // Execute
    m_outputImage = processingImage.clone();
    result.success = plugin->execute(processingImage, m_outputImage, result);

    auto endTime = std::chrono::high_resolution_clock::now();
    result.processingTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    if (!result.success) {
        result.errorMessage = plugin->getLastError();
        std::cerr << "[VisionManager] Plugin failed: " << result.errorMessage << std::endl;
    } else {
        // Apply ROI offset to convert coordinates back to full image space
        if (usingROI) {
            applyROIOffsetToResult(result, roiOffset);
        }

        // Apply calibration to all coordinates
        applyCalibrationToResult(result);
        std::cout << "[VisionManager] Plugin succeeded in " << result.processingTimeMs << " ms" << std::endl;
    }

    m_lastResult = result;
    emit resultReady(result);
    emit executionFinished(pluginName, result.success);
    emit imageUpdated();

    return result;
}

VisionResult VisionManager::getLastResult() const
{
    return m_lastResult;
}

// ========== Calibration ==========

void VisionManager::setPixelToMm(double x, double y)
{
    if (x <= 0 || y <= 0) {
        std::cerr << "[VisionManager] Invalid calibration values: x=" << x << ", y=" << y << std::endl;
        return;
    }

    m_pixelToMmX = x;
    m_pixelToMmY = y;

    // Save to QSettings (persistent storage)
    saveCalibrationToSettings();

    std::cout << "[VisionManager] Calibration set and saved: " << x << " mm/px (X), " << y << " mm/px (Y)" << std::endl;

    emit calibrationChanged(x, y);
}

double VisionManager::getPixelToMmX() const
{
    return m_pixelToMmX;
}

double VisionManager::getPixelToMmY() const
{
    return m_pixelToMmY;
}

void VisionManager::loadCalibrationFromSettings()
{
    // Try to load from JSON config file first (priority)
    QString appDir = QCoreApplication::applicationDirPath();
    QString configPath = appDir + "/" + CALIBRATION_CONFIG_FILE;

    if (QFile::exists(configPath)) {
        try {
            std::ifstream file(configPath.toStdString());
            if (file.is_open()) {
                nlohmann::json j;
                file >> j;

                if (j.contains("calibration")) {
                    auto& cal = j["calibration"];
                    m_pixelToMmX = cal.value("pixel_to_mm_x", 0.00248);
                    m_pixelToMmY = cal.value("pixel_to_mm_y", 0.00248);
                    std::cout << "[VisionManager] Loaded calibration from JSON: " << configPath.toStdString() << std::endl;

                    // Sync to QSettings for compatibility
                    QSettings settings;
                    settings.setValue(SETTINGS_KEY_PIXEL_TO_MM_X, m_pixelToMmX);
                    settings.setValue(SETTINGS_KEY_PIXEL_TO_MM_Y, m_pixelToMmY);
                    return;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[VisionManager] Error loading JSON config: " << e.what() << std::endl;
        }
    } else {
        std::cout << "[VisionManager] JSON config not found: " << configPath.toStdString() << std::endl;
    }

    // Fallback to QSettings
    QSettings settings;
    m_pixelToMmX = settings.value(SETTINGS_KEY_PIXEL_TO_MM_X, 0.00248).toDouble();
    m_pixelToMmY = settings.value(SETTINGS_KEY_PIXEL_TO_MM_Y, 0.00248).toDouble();
    std::cout << "[VisionManager] Loaded calibration from QSettings (fallback)" << std::endl;
}

void VisionManager::saveCalibrationToSettings()
{
    // Save to JSON config file
    QString appDir = QCoreApplication::applicationDirPath();
    QString configPath = appDir + "/" + CALIBRATION_CONFIG_FILE;

    try {
        nlohmann::json j;
        j["description"] = "Pixel to millimeter calibration values for vision system";
        j["calibration"]["pixel_to_mm_x"] = m_pixelToMmX;
        j["calibration"]["pixel_to_mm_y"] = m_pixelToMmY;
        j["notes"] = "These values are hardware-specific and should be calibrated for each camera setup";

        std::ofstream file(configPath.toStdString());
        if (file.is_open()) {
            file << j.dump(2);
            file.close();
            std::cout << "[VisionManager] Saved calibration to JSON: " << configPath.toStdString() << std::endl;
        } else {
            std::cerr << "[VisionManager] Failed to open JSON config for writing: " << configPath.toStdString() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[VisionManager] Error saving JSON config: " << e.what() << std::endl;
    }

    // Also save to QSettings for compatibility
    QSettings settings;
    settings.setValue(SETTINGS_KEY_PIXEL_TO_MM_X, m_pixelToMmX);
    settings.setValue(SETTINGS_KEY_PIXEL_TO_MM_Y, m_pixelToMmY);
    std::cout << "[VisionManager] Saved calibration to QSettings" << std::endl;
}

cv::Point2d VisionManager::convertPixelToMm(const cv::Point2d& pixelPt) const
{
    return cv::Point2d(pixelPt.x * m_pixelToMmX, pixelPt.y * m_pixelToMmY);
}

cv::Point2d VisionManager::convertMmToPixel(const cv::Point2d& mmPt) const
{
    return cv::Point2d(mmPt.x / m_pixelToMmX, mmPt.y / m_pixelToMmY);
}

double VisionManager::convertRadiusToMm(double radiusPx) const
{
    // Use average of X and Y for radius (assumes square pixels)
    return radiusPx * (m_pixelToMmX + m_pixelToMmY) / 2.0;
}

void VisionManager::applyCalibrationToResult(VisionResult& result)
{
    // Convert all pixel coordinates to mm
    for (auto& pt : result.points) {
        pt.mmCoord = convertPixelToMm(pt.pixelCoord);
    }

    for (auto& circle : result.circles) {
        circle.centerMm = convertPixelToMm(circle.centerPx);
        circle.radiusMm = convertRadiusToMm(circle.radiusPx);
        circle.diameterMm = circle.radiusMm * 2.0;
    }

    for (auto& line : result.lines) {
        line.startMm = convertPixelToMm(line.startPx);
        line.endMm = convertPixelToMm(line.endPx);
        line.lengthMm = cv::norm(line.endMm - line.startMm);
    }

    for (auto& rect : result.rectangles) {
        rect.centerMm = convertPixelToMm(rect.centerPx);
        rect.boundingBoxMm.x = rect.boundingBoxPx.x * m_pixelToMmX;
        rect.boundingBoxMm.y = rect.boundingBoxPx.y * m_pixelToMmY;
        rect.boundingBoxMm.width = rect.boundingBoxPx.width * m_pixelToMmX;
        rect.boundingBoxMm.height = rect.boundingBoxPx.height * m_pixelToMmY;
        rect.widthMm = rect.widthPx * m_pixelToMmX;
        rect.heightMm = rect.heightPx * m_pixelToMmY;
        rect.areaMm = rect.areaPx * m_pixelToMmX * m_pixelToMmY;
    }
}

void VisionManager::applyROIOffsetToResult(VisionResult& result, const cv::Point2d& offset)
{
    // Adjust all pixel coordinates by ROI offset (convert back to full image coordinates)
    for (auto& pt : result.points) {
        pt.pixelCoord.x += offset.x;
        pt.pixelCoord.y += offset.y;
    }

    for (auto& circle : result.circles) {
        circle.centerPx.x += offset.x;
        circle.centerPx.y += offset.y;
    }

    for (auto& line : result.lines) {
        line.startPx.x += offset.x;
        line.startPx.y += offset.y;
        line.endPx.x += offset.x;
        line.endPx.y += offset.y;
    }

    for (auto& rect : result.rectangles) {
        rect.centerPx.x += offset.x;
        rect.centerPx.y += offset.y;
        rect.boundingBoxPx.x += offset.x;
        rect.boundingBoxPx.y += offset.y;
    }

    // Adjust overlay coordinates as well
    for (auto& overlay : result.overlays) {
        overlay.p1.x += offset.x;
        overlay.p1.y += offset.y;
        overlay.p2.x += offset.x;
        overlay.p2.y += offset.y;
        overlay.rect.x += offset.x;
        overlay.rect.y += offset.y;

        // Adjust contour points
        for (auto& pt : overlay.contour) {
            pt.x += static_cast<int>(offset.x);
            pt.y += static_cast<int>(offset.y);
        }
    }
}

// ========== ROI Management ==========

void VisionManager::setROI(const VisionROI& roi)
{
    m_roi = roi;
    std::cout << "[VisionManager] ROI set: " << roi.toString() << std::endl;
    emit roiChanged(m_roi);
}

VisionROI VisionManager::getROI() const
{
    return m_roi;
}

void VisionManager::setROIEnabled(bool enabled)
{
    m_roi.setEnabled(enabled);
    std::cout << "[VisionManager] ROI " << (enabled ? "enabled" : "disabled") << std::endl;
    emit roiChanged(m_roi);
}

bool VisionManager::isROIEnabled() const
{
    return m_roi.isEnabled();
}

void VisionManager::clearROI()
{
    m_roi.reset();
    std::cout << "[VisionManager] ROI cleared" << std::endl;
    emit roiChanged(m_roi);
}

// ========== Recipe Management ==========

void VisionManager::newRecipe()
{
    m_recipe.clear();
    m_currentRecipePath.clear();
    m_unsavedChanges = false;
    std::cout << "[VisionManager] New recipe created" << std::endl;
    emit recipeChanged();
}

bool VisionManager::loadRecipe(const QString& filePath)
{
    try {
        std::ifstream file(filePath.toStdString());
        if (!file.is_open()) {
            std::cerr << "[VisionManager] Cannot open recipe file: " << filePath.toStdString() << std::endl;
            return false;
        }

        nlohmann::json j;
        file >> j;
        file.close();

        m_recipe.clear();

        if (j.contains("steps") && j["steps"].is_array()) {
            for (const auto& step : j["steps"]) {
                RecipeStep rs;
                rs.pluginName = QString::fromStdString(step.value("plugin", ""));
                rs.parameters = step.value("parameters", nlohmann::json::object());
                rs.enabled = step.value("enabled", true);
                m_recipe.push_back(rs);
            }
        }

        m_currentRecipePath = filePath;
        m_unsavedChanges = false;

        std::cout << "[VisionManager] Loaded recipe: " << filePath.toStdString()
                  << " (" << m_recipe.size() << " steps)" << std::endl;

        emit recipeChanged();
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[VisionManager] Error loading recipe: " << e.what() << std::endl;
        return false;
    }
}

bool VisionManager::saveRecipe(const QString& filePath)
{
    try {
        nlohmann::json j;
        j["version"] = "1.0";
        j["steps"] = nlohmann::json::array();

        for (const auto& step : m_recipe) {
            nlohmann::json stepJ;
            stepJ["plugin"] = step.pluginName.toStdString();
            stepJ["parameters"] = step.parameters;
            stepJ["enabled"] = step.enabled;
            j["steps"].push_back(stepJ);
        }

        std::ofstream file(filePath.toStdString());
        if (!file.is_open()) {
            std::cerr << "[VisionManager] Cannot write recipe file: " << filePath.toStdString() << std::endl;
            return false;
        }

        file << j.dump(2);
        file.close();

        m_currentRecipePath = filePath;
        m_unsavedChanges = false;

        std::cout << "[VisionManager] Saved recipe: " << filePath.toStdString() << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[VisionManager] Error saving recipe: " << e.what() << std::endl;
        return false;
    }
}

bool VisionManager::saveRecipeAs(const QString& filePath)
{
    return saveRecipe(filePath);
}

void VisionManager::addRecipeStep(const QString& pluginName, const nlohmann::json& params)
{
    RecipeStep step;
    step.pluginName = pluginName;
    step.parameters = params;
    step.enabled = true;

    m_recipe.push_back(step);
    m_unsavedChanges = true;

    std::cout << "[VisionManager] Added recipe step: " << pluginName.toStdString() << std::endl;

    emit recipeStepAdded(static_cast<int>(m_recipe.size() - 1));
    emit recipeChanged();
}

void VisionManager::removeRecipeStep(int index)
{
    if (index < 0 || index >= static_cast<int>(m_recipe.size())) {
        return;
    }

    m_recipe.erase(m_recipe.begin() + index);
    m_unsavedChanges = true;

    emit recipeStepRemoved(index);
    emit recipeChanged();
}

void VisionManager::updateRecipeStep(int index, const nlohmann::json& params)
{
    if (index < 0 || index >= static_cast<int>(m_recipe.size())) {
        return;
    }

    m_recipe[index].parameters = params;
    m_unsavedChanges = true;
    emit recipeChanged();
}

void VisionManager::clearRecipe()
{
    m_recipe.clear();
    m_unsavedChanges = true;
    emit recipeChanged();
}

int VisionManager::getRecipeStepCount() const
{
    return static_cast<int>(m_recipe.size());
}

VisionManager::RecipeStep VisionManager::getRecipeStep(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_recipe.size())) {
        return RecipeStep();
    }
    return m_recipe[index];
}

std::vector<VisionResult> VisionManager::runRecipe()
{
    std::vector<VisionResult> results;

    std::cout << "[VisionManager] Running recipe with " << m_recipe.size() << " steps" << std::endl;

    for (size_t i = 0; i < m_recipe.size(); ++i) {
        const auto& step = m_recipe[i];

        if (!step.enabled) {
            std::cout << "[VisionManager] Skipping disabled step " << i << std::endl;
            continue;
        }

        VisionResult result = executePlugin(step.pluginName, step.parameters);
        results.push_back(result);

        if (!result.success) {
            std::cerr << "[VisionManager] Recipe stopped at step " << i
                      << " due to failure" << std::endl;
            break;
        }

        // Use output as next input
        if (!m_outputImage.empty()) {
            m_inputImage = m_outputImage.clone();
        }
    }

    std::cout << "[VisionManager] Recipe completed: " << results.size() << " steps executed" << std::endl;
    return results;
}

QString VisionManager::getCurrentRecipePath() const
{
    return m_currentRecipePath;
}

bool VisionManager::hasUnsavedChanges() const
{
    return m_unsavedChanges;
}

#endif // HAS_OPENCV
