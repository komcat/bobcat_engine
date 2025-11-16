// CornerDetectorPlugin.cpp
#ifdef HAS_OPENCV

#include "CornerDetectorPlugin.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>

CornerDetectorPlugin::CornerDetectorPlugin()
    : VisionPluginBase("CornerDetector", "PR", "Detect L-shaped square corners (90-degree)")
{
}

bool CornerDetectorPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result)
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

    // Binary threshold based on polarity
    cv::Mat binary;
    if (m_polarity == 0) {  // Black corner
        cv::threshold(processed, binary, m_thresholdValue, 255, cv::THRESH_BINARY_INV);
    } else {  // White corner
        cv::threshold(processed, binary, m_thresholdValue, 255, cv::THRESH_BINARY);
    }

    // Convert to output
    if (input.channels() == 1) {
        cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
    } else {
        output = input.clone();
    }

    // Find contours in binary image
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::cout << "[CornerDetector] Found " << contours.size() << " contours" << std::endl;

    // Find corners in contours
    bool cornerFound = false;
    cv::Point2d bestCornerPt;
    cv::Point2d bestLeg1Dir, bestLeg2Dir;
    double bestAngle = 0;
    double bestScore = -1;

    CornerQuadrant targetQuadrant = static_cast<CornerQuadrant>(m_quadrant);

    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);

        // Filter by area
        if (area < m_minContourArea || area > m_maxContourArea) {
            std::cout << "[CornerDetector] Skipping contour with area " << area << std::endl;
            continue;
        }

        std::cout << "[CornerDetector] Analyzing contour with area " << area << std::endl;

        // Approximate contour to polygon
        std::vector<cv::Point> approx;
        double epsilon = 0.02 * cv::arcLength(contour, true);
        cv::approxPolyDP(contour, approx, epsilon, true);

        std::cout << "[CornerDetector] Polygon has " << approx.size() << " vertices" << std::endl;

        // For rectangles (4 vertices), check all 4 corners
        if (approx.size() >= 3 && approx.size() <= 20) {
            // Check each vertex as a potential corner
            for (size_t i = 0; i < approx.size(); ++i) {
                cv::Point prev = approx[(i + approx.size() - 1) % approx.size()];
                cv::Point curr = approx[i];
                cv::Point next = approx[(i + 1) % approx.size()];

                // Calculate vectors from current point (pointing OUTWARD from corner)
                cv::Point2d v1(prev.x - curr.x, prev.y - curr.y);
                cv::Point2d v2(next.x - curr.x, next.y - curr.y);

                // Normalize vectors
                double len1 = std::sqrt(v1.x * v1.x + v1.y * v1.y);
                double len2 = std::sqrt(v2.x * v2.x + v2.y * v2.y);

                if (len1 < 1e-6 || len2 < 1e-6) continue;

                v1.x /= len1; v1.y /= len1;
                v2.x /= len2; v2.y /= len2;

                // Calculate angle between vectors
                double dot = v1.x * v2.x + v1.y * v2.y;
                dot = std::max(-1.0, std::min(1.0, dot));
                double angleBetween = std::acos(dot) * 180.0 / CV_PI;

                // Check if angle is close to 90 degrees
                double angleDiff = std::abs(angleBetween - 90.0);
                if (angleDiff > m_angleTolerance) continue;

                // Reject corners too close to image boundaries (likely ROI edge artifacts)
                if (curr.x < m_edgeMargin || curr.x > input.cols - m_edgeMargin ||
                    curr.y < m_edgeMargin || curr.y > input.rows - m_edgeMargin) {
                    std::cout << "[CornerDetector] Rejecting corner at (" << curr.x << "," << curr.y
                              << ") - too close to image edge (margin=" << m_edgeMargin << ")" << std::endl;
                    continue;
                }

                // Check quadrant if specified
                if (targetQuadrant != CornerQuadrant::Any) {
                    if (!isCornerInQuadrant(v1, v2, targetQuadrant)) {
                        continue;
                    }
                }

                // Score this corner
                double angleScore = 1.0 - angleDiff / m_angleTolerance;

                // Prefer corners closer to expected size (based on edge lengths)
                double edgeLen = (len1 + len2) / 2.0;
                double sizeScore = 1.0 - std::abs(edgeLen - m_cornerSize) / m_cornerSize;
                sizeScore = std::max(0.0, std::min(1.0, sizeScore));

                double score = angleScore * 0.7 + sizeScore * 0.3;

                std::cout << "[CornerDetector] Vertex " << i << " at (" << curr.x << "," << curr.y
                          << ") angle=" << angleBetween << " score=" << score << std::endl;

                if (score > bestScore) {
                    bestScore = score;
                    bestCornerPt = cv::Point2d(curr.x, curr.y);
                    bestLeg1Dir = v1;
                    bestLeg2Dir = v2;
                    bestAngle = angleBetween;
                    cornerFound = true;
                }
            }
        }
    }

    // Report results
    if (cornerFound) {
        // Add measurements
        result.addMeasurement("corner_x", bestCornerPt.x);
        result.addMeasurement("corner_y", bestCornerPt.y);
        result.addMeasurement("corner_angle_deg", bestAngle);
        result.addMeasurement("score", bestScore);

        // Calculate which quadrant the corner is in
        // The opening direction is the average of the two leg vectors
        double avgDirX = (bestLeg1Dir.x + bestLeg2Dir.x) / 2.0;
        double avgDirY = (bestLeg1Dir.y + bestLeg2Dir.y) / 2.0;
        int detectedQuadrant = 0;

        // Note: In image coordinates, Y increases downward
        // Quadrant is based on which direction the corner "opens" toward
        if (avgDirX > 0 && avgDirY < 0) detectedQuadrant = 1;       // First (upper-right)
        else if (avgDirX < 0 && avgDirY < 0) detectedQuadrant = 2;  // Second (upper-left)
        else if (avgDirX < 0 && avgDirY > 0) detectedQuadrant = 3;  // Third (lower-left)
        else if (avgDirX > 0 && avgDirY > 0) detectedQuadrant = 4;  // Fourth (lower-right)

        result.addMeasurement("detected_quadrant", static_cast<double>(detectedQuadrant));

        // Draw corner overlay
        // Draw corner point (large crosshair)
        OverlayCommand cornerMarker;
        cornerMarker.type = OverlayCommand::DrawCrosshair;
        cornerMarker.p1 = bestCornerPt;
        cornerMarker.radius = 15;
        cornerMarker.color = cv::Scalar(0, 255, 0);  // Green
        cornerMarker.thickness = 3;
        result.overlays.push_back(cornerMarker);

        // Draw legs of the corner (showing the actual edges)
        double legLength = m_cornerSize * 0.8;

        // Leg 1 (Blue)
        OverlayCommand leg1Line;
        leg1Line.type = OverlayCommand::DrawLine;
        leg1Line.p1 = bestCornerPt;
        leg1Line.p2 = cv::Point2d(bestCornerPt.x + bestLeg1Dir.x * legLength,
                                   bestCornerPt.y + bestLeg1Dir.y * legLength);
        leg1Line.color = cv::Scalar(255, 0, 0);  // Blue
        leg1Line.thickness = 3;
        result.overlays.push_back(leg1Line);

        // Leg 2 (Red)
        OverlayCommand leg2Line;
        leg2Line.type = OverlayCommand::DrawLine;
        leg2Line.p1 = bestCornerPt;
        leg2Line.p2 = cv::Point2d(bestCornerPt.x + bestLeg2Dir.x * legLength,
                                   bestCornerPt.y + bestLeg2Dir.y * legLength);
        leg2Line.color = cv::Scalar(0, 0, 255);  // Red
        leg2Line.thickness = 3;
        result.overlays.push_back(leg2Line);

        // Draw angle arc indicator
        OverlayCommand angleArc;
        angleArc.type = OverlayCommand::DrawCircle;
        angleArc.p1 = bestCornerPt;
        angleArc.radius = 20;
        angleArc.color = cv::Scalar(0, 255, 255);  // Yellow
        angleArc.thickness = 2;
        result.overlays.push_back(angleArc);

        // Add text overlay for angle
        OverlayCommand angleText;
        angleText.type = OverlayCommand::DrawText;
        angleText.p1 = cv::Point2d(bestCornerPt.x + 25, bestCornerPt.y - 25);
        angleText.text = std::to_string(static_cast<int>(std::round(bestAngle))) + " deg";
        angleText.color = cv::Scalar(255, 255, 0);  // Cyan
        angleText.thickness = 2;
        result.overlays.push_back(angleText);

        // Draw semi-transparent polygon representing corner area
        // The polygon follows the two legs of the corner to show the actual corner shape
        OverlayCommand cornerArea;
        cornerArea.type = OverlayCommand::DrawFilledPolygon;
        double halfSize = m_cornerSize * 0.5;

        // Create a parallelogram/rectangle that follows the corner legs
        // Point 0: corner vertex
        // Point 1: along leg 1
        // Point 2: diagonal (sum of both legs)
        // Point 3: along leg 2
        cv::Point pt0(static_cast<int>(bestCornerPt.x), static_cast<int>(bestCornerPt.y));
        cv::Point pt1(static_cast<int>(bestCornerPt.x + bestLeg1Dir.x * halfSize),
                      static_cast<int>(bestCornerPt.y + bestLeg1Dir.y * halfSize));
        cv::Point pt2(static_cast<int>(bestCornerPt.x + (bestLeg1Dir.x + bestLeg2Dir.x) * halfSize),
                      static_cast<int>(bestCornerPt.y + (bestLeg1Dir.y + bestLeg2Dir.y) * halfSize));
        cv::Point pt3(static_cast<int>(bestCornerPt.x + bestLeg2Dir.x * halfSize),
                      static_cast<int>(bestCornerPt.y + bestLeg2Dir.y * halfSize));

        cornerArea.contour.push_back(pt0);
        cornerArea.contour.push_back(pt1);
        cornerArea.contour.push_back(pt2);
        cornerArea.contour.push_back(pt3);
        cornerArea.color = cv::Scalar(0, 0, 255);  // Red (BGR)
        cornerArea.thickness = -1;  // Filled
        cornerArea.opacity = 0.5;   // 50% opacity
        result.overlays.push_back(cornerArea);

        // Add metadata
        std::string quadrantName;
        switch (detectedQuadrant) {
            case 1: quadrantName = "1st (Upper-Right)"; break;
            case 2: quadrantName = "2nd (Upper-Left)"; break;
            case 3: quadrantName = "3rd (Lower-Left)"; break;
            case 4: quadrantName = "4th (Lower-Right)"; break;
            default: quadrantName = "Unknown"; break;
        }
        result.addMetadata("quadrant", quadrantName);
        result.addMetadata("polarity", m_polarity == 0 ? "Black" : "White");

        std::cout << "[CornerDetector] Found corner at (" << bestCornerPt.x << ", " << bestCornerPt.y
                  << ") angle=" << bestAngle << " deg, quadrant=" << detectedQuadrant << std::endl;
    } else {
        std::cout << "[CornerDetector] No valid corner found" << std::endl;
        result.addMetadata("error", "No corner matching criteria found");
    }

    // Add general measurements
    result.addMeasurement("contours_analyzed", static_cast<double>(contours.size()));
    result.addMeasurement("image_width", static_cast<double>(input.cols));
    result.addMeasurement("image_height", static_cast<double>(input.rows));

    return cornerFound;
}

