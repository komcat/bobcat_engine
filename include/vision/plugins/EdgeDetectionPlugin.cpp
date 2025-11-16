// EdgeDetectionPlugin.cpp
#ifdef HAS_OPENCV

#include "EdgeDetectionPlugin.h"
#include <opencv2/imgproc.hpp>
#include <iostream>

EdgeDetectionPlugin::EdgeDetectionPlugin()
    : VisionPluginBase("EdgeDetection", "Filter", "Detect edges using Canny algorithm")
{
}

bool EdgeDetectionPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result)
{
    if (input.empty()) {
        m_lastError = "Input image is empty";
        return false;
    }

    // Convert to grayscale if needed
    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else if (input.channels() == 1) {
        gray = input;
    } else {
        m_lastError = "Unsupported number of channels: " + std::to_string(input.channels());
        return false;
    }

    // Optional blur
    cv::Mat processed = gray;
    if (m_applyBlur && m_blurKernelSize > 1) {
        int ksize = m_blurKernelSize;
        if (ksize % 2 == 0) ksize++;  // Ensure odd
        cv::GaussianBlur(gray, processed, cv::Size(ksize, ksize), 0);
    }

    // Apply Canny edge detection
    cv::Mat edges;
    try {
        cv::Canny(processed, edges, m_threshold1, m_threshold2,
                  m_apertureSize, m_useL2Gradient);
    }
    catch (const cv::Exception& e) {
        m_lastError = "Canny edge detection failed: " + std::string(e.what());
        return false;
    }

    // Convert edges to BGR for visualization
    cv::cvtColor(edges, output, cv::COLOR_GRAY2BGR);

    // Count edge pixels
    int edgePixelCount = cv::countNonZero(edges);
    double edgePercentage = (edgePixelCount * 100.0) / (edges.rows * edges.cols);

    // Add measurements
    result.addMeasurement("edge_pixel_count", static_cast<double>(edgePixelCount));
    result.addMeasurement("edge_percentage", edgePercentage);
    result.addMeasurement("image_width", static_cast<double>(edges.cols));
    result.addMeasurement("image_height", static_cast<double>(edges.rows));

    // Find contours to get additional information
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    result.addMeasurement("contour_count", static_cast<double>(contours.size()));

    // Calculate total contour length
    double totalLength = 0;
    for (const auto& contour : contours) {
        totalLength += cv::arcLength(contour, false);
    }
    result.addMeasurement("total_edge_length_px", totalLength);

    // Add contours as overlay commands for visualization (limit to avoid performance issues)
    const size_t maxContours = 100;
    for (size_t i = 0; i < std::min(contours.size(), maxContours); ++i) {
        OverlayCommand overlay;
        overlay.type = OverlayCommand::DrawContour;
        overlay.color = cv::Scalar(0, 255, 0);  // Green edges
        overlay.thickness = 1;
        overlay.contour = contours[i];
        result.overlays.push_back(overlay);
    }

    // Add metadata
    result.addMetadata("algorithm", "Canny");
    result.addMetadata("threshold1", std::to_string(m_threshold1));
    result.addMetadata("threshold2", std::to_string(m_threshold2));
    result.addMetadata("aperture_size", std::to_string(m_apertureSize));
    result.addMetadata("l2_gradient", m_useL2Gradient ? "true" : "false");
    result.addMetadata("blur_applied", m_applyBlur ? "true" : "false");

    std::cout << "[EdgeDetection] Found " << edgePixelCount << " edge pixels ("
              << edgePercentage << "%), " << contours.size() << " contours" << std::endl;

    return true;
}

bool EdgeDetectionPlugin::validate() const
{
    if (m_threshold1 < 0) {
        m_lastError = "threshold1 cannot be negative";
        return false;
    }
    if (m_threshold2 < 0) {
        m_lastError = "threshold2 cannot be negative";
        return false;
    }
    if (m_threshold2 <= m_threshold1) {
        m_lastError = "threshold2 must be greater than threshold1";
        return false;
    }
    if (m_apertureSize != 3 && m_apertureSize != 5 && m_apertureSize != 7) {
        m_lastError = "apertureSize must be 3, 5, or 7";
        return false;
    }
    if (m_blurKernelSize < 1) {
        m_lastError = "blurKernelSize must be at least 1";
        return false;
    }
    if (m_blurKernelSize % 2 == 0) {
        m_lastError = "blurKernelSize must be odd";
        return false;
    }
    return true;
}

