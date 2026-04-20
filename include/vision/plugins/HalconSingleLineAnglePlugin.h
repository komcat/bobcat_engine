// HalconSingleLineAnglePlugin.h
// Single-line detection reporting angle relative to the camera X-axis.
#pragma once

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

#ifndef NOMINMAX
#define NOMINMAX
#endif

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
 * @brief Detects at most one straight line and reports its angle vs the camera X-axis.
 *
 * Forces exactly 1 line (the longest contour that passes length/polarity filters).
 * The reported angle_from_camera_axis_deg is normalized to [-90, 90]:
 *   0 degrees  = line is parallel to camera horizontal (X) axis
 *  +90/-90 deg = line is vertical
 * Sign convention follows image coordinates (Y-axis down), so a positive
 * angle means the line tilts downward to the right.
 */
class HalconSingleLineAnglePlugin : public VisionPluginBase {
public:
    HalconSingleLineAnglePlugin();
    ~HalconSingleLineAnglePlugin();

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
    double m_sigma = 1.0;
    double m_lowThreshold = 20.0;
    double m_highThreshold = 40.0;
    int m_minLength = 50;
    int m_polarity = 0;    // 0 = All, 1 = Dark->Bright, 2 = Bright->Dark
    int m_fitMethod = 0;   // 0 = regression, 1 = tukey

    // Configuration widget (not owned)
    QWidget* m_configWidget = nullptr;
    QDoubleSpinBox* m_sigmaSpin = nullptr;
    QDoubleSpinBox* m_lowThresholdSpin = nullptr;
    QDoubleSpinBox* m_highThresholdSpin = nullptr;
    QSpinBox* m_minLengthSpin = nullptr;
    QComboBox* m_polarityCombo = nullptr;
    QComboBox* m_fitMethodCombo = nullptr;
};

#endif // HAS_OPENCV && HAS_HALCON
