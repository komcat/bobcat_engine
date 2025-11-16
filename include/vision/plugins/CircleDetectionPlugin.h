// CircleDetectionPlugin.h
#pragma once

#ifdef HAS_OPENCV

#include "../IVisionPlugin.h"
#include "../VisionResult.h"
#include <QWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

/**
 * @brief Circle detection plugin using Hough Circle Transform
 *
 * Detects circles in grayscale images using OpenCV's HoughCircles
 */
class CircleDetectionPlugin : public VisionPluginBase {
public:
    CircleDetectionPlugin();

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
    double m_dp = 1.0;           // Inverse ratio of accumulator resolution
    double m_minDist = 50.0;     // Minimum distance between circle centers
    double m_param1 = 100.0;     // Upper threshold for Canny edge detector
    double m_param2 = 30.0;      // Threshold for center detection
    int m_minRadius = 10;        // Minimum circle radius
    int m_maxRadius = 200;       // Maximum circle radius
    int m_maxCircles = 10;       // Maximum number of circles to detect

    // Configuration widget (not owned)
    QWidget* m_configWidget = nullptr;
    QDoubleSpinBox* m_dpSpin = nullptr;
    QDoubleSpinBox* m_minDistSpin = nullptr;
    QDoubleSpinBox* m_param1Spin = nullptr;
    QDoubleSpinBox* m_param2Spin = nullptr;
    QSpinBox* m_minRadiusSpin = nullptr;
    QSpinBox* m_maxRadiusSpin = nullptr;
    QSpinBox* m_maxCirclesSpin = nullptr;
};

#endif // HAS_OPENCV