bool CornerDetectorPlugin::findCornerInContour(const std::vector<cv::Point>& contour,
                                                cv::Point2d& cornerPt,
                                                cv::Point2d& leg1Dir,
                                                cv::Point2d& leg2Dir,
                                                double& angle)
{
    if (contour.size() < 3) return false;

    // Find the vertex with the sharpest angle (closest to 90 degrees)
    double bestAngleDiff = 180.0;
    int bestIdx = -1;

    for (size_t i = 0; i < contour.size(); ++i) {
        // Get three consecutive points
        cv::Point prev = contour[(i + contour.size() - 1) % contour.size()];
        cv::Point curr = contour[i];
        cv::Point next = contour[(i + 1) % contour.size()];

        // Calculate vectors from current point
        cv::Point2d v1(prev.x - curr.x, prev.y - curr.y);
        cv::Point2d v2(next.x - curr.x, next.y - curr.y);

        // Normalize vectors
        double len1 = std::sqrt(v1.x * v1.x + v1.y * v1.y);
        double len2 = std::sqrt(v2.x * v2.x + v2.y * v2.y);

        if (len1 < 1e-6 || len2 < 1e-6) continue;

        v1.x /= len1; v1.y /= len1;
        v2.x /= len2; v2.y /= len2;

        // Calculate angle between vectors
        double dot = v1.x * v2.x + v1.y * v2.y;
        dot = std::max(-1.0, std::min(1.0, dot));  // Clamp to [-1, 1]
        double angleBetween = std::acos(dot) * 180.0 / CV_PI;

        // We want angles close to 90 degrees
        double diff = std::abs(angleBetween - 90.0);
        if (diff < bestAngleDiff) {
            bestAngleDiff = diff;
            bestIdx = static_cast<int>(i);
            cornerPt = cv::Point2d(curr.x, curr.y);
            leg1Dir = v1;
            leg2Dir = v2;
            angle = angleBetween;
        }
    }

    return (bestIdx >= 0 && bestAngleDiff < m_angleTolerance);
}

