// CornerDetectorPlugin.h
#pragma once

#ifdef HAS_OPENCV

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

/**
 * @brief Corner detection plugin for detecting L-shaped square corners
 *
 * Detects 90-degree corners (like square edges) in images using contour analysis
 * and corner fitting. Supports detection of corners in different quadrants.
 */
class CornerDetectorPlugin : public VisionPluginBase {
public:
    // Quadrant definitions (where the corner opens toward)
    enum class CornerQuadrant {
        First = 1,    // Opens toward upper-right (standard L)
        Second = 2,   // Opens toward upper-left (mirrored L)
        Third = 3,    // Opens toward lower-left (inverted mirrored L)
        Fourth = 4,   // Opens toward lower-right (inverted L)
        Any = 0       // Any quadrant
    };

    enum class CornerPolarity {
        Black = 0,    // Black corner on white/light background
        White = 1     // White corner on black/dark background
    };

    CornerDetectorPlugin();

    // IVisionPlugin interface
    bool execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) override;
    bool validate() const override;
    QWidget* createConfigWidget(QWidget* parent = nullptr) override;
    nlohmann::json getParameters() const override;
    void setParameters(const nlohmann::json& params) override;
    nlohmann::json getDefaultParameters() const override;
    ParameterSchema getParameterSchema() const override;

private:
    // Helper methods
    bool findCornerInContour(const std::vector<cv::Point>& contour,
                             cv::Point2d& cornerPt,
                             cv::Point2d& leg1Dir,
                             cv::Point2d& leg2Dir,
                             double& angle);
    bool isCornerInQuadrant(const cv::Point2d& leg1Dir, const cv::Point2d& leg2Dir,
                            CornerQuadrant targetQuadrant);
    double calculateCornerAngle(const cv::Point2d& leg1Dir, const cv::Point2d& leg2Dir);

    // Algorithm parameters
    int m_cornerSize = 100;           // Expected size of corner in pixels
    int m_quadrant = 0;               // 0=Any, 1-4=specific quadrant
    int m_polarity = 0;               // 0=Black, 1=White
    int m_thresholdValue = 128;       // Binary threshold value
    int m_minContourArea = 500;       // Minimum contour area
    int m_maxContourArea = 500000;    // Maximum contour area
    double m_angleTolerance = 15.0;   // Tolerance for 90-degree angle
    int m_edgeMargin = 10;            // Reject corners within this margin from image edge
    bool m_applyBlur = true;          // Apply Gaussian blur
    int m_blurKernelSize = 5;         // Blur kernel size
    double m_cannyThreshold1 = 50.0;  // Canny lower threshold
    double m_cannyThreshold2 = 150.0; // Canny upper threshold

    // Configuration widget (not owned)
    QWidget* m_configWidget = nullptr;
    QSpinBox* m_cornerSizeSpin = nullptr;
    QComboBox* m_quadrantCombo = nullptr;
    QComboBox* m_polarityCombo = nullptr;
    QSpinBox* m_thresholdSpin = nullptr;
    QSpinBox* m_minAreaSpin = nullptr;
    QSpinBox* m_maxAreaSpin = nullptr;
    QDoubleSpinBox* m_angleToleranceSpin = nullptr;
    QSpinBox* m_edgeMarginSpin = nullptr;
    QCheckBox* m_applyBlurCheck = nullptr;
    QSpinBox* m_blurKernelSizeSpin = nullptr;
    QDoubleSpinBox* m_threshold1Spin = nullptr;
    QDoubleSpinBox* m_threshold2Spin = nullptr;
};

#endif // HAS_OPENCV
