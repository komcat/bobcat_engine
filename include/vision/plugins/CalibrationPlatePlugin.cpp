// CalibrationPlatePlugin.cpp
#ifdef HAS_OPENCV

#include "CalibrationPlatePlugin.h"
#include "../VisionManager.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <numeric>

CalibrationPlatePlugin::CalibrationPlatePlugin()
    : VisionPluginBase("CalibrationPlate", "Calibration",
                       "Calculate mm/pixel ratio from MVTec calibration plate")
{
}

bool CalibrationPlatePlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result)
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
        gray = input.clone();
    } else {
        m_lastError = "Unsupported number of channels: " + std::to_string(input.channels());
        return false;
    }

    // Optional: Apply CLAHE contrast enhancement for low contrast images
    cv::Mat enhanced;
    if (m_enhanceContrast) {
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
        clahe->apply(gray, enhanced);
    } else {
        enhanced = gray;
    }

    // Apply Gaussian blur
    cv::Mat blurred;
    int ksize = m_blurSize | 1;  // Ensure odd
    cv::GaussianBlur(enhanced, blurred, cv::Size(ksize, ksize), 0);

    // Find contours using either Canny edge or adaptive threshold
    cv::Mat binary;
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;

    if (m_useCannyEdge) {
        // Canny edge detection - better for low contrast circles
        cv::Mat edges;
        cv::Canny(blurred, edges, m_cannyLow, m_cannyHigh);

        // Dilate to close gaps in edges
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
        cv::dilate(edges, binary, kernel, cv::Point(-1, -1), 1);

        // Find contours from edge image
        cv::findContours(binary, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    } else {
        // Adaptive threshold method
        int blockSize = m_thresholdBlockSize | 1;  // Ensure odd
        cv::adaptiveThreshold(blurred, binary, 255,
                              cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                              cv::THRESH_BINARY,
                              blockSize, m_thresholdC);

        // Find contours
        cv::findContours(binary, contours, hierarchy, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    }

    // Prepare output image
    if (input.channels() == 1) {
        cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
    } else {
        output = input.clone();
    }

    // Filter and analyze circles
    std::vector<double> validDiameters;
    std::vector<cv::Point2f> validCenters;
    std::vector<float> validRadii;
    int finderMarkCount = 0;
    int rejectedCount = 0;

    for (size_t i = 0; i < contours.size(); i++) {
        const auto& contour = contours[i];

        // Check area
        double area = cv::contourArea(contour);
        if (area < m_minArea || area > m_maxArea) {
            rejectedCount++;
            continue;
        }

        // Check circularity
        double circularity = calculateCircularity(contour);
        if (circularity < m_circularityMin || circularity > m_circularityMax) {
            rejectedCount++;
            continue;
        }

        // Fit minimum enclosing circle
        cv::Point2f center;
        float radius;
        cv::minEnclosingCircle(contour, center, radius);

        // Check if it's a finder mark (has inner circle)
        if (m_filterFinderMarks && isFinderMark(gray, center, radius)) {
            // Draw finder mark in yellow
            cv::circle(output, center, static_cast<int>(radius), cv::Scalar(0, 255, 255), 2);
            cv::drawMarker(output, center, cv::Scalar(0, 255, 255), cv::MARKER_CROSS, 10, 2);
            finderMarkCount++;
            continue;
        }

        // Valid calibration circle
        double diameter = radius * 2.0;
        validDiameters.push_back(diameter);
        validCenters.push_back(center);
        validRadii.push_back(radius);

        // Draw valid circle in green
        cv::circle(output, center, static_cast<int>(radius), cv::Scalar(0, 255, 0), 2);
        cv::circle(output, center, 3, cv::Scalar(0, 0, 255), -1);  // Red center dot
    }

    // Calculate statistics
    int validCount = static_cast<int>(validDiameters.size());
    result.addMeasurement("valid_circle_count", validCount);
    result.addMeasurement("finder_mark_count", finderMarkCount);
    result.addMeasurement("rejected_count", rejectedCount);

    if (validCount == 0) {
        m_lastError = "No valid calibration circles detected";
        result.addMetadata("status", "FAILED - No circles found");

        // Draw text on image
        cv::putText(output, "No valid circles detected!", cv::Point(20, 40),
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
        return false;
    }

    // Calculate mean and standard deviation
    double sum = std::accumulate(validDiameters.begin(), validDiameters.end(), 0.0);
    double meanDiameterPx = sum / validCount;

    double sqSum = 0.0;
    for (double d : validDiameters) {
        sqSum += (d - meanDiameterPx) * (d - meanDiameterPx);
    }
    double stdDevPx = std::sqrt(sqSum / validCount);

    // Find min and max
    auto minmax = std::minmax_element(validDiameters.begin(), validDiameters.end());
    double minDiameterPx = *minmax.first;
    double maxDiameterPx = *minmax.second;

    // Calculate mm/pixel ratio
    double mmPerPixel = m_knownDiameterMm / meanDiameterPx;
    double pixelsPerMm = meanDiameterPx / m_knownDiameterMm;

    // Add measurements
    result.addMeasurement("mean_diameter_px", meanDiameterPx);
    result.addMeasurement("std_dev_px", stdDevPx);
    result.addMeasurement("min_diameter_px", minDiameterPx);
    result.addMeasurement("max_diameter_px", maxDiameterPx);
    result.addMeasurement("known_diameter_mm", m_knownDiameterMm);
    result.addMeasurement("mm_per_pixel", mmPerPixel);
    result.addMeasurement("pixels_per_mm", pixelsPerMm);
    result.addMeasurement("um_per_pixel", mmPerPixel * 1000.0);

    // Add metadata
    result.addMetadata("plate_type", std::to_string(static_cast<int>(m_plateType)));
    result.addMetadata("algorithm", "Contour + Adaptive Threshold");
    result.addMetadata("status", "SUCCESS");

    // Add circles to result
    for (size_t i = 0; i < validCenters.size(); i++) {
        CircleResult cr;
        cr.name = "calib_" + std::to_string(i);
        cr.centerPx = cv::Point2d(validCenters[i].x, validCenters[i].y);
        cr.radiusPx = validRadii[i];
        cr.diameterPx = validRadii[i] * 2.0;
        cr.radiusMm = validRadii[i] * mmPerPixel;
        cr.diameterMm = validRadii[i] * 2.0 * mmPerPixel;
        cr.confidence = 1.0 - (std::abs(validRadii[i] * 2.0 - meanDiameterPx) / meanDiameterPx);
        result.circles.push_back(cr);
    }

    // Draw statistics on image
    int y = 30;
    cv::Scalar textColor(0, 255, 0);
    cv::putText(output, "Calibration Plate Analysis", cv::Point(10, y),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, textColor, 2);
    y += 25;

    char buf[128];
    snprintf(buf, sizeof(buf), "Valid circles: %d", validCount);
    cv::putText(output, buf, cv::Point(10, y), cv::FONT_HERSHEY_SIMPLEX, 0.6, textColor, 1);
    y += 22;

    snprintf(buf, sizeof(buf), "Finder marks: %d", finderMarkCount);
    cv::putText(output, buf, cv::Point(10, y), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 1);
    y += 22;

    snprintf(buf, sizeof(buf), "Mean diameter: %.2f px", meanDiameterPx);
    cv::putText(output, buf, cv::Point(10, y), cv::FONT_HERSHEY_SIMPLEX, 0.6, textColor, 1);
    y += 22;

    snprintf(buf, sizeof(buf), "Std dev: %.2f px (%.1f%%)", stdDevPx, (stdDevPx / meanDiameterPx) * 100.0);
    cv::putText(output, buf, cv::Point(10, y), cv::FONT_HERSHEY_SIMPLEX, 0.6, textColor, 1);
    y += 22;

    snprintf(buf, sizeof(buf), "Known diameter: %.3f mm", m_knownDiameterMm);
    cv::putText(output, buf, cv::Point(10, y), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 200, 0), 1);
    y += 30;

    // Highlight the calibration result
    snprintf(buf, sizeof(buf), ">> %.6f mm/pixel", mmPerPixel);
    cv::putText(output, buf, cv::Point(10, y), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 255), 2);
    y += 25;

    snprintf(buf, sizeof(buf), ">> %.4f um/pixel", mmPerPixel * 1000.0);
    cv::putText(output, buf, cv::Point(10, y), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 255), 2);

    // Apply calibration to VisionManager if requested
    if (m_applyCalibration) {
        VisionManager::instance().setPixelToMm(mmPerPixel, mmPerPixel);
        std::cout << "[CalibrationPlate] Applied calibration: " << mmPerPixel << " mm/pixel" << std::endl;
        result.addMetadata("calibration_applied", "true");
    }

    std::cout << "[CalibrationPlate] Detected " << validCount << " circles, "
              << "mean diameter=" << meanDiameterPx << " px, "
              << "calibration=" << mmPerPixel << " mm/px ("
              << (mmPerPixel * 1000.0) << " um/px)" << std::endl;

    return true;
}

