// FiducialDetectorPlugin.cpp
#ifdef HAS_OPENCV

#include "FiducialDetectorPlugin.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <algorithm>

FiducialDetectorPlugin::FiducialDetectorPlugin()
    : VisionPluginBase("FiducialDetector", "PR", "Detect circular fiducial markers for alignment")
{
}

bool FiducialDetectorPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result)
{
    if (input.empty()) {
        m_lastError = "Input image is empty";
        return false;
    }

    // Convert to grayscale
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
        if (ksize % 2 == 0) ksize++;
        cv::GaussianBlur(gray, processed, cv::Size(ksize, ksize), 0);
    }

    // Edge detection
    cv::Mat edges;
    cv::Canny(processed, edges, m_cannyThreshold1, m_cannyThreshold2);

    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // Analyze each contour for circular fiducials
    struct FiducialCandidate {
        cv::Point2f center;
        float radius;
        double circularity;
        double area;
    };
    std::vector<FiducialCandidate> candidates;

    double minRadius = m_minDiameter / 2.0;
    double maxRadius = m_maxDiameter / 2.0;

    for (const auto& contour : contours) {
        if (contour.size() < 5) continue;  // Need at least 5 points for fitEllipse

        // Fit minimum enclosing circle
        cv::Point2f center;
        float radius;
        cv::minEnclosingCircle(contour, center, radius);

        // Check size constraints
        if (radius < minRadius || radius > maxRadius) continue;

        // Calculate circularity: 4*pi*area / perimeter^2
        double area = cv::contourArea(contour);
        double perimeter = cv::arcLength(contour, true);
        if (perimeter < 1.0) continue;

        double circularity = (4.0 * CV_PI * area) / (perimeter * perimeter);

        // Filter by circularity
        if (circularity < m_circularityThreshold) continue;

        // Valid candidate
        FiducialCandidate candidate;
        candidate.center = center;
        candidate.radius = radius;
        candidate.circularity = circularity;
        candidate.area = area;
        candidates.push_back(candidate);
    }

    // Sort candidates
    if (m_sortBySize) {
        std::sort(candidates.begin(), candidates.end(),
            [](const FiducialCandidate& a, const FiducialCandidate& b) {
                return a.radius > b.radius;  // Largest first
            });
    } else {
        // Sort by circularity (best match first)
        std::sort(candidates.begin(), candidates.end(),
            [](const FiducialCandidate& a, const FiducialCandidate& b) {
                return a.circularity > b.circularity;
            });
    }

    // Limit to expected count
    if (candidates.size() > static_cast<size_t>(m_expectedCount)) {
        candidates.resize(m_expectedCount);
    }

    // Convert to output image (copy input for visualization)
    if (input.channels() == 1) {
        cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
    } else {
        output = input.clone();
    }

    // Add results
    int fiducialNum = 1;
    for (const auto& fid : candidates) {
        // Add to circles result
        CircleResult circle;
        circle.centerPx.x = fid.center.x;
        circle.centerPx.y = fid.center.y;
        circle.radiusPx = fid.radius;
        circle.diameterPx = fid.radius * 2.0;
        circle.confidence = fid.circularity;
        circle.name = "Fiducial_" + std::to_string(fiducialNum);
        result.circles.push_back(circle);

        // Add overlay for visualization (green circle with crosshair)
        OverlayCommand circleOverlay;
        circleOverlay.type = OverlayCommand::DrawCircle;
        circleOverlay.p1 = cv::Point2d(fid.center.x, fid.center.y);
        circleOverlay.radius = fid.radius;
        circleOverlay.color = cv::Scalar(0, 255, 0);  // Green
        circleOverlay.thickness = 2;
        result.overlays.push_back(circleOverlay);

        // Add crosshair at center
        OverlayCommand crosshairOverlay;
        crosshairOverlay.type = OverlayCommand::DrawCrosshair;
        crosshairOverlay.p1 = cv::Point2d(fid.center.x, fid.center.y);
        crosshairOverlay.radius = fid.radius * 0.5;  // Crosshair size
        crosshairOverlay.color = cv::Scalar(0, 255, 255);  // Yellow
        crosshairOverlay.thickness = 1;
        result.overlays.push_back(crosshairOverlay);

        // Add label
        OverlayCommand textOverlay;
        textOverlay.type = OverlayCommand::DrawText;
        textOverlay.p1 = cv::Point2d(fid.center.x + fid.radius + 5, fid.center.y - 5);
        textOverlay.text = "F" + std::to_string(fiducialNum);
        textOverlay.color = cv::Scalar(255, 255, 0);  // Cyan
        textOverlay.fontScale = 1;
        result.overlays.push_back(textOverlay);

        fiducialNum++;
    }

    // Add measurements
    result.addMeasurement("fiducials_found", static_cast<double>(candidates.size()));
    result.addMeasurement("expected_count", static_cast<double>(m_expectedCount));

    if (!candidates.empty()) {
        result.addMeasurement("primary_x", candidates[0].center.x);
        result.addMeasurement("primary_y", candidates[0].center.y);
        result.addMeasurement("primary_diameter", candidates[0].radius * 2.0);
        result.addMeasurement("primary_circularity", candidates[0].circularity);
    }

    // Add metadata
    result.addMetadata("min_diameter", std::to_string(m_minDiameter));
    result.addMetadata("max_diameter", std::to_string(m_maxDiameter));
    result.addMetadata("circularity_threshold", std::to_string(m_circularityThreshold));
    result.addMetadata("total_contours_analyzed", std::to_string(contours.size()));

    std::cout << "[FiducialDetector] Found " << candidates.size() << " fiducials out of "
              << m_expectedCount << " expected" << std::endl;

    // Success if we found at least one fiducial
    return candidates.size() > 0;
}

