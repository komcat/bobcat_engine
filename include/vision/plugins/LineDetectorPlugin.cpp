// LineDetectorPlugin.cpp
#ifdef HAS_OPENCV

#include "LineDetectorPlugin.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <cmath>

LineDetectorPlugin::LineDetectorPlugin()
    : VisionPluginBase("LineDetector", "Measurement", "Detect straight lines using Hough Transform")
{
}

bool LineDetectorPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result)
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

    // Convert to output
    if (input.channels() == 1) {
        cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
    } else {
        output = input.clone();
    }

    // Hough Line Transform
    double thetaRad = m_thetaResolution * CV_PI / 180.0;

    LineResult bestLine;
    bool lineFound = false;
    int totalLinesFound = 0;

    if (m_useProbabilistic) {
        // Probabilistic Hough Transform - returns line segments
        std::vector<cv::Vec4i> lines;
        cv::HoughLinesP(edges, lines, m_rhoResolution, thetaRad, m_threshold,
                        m_minLineLength, m_maxLineGap);

        totalLinesFound = static_cast<int>(lines.size());

        if (!lines.empty()) {
            // Find the longest line (best score)
            double maxLength = 0;
            cv::Vec4i longestLine;

            for (const auto& line : lines) {
                double dx = line[2] - line[0];
                double dy = line[3] - line[1];
                double length = std::sqrt(dx * dx + dy * dy);
                if (length > maxLength) {
                    maxLength = length;
                    longestLine = line;
                }
            }

            // Calculate angle
            double angle = std::atan2(longestLine[3] - longestLine[1],
                                      longestLine[2] - longestLine[0]) * 180.0 / CV_PI;

            bestLine.startPx.x = longestLine[0];
            bestLine.startPx.y = longestLine[1];
            bestLine.endPx.x = longestLine[2];
            bestLine.endPx.y = longestLine[3];
            bestLine.lengthPx = maxLength;
            bestLine.angleDeg = angle;
            bestLine.name = "BestLine";
            lineFound = true;

            result.addMeasurement("line_angle_deg", angle);
            result.addMeasurement("line_length_px", maxLength);
        }
    } else {
        // Standard Hough Transform - returns rho/theta
        std::vector<cv::Vec2f> lines;
        cv::HoughLines(edges, lines, m_rhoResolution, thetaRad, m_threshold);

        totalLinesFound = static_cast<int>(lines.size());

        if (!lines.empty()) {
            // Use the first line (highest votes)
            float rho = lines[0][0];
            float theta = lines[0][1];

            // Convert rho/theta to line endpoints
            double a = std::cos(theta);
            double b = std::sin(theta);
            double x0 = a * rho;
            double y0 = b * rho;

            // Extend line across the image
            double lineLen = std::max(input.cols, input.rows) * 2.0;
            cv::Point2d pt1(x0 + lineLen * (-b), y0 + lineLen * (a));
            cv::Point2d pt2(x0 - lineLen * (-b), y0 - lineLen * (a));

            // Calculate angle (perpendicular to theta)
            double angle = (theta * 180.0 / CV_PI) - 90.0;
            if (angle < -90.0) angle += 180.0;
            if (angle > 90.0) angle -= 180.0;

            // Calculate line length within image bounds
            double length = std::sqrt(
                std::pow(pt2.x - pt1.x, 2) + std::pow(pt2.y - pt1.y, 2)
            );

            bestLine.startPx = pt1;
            bestLine.endPx = pt2;
            bestLine.angleDeg = angle;
            bestLine.lengthPx = length;
            bestLine.name = "BestLine";
            lineFound = true;

            result.addMeasurement("line_angle_deg", angle);
            result.addMeasurement("line_rho", static_cast<double>(rho));
            result.addMeasurement("line_theta_deg", theta * 180.0 / CV_PI);
            result.addMeasurement("line_length_px", length);
        }
    }

    // Add line to results
    if (lineFound) {
        result.lines.push_back(bestLine);

        // Calculate line endpoints for visualization
        double x1 = bestLine.startPx.x;
        double y1 = bestLine.startPx.y;
        double x2 = bestLine.endPx.x;
        double y2 = bestLine.endPx.y;

        // Optionally extend line to image boundaries
        if (m_extendLine && !m_useProbabilistic) {
            // Already extended for standard Hough
        } else if (m_extendLine && m_useProbabilistic) {
            // Extend the line segment to image boundaries
            double dx = x2 - x1;
            double dy = y2 - y1;
            double len = std::sqrt(dx * dx + dy * dy);
            if (len > 0) {
                // Unit direction vector
                double ux = dx / len;
                double uy = dy / len;

                // Extend in both directions
                double extLen = std::max(input.cols, input.rows) * 2.0;
                x1 = x1 - ux * extLen;
                y1 = y1 - uy * extLen;
                x2 = x2 + ux * extLen;
                y2 = y2 + uy * extLen;
            }
        }

        // Add overlay for the line (bright green, thick)
        OverlayCommand lineOverlay;
        lineOverlay.type = OverlayCommand::DrawLine;
        lineOverlay.p1 = cv::Point2d(x1, y1);
        lineOverlay.p2 = cv::Point2d(x2, y2);
        lineOverlay.color = cv::Scalar(0, 255, 0);  // Green
        lineOverlay.thickness = 3;
        result.overlays.push_back(lineOverlay);

        // Add center point marker
        double midX = (bestLine.startPx.x + bestLine.endPx.x) / 2.0;
        double midY = (bestLine.startPx.y + bestLine.endPx.y) / 2.0;

        OverlayCommand centerOverlay;
        centerOverlay.type = OverlayCommand::DrawCrosshair;
        centerOverlay.p1 = cv::Point2d(midX, midY);
        centerOverlay.radius = 10;
        centerOverlay.color = cv::Scalar(0, 255, 255);  // Yellow
        centerOverlay.thickness = 2;
        result.overlays.push_back(centerOverlay);

        // Add measurements for original segment endpoints
        result.addMeasurement("start_x", bestLine.startPx.x);
        result.addMeasurement("start_y", bestLine.startPx.y);
        result.addMeasurement("end_x", bestLine.endPx.x);
        result.addMeasurement("end_y", bestLine.endPx.y);
        result.addMeasurement("center_x", midX);
        result.addMeasurement("center_y", midY);
    }

    // Add general measurements
    result.addMeasurement("lines_found", static_cast<double>(totalLinesFound));
    result.addMeasurement("image_width", static_cast<double>(input.cols));
    result.addMeasurement("image_height", static_cast<double>(input.rows));

    // Add metadata
    result.addMetadata("method", m_useProbabilistic ? "Probabilistic" : "Standard");
    result.addMetadata("threshold", std::to_string(m_threshold));
    result.addMetadata("rho_resolution", std::to_string(m_rhoResolution));
    result.addMetadata("theta_resolution", std::to_string(m_thetaResolution));
    result.addMetadata("line_extended", m_extendLine ? "true" : "false");

    std::cout << "[LineDetector] Found " << totalLinesFound << " lines, "
              << (lineFound ? "best line selected" : "no valid line") << std::endl;

    return lineFound;
}