QWidget* EdgeDetectionPlugin::createConfigWidget(QWidget* parent)
{
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    QGroupBox* paramsGroup = new QGroupBox("Canny Edge Detection Parameters");
    QFormLayout* formLayout = new QFormLayout(paramsGroup);

    // Threshold 1 (lower)
    m_threshold1Spin = new QDoubleSpinBox();
    m_threshold1Spin->setRange(0.0, 1000.0);
    m_threshold1Spin->setSingleStep(10.0);
    m_threshold1Spin->setValue(m_threshold1);
    m_threshold1Spin->setToolTip("Lower threshold for hysteresis");
    formLayout->addRow("Lower Threshold:", m_threshold1Spin);

    // Threshold 2 (upper)
    m_threshold2Spin = new QDoubleSpinBox();
    m_threshold2Spin->setRange(0.0, 1000.0);
    m_threshold2Spin->setSingleStep(10.0);
    m_threshold2Spin->setValue(m_threshold2);
    m_threshold2Spin->setToolTip("Upper threshold for hysteresis");
    formLayout->addRow("Upper Threshold:", m_threshold2Spin);

    // Aperture Size
    m_apertureSizeCombo = new QComboBox();
    m_apertureSizeCombo->addItem("3", 3);
    m_apertureSizeCombo->addItem("5", 5);
    m_apertureSizeCombo->addItem("7", 7);
    m_apertureSizeCombo->setCurrentIndex(m_apertureSize == 3 ? 0 : (m_apertureSize == 5 ? 1 : 2));
    m_apertureSizeCombo->setToolTip("Sobel operator aperture size");
    formLayout->addRow("Aperture Size:", m_apertureSizeCombo);

    // L2 Gradient
    m_useL2GradientCheck = new QCheckBox("Use L2 Gradient");
    m_useL2GradientCheck->setChecked(m_useL2Gradient);
    m_useL2GradientCheck->setToolTip("Use more accurate L2 norm for gradient magnitude");
    formLayout->addRow("", m_useL2GradientCheck);

    // Apply Blur
    m_applyBlurCheck = new QCheckBox("Apply Gaussian Blur");
    m_applyBlurCheck->setChecked(m_applyBlur);
    m_applyBlurCheck->setToolTip("Apply Gaussian blur before edge detection");
    formLayout->addRow("", m_applyBlurCheck);

    // Blur Kernel Size
    m_blurKernelSizeSpin = new QSpinBox();
    m_blurKernelSizeSpin->setRange(1, 31);
    m_blurKernelSizeSpin->setSingleStep(2);
    m_blurKernelSizeSpin->setValue(m_blurKernelSize);
    m_blurKernelSizeSpin->setToolTip("Gaussian blur kernel size (must be odd)");
    formLayout->addRow("Blur Kernel Size:", m_blurKernelSizeSpin);

    mainLayout->addWidget(paramsGroup);
    mainLayout->addStretch();

    // Connect signals to update parameters
    QObject::connect(m_threshold1Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_threshold1 = val; });
    QObject::connect(m_threshold2Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_threshold2 = val; });
    QObject::connect(m_apertureSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) { m_apertureSize = m_apertureSizeCombo->itemData(index).toInt(); });
    QObject::connect(m_useL2GradientCheck, &QCheckBox::toggled,
            [this](bool checked) { m_useL2Gradient = checked; });
    QObject::connect(m_applyBlurCheck, &QCheckBox::toggled,
            [this](bool checked) { m_applyBlur = checked; });
    QObject::connect(m_blurKernelSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) {
                // Ensure odd
                if (val % 2 == 0) val++;
                m_blurKernelSize = val;
            });

    return m_configWidget;
}