double CalibrationPlatePlugin::calculateCircularity(const std::vector<cv::Point>& contour)
{
    double area = cv::contourArea(contour);
    double perimeter = cv::arcLength(contour, true);
    if (perimeter == 0) return 0;

    // Circularity = 4 * pi * area / perimeter^2
    // Perfect circle = 1.0
    return (4.0 * CV_PI * area) / (perimeter * perimeter);
}

bool CalibrationPlatePlugin::isFinderMark(const cv::Mat& grayImage, const cv::Point2f& center, float radius)
{
    // Check for inner dark region (hole in center of finder mark)
    // Sample a small region in the center of the circle

    int sampleRadius = static_cast<int>(radius * m_finderHoleRatio);
    if (sampleRadius < 3) sampleRadius = 3;

    // Create ROI for center region
    int x1 = std::max(0, static_cast<int>(center.x) - sampleRadius);
    int y1 = std::max(0, static_cast<int>(center.y) - sampleRadius);
    int x2 = std::min(grayImage.cols, static_cast<int>(center.x) + sampleRadius);
    int y2 = std::min(grayImage.rows, static_cast<int>(center.y) + sampleRadius);

    if (x2 <= x1 || y2 <= y1) return false;

    cv::Rect roi(x1, y1, x2 - x1, y2 - y1);
    cv::Mat centerRegion = grayImage(roi);

    // Sample the outer ring for comparison
    int outerSampleRadius = static_cast<int>(radius * 0.7);
    std::vector<uchar> outerSamples;

    // Sample points on outer ring
    for (int angle = 0; angle < 360; angle += 30) {
        double rad = angle * CV_PI / 180.0;
        int px = static_cast<int>(center.x + outerSampleRadius * std::cos(rad));
        int py = static_cast<int>(center.y + outerSampleRadius * std::sin(rad));

        if (px >= 0 && px < grayImage.cols && py >= 0 && py < grayImage.rows) {
            outerSamples.push_back(grayImage.at<uchar>(py, px));
        }
    }

    if (outerSamples.empty()) return false;

    // Calculate mean intensity of outer ring (should be bright for white circle)
    double outerMean = std::accumulate(outerSamples.begin(), outerSamples.end(), 0.0) / outerSamples.size();

    // Calculate mean intensity of center region
    cv::Scalar centerMean = cv::mean(centerRegion);

    // If center is significantly darker than outer, it's a finder mark
    // Finder marks have a dark hole in the center
    double intensityRatio = centerMean[0] / outerMean;

    // Typical finder mark has intensity ratio < 0.7 (dark center vs bright ring)
    return intensityRatio < 0.75;
}

