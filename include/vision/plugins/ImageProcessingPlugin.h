// ImageProcessingPlugin.h
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
 * @brief Basic image processing plugin for common operations
 *
 * Supports:
 * - Rotation: 90 degrees left/right, 180 degrees
 * - Flip: Horizontal, Vertical
 * - Color conversion: Grayscale
 * - Binary threshold
 * - Gaussian blur
 */
class ImageProcessingPlugin : public VisionPluginBase {
public:
    // Operation modes
    enum class Operation {
        None = 0,
        RotateLeft,      // 90 degrees counter-clockwise
        RotateRight,     // 90 degrees clockwise
        Rotate180,       // 180 degrees
        FlipHorizontal,  // Mirror horizontally
        FlipVertical,    // Mirror vertically
        Grayscale,       // Convert to grayscale
        Binary,          // Binary threshold
        GaussianBlur     // Gaussian blur
    };

    ImageProcessingPlugin();

    // IVisionPlugin interface
    bool execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) override;
    bool validate() const override;
    QWidget* createConfigWidget(QWidget* parent = nullptr) override;
    nlohmann::json getParameters() const override;
    void setParameters(const nlohmann::json& params) override;
    nlohmann::json getDefaultParameters() const override;
    ParameterSchema getParameterSchema() const override;

private:
    // Operation to perform
    Operation m_operation = Operation::Grayscale;

    // Binary threshold parameters
    int m_threshold = 128;           // Threshold value (0-255)
    int m_maxValue = 255;            // Max value for binary
    int m_thresholdType = 0;         // 0=Binary, 1=Binary Inv, 2=Trunc, 3=ToZero, 4=ToZero Inv

    // Gaussian blur parameters
    int m_blurKernelSize = 5;        // Kernel size (must be odd)
    double m_blurSigmaX = 0.0;       // Sigma X (0 = auto calculate)
    double m_blurSigmaY = 0.0;       // Sigma Y (0 = same as X)

    // Configuration widget
    QWidget* m_configWidget = nullptr;
    QComboBox* m_operationCombo = nullptr;
    QSpinBox* m_thresholdSpin = nullptr;
    QSpinBox* m_maxValueSpin = nullptr;
    QComboBox* m_thresholdTypeCombo = nullptr;
    QSpinBox* m_blurKernelSizeSpin = nullptr;
    QDoubleSpinBox* m_blurSigmaXSpin = nullptr;
    QDoubleSpinBox* m_blurSigmaYSpin = nullptr;
    QGroupBox* m_thresholdGroup = nullptr;
    QGroupBox* m_blurGroup = nullptr;

    void updateParameterVisibility();
};

#endif // HAS_OPENCV
