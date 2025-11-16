// IVisionPlugin.h
#pragma once

#ifdef HAS_OPENCV

#include <string>
#include <memory>
#include <QWidget>
#include <opencv2/core.hpp>
#include "nlohmann/json.hpp"
#include "PluginParameter.h"

// Forward declaration
struct VisionResult;

/**
 * @brief Interface for all vision processing plugins
 *
 * Each plugin implements a specific image processing algorithm
 * (pattern recognition, measurement, filtering, etc.)
 */
class IVisionPlugin {
public:
    virtual ~IVisionPlugin() = default;

    // ========== Identity ==========

    /**
     * @brief Get plugin name (unique identifier)
     */
    virtual std::string getName() const = 0;

    /**
     * @brief Get plugin category
     * @return "PR", "Measurement", "Filter", "Calibration"
     */
    virtual std::string getCategory() const = 0;

    /**
     * @brief Get human-readable description
     */
    virtual std::string getDescription() const = 0;

    // ========== Execution ==========

    /**
     * @brief Execute the plugin algorithm
     * @param input Input image (CV_8UC1 or CV_8UC3)
     * @param output Output image (can be same as input or modified)
     * @param result Structured results to be filled
     * @return true if execution succeeded
     */
    virtual bool execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) = 0;

    /**
     * @brief Validate current parameters
     * @return true if parameters are valid for execution
     */
    virtual bool validate() const = 0;

    /**
     * @brief Get last error message
     */
    virtual std::string getLastError() const = 0;

    // ========== Configuration ==========

    /**
     * @brief Create configuration widget for this plugin
     * @param parent Parent widget
     * @return Widget for editing plugin parameters (caller owns)
     */
    virtual QWidget* createConfigWidget(QWidget* parent = nullptr) = 0;

    /**
     * @brief Get current parameters as JSON
     */
    virtual nlohmann::json getParameters() const = 0;

    /**
     * @brief Set parameters from JSON
     * @param params JSON object with parameter values
     */
    virtual void setParameters(const nlohmann::json& params) = 0;

    /**
     * @brief Get default parameters for this plugin
     */
    virtual nlohmann::json getDefaultParameters() const = 0;

    /**
     * @brief Get parameter schema for automatic UI generation
     * @return ParameterSchema describing all parameters with metadata
     */
    virtual ParameterSchema getParameterSchema() const = 0;

    /**
     * @brief Reset parameters to defaults
     */
    virtual void resetToDefaults() {
        setParameters(getDefaultParameters());
    }
};

/**
 * @brief Base class for plugins providing common functionality
 */
class VisionPluginBase : public IVisionPlugin {
public:
    VisionPluginBase(const std::string& name,
                     const std::string& category,
                     const std::string& description)
        : m_name(name)
        , m_category(category)
        , m_description(description)
    {}

    std::string getName() const override { return m_name; }
    std::string getCategory() const override { return m_category; }
    std::string getDescription() const override { return m_description; }
    std::string getLastError() const override { return m_lastError; }

protected:
    std::string m_name;
    std::string m_category;
    std::string m_description;
    mutable std::string m_lastError;
};

#endif // HAS_OPENCV
