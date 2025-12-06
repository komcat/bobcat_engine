// HalconCircleDetectionPlugin.h
// Circle detection using Halcon library
#pragma once

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

#include "../IVisionPlugin.h"
#include "../VisionResult.h"
#include <QWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

/**
 * @brief Circle detection plugin using Halcon's circle fitting algorithms
 *
 * Uses Halcon's robust circle detection which provides:
 * - Sub-pixel accuracy
 * - Better handling of partial circles
 * - More robust to noise than OpenCV Hough
 */
class HalconCircleDetectionPlugin : public VisionPluginBase {
public:
    HalconCircleDetectionPlugin();
    ~HalconCircleDetectionPlugin();

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
    int m_minRadius = 10;        // Minimum circle radius in pixels
    int m_maxRadius = 200;       // Maximum circle radius in pixels
    int m_threshold = 80;        // Threshold for circle detection
    int m_maxCircles = 10;       // Maximum number of circles to detect
    double m_circularity = 0.8;  // Minimum circularity (0-1)
    int m_polarity = 0;          // 0 = Dark circles, 1 = Bright circles

    // Configuration widget (not owned)
    QWidget* m_configWidget = nullptr;
    QSpinBox* m_minRadiusSpin = nullptr;
    QSpinBox* m_maxRadiusSpin = nullptr;
    QSpinBox* m_thresholdSpin = nullptr;
    QSpinBox* m_maxCirclesSpin = nullptr;
    QDoubleSpinBox* m_circularitySpin = nullptr;
    QComboBox* m_polarityCombo = nullptr;
};

#endif // HAS_OPENCV && HAS_HALCON