bool CornerDetectorPlugin::isCornerInQuadrant(const cv::Point2d& leg1Dir, const cv::Point2d& leg2Dir,
                                               CornerQuadrant targetQuadrant)
{
    // Calculate the average direction (bisector of the corner)
    double avgDirX = (leg1Dir.x + leg2Dir.x) / 2.0;
    double avgDirY = (leg1Dir.y + leg2Dir.y) / 2.0;

    // Normalize
    double len = std::sqrt(avgDirX * avgDirX + avgDirY * avgDirY);
    if (len > 1e-6) {
        avgDirX /= len;
        avgDirY /= len;
    }

    // Determine quadrant based on opening direction
    // Note: In image coordinates, Y increases downward
    switch (targetQuadrant) {
        case CornerQuadrant::First:   // Opens upper-right (X+, Y-)
            return (avgDirX > 0 && avgDirY < 0);
        case CornerQuadrant::Second:  // Opens upper-left (X-, Y-)
            return (avgDirX < 0 && avgDirY < 0);
        case CornerQuadrant::Third:   // Opens lower-left (X-, Y+)
            return (avgDirX < 0 && avgDirY > 0);
        case CornerQuadrant::Fourth:  // Opens lower-right (X+, Y+)
            return (avgDirX > 0 && avgDirY > 0);
        case CornerQuadrant::Any:
            return true;
    }
    return false;
}