void CalibrationPlatePlugin::updatePlateTypeUI()
{
    if (!m_knownDiameterSpin) return;

    switch (m_plateType) {
        case PlateType::Plate20mm:
            m_knownDiameterMm = 0.322;
            m_knownDiameterSpin->setValue(0.322);
            m_knownDiameterSpin->setEnabled(false);
            break;
        case PlateType::Plate40mm:
            m_knownDiameterMm = 0.646;
            m_knownDiameterSpin->setValue(0.646);
            m_knownDiameterSpin->setEnabled(false);
            break;
        case PlateType::Plate80mm:
            m_knownDiameterMm = 1.290;
            m_knownDiameterSpin->setValue(1.290);
            m_knownDiameterSpin->setEnabled(false);
            break;
        case PlateType::Custom:
            m_knownDiameterSpin->setEnabled(true);
            break;
    }
}

bool CalibrationPlatePlugin::validate() const
{
    if (m_blurSize < 1) {
        m_lastError = "Blur size must be >= 1";
        return false;
    }
    if (m_thresholdBlockSize < 3) {
        m_lastError = "Threshold block size must be >= 3";
        return false;
    }
    if (m_circularityMin < 0 || m_circularityMin > 1) {
        m_lastError = "Circularity min must be between 0 and 1";
        return false;
    }
    if (m_circularityMax < m_circularityMin) {
        m_lastError = "Circularity max must be >= min";
        return false;
    }
    if (m_minArea < 0) {
        m_lastError = "Min area cannot be negative";
        return false;
    }
    if (m_maxArea <= m_minArea) {
        m_lastError = "Max area must be > min area";
        return false;
    }
    if (m_knownDiameterMm <= 0) {
        m_lastError = "Known diameter must be positive";
        return false;
    }
    return true;
}