bool FiducialDetectorPlugin::validate() const
{
    if (m_minDiameter < 1.0) {
        m_lastError = "Minimum diameter must be at least 1 pixel";
        return false;
    }
    if (m_maxDiameter <= m_minDiameter) {
        m_lastError = "Maximum diameter must be greater than minimum diameter";
        return false;
    }
    if (m_circularityThreshold < 0.0 || m_circularityThreshold > 1.0) {
        m_lastError = "Circularity threshold must be between 0 and 1";
        return false;
    }
    if (m_expectedCount < 1) {
        m_lastError = "Expected count must be at least 1";
        return false;
    }
    if (m_cannyThreshold2 <= m_cannyThreshold1) {
        m_lastError = "Upper threshold must be greater than lower threshold";
        return false;
    }
    return true;
}

QWidget* FiducialDetectorPlugin::createConfigWidget(QWidget* parent)
{
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    // Fiducial Parameters
    QGroupBox* fiducialGroup = new QGroupBox("Fiducial Detection");
    QFormLayout* fiducialLayout = new QFormLayout(fiducialGroup);

    m_minDiameterSpin = new QDoubleSpinBox();
    m_minDiameterSpin->setRange(1.0, 1000.0);
    m_minDiameterSpin->setSingleStep(5.0);
    m_minDiameterSpin->setValue(m_minDiameter);
    m_minDiameterSpin->setSuffix(" px");
    m_minDiameterSpin->setToolTip("Minimum fiducial diameter in pixels");
    fiducialLayout->addRow("Min Diameter:", m_minDiameterSpin);

    m_maxDiameterSpin = new QDoubleSpinBox();
    m_maxDiameterSpin->setRange(1.0, 2000.0);
    m_maxDiameterSpin->setSingleStep(10.0);
    m_maxDiameterSpin->setValue(m_maxDiameter);
    m_maxDiameterSpin->setSuffix(" px");
    m_maxDiameterSpin->setToolTip("Maximum fiducial diameter in pixels");
    fiducialLayout->addRow("Max Diameter:", m_maxDiameterSpin);

    m_circularitySpin = new QDoubleSpinBox();
    m_circularitySpin->setRange(0.0, 1.0);
    m_circularitySpin->setSingleStep(0.05);
    m_circularitySpin->setValue(m_circularityThreshold);
    m_circularitySpin->setToolTip("Minimum circularity (0-1, 1=perfect circle)");
    fiducialLayout->addRow("Circularity:", m_circularitySpin);

    m_expectedCountSpin = new QSpinBox();
    m_expectedCountSpin->setRange(1, 100);
    m_expectedCountSpin->setValue(m_expectedCount);
    m_expectedCountSpin->setToolTip("Expected number of fiducials to find");
    fiducialLayout->addRow("Expected Count:", m_expectedCountSpin);

    m_sortBySizeCheck = new QCheckBox("Sort by Size");
    m_sortBySizeCheck->setChecked(m_sortBySize);
    m_sortBySizeCheck->setToolTip("Sort by size (checked) or circularity (unchecked)");
    fiducialLayout->addRow("", m_sortBySizeCheck);

    mainLayout->addWidget(fiducialGroup);

    // Edge Detection Parameters
    QGroupBox* edgeGroup = new QGroupBox("Edge Detection");
    QFormLayout* edgeLayout = new QFormLayout(edgeGroup);

    m_threshold1Spin = new QDoubleSpinBox();
    m_threshold1Spin->setRange(0.0, 1000.0);
    m_threshold1Spin->setSingleStep(10.0);
    m_threshold1Spin->setValue(m_cannyThreshold1);
    m_threshold1Spin->setToolTip("Canny lower threshold");
    edgeLayout->addRow("Lower Threshold:", m_threshold1Spin);

    m_threshold2Spin = new QDoubleSpinBox();
    m_threshold2Spin->setRange(0.0, 1000.0);
    m_threshold2Spin->setSingleStep(10.0);
    m_threshold2Spin->setValue(m_cannyThreshold2);
    m_threshold2Spin->setToolTip("Canny upper threshold");
    edgeLayout->addRow("Upper Threshold:", m_threshold2Spin);

    m_applyBlurCheck = new QCheckBox("Apply Gaussian Blur");
    m_applyBlurCheck->setChecked(m_applyBlur);
    edgeLayout->addRow("", m_applyBlurCheck);

    m_blurKernelSizeSpin = new QSpinBox();
    m_blurKernelSizeSpin->setRange(1, 31);
    m_blurKernelSizeSpin->setSingleStep(2);
    m_blurKernelSizeSpin->setValue(m_blurKernelSize);
    m_blurKernelSizeSpin->setToolTip("Blur kernel size (must be odd)");
    edgeLayout->addRow("Blur Kernel:", m_blurKernelSizeSpin);

    mainLayout->addWidget(edgeGroup);
    mainLayout->addStretch();

    // Connect signals
    QObject::connect(m_minDiameterSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_minDiameter = val; });
    QObject::connect(m_maxDiameterSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_maxDiameter = val; });
    QObject::connect(m_circularitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_circularityThreshold = val; });
    QObject::connect(m_expectedCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_expectedCount = val; });
    QObject::connect(m_sortBySizeCheck, &QCheckBox::toggled,
            [this](bool checked) { m_sortBySize = checked; });
    QObject::connect(m_threshold1Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_cannyThreshold1 = val; });
    QObject::connect(m_threshold2Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_cannyThreshold2 = val; });
    QObject::connect(m_applyBlurCheck, &QCheckBox::toggled,
            [this](bool checked) { m_applyBlur = checked; });
    QObject::connect(m_blurKernelSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) {
                if (val % 2 == 0) val++;
                m_blurKernelSize = val;
            });

    return m_configWidget;
}