double CornerDetectorPlugin::calculateCornerAngle(const cv::Point2d& leg1Dir, const cv::Point2d& leg2Dir)
{
    double dot = leg1Dir.x * leg2Dir.x + leg1Dir.y * leg2Dir.y;
    dot = std::max(-1.0, std::min(1.0, dot));
    return std::acos(dot) * 180.0 / CV_PI;
}

bool CornerDetectorPlugin::validate() const
{
    if (m_cornerSize < 10) {
        m_lastError = "Corner size must be at least 10 pixels";
        return false;
    }
    if (m_minContourArea < 1) {
        m_lastError = "Minimum contour area must be positive";
        return false;
    }
    if (m_maxContourArea <= m_minContourArea) {
        m_lastError = "Maximum contour area must be greater than minimum";
        return false;
    }
    if (m_angleTolerance <= 0 || m_angleTolerance > 45) {
        m_lastError = "Angle tolerance must be between 0 and 45 degrees";
        return false;
    }
    return true;
}

QWidget* CornerDetectorPlugin::createConfigWidget(QWidget* parent)
{
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    // Corner Parameters
    QGroupBox* cornerGroup = new QGroupBox("Corner Detection");
    QFormLayout* cornerLayout = new QFormLayout(cornerGroup);

    m_cornerSizeSpin = new QSpinBox();
    m_cornerSizeSpin->setRange(10, 1000);
    m_cornerSizeSpin->setValue(m_cornerSize);
    m_cornerSizeSpin->setSuffix(" px");
    m_cornerSizeSpin->setToolTip("Expected size of the corner");
    cornerLayout->addRow("Corner Size:", m_cornerSizeSpin);

    m_quadrantCombo = new QComboBox();
    m_quadrantCombo->addItem("Any Quadrant", 0);
    m_quadrantCombo->addItem("1st (Opens Upper-Right)", 1);
    m_quadrantCombo->addItem("2nd (Opens Upper-Left)", 2);
    m_quadrantCombo->addItem("3rd (Opens Lower-Left)", 3);
    m_quadrantCombo->addItem("4th (Opens Lower-Right)", 4);
    m_quadrantCombo->setCurrentIndex(m_quadrant);
    m_quadrantCombo->setToolTip("Expected quadrant where the corner opens toward");
    cornerLayout->addRow("Quadrant:", m_quadrantCombo);

    m_polarityCombo = new QComboBox();
    m_polarityCombo->addItem("Black Corner", 0);
    m_polarityCombo->addItem("White Corner", 1);
    m_polarityCombo->setCurrentIndex(m_polarity);
    m_polarityCombo->setToolTip("Corner color relative to background");
    cornerLayout->addRow("Polarity:", m_polarityCombo);

    m_angleToleranceSpin = new QDoubleSpinBox();
    m_angleToleranceSpin->setRange(1.0, 45.0);
    m_angleToleranceSpin->setSingleStep(1.0);
    m_angleToleranceSpin->setValue(m_angleTolerance);
    m_angleToleranceSpin->setSuffix(" deg");
    m_angleToleranceSpin->setToolTip("Tolerance for 90-degree angle");
    cornerLayout->addRow("Angle Tolerance:", m_angleToleranceSpin);

    m_edgeMarginSpin = new QSpinBox();
    m_edgeMarginSpin->setRange(0, 100);
    m_edgeMarginSpin->setValue(m_edgeMargin);
    m_edgeMarginSpin->setSuffix(" px");
    m_edgeMarginSpin->setToolTip("Reject corners within this distance from image/ROI edge");
    cornerLayout->addRow("Edge Margin:", m_edgeMarginSpin);

    mainLayout->addWidget(cornerGroup);

    // Thresholding Parameters
    QGroupBox* threshGroup = new QGroupBox("Thresholding");
    QFormLayout* threshLayout = new QFormLayout(threshGroup);

    m_thresholdSpin = new QSpinBox();
    m_thresholdSpin->setRange(0, 255);
    m_thresholdSpin->setValue(m_thresholdValue);
    m_thresholdSpin->setToolTip("Binary threshold value");
    threshLayout->addRow("Threshold:", m_thresholdSpin);

    m_minAreaSpin = new QSpinBox();
    m_minAreaSpin->setRange(1, 1000000);
    m_minAreaSpin->setValue(m_minContourArea);
    m_minAreaSpin->setSuffix(" px²");
    m_minAreaSpin->setToolTip("Minimum contour area");
    threshLayout->addRow("Min Area:", m_minAreaSpin);

    m_maxAreaSpin = new QSpinBox();
    m_maxAreaSpin->setRange(1, 1000000);
    m_maxAreaSpin->setValue(m_maxContourArea);
    m_maxAreaSpin->setSuffix(" px²");
    m_maxAreaSpin->setToolTip("Maximum contour area");
    threshLayout->addRow("Max Area:", m_maxAreaSpin);

    mainLayout->addWidget(threshGroup);

    // Preprocessing
    QGroupBox* preprocGroup = new QGroupBox("Preprocessing");
    QFormLayout* preprocLayout = new QFormLayout(preprocGroup);

    m_applyBlurCheck = new QCheckBox("Apply Gaussian Blur");
    m_applyBlurCheck->setChecked(m_applyBlur);
    preprocLayout->addRow("", m_applyBlurCheck);

    m_blurKernelSizeSpin = new QSpinBox();
    m_blurKernelSizeSpin->setRange(1, 31);
    m_blurKernelSizeSpin->setSingleStep(2);
    m_blurKernelSizeSpin->setValue(m_blurKernelSize);
    m_blurKernelSizeSpin->setToolTip("Blur kernel size (must be odd)");
    preprocLayout->addRow("Blur Kernel:", m_blurKernelSizeSpin);

    m_threshold1Spin = new QDoubleSpinBox();
    m_threshold1Spin->setRange(0.0, 1000.0);
    m_threshold1Spin->setSingleStep(10.0);
    m_threshold1Spin->setValue(m_cannyThreshold1);
    m_threshold1Spin->setToolTip("Canny lower threshold");
    preprocLayout->addRow("Canny Lower:", m_threshold1Spin);

    m_threshold2Spin = new QDoubleSpinBox();
    m_threshold2Spin->setRange(0.0, 1000.0);
    m_threshold2Spin->setSingleStep(10.0);
    m_threshold2Spin->setValue(m_cannyThreshold2);
    m_threshold2Spin->setToolTip("Canny upper threshold");
    preprocLayout->addRow("Canny Upper:", m_threshold2Spin);

    mainLayout->addWidget(preprocGroup);
    mainLayout->addStretch();

    // Connect signals
    QObject::connect(m_cornerSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_cornerSize = val; });
    QObject::connect(m_quadrantCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int idx) { m_quadrant = m_quadrantCombo->itemData(idx).toInt(); });
    QObject::connect(m_polarityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int idx) { m_polarity = idx; });
    QObject::connect(m_angleToleranceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_angleTolerance = val; });
    QObject::connect(m_edgeMarginSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_edgeMargin = val; });
    QObject::connect(m_thresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_thresholdValue = val; });
    QObject::connect(m_minAreaSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_minContourArea = val; });
    QObject::connect(m_maxAreaSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxContourArea = val; });
    QObject::connect(m_applyBlurCheck, &QCheckBox::toggled,
            [this](bool checked) { m_applyBlur = checked; });
    QObject::connect(m_blurKernelSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) {
                if (val % 2 == 0) val++;
                m_blurKernelSize = val;
            });
    QObject::connect(m_threshold1Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_cannyThreshold1 = val; });
    QObject::connect(m_threshold2Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_cannyThreshold2 = val; });

    return m_configWidget;
}