bool LineDetectorPlugin::validate() const
{
    if (m_rhoResolution <= 0) {
        m_lastError = "Rho resolution must be positive";
        return false;
    }
    if (m_thetaResolution <= 0) {
        m_lastError = "Theta resolution must be positive";
        return false;
    }
    if (m_threshold < 1) {
        m_lastError = "Threshold must be at least 1";
        return false;
    }
    if (m_minLineLength < 0) {
        m_lastError = "Minimum line length cannot be negative";
        return false;
    }
    if (m_maxLineGap < 0) {
        m_lastError = "Maximum line gap cannot be negative";
        return false;
    }
    if (m_cannyThreshold2 <= m_cannyThreshold1) {
        m_lastError = "Upper threshold must be greater than lower threshold";
        return false;
    }
    return true;
}

QWidget* LineDetectorPlugin::createConfigWidget(QWidget* parent)
{
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    // Hough Transform Parameters
    QGroupBox* houghGroup = new QGroupBox("Hough Transform");
    QFormLayout* houghLayout = new QFormLayout(houghGroup);

    m_useProbabilisticCheck = new QCheckBox("Use Probabilistic Method");
    m_useProbabilisticCheck->setChecked(m_useProbabilistic);
    m_useProbabilisticCheck->setToolTip("Probabilistic finds line segments, Standard finds infinite lines");
    houghLayout->addRow("", m_useProbabilisticCheck);

    m_rhoSpin = new QDoubleSpinBox();
    m_rhoSpin->setRange(0.1, 10.0);
    m_rhoSpin->setSingleStep(0.1);
    m_rhoSpin->setValue(m_rhoResolution);
    m_rhoSpin->setSuffix(" px");
    m_rhoSpin->setToolTip("Distance resolution of accumulator");
    houghLayout->addRow("Rho Resolution:", m_rhoSpin);

    m_thetaSpin = new QDoubleSpinBox();
    m_thetaSpin->setRange(0.1, 10.0);
    m_thetaSpin->setSingleStep(0.1);
    m_thetaSpin->setValue(m_thetaResolution);
    m_thetaSpin->setSuffix(" deg");
    m_thetaSpin->setToolTip("Angle resolution of accumulator");
    houghLayout->addRow("Theta Resolution:", m_thetaSpin);

    m_thresholdSpin = new QSpinBox();
    m_thresholdSpin->setRange(1, 1000);
    m_thresholdSpin->setValue(m_threshold);
    m_thresholdSpin->setToolTip("Minimum votes to detect a line");
    houghLayout->addRow("Threshold:", m_thresholdSpin);

    m_minLineLengthSpin = new QDoubleSpinBox();
    m_minLineLengthSpin->setRange(0.0, 2000.0);
    m_minLineLengthSpin->setSingleStep(10.0);
    m_minLineLengthSpin->setValue(m_minLineLength);
    m_minLineLengthSpin->setSuffix(" px");
    m_minLineLengthSpin->setToolTip("Minimum line length (probabilistic only)");
    houghLayout->addRow("Min Line Length:", m_minLineLengthSpin);

    m_maxLineGapSpin = new QDoubleSpinBox();
    m_maxLineGapSpin->setRange(0.0, 500.0);
    m_maxLineGapSpin->setSingleStep(5.0);
    m_maxLineGapSpin->setValue(m_maxLineGap);
    m_maxLineGapSpin->setSuffix(" px");
    m_maxLineGapSpin->setToolTip("Maximum gap between line segments (probabilistic only)");
    houghLayout->addRow("Max Line Gap:", m_maxLineGapSpin);

    m_extendLineCheck = new QCheckBox("Extend Line to Boundaries");
    m_extendLineCheck->setChecked(m_extendLine);
    m_extendLineCheck->setToolTip("Draw line extended to image boundaries");
    houghLayout->addRow("", m_extendLineCheck);

    mainLayout->addWidget(houghGroup);

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
    QObject::connect(m_useProbabilisticCheck, &QCheckBox::toggled,
            [this](bool checked) { m_useProbabilistic = checked; });
    QObject::connect(m_rhoSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_rhoResolution = val; });
    QObject::connect(m_thetaSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_thetaResolution = val; });
    QObject::connect(m_thresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_threshold = val; });
    QObject::connect(m_minLineLengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_minLineLength = val; });
    QObject::connect(m_maxLineGapSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_maxLineGap = val; });
    QObject::connect(m_extendLineCheck, &QCheckBox::toggled,
            [this](bool checked) { m_extendLine = checked; });
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

