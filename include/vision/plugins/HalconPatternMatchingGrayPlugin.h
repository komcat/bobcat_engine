// HalconPatternMatchingGrayPlugin.h
// Gray-value template matching using Halcon's HTemplate (best_match / best_match_rot)
#pragma once

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

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
 * @brief Gray-value template matching plugin using Halcon's HTemplate.
 *
 * This is the classic/legacy approach: uses create_template_rot to build a
 * multi-resolution gray-value pyramid, then best_match_rot to find the
 * minimum mean-gray-value-difference location.
 *
 * Unlike NCC/shape/descriptor matching, this reports an *error* value
 * (mean gray-value difference, 0-255 range) — lower is better. The user
 * configures a MaxError threshold, not a score.
 *
 * Best for:
 *  - Very fast matching when targets are close to template appearance
 *  - Simple, stable lighting conditions
 *  - Low-resolution / small templates
 */
class HalconPatternMatchingGrayPlugin : public TemplateMatchingBase {
public:
    HalconPatternMatchingGrayPlugin();
    ~HalconPatternMatchingGrayPlugin();

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
    std::string getTemplateFileSuffix() const override { return "_halcon_gray_template.png"; }

private:
    bool m_hasModel = false;
    HalconCpp::HTemplate m_template_handle;
    int m_templateWidth = 0;
    int m_templateHeight = 0;
    int m_actualLevels = 1;                // Actual pyramid depth built during teach; used by BestMatchMg

    // Algorithm parameters
    double m_maxError = 30.0;              // Max mean gray-value diff (0-255); lower = stricter
    int m_numLevels = 0;                   // Pyramid levels (0 = auto, Halcon requires >=1 typically)
    double m_angleTolerance = 0.0;         // Rotation search +/- degrees
    std::string m_optimize = "sort";       // none | sort | point_reduction_low | point_reduction_high — "sort" is ~2-5x faster
    std::string m_grayValues = "normalized"; // not_normalized | normalized
    std::string m_subPixel = "true";       // true | false

    // Widgets (not owned)
    QWidget* m_configWidget = nullptr;
    QDoubleSpinBox* m_maxErrorSpin = nullptr;
    QSpinBox* m_numLevelsSpin = nullptr;
    QDoubleSpinBox* m_angleToleranceSpin = nullptr;
    QSlider* m_angleToleranceSlider = nullptr;
    QLabel* m_angleToleranceLabel = nullptr;
    QComboBox* m_optimizeCombo = nullptr;
    QComboBox* m_grayValuesCombo = nullptr;
};

#endif // HAS_OPENCV && HAS_HALCON