nlohmann::json CornerDetectorPlugin::getParameters() const
{
    nlohmann::json params;
    params["cornerSize"] = m_cornerSize;
    params["quadrant"] = m_quadrant;
    params["polarity"] = m_polarity;
    params["thresholdValue"] = m_thresholdValue;
    params["minContourArea"] = m_minContourArea;
    params["maxContourArea"] = m_maxContourArea;
    params["angleTolerance"] = m_angleTolerance;
    params["edgeMargin"] = m_edgeMargin;
    params["applyBlur"] = m_applyBlur;
    params["blurKernelSize"] = m_blurKernelSize;
    params["cannyThreshold1"] = m_cannyThreshold1;
    params["cannyThreshold2"] = m_cannyThreshold2;
    return params;
}

void CornerDetectorPlugin::setParameters(const nlohmann::json& params)
{
    if (params.contains("cornerSize")) m_cornerSize = params["cornerSize"].get<int>();
    if (params.contains("quadrant")) m_quadrant = params["quadrant"].get<int>();
    if (params.contains("polarity")) m_polarity = params["polarity"].get<int>();
    if (params.contains("thresholdValue")) m_thresholdValue = params["thresholdValue"].get<int>();
    if (params.contains("minContourArea")) m_minContourArea = params["minContourArea"].get<int>();
    if (params.contains("maxContourArea")) m_maxContourArea = params["maxContourArea"].get<int>();
    if (params.contains("angleTolerance")) m_angleTolerance = params["angleTolerance"].get<double>();
    if (params.contains("edgeMargin")) m_edgeMargin = params["edgeMargin"].get<int>();
    if (params.contains("applyBlur")) m_applyBlur = params["applyBlur"].get<bool>();
    if (params.contains("blurKernelSize")) m_blurKernelSize = params["blurKernelSize"].get<int>();
    if (params.contains("cannyThreshold1")) m_cannyThreshold1 = params["cannyThreshold1"].get<double>();
    if (params.contains("cannyThreshold2")) m_cannyThreshold2 = params["cannyThreshold2"].get<double>();

    // Update UI
    if (m_cornerSizeSpin) m_cornerSizeSpin->setValue(m_cornerSize);
    if (m_quadrantCombo) {
        int idx = m_quadrantCombo->findData(m_quadrant);
        if (idx >= 0) m_quadrantCombo->setCurrentIndex(idx);
    }
    if (m_polarityCombo) m_polarityCombo->setCurrentIndex(m_polarity);
    if (m_angleToleranceSpin) m_angleToleranceSpin->setValue(m_angleTolerance);
    if (m_edgeMarginSpin) m_edgeMarginSpin->setValue(m_edgeMargin);
    if (m_thresholdSpin) m_thresholdSpin->setValue(m_thresholdValue);
    if (m_minAreaSpin) m_minAreaSpin->setValue(m_minContourArea);
    if (m_maxAreaSpin) m_maxAreaSpin->setValue(m_maxContourArea);
    if (m_applyBlurCheck) m_applyBlurCheck->setChecked(m_applyBlur);
    if (m_blurKernelSizeSpin) m_blurKernelSizeSpin->setValue(m_blurKernelSize);
    if (m_threshold1Spin) m_threshold1Spin->setValue(m_cannyThreshold1);
    if (m_threshold2Spin) m_threshold2Spin->setValue(m_cannyThreshold2);
}