nlohmann::json FiducialDetectorPlugin::getParameters() const
{
    nlohmann::json params;
    params["minDiameter"] = m_minDiameter;
    params["maxDiameter"] = m_maxDiameter;
    params["circularityThreshold"] = m_circularityThreshold;
    params["expectedCount"] = m_expectedCount;
    params["sortBySize"] = m_sortBySize;
    params["cannyThreshold1"] = m_cannyThreshold1;
    params["cannyThreshold2"] = m_cannyThreshold2;
    params["blurKernelSize"] = m_blurKernelSize;
    params["applyBlur"] = m_applyBlur;
    return params;
}

void FiducialDetectorPlugin::setParameters(const nlohmann::json& params)
{
    if (params.contains("minDiameter")) m_minDiameter = params["minDiameter"].get<double>();
    if (params.contains("maxDiameter")) m_maxDiameter = params["maxDiameter"].get<double>();
    if (params.contains("circularityThreshold")) m_circularityThreshold = params["circularityThreshold"].get<double>();
    if (params.contains("expectedCount")) m_expectedCount = params["expectedCount"].get<int>();
    if (params.contains("sortBySize")) m_sortBySize = params["sortBySize"].get<bool>();
    if (params.contains("cannyThreshold1")) m_cannyThreshold1 = params["cannyThreshold1"].get<double>();
    if (params.contains("cannyThreshold2")) m_cannyThreshold2 = params["cannyThreshold2"].get<double>();
    if (params.contains("blurKernelSize")) m_blurKernelSize = params["blurKernelSize"].get<int>();
    if (params.contains("applyBlur")) m_applyBlur = params["applyBlur"].get<bool>();

    // Update UI
    if (m_minDiameterSpin) m_minDiameterSpin->setValue(m_minDiameter);
    if (m_maxDiameterSpin) m_maxDiameterSpin->setValue(m_maxDiameter);
    if (m_circularitySpin) m_circularitySpin->setValue(m_circularityThreshold);
    if (m_expectedCountSpin) m_expectedCountSpin->setValue(m_expectedCount);
    if (m_sortBySizeCheck) m_sortBySizeCheck->setChecked(m_sortBySize);
    if (m_threshold1Spin) m_threshold1Spin->setValue(m_cannyThreshold1);
    if (m_threshold2Spin) m_threshold2Spin->setValue(m_cannyThreshold2);
    if (m_blurKernelSizeSpin) m_blurKernelSizeSpin->setValue(m_blurKernelSize);
    if (m_applyBlurCheck) m_applyBlurCheck->setChecked(m_applyBlur);
}

