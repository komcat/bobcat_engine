// CalibrationPlatePlugin.h
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
#include <QPushButton>
#include <QLabel>

/**
 * @brief Calibration plate plugin for calculating mm/pixel ratio
 *
 * Detects circles on MVTec/Halcon calibration plates to calculate
 * pixel-to-mm conversion factors. Filters out finder marks (circles
 * with center dots) and uses regular circles for measurement.
 *
 * Supported plate types:
 * - 20mm plate: Mark radius 0.161mm (diameter 0.322mm)
 * - 40mm plate: Mark radius 0.323mm (diameter 0.646mm)
 * - 80mm plate: Mark radius 0.645mm (diameter 1.290mm)
 */
class CalibrationPlatePlugin : public VisionPluginBase {
public:
    CalibrationPlatePlugin();

    // IVisionPlugin interface
    bool execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) override;
    bool validate() const override;
    QWidget* createConfigWidget(QWidget* parent = nullptr) override;
    nlohmann::json getParameters() const override;
    void setParameters(const nlohmann::json& params) override;
    nlohmann::json getDefaultParameters() const override;
    ParameterSchema getParameterSchema() const override;

private:
    // Plate type presets (diameter in mm)
    enum class PlateType {
        Plate20mm = 0,  // 0.322mm diameter
        Plate40mm = 1,  // 0.646mm diameter
        Plate80mm = 2,  // 1.290mm diameter
        Custom = 3      // User-defined
    };

    // Detection parameters
    int m_blurSize = 5;               // Gaussian blur kernel size
    bool m_useCannyEdge = true;       // Use Canny edge detection (better for low contrast)
    int m_cannyLow = 30;              // Canny low threshold
    int m_cannyHigh = 100;            // Canny high threshold
    bool m_enhanceContrast = true;    // Apply CLAHE contrast enhancement
    int m_thresholdBlockSize = 51;    // Adaptive threshold block size
    double m_thresholdC = 5.0;        // Adaptive threshold constant
    double m_circularityMin = 0.80;   // Minimum circularity (0-1)
    double m_circularityMax = 1.0;    // Maximum circularity
    int m_minArea = 500;              // Minimum contour area (pixels^2)
    int m_maxArea = 100000;           // Maximum contour area (pixels^2)
    bool m_filterFinderMarks = true;  // Filter out circles with center dots
    double m_finderHoleRatio = 0.3;   // Max inner/outer ratio for finder marks

    // Calibration parameters
    PlateType m_plateType = PlateType::Plate20mm;
    double m_knownDiameterMm = 0.322;  // Known circle diameter in mm
    bool m_applyCalibration = false;   // Apply result to VisionManager

    // Configuration widget (not owned)
    QWidget* m_configWidget = nullptr;
    QSpinBox* m_blurSizeSpin = nullptr;
    QCheckBox* m_useCannyCheck = nullptr;
    QSpinBox* m_cannyLowSpin = nullptr;
    QSpinBox* m_cannyHighSpin = nullptr;
    QCheckBox* m_enhanceContrastCheck = nullptr;
    QSpinBox* m_threshBlockSpin = nullptr;
    QDoubleSpinBox* m_threshCSpin = nullptr;
    QDoubleSpinBox* m_circMinSpin = nullptr;
    QDoubleSpinBox* m_circMaxSpin = nullptr;
    QSpinBox* m_minAreaSpin = nullptr;
    QSpinBox* m_maxAreaSpin = nullptr;
    QCheckBox* m_filterFinderCheck = nullptr;
    QDoubleSpinBox* m_finderRatioSpin = nullptr;
    QComboBox* m_plateTypeCombo = nullptr;
    QDoubleSpinBox* m_knownDiameterSpin = nullptr;
    QCheckBox* m_applyCalibCheck = nullptr;
    QLabel* m_resultLabel = nullptr;

    // Helper methods
    double calculateCircularity(const std::vector<cv::Point>& contour);
    bool isFinderMark(const cv::Mat& grayImage, const cv::Point2f& center, float radius);
    void updatePlateTypeUI();
};

#endif // HAS_OPENCV
