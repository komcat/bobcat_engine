// HalconD4SigmaBeamDetectionPlugin.h
// D4sigma (ISO 11146 second-moment) beam detection using Halcon
#pragma once

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

#include "../IVisionPlugin.h"
#include "../VisionResult.h"
#include <QWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QLabel>

/**
 * @brief D4sigma beam detection plugin using Halcon image processing
 *
 * Implements ISO 11146 second-moment (D4sigma) beam width measurement:
 * - Background estimation from corner regions
 * - Threshold-based beam candidate finding
 * - Per-beam D4sigma calculation (4 * sqrt(variance)) for X and Y axes
 * - Intensity-weighted centroid (first moment)
 * - Saturation detection
 *
 * Direct C++ port of the Python detect_beams_d4sigma_params algorithm.
 */
class HalconD4SigmaBeamDetectionPlugin : public VisionPluginBase {
public:
    HalconD4SigmaBeamDetectionPlugin();
    ~HalconD4SigmaBeamDetectionPlugin();

    // IVisionPlugin interface
    bool execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) override;
    bool validate() const override;
    QWidget* createConfigWidget(QWidget* parent = nullptr) override;
    nlohmann::json getParameters() const override;
    void setParameters(const nlohmann::json& params) override;
    nlohmann::json getDefaultParameters() const override;
    ParameterSchema getParameterSchema() const override;

private:
    // Detection parameters
    int m_minSignalAboveBg = 30;         // Signal above background for beam finding
    int m_minBeamArea = 20;              // Minimum beam area in pixels^2
    int m_maxBeamArea = 50000;           // Maximum beam area in pixels^2
    int m_maxBeams = 10;                 // Maximum number of beams to detect
    double m_d4sigmaThresh = 0.1;        // Fraction of peak for D4sigma integration region
    double m_roiExpandFactor = 1.5;      // Bounding box expansion factor for wing capture
    int m_bgBoxSize = 50;               // Corner box size for background estimation
    int m_saturationThreshold = 250;     // Gray value threshold for saturation warning

    // Display options
    bool m_showD4SigmaCross = true;      // Draw D4sigma extent cross-lines
    bool m_showCentroid = true;          // Draw centroid marker
    bool m_showLabels = true;            // Show text labels
    bool m_showBoundingBox = false;      // Show expanded ROI bounding box

    // Configuration widget (not owned - parent manages lifetime)
    QWidget* m_configWidget = nullptr;
    QSpinBox* m_minSignalAboveBgSpin = nullptr;
    QSpinBox* m_minBeamAreaSpin = nullptr;
    QSpinBox* m_maxBeamAreaSpin = nullptr;
    QSpinBox* m_maxBeamsSpin = nullptr;
    QDoubleSpinBox* m_d4sigmaThreshSpin = nullptr;
    QDoubleSpinBox* m_roiExpandFactorSpin = nullptr;
    QSpinBox* m_bgBoxSizeSpin = nullptr;
    QSpinBox* m_saturationThresholdSpin = nullptr;
    QCheckBox* m_showD4SigmaCrossCheck = nullptr;
    QCheckBox* m_showCentroidCheck = nullptr;
    QCheckBox* m_showLabelsCheck = nullptr;
    QCheckBox* m_showBoundingBoxCheck = nullptr;
};

#endif // HAS_OPENCV && HAS_HALCON