nlohmann::json FiducialDetectorPlugin::getDefaultParameters() const
{
    nlohmann::json params;
    params["minDiameter"] = 20.0;
    params["maxDiameter"] = 200.0;
    params["circularityThreshold"] = 0.7;
    params["expectedCount"] = 1;
    params["sortBySize"] = true;
    params["cannyThreshold1"] = 50.0;
    params["cannyThreshold2"] = 150.0;
    params["blurKernelSize"] = 5;
    params["applyBlur"] = true;
    return params;
}

ParameterSchema FiducialDetectorPlugin::getParameterSchema() const
{
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    // Fiducial Parameters
    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "minDiameter", "Min Diameter",
        1.0, 1000.0, 20.0,
        "Minimum fiducial diameter. Circles smaller than this are ignored.",
        "px", 5.0,
        "Fiducial Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "maxDiameter", "Max Diameter",
        1.0, 2000.0, 200.0,
        "Maximum fiducial diameter. Circles larger than this are ignored.",
        "px", 10.0,
        "Fiducial Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "circularityThreshold", "Circularity",
        0.0, 1.0, 0.7,
        "Minimum circularity score (0=any shape, 1=perfect circle). "
        "Higher values require more circular shapes.",
        "", 0.05,
        "Fiducial Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "expectedCount", "Expected Count",
        1, 100, 1,
        "Maximum number of fiducials to detect. Results are sorted and trimmed to this count.",
        "", 1,
        "Fiducial Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createBool(
        "sortBySize", "Sort by Size",
        true,
        "Sort results by size (largest first) or by circularity (best match first).",
        "Fiducial Detection"
    ));

    // Edge Detection
    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "cannyThreshold1", "Lower Threshold",
        0.0, 1000.0, 50.0,
        "Canny edge detection lower threshold.",
        "", 10.0,
        "Edge Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "cannyThreshold2", "Upper Threshold",
        0.0, 1000.0, 150.0,
        "Canny edge detection upper threshold.",
        "", 10.0,
        "Edge Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createBool(
        "applyBlur", "Apply Blur",
        true,
        "Apply Gaussian blur before edge detection to reduce noise.",
        "Preprocessing"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "blurKernelSize", "Blur Kernel",
        1, 31, 5,
        "Gaussian blur kernel size (must be odd).",
        "px", 2,
        "Preprocessing"
    ));

    return schema;
}

#endif // HAS_OPENCV