nlohmann::json LineDetectorPlugin::getParameters() const
{
    nlohmann::json params;
    params["rhoResolution"] = m_rhoResolution;
    params["thetaResolution"] = m_thetaResolution;
    params["threshold"] = m_threshold;
    params["minLineLength"] = m_minLineLength;
    params["maxLineGap"] = m_maxLineGap;
    params["useProbabilistic"] = m_useProbabilistic;
    params["cannyThreshold1"] = m_cannyThreshold1;
    params["cannyThreshold2"] = m_cannyThreshold2;
    params["blurKernelSize"] = m_blurKernelSize;
    params["applyBlur"] = m_applyBlur;
    params["extendLine"] = m_extendLine;
    return params;
}

void LineDetectorPlugin::setParameters(const nlohmann::json& params)
{
    if (params.contains("rhoResolution")) m_rhoResolution = params["rhoResolution"].get<double>();
    if (params.contains("thetaResolution")) m_thetaResolution = params["thetaResolution"].get<double>();
    if (params.contains("threshold")) m_threshold = params["threshold"].get<int>();
    if (params.contains("minLineLength")) m_minLineLength = params["minLineLength"].get<double>();
    if (params.contains("maxLineGap")) m_maxLineGap = params["maxLineGap"].get<double>();
    if (params.contains("useProbabilistic")) m_useProbabilistic = params["useProbabilistic"].get<bool>();
    if (params.contains("cannyThreshold1")) m_cannyThreshold1 = params["cannyThreshold1"].get<double>();
    if (params.contains("cannyThreshold2")) m_cannyThreshold2 = params["cannyThreshold2"].get<double>();
    if (params.contains("blurKernelSize")) m_blurKernelSize = params["blurKernelSize"].get<int>();
    if (params.contains("applyBlur")) m_applyBlur = params["applyBlur"].get<bool>();
    if (params.contains("extendLine")) m_extendLine = params["extendLine"].get<bool>();

    // Update UI
    if (m_rhoSpin) m_rhoSpin->setValue(m_rhoResolution);
    if (m_thetaSpin) m_thetaSpin->setValue(m_thetaResolution);
    if (m_thresholdSpin) m_thresholdSpin->setValue(m_threshold);
    if (m_minLineLengthSpin) m_minLineLengthSpin->setValue(m_minLineLength);
    if (m_maxLineGapSpin) m_maxLineGapSpin->setValue(m_maxLineGap);
    if (m_useProbabilisticCheck) m_useProbabilisticCheck->setChecked(m_useProbabilistic);
    if (m_threshold1Spin) m_threshold1Spin->setValue(m_cannyThreshold1);
    if (m_threshold2Spin) m_threshold2Spin->setValue(m_cannyThreshold2);
    if (m_blurKernelSizeSpin) m_blurKernelSizeSpin->setValue(m_blurKernelSize);
    if (m_applyBlurCheck) m_applyBlurCheck->setChecked(m_applyBlur);
    if (m_extendLineCheck) m_extendLineCheck->setChecked(m_extendLine);
}

