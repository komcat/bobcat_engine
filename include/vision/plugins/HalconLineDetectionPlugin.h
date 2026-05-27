// HalconLineDetectionPlugin.h
// Line detection using Halcon library - Edge-based straight line detection
#pragma once

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

// Prevent Windows min/max macros from conflicting with std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../IVisionPlugin.h"
#include "../VisionResult.h"
#include <QWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

/**
 * @brief Line detection plugin using Halcon's edge detection and line fitting
 *
 * Algorithm:
 * 1. Apply edge detection (Canny) to find sub-pixel edges
 * 2. Segment contours into line segments
 * 3. Select contours by length
 * 4. Fit straight lines using robust fitting
 *
 * Supports both white-to-black and black-to-white transitions
 */
class HalconLineDetectionPlugin : public VisionPluginBase {
public:
    HalconLineDetectionPlugin();
    ~HalconLineDetectionPlugin();

    // IVisionPlugin interface
    bool execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) override;
    bool validate() const override;
    QWidget* createConfigWidget(QWidget* parent = nullptr) override;
    nlohmann::json getParameters() const override;
    void setParameters(const nlohmann::json& params) override;
    nlohmann::json getDefaultParameters() const override;
    ParameterSchema getParameterSchema() const override;

private:
    // Algorithm parameters
    double m_sigma = 1.0;           // Gaussian smoothing sigma for edge detection
    double m_lowThreshold = 20.0;   // Canny low threshold
    double m_highThreshold = 40.0;  // Canny high threshold
    int m_minLength = 50;           // Minimum line length in pixels
    int m_maxLines = 10;            // Maximum number of lines to detect
    int m_polarity = 0;             // 0 = All edges, 1 = Dark to Bright, 2 = Bright to Dark
    int m_fitMethod = 0;            // 0 = regression, 1 = tukey (robust)
    bool m_showEdgeOverlay = false; // Render Canny edges as green overlay for tuning

    // Configuration widget (not owned)
    QWidget* m_configWidget = nullptr;
    QDoubleSpinBox* m_sigmaSpin = nullptr;
    QDoubleSpinBox* m_lowThresholdSpin = nullptr;
    QDoubleSpinBox* m_highThresholdSpin = nullptr;
    QSpinBox* m_minLengthSpin = nullptr;
    QSpinBox* m_maxLinesSpin = nullptr;
    QComboBox* m_polarityCombo = nullptr;
    QComboBox* m_fitMethodCombo = nullptr;
    QCheckBox* m_showEdgeOverlayCheck = nullptr;
};

#endif // HAS_OPENCV && HAS_HALCON