nlohmann::json EdgeDetectionPlugin::getParameters() const
{
    nlohmann::json params;
    params["threshold1"] = m_threshold1;
    params["threshold2"] = m_threshold2;
    params["apertureSize"] = m_apertureSize;
    params["useL2Gradient"] = m_useL2Gradient;
    params["applyBlur"] = m_applyBlur;
    params["blurKernelSize"] = m_blurKernelSize;
    return params;
}

void EdgeDetectionPlugin::setParameters(const nlohmann::json& params)
{
    if (params.contains("threshold1")) m_threshold1 = params["threshold1"].get<double>();
    if (params.contains("threshold2")) m_threshold2 = params["threshold2"].get<double>();
    if (params.contains("apertureSize")) m_apertureSize = params["apertureSize"].get<int>();
    if (params.contains("useL2Gradient")) m_useL2Gradient = params["useL2Gradient"].get<bool>();
    if (params.contains("applyBlur")) m_applyBlur = params["applyBlur"].get<bool>();
    if (params.contains("blurKernelSize")) m_blurKernelSize = params["blurKernelSize"].get<int>();

    // Update UI if widget exists
    if (m_threshold1Spin) m_threshold1Spin->setValue(m_threshold1);
    if (m_threshold2Spin) m_threshold2Spin->setValue(m_threshold2);
    if (m_apertureSizeCombo) {
        int index = (m_apertureSize == 3) ? 0 : (m_apertureSize == 5) ? 1 : 2;
        m_apertureSizeCombo->setCurrentIndex(index);
    }
    if (m_useL2GradientCheck) m_useL2GradientCheck->setChecked(m_useL2Gradient);
    if (m_applyBlurCheck) m_applyBlurCheck->setChecked(m_applyBlur);
    if (m_blurKernelSizeSpin) m_blurKernelSizeSpin->setValue(m_blurKernelSize);
}

nlohmann::json EdgeDetectionPlugin::getDefaultParameters() const
{
    nlohmann::json params;
    params["threshold1"] = 50.0;
    params["threshold2"] = 150.0;
    params["apertureSize"] = 3;
    params["useL2Gradient"] = false;
    params["applyBlur"] = true;
    params["blurKernelSize"] = 5;
    return params;
}

ParameterSchema EdgeDetectionPlugin::getParameterSchema() const
{
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    // Canny Parameters
    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "threshold1", "Lower Threshold",
        0.0, 1000.0, 50.0,
        "Lower threshold for hysteresis. Edges with gradient below this are rejected.",
        "", 10.0,
        "Canny Algorithm"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "threshold2", "Upper Threshold",
        0.0, 1000.0, 150.0,
        "Upper threshold for hysteresis. Edges with gradient above this are always kept.",
        "", 10.0,
        "Canny Algorithm"
    ));

    schema.parameters.push_back(ParameterDescriptor::createChoice(
        "apertureSize", "Aperture Size",
        {"3x3", "5x5", "7x7"},
        {3, 5, 7},
        0,  // Default index (3x3)
        "Sobel operator kernel size. Larger sizes detect smoother edges.",
        "Canny Algorithm"
    ));

    schema.parameters.push_back(ParameterDescriptor::createBool(
        "useL2Gradient", "Use L2 Gradient",
        false,
        "Use more accurate L2 norm (sqrt(gx^2+gy^2)) instead of L1 norm (|gx|+|gy|) "
        "for gradient magnitude calculation.",
        "Canny Algorithm"
    ));

    // Preprocessing
    schema.parameters.push_back(ParameterDescriptor::createBool(
        "applyBlur", "Apply Gaussian Blur",
        true,
        "Apply Gaussian blur before edge detection to reduce noise.",
        "Preprocessing"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "blurKernelSize", "Blur Kernel Size",
        1, 31, 5,
        "Gaussian blur kernel size (must be odd). Larger values apply stronger blur.",
        "px", 2,
        "Preprocessing"
    ));

    return schema;
}

#endif // HAS_OPENCV
