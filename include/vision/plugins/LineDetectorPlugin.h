// LineDetectorPlugin.h
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
 * @brief Detects straight lines using edge detection and Hough Transform
 *
 * Uses Canny edge detection followed by Hough Line Transform to find
 * the best straight line in the image. Returns the single best scoring line.
 */
class LineDetectorPlugin : public VisionPluginBase {
public:
    LineDetectorPlugin();

    bool execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) override;
    bool validate() const override;

    QWidget* createConfigWidget(QWidget* parent = nullptr) override;
    nlohmann::json getParameters() const override;
    void setParameters(const nlohmann::json& params) override;
    nlohmann::json getDefaultParameters() const override;
    ParameterSchema getParameterSchema() const override;

private:
    // Hough Transform parameters
    double m_rhoResolution = 1.0;        // Distance resolution in pixels
    double m_thetaResolution = 1.0;      // Angle resolution in degrees
    int m_threshold = 100;               // Minimum votes for line detection
    double m_minLineLength = 50.0;       // Minimum line length (for probabilistic)
    double m_maxLineGap = 10.0;          // Maximum gap between line segments
    bool m_useProbabilistic = false;     // Use probabilistic vs standard Hough

    // Edge detection parameters
    double m_cannyThreshold1 = 50.0;
    double m_cannyThreshold2 = 150.0;
    int m_blurKernelSize = 5;
    bool m_applyBlur = true;

    // Display
    bool m_extendLine = true;            // Extend line to image boundaries

    // Line orientation filter
    int m_expectedOrientation = -1;      // -1=Any, 0=Horizontal(0°), 1=Vertical(90°), 2=Horizontal(180°), 3=Vertical(270°)
    double m_orientationTolerance = 15.0; // Angle tolerance in degrees

    // UI widgets
    QWidget* m_configWidget = nullptr;
    QDoubleSpinBox* m_rhoSpin = nullptr;
    QDoubleSpinBox* m_thetaSpin = nullptr;
    QSpinBox* m_thresholdSpin = nullptr;
    QDoubleSpinBox* m_minLineLengthSpin = nullptr;
    QDoubleSpinBox* m_maxLineGapSpin = nullptr;
    QCheckBox* m_useProbabilisticCheck = nullptr;
    QDoubleSpinBox* m_threshold1Spin = nullptr;
    QDoubleSpinBox* m_threshold2Spin = nullptr;
    QSpinBox* m_blurKernelSizeSpin = nullptr;
    QCheckBox* m_applyBlurCheck = nullptr;
    QCheckBox* m_extendLineCheck = nullptr;
    QComboBox* m_orientationCombo = nullptr;
    QDoubleSpinBox* m_orientationToleranceSpin = nullptr;
};

#endif // HAS_OPENCV
