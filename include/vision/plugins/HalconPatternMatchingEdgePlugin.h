// HalconPatternMatchingEdgePlugin.h
// Edge-based pattern matching using Halcon's Shape-Based Matching
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
#include <halconcpp/HalconCpp.h>

/**
 * @brief Edge-based pattern matching plugin using Halcon's Shape-Based Matching
 *
 * Uses Halcon's create_shape_model and find_shape_model for robust edge-based
 * pattern matching with:
 * - Sub-pixel accuracy
 * - Rotation invariance (optional)
 * - Scale invariance (optional)
 * - Better handling of partial occlusion
 */
class HalconPatternMatchingEdgePlugin : public TemplateMatchingBase {
public:
    HalconPatternMatchingEdgePlugin();
    ~HalconPatternMatchingEdgePlugin();

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
    std::string getTemplateFileSuffix() const override { return "_halcon_edge_template.png"; }

private:
    // Halcon shape model
    bool m_hasModel = false;               // Whether a shape model exists
    HalconCpp::HShapeModel m_shapeModel;   // Halcon shape model

    // Algorithm parameters
    double m_minScore = 0.5;               // Minimum match score (0-1)
    int m_maxMatches = 1;                  // Maximum number of matches to find
    double m_greediness = 0.9;             // Search greediness (0=safe, 1=fast)
    int m_numLevels = 0;                   // Number of pyramid levels (0=auto)
    double m_angleTolerance = 0.0;         // Angle tolerance in degrees (0=no rotation, searches ±tolerance)
    double m_scaleMin = 1.0;               // Minimum scale factor
    double m_scaleMax = 1.0;               // Maximum scale factor
    int m_contrast = 30;                   // Edge contrast threshold for model creation
    int m_minContrast = 10;                // Minimum contrast for matching

    // Configuration widget (not owned) - plugin-specific widgets only
    QWidget* m_configWidget = nullptr;
    QDoubleSpinBox* m_minScoreSpin = nullptr;
    QSpinBox* m_maxMatchesSpin = nullptr;
    QDoubleSpinBox* m_greedinessSpin = nullptr;
    QSpinBox* m_numLevelsSpin = nullptr;
    QDoubleSpinBox* m_angleToleranceSpin = nullptr;
    QSlider* m_angleToleranceSlider = nullptr;
    QLabel* m_angleToleranceLabel = nullptr;
    QSpinBox* m_contrastSpin = nullptr;
    QSpinBox* m_minContrastSpin = nullptr;
};

#endif // HAS_OPENCV && HAS_HALCON
