// CircleDetectionPlugin.cpp
#ifdef HAS_OPENCV

#include "CircleDetectionPlugin.h"
#include <opencv2/imgproc.hpp>
#include <iostream>

CircleDetectionPlugin::CircleDetectionPlugin()
    : VisionPluginBase("CircleDetection", "PR", "Detect circles using Hough Circle Transform")
{
}

bool CircleDetectionPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result)
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

    // Apply Gaussian blur to reduce noise
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(9, 9), 2, 2);

    // Detect circles
    std::vector<cv::Vec3f> circles;
    try {
        cv::HoughCircles(blurred, circles, cv::HOUGH_GRADIENT,
                         m_dp, m_minDist, m_param1, m_param2,
                         m_minRadius, m_maxRadius);
    }
    catch (const cv::Exception& e) {
        m_lastError = "HoughCircles failed: " + std::string(e.what());
        return false;
    }

    // Limit number of circles
    if (circles.size() > static_cast<size_t>(m_maxCircles)) {
        circles.resize(m_maxCircles);
    }

    // Prepare output image (copy input or convert to color for overlay)
    if (input.channels() == 1) {
        cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
    } else {
        output = input.clone();
    }

    // Process detected circles
    for (size_t i = 0; i < circles.size(); i++) {
        cv::Point2d center(circles[i][0], circles[i][1]);
        double radius = circles[i][2];

        // Add to result (mm coordinates will be filled by VisionManager)
        CircleResult circleResult;
        circleResult.name = "circle_" + std::to_string(i);
        circleResult.centerPx = center;
        circleResult.radiusPx = radius;
        circleResult.diameterPx = radius * 2.0;
        circleResult.confidence = 1.0;  // HoughCircles doesn't provide confidence
        result.circles.push_back(circleResult);

        // Add overlay
        cv::Scalar color(0, 255, 0);  // Green
        result.addCircleOverlay(center, radius, color, 2);
        result.addPointOverlay(center, cv::Scalar(0, 0, 255), 5);  // Red center

        // Add text label
        std::string label = "C" + std::to_string(i) + " r=" + std::to_string(static_cast<int>(radius));
        result.addTextOverlay(label, cv::Point2d(center.x - radius, center.y - radius - 10),
                             cv::Scalar(255, 255, 0), 1);
    }

    // Add measurements
    result.addMeasurement("circle_count", static_cast<double>(circles.size()));

    if (!circles.empty()) {
        double totalRadius = 0;
        for (const auto& c : circles) {
            totalRadius += c[2];
        }
        result.addMeasurement("avg_radius_px", totalRadius / circles.size());
    }

    // Add metadata
    result.addMetadata("algorithm", "HoughCircles");
    result.addMetadata("dp", std::to_string(m_dp));
    result.addMetadata("param1", std::to_string(m_param1));
    result.addMetadata("param2", std::to_string(m_param2));

    std::cout << "[CircleDetection] Found " << circles.size() << " circles" << std::endl;
    return true;
}

bool CircleDetectionPlugin::validate() const
{
    if (m_dp <= 0) {
        m_lastError = "dp must be positive";
        return false;
    }
    if (m_minDist <= 0) {
        m_lastError = "minDist must be positive";
        return false;
    }
    if (m_param1 <= 0) {
        m_lastError = "param1 must be positive";
        return false;
    }
    if (m_param2 <= 0) {
        m_lastError = "param2 must be positive";
        return false;
    }
    if (m_minRadius < 0) {
        m_lastError = "minRadius cannot be negative";
        return false;
    }
    if (m_maxRadius <= m_minRadius) {
        m_lastError = "maxRadius must be greater than minRadius";
        return false;
    }
    if (m_maxCircles <= 0) {
        m_lastError = "maxCircles must be positive";
        return false;
    }
    return true;
}