nlohmann::json LineDetectorPlugin::getDefaultParameters() const
{
    nlohmann::json params;
    params["rhoResolution"] = 1.0;
    params["thetaResolution"] = 1.0;
    params["threshold"] = 100;
    params["minLineLength"] = 50.0;
    params["maxLineGap"] = 10.0;
    params["useProbabilistic"] = false;
    params["cannyThreshold1"] = 50.0;
    params["cannyThreshold2"] = 150.0;
    params["blurKernelSize"] = 5;
    params["applyBlur"] = true;
    params["extendLine"] = true;
    return params;
}

ParameterSchema LineDetectorPlugin::getParameterSchema() const
{
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    // Hough Transform Parameters
    schema.parameters.push_back(ParameterDescriptor::createBool(
        "useProbabilistic", "Use Probabilistic",
        false,
        "Probabilistic Hough finds line segments, Standard finds infinite lines.",
        "Hough Transform"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "rhoResolution", "Rho Resolution",
        0.1, 10.0, 1.0,
        "Distance resolution of the accumulator in pixels.",
        "px", 0.1,
        "Hough Transform"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "thetaResolution", "Theta Resolution",
        0.1, 10.0, 1.0,
        "Angle resolution of the accumulator in degrees.",
        "deg", 0.1,
        "Hough Transform"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "threshold", "Vote Threshold",
        1, 1000, 100,
        "Minimum number of votes (intersections) to detect a line.",
        "", 10,
        "Hough Transform"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "minLineLength", "Min Line Length",
        0.0, 2000.0, 50.0,
        "Minimum line length (probabilistic method only).",
        "px", 10.0,
        "Hough Transform"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "maxLineGap", "Max Line Gap",
        0.0, 500.0, 10.0,
        "Maximum gap between line segments to treat as single line (probabilistic only).",
        "px", 5.0,
        "Hough Transform"
    ));

    schema.parameters.push_back(ParameterDescriptor::createBool(
        "extendLine", "Extend Line",
        true,
        "Draw the detected line extended to image boundaries.",
        "Display"
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
        "Apply Gaussian blur before edge detection.",
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