QWidget* CalibrationPlatePlugin::createConfigWidget(QWidget* parent)
{
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    // === Plate Type Group ===
    QGroupBox* plateGroup = new QGroupBox("Calibration Plate");
    QFormLayout* plateLayout = new QFormLayout(plateGroup);

    m_plateTypeCombo = new QComboBox();
    m_plateTypeCombo->addItem("20mm Plate (0.322mm)", static_cast<int>(PlateType::Plate20mm));
    m_plateTypeCombo->addItem("40mm Plate (0.646mm)", static_cast<int>(PlateType::Plate40mm));
    m_plateTypeCombo->addItem("80mm Plate (1.290mm)", static_cast<int>(PlateType::Plate80mm));
    m_plateTypeCombo->addItem("Custom", static_cast<int>(PlateType::Custom));
    m_plateTypeCombo->setCurrentIndex(static_cast<int>(m_plateType));
    m_plateTypeCombo->setToolTip("Select MVTec calibration plate type");
    plateLayout->addRow("Plate Type:", m_plateTypeCombo);

    m_knownDiameterSpin = new QDoubleSpinBox();
    m_knownDiameterSpin->setRange(0.001, 100.0);
    m_knownDiameterSpin->setDecimals(4);
    m_knownDiameterSpin->setSingleStep(0.001);
    m_knownDiameterSpin->setValue(m_knownDiameterMm);
    m_knownDiameterSpin->setSuffix(" mm");
    m_knownDiameterSpin->setToolTip("Known circle diameter on calibration plate");
    m_knownDiameterSpin->setEnabled(m_plateType == PlateType::Custom);
    plateLayout->addRow("Circle Diameter:", m_knownDiameterSpin);

    m_applyCalibCheck = new QCheckBox("Apply calibration to system");
    m_applyCalibCheck->setChecked(m_applyCalibration);
    m_applyCalibCheck->setToolTip("Automatically set VisionManager calibration after successful detection");
    plateLayout->addRow("", m_applyCalibCheck);

    mainLayout->addWidget(plateGroup);

    // === Detection Parameters Group ===
    QGroupBox* detectGroup = new QGroupBox("Detection Parameters");
    QFormLayout* detectLayout = new QFormLayout(detectGroup);

    m_enhanceContrastCheck = new QCheckBox("Enhance contrast (CLAHE)");
    m_enhanceContrastCheck->setChecked(m_enhanceContrast);
    m_enhanceContrastCheck->setToolTip("Apply CLAHE contrast enhancement for low contrast images");
    detectLayout->addRow("", m_enhanceContrastCheck);

    m_blurSizeSpin = new QSpinBox();
    m_blurSizeSpin->setRange(1, 31);
    m_blurSizeSpin->setSingleStep(2);
    m_blurSizeSpin->setValue(m_blurSize);
    m_blurSizeSpin->setToolTip("Gaussian blur kernel size (odd number)");
    detectLayout->addRow("Blur Size:", m_blurSizeSpin);

    m_useCannyCheck = new QCheckBox("Use Canny edge detection");
    m_useCannyCheck->setChecked(m_useCannyEdge);
    m_useCannyCheck->setToolTip("Use Canny edge detection (recommended for low contrast)");
    detectLayout->addRow("", m_useCannyCheck);

    m_cannyLowSpin = new QSpinBox();
    m_cannyLowSpin->setRange(1, 255);
    m_cannyLowSpin->setValue(m_cannyLow);
    m_cannyLowSpin->setToolTip("Canny low threshold");
    detectLayout->addRow("Canny Low:", m_cannyLowSpin);

    m_cannyHighSpin = new QSpinBox();
    m_cannyHighSpin->setRange(1, 255);
    m_cannyHighSpin->setValue(m_cannyHigh);
    m_cannyHighSpin->setToolTip("Canny high threshold");
    detectLayout->addRow("Canny High:", m_cannyHighSpin);

    m_threshBlockSpin = new QSpinBox();
    m_threshBlockSpin->setRange(3, 201);
    m_threshBlockSpin->setSingleStep(2);
    m_threshBlockSpin->setValue(m_thresholdBlockSize);
    m_threshBlockSpin->setToolTip("Adaptive threshold block size (for non-Canny mode)");
    detectLayout->addRow("Threshold Block:", m_threshBlockSpin);

    m_threshCSpin = new QDoubleSpinBox();
    m_threshCSpin->setRange(-50.0, 50.0);
    m_threshCSpin->setSingleStep(1.0);
    m_threshCSpin->setValue(m_thresholdC);
    m_threshCSpin->setToolTip("Adaptive threshold constant (for non-Canny mode)");
    detectLayout->addRow("Threshold C:", m_threshCSpin);

    mainLayout->addWidget(detectGroup);

    // === Circle Filtering Group ===
    QGroupBox* filterGroup = new QGroupBox("Circle Filtering");
    QFormLayout* filterLayout = new QFormLayout(filterGroup);

    m_circMinSpin = new QDoubleSpinBox();
    m_circMinSpin->setRange(0.0, 1.0);
    m_circMinSpin->setDecimals(2);
    m_circMinSpin->setSingleStep(0.05);
    m_circMinSpin->setValue(m_circularityMin);
    m_circMinSpin->setToolTip("Minimum circularity (1.0 = perfect circle)");
    filterLayout->addRow("Min Circularity:", m_circMinSpin);

    m_circMaxSpin = new QDoubleSpinBox();
    m_circMaxSpin->setRange(0.0, 1.5);
    m_circMaxSpin->setDecimals(2);
    m_circMaxSpin->setSingleStep(0.05);
    m_circMaxSpin->setValue(m_circularityMax);
    m_circMaxSpin->setToolTip("Maximum circularity");
    filterLayout->addRow("Max Circularity:", m_circMaxSpin);

    m_minAreaSpin = new QSpinBox();
    m_minAreaSpin->setRange(10, 100000);
    m_minAreaSpin->setSingleStep(100);
    m_minAreaSpin->setValue(m_minArea);
    m_minAreaSpin->setSuffix(" px^2");
    m_minAreaSpin->setToolTip("Minimum contour area");
    filterLayout->addRow("Min Area:", m_minAreaSpin);

    m_maxAreaSpin = new QSpinBox();
    m_maxAreaSpin->setRange(100, 1000000);
    m_maxAreaSpin->setSingleStep(1000);
    m_maxAreaSpin->setValue(m_maxArea);
    m_maxAreaSpin->setSuffix(" px^2");
    m_maxAreaSpin->setToolTip("Maximum contour area");
    filterLayout->addRow("Max Area:", m_maxAreaSpin);

    mainLayout->addWidget(filterGroup);

    // === Finder Mark Filtering Group ===
    QGroupBox* finderGroup = new QGroupBox("Finder Mark Filtering");
    QFormLayout* finderLayout = new QFormLayout(finderGroup);

    m_filterFinderCheck = new QCheckBox("Filter out finder marks");
    m_filterFinderCheck->setChecked(m_filterFinderMarks);
    m_filterFinderCheck->setToolTip("Exclude circles with center dots (finder marks)");
    finderLayout->addRow("", m_filterFinderCheck);

    m_finderRatioSpin = new QDoubleSpinBox();
    m_finderRatioSpin->setRange(0.1, 0.8);
    m_finderRatioSpin->setDecimals(2);
    m_finderRatioSpin->setSingleStep(0.05);
    m_finderRatioSpin->setValue(m_finderHoleRatio);
    m_finderRatioSpin->setToolTip("Ratio of inner hole to outer circle radius for finder detection");
    finderLayout->addRow("Hole Ratio:", m_finderRatioSpin);

    mainLayout->addWidget(finderGroup);

    // === Result Label ===
    m_resultLabel = new QLabel("Run detection to calculate calibration");
    m_resultLabel->setStyleSheet("font-weight: bold; padding: 10px; background: #f0f0f0; border-radius: 5px;");
    m_resultLabel->setWordWrap(true);
    mainLayout->addWidget(m_resultLabel);

    mainLayout->addStretch();

    // Connect signals
    QObject::connect(m_plateTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
                m_plateType = static_cast<PlateType>(m_plateTypeCombo->itemData(index).toInt());
                updatePlateTypeUI();
            });

    QObject::connect(m_knownDiameterSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_knownDiameterMm = val; });
    QObject::connect(m_applyCalibCheck, &QCheckBox::toggled,
            [this](bool checked) { m_applyCalibration = checked; });

    QObject::connect(m_enhanceContrastCheck, &QCheckBox::toggled,
            [this](bool checked) { m_enhanceContrast = checked; });
    QObject::connect(m_blurSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_blurSize = val | 1; });  // Ensure odd
    QObject::connect(m_useCannyCheck, &QCheckBox::toggled,
            [this](bool checked) { m_useCannyEdge = checked; });
    QObject::connect(m_cannyLowSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_cannyLow = val; });
    QObject::connect(m_cannyHighSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_cannyHigh = val; });
    QObject::connect(m_threshBlockSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_thresholdBlockSize = val | 1; });  // Ensure odd
    QObject::connect(m_threshCSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_thresholdC = val; });
    QObject::connect(m_circMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_circularityMin = val; });
    QObject::connect(m_circMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_circularityMax = val; });
    QObject::connect(m_minAreaSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_minArea = val; });
    QObject::connect(m_maxAreaSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxArea = val; });
    QObject::connect(m_filterFinderCheck, &QCheckBox::toggled,
            [this](bool checked) { m_filterFinderMarks = checked; });
    QObject::connect(m_finderRatioSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_finderHoleRatio = val; });

    return m_configWidget;
}

