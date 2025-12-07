// PatternMatchingEdgePlugin.h
#pragma once

#ifdef HAS_OPENCV

#include "TemplateMatchingBase.h"
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>

/**
 * @brief Edge-based pattern matching plugin
 *
 * Converts both template and search image to edge maps using Canny edge detection,
 * then performs template matching on the edge images for lighting-invariant matching.
 */
class PatternMatchingEdgePlugin : public TemplateMatchingBase {
public:
    PatternMatchingEdgePlugin();

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
    bool hasTemplate() const override { return !m_template.empty() && !m_templateEdge.empty(); }
    void clearTemplate() override;

protected:
    // Override to customize template file suffix
    std::string getTemplateFileSuffix() const override { return "_edge_template.png"; }

private:
    // Template edge data
    cv::Mat m_templateEdge;                // Edge-detected template for matching
    cv::Point2d m_templateOrigin;          // Reference point in template (center by default)

    // Algorithm parameters
    double m_matchThreshold = 0.7;         // Minimum match score (0-1)
    bool m_subpixelRefinement = true;      // Refine to subpixel accuracy
    int m_maxMatches = 1;                  // Maximum number of matches to find (1 = single best match)

    // Edge detection parameters
    double m_cannyThreshold1 = 50.0;       // Canny low threshold
    double m_cannyThreshold2 = 150.0;      // Canny high threshold
    int m_gaussianBlurSize = 5;            // Gaussian blur kernel size before Canny

    // Configuration widget (not owned) - plugin-specific widgets only
    QWidget* m_configWidget = nullptr;
    QDoubleSpinBox* m_thresholdSpin = nullptr;
    QCheckBox* m_subpixelCheck = nullptr;
    QSpinBox* m_maxMatchesSpin = nullptr;
    QDoubleSpinBox* m_cannyThreshold1Spin = nullptr;
    QDoubleSpinBox* m_cannyThreshold2Spin = nullptr;
    QSpinBox* m_gaussianBlurSizeSpin = nullptr;
};

#endif // HAS_OPENCV
