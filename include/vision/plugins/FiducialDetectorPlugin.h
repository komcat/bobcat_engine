// FiducialDetectorPlugin.h
#pragma once

#ifdef HAS_OPENCV

#include "../IVisionPlugin.h"
#include "../VisionResult.h"
#include <QWidget>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

/**
 * @brief Detects circular fiducial markers for alignment
 *
 * Uses edge detection + contour analysis + circle fitting to find
 * circular fiducials. Common in PCB alignment, pick-and-place operations.
 */
class FiducialDetectorPlugin : public VisionPluginBase {
public:
    FiducialDetectorPlugin();

    bool execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) override;
    bool validate() const override;

    QWidget* createConfigWidget(QWidget* parent = nullptr) override;
    nlohmann::json getParameters() const override;
    void setParameters(const nlohmann::json& params) override;
    nlohmann::json getDefaultParameters() const override;
    ParameterSchema getParameterSchema() const override;

private:
    // Detection parameters
    double m_minDiameter = 20.0;     // Minimum fiducial diameter in pixels
    double m_maxDiameter = 200.0;    // Maximum fiducial diameter in pixels
    double m_circularityThreshold = 0.7;  // How circular the shape must be (0-1)
    int m_expectedCount = 1;         // Expected number of fiducials
    bool m_sortBySize = true;        // Sort results by size (largest first)

    // Preprocessing
    double m_cannyThreshold1 = 50.0;
    double m_cannyThreshold2 = 150.0;
    int m_blurKernelSize = 5;
    bool m_applyBlur = true;

    // UI widgets
    QWidget* m_configWidget = nullptr;
    QDoubleSpinBox* m_minDiameterSpin = nullptr;
    QDoubleSpinBox* m_maxDiameterSpin = nullptr;
    QDoubleSpinBox* m_circularitySpin = nullptr;
    QSpinBox* m_expectedCountSpin = nullptr;
    QCheckBox* m_sortBySizeCheck = nullptr;
    QDoubleSpinBox* m_threshold1Spin = nullptr;
    QDoubleSpinBox* m_threshold2Spin = nullptr;
    QSpinBox* m_blurKernelSizeSpin = nullptr;
    QCheckBox* m_applyBlurCheck = nullptr;
};

#endif // HAS_OPENCV
