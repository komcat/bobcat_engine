// EdgeDetectionPlugin.h
#pragma once

#ifdef HAS_OPENCV

#include "../IVisionPlugin.h"
#include "../VisionResult.h"
#include <QWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

/**
 * @brief Edge detection plugin using Canny algorithm
 *
 * Detects edges in images using OpenCV's Canny edge detector
 */
class EdgeDetectionPlugin : public VisionPluginBase {
public:
    EdgeDetectionPlugin();

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
    double m_threshold1 = 50.0;      // Lower threshold for hysteresis
    double m_threshold2 = 150.0;     // Upper threshold for hysteresis
    int m_apertureSize = 3;          // Sobel operator aperture size (3, 5, or 7)
    bool m_useL2Gradient = false;    // Use more accurate L2 gradient magnitude
    bool m_applyBlur = true;         // Apply Gaussian blur before edge detection
    int m_blurKernelSize = 5;        // Gaussian blur kernel size (must be odd)

    // Configuration widget (not owned)
    QWidget* m_configWidget = nullptr;
    QDoubleSpinBox* m_threshold1Spin = nullptr;
    QDoubleSpinBox* m_threshold2Spin = nullptr;
    QComboBox* m_apertureSizeCombo = nullptr;
    QCheckBox* m_useL2GradientCheck = nullptr;
    QCheckBox* m_applyBlurCheck = nullptr;
    QSpinBox* m_blurKernelSizeSpin = nullptr;
};

#endif // HAS_OPENCV
