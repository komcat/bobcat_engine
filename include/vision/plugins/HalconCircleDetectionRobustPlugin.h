// HalconCircleDetectionRobustPlugin.h
// Robust circle detection using Halcon - Blob analysis with largest contour fitting
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
 * @brief Robust circle detection plugin using Halcon's blob analysis
 *
 * Algorithm:
 * 1. Threshold to extract dark/bright regions
 * 2. Morphological closing to fill gaps
 * 3. Connection to separate blobs
 * 4. Select by area and circularity
 * 5. Select largest region
 * 6. Generate contour and fit circle using robust Tukey estimator
 *
 * Based on HDevelop script using 'atukey' and 'geotukey' fitting methods
 */
class HalconCircleDetectionRobustPlugin : public VisionPluginBase {
public:
    HalconCircleDetectionRobustPlugin();
    ~HalconCircleDetectionRobustPlugin();

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
    int m_thresholdMin = 0;       // Minimum threshold value
    int m_thresholdMax = 100;     // Maximum threshold value
    double m_closingRadius = 10.5; // Morphological closing radius
    int m_minArea = 5000;         // Minimum blob area
    int m_maxArea = 999999;       // Maximum blob area
    double m_minCircularity = 0.7; // Minimum circularity (0-1)
    int m_polarity = 0;           // 0 = Dark blobs, 1 = Bright blobs
    int m_fitMethod = 0;          // 0 = atukey (algebraic), 1 = geotukey (geometric)

    // Configuration widget (not owned)
    QWidget* m_configWidget = nullptr;
    QSpinBox* m_thresholdMinSpin = nullptr;
    QSpinBox* m_thresholdMaxSpin = nullptr;
    QDoubleSpinBox* m_closingRadiusSpin = nullptr;
    QSpinBox* m_minAreaSpin = nullptr;
    QSpinBox* m_maxAreaSpin = nullptr;
    QDoubleSpinBox* m_minCircularitySpin = nullptr;
    QComboBox* m_polarityCombo = nullptr;
    QComboBox* m_fitMethodCombo = nullptr;
};

#endif // HAS_OPENCV && HAS_HALCON