nlohmann::json CornerDetectorPlugin::getDefaultParameters() const
{
    nlohmann::json params;
    params["cornerSize"] = 100;
    params["quadrant"] = 0;        // Any
    params["polarity"] = 0;        // Black
    params["thresholdValue"] = 128;
    params["minContourArea"] = 500;
    params["maxContourArea"] = 500000;  // Large enough for big rectangles
    params["angleTolerance"] = 15.0;
    params["edgeMargin"] = 10;     // Reject corners within 10px of image edge
    params["applyBlur"] = true;
    params["blurKernelSize"] = 5;
    params["cannyThreshold1"] = 50.0;
    params["cannyThreshold2"] = 150.0;
    return params;
}

ParameterSchema CornerDetectorPlugin::getParameterSchema() const
{
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    // Corner Detection Parameters
    schema.parameters.push_back(ParameterDescriptor::createInt(
        "cornerSize", "Corner Size",
        10, 1000, 100,
        "Expected size of the corner in pixels.",
        "px", 10,
        "Corner Detection"
    ));

    // Note: Quadrant is an enum, we'll treat it as int
    schema.parameters.push_back(ParameterDescriptor::createInt(
        "quadrant", "Quadrant",
        0, 4, 0,
        "Expected quadrant (0=Any, 1=Upper-Right, 2=Upper-Left, 3=Lower-Left, 4=Lower-Right).",
        "", 1,
        "Corner Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "polarity", "Polarity",
        0, 1, 0,
        "Corner color (0=Black, 1=White).",
        "", 1,
        "Corner Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "angleTolerance", "Angle Tolerance",
        1.0, 45.0, 15.0,
        "Tolerance for 90-degree angle detection.",
        "deg", 1.0,
        "Corner Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "edgeMargin", "Edge Margin",
        0, 100, 10,
        "Reject corners within this distance from image/ROI boundary.",
        "px", 5,
        "Corner Detection"
    ));

    // Thresholding
    schema.parameters.push_back(ParameterDescriptor::createInt(
        "thresholdValue", "Threshold",
        0, 255, 128,
        "Binary threshold value for corner segmentation.",
        "", 5,
        "Thresholding"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "minContourArea", "Min Area",
        1, 1000000, 500,
        "Minimum contour area to consider.",
        "px²", 100,
        "Thresholding"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxContourArea", "Max Area",
        1, 1000000, 100000,
        "Maximum contour area to consider.",
        "px²", 1000,
        "Thresholding"
    ));

    // Preprocessing
    schema.parameters.push_back(ParameterDescriptor::createBool(
        "applyBlur", "Apply Blur",
        true,
        "Apply Gaussian blur before processing.",
        "Preprocessing"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "blurKernelSize", "Blur Kernel",
        1, 31, 5,
        "Gaussian blur kernel size (must be odd).",
        "px", 2,
        "Preprocessing"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "cannyThreshold1", "Canny Lower",
        0.0, 1000.0, 50.0,
        "Canny edge detection lower threshold.",
        "", 10.0,
        "Preprocessing"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "cannyThreshold2", "Canny Upper",
        0.0, 1000.0, 150.0,
        "Canny edge detection upper threshold.",
        "", 10.0,
        "Preprocessing"
    ));

    return schema;
}

#endif // HAS_OPENCV
