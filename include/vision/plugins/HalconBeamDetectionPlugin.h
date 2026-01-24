// HalconBeamDetectionPlugin.h
// Beam/spot detection using Halcon Aurora beam detection algorithms
#pragma once

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

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
#include <QLabel>

/**
 * @brief Beam/spot detection plugin using Halcon's Aurora beam detection library
 *
 * Detects laser beams, light spots, or similar bright/dark regions and provides:
 * - Real-world (mm) position of beam centroid (intensity-weighted)
 * - Beam area in pixels and mm²
 * - Mean, min, max intensity with saturation detection
 * - FWHM (Full Width at Half Maximum) major and minor axes
 * - Beam orientation angle
 * - Ellipticity (circularity measure)
 *
 * Based on Halcon's beam_detection_lib from Aurora beam examples.
 */
class HalconBeamDetectionPlugin : public VisionPluginBase {
public:
    HalconBeamDetectionPlugin();
    ~HalconBeamDetectionPlugin();

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
    int m_minGrayValue = 80;         // Minimum gray value threshold for beam detection
    int m_minBeamArea = 20;          // Minimum beam area in pixels²
    int m_maxBeamArea = 50000;       // Maximum beam area in pixels²
    int m_maxBeams = 10;             // Maximum number of beams to detect
    int m_saturationThreshold = 250; // Gray value threshold for saturation warning

    // Display options
    bool m_showFWHMEllipse = true;   // Draw FWHM ellipse overlay
    bool m_showCentroid = true;      // Draw centroid marker
    bool m_showLabels = true;        // Show text labels

    // Configuration widget (not owned - parent manages lifetime)
    QWidget* m_configWidget = nullptr;
    QSpinBox* m_minGrayValueSpin = nullptr;
    QSpinBox* m_minBeamAreaSpin = nullptr;
    QSpinBox* m_maxBeamAreaSpin = nullptr;
    QSpinBox* m_maxBeamsSpin = nullptr;
    QSpinBox* m_saturationThresholdSpin = nullptr;
    QCheckBox* m_showFWHMEllipseCheck = nullptr;
    QCheckBox* m_showCentroidCheck = nullptr;
    QCheckBox* m_showLabelsCheck = nullptr;
};

#endif // HAS_OPENCV && HAS_HALCON