QWidget* CircleDetectionPlugin::createConfigWidget(QWidget* parent)
{
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    QGroupBox* paramsGroup = new QGroupBox("Hough Circle Parameters");
    QFormLayout* formLayout = new QFormLayout(paramsGroup);

    // DP (accumulator resolution)
    m_dpSpin = new QDoubleSpinBox();
    m_dpSpin->setRange(0.1, 10.0);
    m_dpSpin->setSingleStep(0.1);
    m_dpSpin->setValue(m_dp);
    m_dpSpin->setToolTip("Inverse ratio of accumulator resolution to image resolution");
    formLayout->addRow("DP:", m_dpSpin);

    // Min Distance
    m_minDistSpin = new QDoubleSpinBox();
    m_minDistSpin->setRange(1.0, 1000.0);
    m_minDistSpin->setSingleStep(10.0);
    m_minDistSpin->setValue(m_minDist);
    m_minDistSpin->setToolTip("Minimum distance between circle centers");
    formLayout->addRow("Min Distance:", m_minDistSpin);

    // Param1 (Canny threshold)
    m_param1Spin = new QDoubleSpinBox();
    m_param1Spin->setRange(1.0, 500.0);
    m_param1Spin->setSingleStep(10.0);
    m_param1Spin->setValue(m_param1);
    m_param1Spin->setToolTip("Upper threshold for Canny edge detector");
    formLayout->addRow("Canny Threshold:", m_param1Spin);

    // Param2 (center threshold)
    m_param2Spin = new QDoubleSpinBox();
    m_param2Spin->setRange(1.0, 500.0);
    m_param2Spin->setSingleStep(5.0);
    m_param2Spin->setValue(m_param2);
    m_param2Spin->setToolTip("Accumulator threshold for center detection");
    formLayout->addRow("Center Threshold:", m_param2Spin);

    // Min Radius
    m_minRadiusSpin = new QSpinBox();
    m_minRadiusSpin->setRange(0, 10000);
    m_minRadiusSpin->setSingleStep(5);
    m_minRadiusSpin->setValue(m_minRadius);
    m_minRadiusSpin->setToolTip("Minimum circle radius in pixels");
    formLayout->addRow("Min Radius (px):", m_minRadiusSpin);

    // Max Radius
    m_maxRadiusSpin = new QSpinBox();
    m_maxRadiusSpin->setRange(1, 10000);
    m_maxRadiusSpin->setSingleStep(10);
    m_maxRadiusSpin->setValue(m_maxRadius);
    m_maxRadiusSpin->setToolTip("Maximum circle radius in pixels");
    formLayout->addRow("Max Radius (px):", m_maxRadiusSpin);

    // Max Circles
    m_maxCirclesSpin = new QSpinBox();
    m_maxCirclesSpin->setRange(1, 1000);
    m_maxCirclesSpin->setSingleStep(1);
    m_maxCirclesSpin->setValue(m_maxCircles);
    m_maxCirclesSpin->setToolTip("Maximum number of circles to detect");
    formLayout->addRow("Max Circles:", m_maxCirclesSpin);

    mainLayout->addWidget(paramsGroup);
    mainLayout->addStretch();

    // Connect signals to update parameters
    QObject::connect(m_dpSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_dp = val; });
    QObject::connect(m_minDistSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_minDist = val; });
    QObject::connect(m_param1Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_param1 = val; });
    QObject::connect(m_param2Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_param2 = val; });
    QObject::connect(m_minRadiusSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_minRadius = val; });
    QObject::connect(m_maxRadiusSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxRadius = val; });
    QObject::connect(m_maxCirclesSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxCircles = val; });

    return m_configWidget;
}

nlohmann::json CircleDetectionPlugin::getParameters() const
{
    nlohmann::json params;
    params["dp"] = m_dp;
    params["minDist"] = m_minDist;
    params["param1"] = m_param1;
    params["param2"] = m_param2;
    params["minRadius"] = m_minRadius;
    params["maxRadius"] = m_maxRadius;
    params["maxCircles"] = m_maxCircles;
    return params;
}

void CircleDetectionPlugin::setParameters(const nlohmann::json& params)
{
    if (params.contains("dp")) m_dp = params["dp"].get<double>();
    if (params.contains("minDist")) m_minDist = params["minDist"].get<double>();
    if (params.contains("param1")) m_param1 = params["param1"].get<double>();
    if (params.contains("param2")) m_param2 = params["param2"].get<double>();
    if (params.contains("minRadius")) m_minRadius = params["minRadius"].get<int>();
    if (params.contains("maxRadius")) m_maxRadius = params["maxRadius"].get<int>();
    if (params.contains("maxCircles")) m_maxCircles = params["maxCircles"].get<int>();

    // Update UI if widget exists
    if (m_dpSpin) m_dpSpin->setValue(m_dp);
    if (m_minDistSpin) m_minDistSpin->setValue(m_minDist);
    if (m_param1Spin) m_param1Spin->setValue(m_param1);
    if (m_param2Spin) m_param2Spin->setValue(m_param2);
    if (m_minRadiusSpin) m_minRadiusSpin->setValue(m_minRadius);
    if (m_maxRadiusSpin) m_maxRadiusSpin->setValue(m_maxRadius);
    if (m_maxCirclesSpin) m_maxCirclesSpin->setValue(m_maxCircles);
}

nlohmann::json CircleDetectionPlugin::getDefaultParameters() const
{
    nlohmann::json params;
    params["dp"] = 1.0;
    params["minDist"] = 50.0;
    params["param1"] = 100.0;
    params["param2"] = 30.0;
    params["minRadius"] = 10;
    params["maxRadius"] = 200;
    params["maxCircles"] = 10;
    return params;
}

ParameterSchema CircleDetectionPlugin::getParameterSchema() const
{
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    // Hough Transform Parameters
    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "dp", "DP (Accumulator Ratio)",
        0.1, 10.0, 1.0,
        "Inverse ratio of accumulator resolution to image resolution. "
        "Higher values mean lower accumulator resolution.",
        "", 0.1,
        "Hough Transform"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "minDist", "Min Distance",
        1.0, 1000.0, 50.0,
        "Minimum distance between detected circle centers. "
        "Prevents detecting overlapping circles.",
        "px", 10.0,
        "Hough Transform"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "param1", "Canny Threshold",
        1.0, 500.0, 100.0,
        "Upper threshold for internal Canny edge detector. "
        "Higher values detect fewer edges.",
        "", 10.0,
        "Hough Transform"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "param2", "Center Threshold",
        1.0, 500.0, 30.0,
        "Accumulator threshold for circle center detection. "
        "Lower values detect more circles but may include false positives.",
        "", 5.0,
        "Hough Transform"
    ));

    // Size Constraints
    schema.parameters.push_back(ParameterDescriptor::createInt(
        "minRadius", "Min Radius",
        0, 10000, 10,
        "Minimum circle radius to detect. Set to 0 for automatic.",
        "px", 5,
        "Size Constraints"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxRadius", "Max Radius",
        1, 10000, 200,
        "Maximum circle radius to detect. Set to 0 for automatic.",
        "px", 10,
        "Size Constraints"
    ));

    // Output Control
    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxCircles", "Max Circles",
        1, 1000, 10,
        "Maximum number of circles to return in results.",
        "", 1,
        "Output Control"
    ));

    return schema;
}

#endif // HAS_OPENCV