nlohmann::json CalibrationPlatePlugin::getParameters() const
{
    nlohmann::json params;
    params["plateType"] = static_cast<int>(m_plateType);
    params["knownDiameterMm"] = m_knownDiameterMm;
    params["applyCalibration"] = m_applyCalibration;
    params["blurSize"] = m_blurSize;
    params["useCannyEdge"] = m_useCannyEdge;
    params["cannyLow"] = m_cannyLow;
    params["cannyHigh"] = m_cannyHigh;
    params["enhanceContrast"] = m_enhanceContrast;
    params["thresholdBlockSize"] = m_thresholdBlockSize;
    params["thresholdC"] = m_thresholdC;
    params["circularityMin"] = m_circularityMin;
    params["circularityMax"] = m_circularityMax;
    params["minArea"] = m_minArea;
    params["maxArea"] = m_maxArea;
    params["filterFinderMarks"] = m_filterFinderMarks;
    params["finderHoleRatio"] = m_finderHoleRatio;
    return params;
}

void CalibrationPlatePlugin::setParameters(const nlohmann::json& params)
{
    if (params.contains("plateType")) m_plateType = static_cast<PlateType>(params["plateType"].get<int>());
    if (params.contains("knownDiameterMm")) m_knownDiameterMm = params["knownDiameterMm"].get<double>();
    if (params.contains("applyCalibration")) m_applyCalibration = params["applyCalibration"].get<bool>();
    if (params.contains("blurSize")) m_blurSize = params["blurSize"].get<int>();
    if (params.contains("useCannyEdge")) m_useCannyEdge = params["useCannyEdge"].get<bool>();
    if (params.contains("cannyLow")) m_cannyLow = params["cannyLow"].get<int>();
    if (params.contains("cannyHigh")) m_cannyHigh = params["cannyHigh"].get<int>();
    if (params.contains("enhanceContrast")) m_enhanceContrast = params["enhanceContrast"].get<bool>();
    if (params.contains("thresholdBlockSize")) m_thresholdBlockSize = params["thresholdBlockSize"].get<int>();
    if (params.contains("thresholdC")) m_thresholdC = params["thresholdC"].get<double>();
    if (params.contains("circularityMin")) m_circularityMin = params["circularityMin"].get<double>();
    if (params.contains("circularityMax")) m_circularityMax = params["circularityMax"].get<double>();
    if (params.contains("minArea")) m_minArea = params["minArea"].get<int>();
    if (params.contains("maxArea")) m_maxArea = params["maxArea"].get<int>();
    if (params.contains("filterFinderMarks")) m_filterFinderMarks = params["filterFinderMarks"].get<bool>();
    if (params.contains("finderHoleRatio")) m_finderHoleRatio = params["finderHoleRatio"].get<double>();

    // Update UI
    if (m_plateTypeCombo) m_plateTypeCombo->setCurrentIndex(static_cast<int>(m_plateType));
    if (m_knownDiameterSpin) m_knownDiameterSpin->setValue(m_knownDiameterMm);
    if (m_applyCalibCheck) m_applyCalibCheck->setChecked(m_applyCalibration);
    if (m_enhanceContrastCheck) m_enhanceContrastCheck->setChecked(m_enhanceContrast);
    if (m_blurSizeSpin) m_blurSizeSpin->setValue(m_blurSize);
    if (m_useCannyCheck) m_useCannyCheck->setChecked(m_useCannyEdge);
    if (m_cannyLowSpin) m_cannyLowSpin->setValue(m_cannyLow);
    if (m_cannyHighSpin) m_cannyHighSpin->setValue(m_cannyHigh);
    if (m_threshBlockSpin) m_threshBlockSpin->setValue(m_thresholdBlockSize);
    if (m_threshCSpin) m_threshCSpin->setValue(m_thresholdC);
    if (m_circMinSpin) m_circMinSpin->setValue(m_circularityMin);
    if (m_circMaxSpin) m_circMaxSpin->setValue(m_circularityMax);
    if (m_minAreaSpin) m_minAreaSpin->setValue(m_minArea);
    if (m_maxAreaSpin) m_maxAreaSpin->setValue(m_maxArea);
    if (m_filterFinderCheck) m_filterFinderCheck->setChecked(m_filterFinderMarks);
    if (m_finderRatioSpin) m_finderRatioSpin->setValue(m_finderHoleRatio);

    updatePlateTypeUI();
}

