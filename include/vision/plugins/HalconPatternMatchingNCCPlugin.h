// HalconPatternMatchingNCCPlugin.h
// Intensity-based pattern matching using Halcon's NCC (Normalized Cross-Correlation) Matching
#pragma once

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

// Prevent Windows min/max macros from conflicting with std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "TemplateMatchingBase.h"
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <halconcpp/HalconCpp.h>

/**
 * @brief Intensity-based pattern matching plugin using Halcon's NCC Matching
 *
 * Uses Halcon's create_ncc_model and find_ncc_model for grayscale intensity-based
 * pattern matching. Unlike shape-based (edge) matching, NCC works directly on
 * pixel intensities and is ideal for:
 * - Highly textured surfaces (dot arrays, speckle patterns)
 * - Low-contrast targets without clean edges
 * - Scenes where edge extraction would be noisy
 *
 * Returns x, y, and theta (rotation angle) per match, same as the edge plugin.
 */
class HalconPatternMatchingNCCPlugin : public TemplateMatchingBase {
public:
    HalconPatternMatchingNCCPlugin();
    ~HalconPatternMatchingNCCPlugin();

    // IVisionPlugin interface
    bool execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) override;
    bool validate() const override;
    QWidget* createConfigWidget(QWidget* parent = nullptr) override;
    nlohmann::json getParameters() const override;
    void setParameters(const nlohmann::json& params) override;
    nlohmann::json getDefaultParameters() const override;
    ParameterSchema getParameterSchema() const override;

    // TemplateMatchingBase interface
    bool teachFromImage(const cv::Mat& templateImage) override;
    bool hasTemplate() const override { return m_hasModel; }
    void clearTemplate() override;

protected:
    // Override to customize template file suffix
    std::string getTemplateFileSuffix() const override { return "_halcon_ncc_template.png"; }

private:
    // Halcon NCC model
    bool m_hasModel = false;               // Whether an NCC model exists
    HalconCpp::HNCCModel m_nccModel;       // Halcon NCC model
    int m_templateWidth = 0;               // Stored for pose-box visualization
    int m_templateHeight = 0;

    // Algorithm parameters
    double m_minScore = 0.7;               // Minimum match score (0-1); NCC is stricter than edge
    int m_maxMatches = 1;                  // Maximum number of matches to find
    int m_numLevels = 0;                   // Number of pyramid levels (0=auto)
    double m_angleTolerance = 0.0;         // Angle tolerance in degrees (0=no rotation, searches +/- tolerance)
    std::string m_metric = "ignore_global_polarity"; // "use_polarity" or "ignore_global_polarity"

    // Configuration widget (not owned) - plugin-specific widgets only
    QWidget* m_configWidget = nullptr;
    QDoubleSpinBox* m_minScoreSpin = nullptr;
    QSpinBox* m_maxMatchesSpin = nullptr;
    QSpinBox* m_numLevelsSpin = nullptr;
    QDoubleSpinBox* m_angleToleranceSpin = nullptr;
    QSlider* m_angleToleranceSlider = nullptr;
    QLabel* m_angleToleranceLabel = nullptr;
    QComboBox* m_metricCombo = nullptr;
};

#endif // HAS_OPENCV && HAS_HALCON
