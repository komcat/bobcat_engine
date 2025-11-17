// VisionManager.h
#pragma once

#ifdef HAS_OPENCV

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>
#include <map>
#include <vector>
#include <opencv2/core.hpp>
#include "nlohmann/json.hpp"
#include "IVisionPlugin.h"
#include "VisionResult.h"
#include "VisionROI.h"

/**
 * @brief Central manager for vision processing system
 *
 * Manages plugins, image I/O, calibration, and recipe execution.
 * Singleton pattern for global access.
 */
class VisionManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Get singleton instance
     */
    static VisionManager& instance();

    // Prevent copying
    VisionManager(const VisionManager&) = delete;
    VisionManager& operator=(const VisionManager&) = delete;

    // ========== Plugin Registry ==========

    /**
     * @brief Register a new plugin
     * @param plugin Plugin instance (ownership transferred)
     */
    void registerPlugin(std::shared_ptr<IVisionPlugin> plugin);

    /**
     * @brief Unregister a plugin by name
     */
    void unregisterPlugin(const QString& name);

    /**
     * @brief Get plugin by name
     * @return Pointer to plugin or nullptr if not found
     */
    std::shared_ptr<IVisionPlugin> getPlugin(const QString& name);

    /**
     * @brief Get all registered plugin names
     */
    QStringList getAllPluginNames() const;

    /**
     * @brief Get plugin names filtered by category
     * @param category "PR", "Measurement", "Filter", "Calibration"
     */
    QStringList getPluginsByCategory(const QString& category) const;

    /**
     * @brief Get list of all categories
     */
    QStringList getCategories() const;

    /**
     * @brief Get total number of registered plugins
     */
    int getPluginCount() const;

    // ========== Image Management ==========

    /**
     * @brief Set input image for processing
     */
    void setInputImage(const cv::Mat& image);

    /**
     * @brief Get current input image
     */
    cv::Mat getInputImage() const;

    /**
     * @brief Get output image from last execution
     */
    cv::Mat getOutputImage() const;

    /**
     * @brief Check if input image is valid
     */
    bool hasValidImage() const;

    /**
     * @brief Clear all images
     */
    void clearImages();

    // ========== Plugin Execution ==========

    /**
     * @brief Execute a plugin with current parameters
     * @param pluginName Name of plugin to execute
     * @return Execution result
     */
    VisionResult executePlugin(const QString& pluginName);

    /**
     * @brief Execute a plugin with specific parameters
     * @param pluginName Name of plugin
     * @param params Parameters to use
     * @return Execution result
     */
    VisionResult executePlugin(const QString& pluginName, const nlohmann::json& params);

    /**
     * @brief Get last execution result
     */
    VisionResult getLastResult() const;

    // ========== Calibration ==========

    /**
     * @brief Set pixel to mm conversion factors
     * @param x mm per pixel in X direction
     * @param y mm per pixel in Y direction
     */
    void setPixelToMm(double x, double y);

    /**
     * @brief Get X calibration factor
     */
    double getPixelToMmX() const;

    /**
     * @brief Get Y calibration factor
     */
    double getPixelToMmY() const;

    /**
     * @brief Load calibration from QSettings (persistent storage)
     */
    void loadCalibrationFromSettings();

    /**
     * @brief Save calibration to QSettings (persistent storage)
     */
    void saveCalibrationToSettings();

    /**
     * @brief Convert pixel coordinates to mm
     */
    cv::Point2d convertPixelToMm(const cv::Point2d& pixelPt) const;

    /**
     * @brief Convert mm coordinates to pixel
     */
    cv::Point2d convertMmToPixel(const cv::Point2d& mmPt) const;

    /**
     * @brief Convert radius in pixels to mm
     */
    double convertRadiusToMm(double radiusPx) const;

    // ========== ROI Management ==========

    /**
     * @brief Set the Region of Interest
     */
    void setROI(const VisionROI& roi);

    /**
     * @brief Get current ROI
     */
    VisionROI getROI() const;

    /**
     * @brief Enable/disable ROI
     */
    void setROIEnabled(bool enabled);

    /**
     * @brief Check if ROI is enabled
     */
    bool isROIEnabled() const;

    /**
     * @brief Clear/reset ROI
     */
    void clearROI();

    // ========== Recipe Management ==========

    struct RecipeStep {
        QString pluginName;
        nlohmann::json parameters;
        bool enabled = true;
    };

    /**
     * @brief Create new empty recipe
     */
    void newRecipe();

    /**
     * @brief Load recipe from JSON file
     */
    bool loadRecipe(const QString& filePath);

    /**
     * @brief Save current recipe to file
     */
    bool saveRecipe(const QString& filePath);

    /**
     * @brief Save recipe to new file
     */
    bool saveRecipeAs(const QString& filePath);

    /**
     * @brief Add step to recipe
     */
    void addRecipeStep(const QString& pluginName, const nlohmann::json& params);

    /**
     * @brief Remove step from recipe
     */
    void removeRecipeStep(int index);

    /**
     * @brief Update step in recipe
     */
    void updateRecipeStep(int index, const nlohmann::json& params);

    /**
     * @brief Clear all recipe steps
     */
    void clearRecipe();

    /**
     * @brief Get number of steps in recipe
     */
    int getRecipeStepCount() const;

    /**
     * @brief Get recipe step at index
     */
    RecipeStep getRecipeStep(int index) const;

    /**
     * @brief Execute entire recipe
     * @return Vector of results for each step
     */
    std::vector<VisionResult> runRecipe();

    /**
     * @brief Get current recipe file path
     */
    QString getCurrentRecipePath() const;

    /**
     * @brief Check if recipe has unsaved changes
     */
    bool hasUnsavedChanges() const;

signals:
    void pluginRegistered(const QString& name);
    void pluginUnregistered(const QString& name);
    void imageUpdated();
    void resultReady(const VisionResult& result);
    void recipeChanged();
    void recipeStepAdded(int index);
    void recipeStepRemoved(int index);
    void calibrationChanged(double xMmPerPx, double yMmPerPx);
    void executionStarted(const QString& pluginName);
    void executionFinished(const QString& pluginName, bool success);
    void roiChanged(const VisionROI& roi);

private:
    // Private constructor for singleton
    VisionManager();
    ~VisionManager() = default;

    // Plugin registry
    std::map<QString, std::shared_ptr<IVisionPlugin>> m_plugins;

    // Image data
    cv::Mat m_inputImage;
    cv::Mat m_outputImage;
    VisionResult m_lastResult;

    // Calibration
    double m_pixelToMmX = 0.00248;  // Default from camera settings
    double m_pixelToMmY = 0.00248;

    // ROI
    VisionROI m_roi;

    // Recipe
    std::vector<RecipeStep> m_recipe;
    QString m_currentRecipePath;
    bool m_unsavedChanges = false;

    // Helper to update result coordinates with calibration
    void applyCalibrationToResult(VisionResult& result);

    // Helper to apply ROI offset to result coordinates
    void applyROIOffsetToResult(VisionResult& result, const cv::Point2d& offset);
};

#endif // HAS_OPENCV