nlohmann::json CalibrationPlatePlugin::getDefaultParameters() const
{
    nlohmann::json params;
    params["plateType"] = static_cast<int>(PlateType::Plate20mm);
    params["knownDiameterMm"] = 0.322;
    params["applyCalibration"] = false;
    params["blurSize"] = 5;
    params["useCannyEdge"] = true;
    params["cannyLow"] = 30;
    params["cannyHigh"] = 100;
    params["enhanceContrast"] = true;
    params["thresholdBlockSize"] = 51;
    params["thresholdC"] = 5.0;
    params["circularityMin"] = 0.80;
    params["circularityMax"] = 1.0;
    params["minArea"] = 500;
    params["maxArea"] = 100000;
    params["filterFinderMarks"] = true;
    params["finderHoleRatio"] = 0.3;
    return params;
}

ParameterSchema CalibrationPlatePlugin::getParameterSchema() const
{
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    // Plate Configuration
    std::vector<std::string> plateChoices = {
        "20mm Plate (0.322mm)",
        "40mm Plate (0.646mm)",
        "80mm Plate (1.290mm)",
        "Custom"
    };
    std::vector<int> plateValues = {0, 1, 2, 3};
    schema.parameters.push_back(ParameterDescriptor::createChoice(
        "plateType", "Plate Type",
        plateChoices, plateValues, 0,
        "MVTec calibration plate type",
        "Plate Configuration"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "knownDiameterMm", "Circle Diameter",
        0.001, 100.0, 0.322,
        "Known diameter of calibration circles in mm",
        "mm", 0.001,
        "Plate Configuration"
    ));

    schema.parameters.push_back(ParameterDescriptor::createBool(
        "applyCalibration", "Apply to System",
        false,
        "Automatically apply calibration to VisionManager after detection",
        "Plate Configuration"
    ));

    // Detection Parameters
    schema.parameters.push_back(ParameterDescriptor::createBool(
        "enhanceContrast", "Enhance Contrast",
        true,
        "Apply CLAHE contrast enhancement for low contrast images",
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "blurSize", "Blur Size",
        1, 31, 5,
        "Gaussian blur kernel size (odd number)",
        "px", 2,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createBool(
        "useCannyEdge", "Use Canny Edge",
        true,
        "Use Canny edge detection (recommended for low contrast)",
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "cannyLow", "Canny Low",
        1, 255, 30,
        "Canny edge low threshold",
        "", 5,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "cannyHigh", "Canny High",
        1, 255, 100,
        "Canny edge high threshold",
        "", 5,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "thresholdBlockSize", "Threshold Block",
        3, 201, 51,
        "Adaptive threshold block size (for non-Canny mode)",
        "px", 2,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "thresholdC", "Threshold C",
        -50.0, 50.0, 5.0,
        "Adaptive threshold constant (for non-Canny mode)",
        "", 1.0,
        "Detection"
    ));

    // Circle Filtering
    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "circularityMin", "Min Circularity",
        0.0, 1.0, 0.85,
        "Minimum circularity (1.0 = perfect circle)",
        "", 0.05,
        "Filtering"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "circularityMax", "Max Circularity",
        0.0, 1.5, 1.0,
        "Maximum circularity",
        "", 0.05,
        "Filtering"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "minArea", "Min Area",
        10, 100000, 500,
        "Minimum contour area in pixels",
        "px^2", 100,
        "Filtering"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxArea", "Max Area",
        100, 1000000, 50000,
        "Maximum contour area in pixels",
        "px^2", 1000,
        "Filtering"
    ));

    // Finder Mark Filtering
    schema.parameters.push_back(ParameterDescriptor::createBool(
        "filterFinderMarks", "Filter Finder Marks",
        true,
        "Exclude circles with center dots (finder marks)",
        "Finder Marks"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "finderHoleRatio", "Hole Ratio",
        0.1, 0.8, 0.3,
        "Ratio of inner hole to outer circle radius for finder detection",
        "", 0.05,
        "Finder Marks"
    ));

    return schema;
}

#endif // HAS_OPENCV
