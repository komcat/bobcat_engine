// HalconCircleDetectionPlugin.cpp
// Circle detection using Halcon library
#if defined(HAS_OPENCV) && defined(HAS_HALCON)

#include "HalconCircleDetectionPlugin.h"
#include <opencv2/imgproc.hpp>
#include <halconcpp/HalconCpp.h>
#include <iostream>

using namespace HalconCpp;

HalconCircleDetectionPlugin::HalconCircleDetectionPlugin()
    : VisionPluginBase("CircleDetect (Halcon)", "PR", "Detect circles using Halcon's blob analysis and circle fitting")
{
}

HalconCircleDetectionPlugin::~HalconCircleDetectionPlugin()
{
}

bool HalconCircleDetectionPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result)
{
    if (input.empty()) {
        m_lastError = "Input image is empty";
        return false;
    }

    try {
        // Convert OpenCV Mat to grayscale if needed
        cv::Mat gray;
        if (input.channels() == 3) {
            cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
        } else if (input.channels() == 1) {
            gray = input;
        } else {
            m_lastError = "Unsupported number of channels: " + std::to_string(input.channels());
            return false;
        }

        // Ensure continuous data for Halcon
        if (!gray.isContinuous()) {
            gray = gray.clone();
        }

        // Create Halcon image from OpenCV Mat data
        HImage halconImage;
        halconImage.GenImage1("byte", static_cast<Hlong>(gray.cols), static_cast<Hlong>(gray.rows), static_cast<void*>(gray.data));

        // Step 1: Apply binary threshold first
        // Polarity: 0 = Dark circles (pixels 0 to threshold become white), 1 = Bright circles
        HRegion thresholdRegion;
        if (m_polarity == 0) {
            // Dark circles: select dark pixels
            thresholdRegion = halconImage.Threshold(0, m_threshold);
        } else {
            // Bright circles: select bright pixels
            thresholdRegion = halconImage.Threshold(m_threshold, 255);
        }

        // Create binary image from threshold region
        HImage binaryImage = thresholdRegion.RegionToBin(255, 0, gray.cols, gray.rows);

        // Step 2: Apply Canny edge detection on binary image
        // EdgesSubPix extracts sub-pixel precise edges
        HXLDCont edges = binaryImage.EdgesSubPix("canny", 1.5, 15, 30);

        // Check if we have any edges
        Hlong numContours = edges.CountObj();
        std::cout << "[HalconCircleDetection] Found " << numContours << " edge contours" << std::endl;

        if (numContours == 0) {
            // No edges found - return success but with 0 circles
            if (input.channels() == 1) {
                cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
            } else {
                output = input.clone();
            }
            result.addMeasurement("circle_count", 0.0);
            result.addMetadata("algorithm", "Halcon_EdgeBased");
            result.addMetadata("note", "No edges detected");
            std::cout << "[HalconCircleDetection] No edges found" << std::endl;
            return true;
        }

        // Step 3: Select contours by length (filter out very short contours)
        HXLDCont selectedContours = edges.SelectContoursXld("contour_length",
            static_cast<double>(m_minRadius * 2), static_cast<double>(m_maxRadius * 6.28), 0, 0);

        numContours = selectedContours.CountObj();
        std::cout << "[HalconCircleDetection] After length filter: " << numContours << " contours" << std::endl;

        if (numContours == 0) {
            if (input.channels() == 1) {
                cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
            } else {
                output = input.clone();
            }
            result.addMeasurement("circle_count", 0.0);
            result.addMetadata("algorithm", "Halcon_EdgeBased");
            result.addMetadata("note", "No contours of suitable length");
            return true;
        }

        // Step 4: Fit circles to each contour individually
        HTuple allRows, allCols, allRadii;

        for (Hlong i = 1; i <= numContours; i++) {
            HXLDCont singleContour = selectedContours.SelectObj(i);

            // Get contour length
            HTuple contourLength = singleContour.LengthXld();
            if (contourLength.D() < m_minRadius * 2) {
                continue;
            }

            try {
                HTuple row, col, radius, startPhi, endPhi, pointOrder;
                singleContour.FitCircleContourXld("algebraic", -1, 0, 0, 3, 2.0,
                    &row, &col, &radius, &startPhi, &endPhi, &pointOrder);

                // Only add valid circles
                if (radius.Length() > 0 && radius.D() > 0 && !std::isnan(radius.D())) {
                    allRows.Append(row);
                    allCols.Append(col);
                    allRadii.Append(radius);
                }
            }
            catch (HException&) {
                // Skip contours that can't be fitted
                continue;
            }
        }

        HTuple rows = allRows;
        HTuple cols = allCols;
        HTuple radii = allRadii;

        // Prepare output image
        if (input.channels() == 1) {
            cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
        } else {
            output = input.clone();
        }

        // Process fitted circles
        Hlong numCircles = rows.Length();
        int circleCount = 0;

        for (Hlong i = 0; i < numCircles && circleCount < m_maxCircles; i++) {
            double centerRow = rows[i].D();
            double centerCol = cols[i].D();
            double circleRadius = radii[i].D();

            // Filter by radius
            if (circleRadius < m_minRadius || circleRadius > m_maxRadius) {
                continue;
            }

            // Filter out invalid circles (NaN or very small)
            if (circleRadius < 1.0 || std::isnan(centerRow) || std::isnan(centerCol)) {
                continue;
            }

            // Create result (Halcon uses row,col - convert to x,y)
            cv::Point2d center(centerCol, centerRow);

            // Add to result
            CircleResult circleResult;
            circleResult.name = "circle_" + std::to_string(circleCount);
            circleResult.centerPx = center;
            circleResult.radiusPx = circleRadius;
            circleResult.diameterPx = circleRadius * 2.0;
            circleResult.confidence = 1.0;
            result.circles.push_back(circleResult);

            // Add overlay
            cv::Scalar color(0, 255, 0);  // Green
            result.addCircleOverlay(center, circleRadius, color, 2);
            result.addPointOverlay(center, cv::Scalar(0, 0, 255), 5);  // Red center

            // Add text label
            std::string label = "C" + std::to_string(circleCount) + " r=" + std::to_string(static_cast<int>(circleRadius));
            result.addTextOverlay(label, cv::Point2d(center.x - circleRadius, center.y - circleRadius - 10),
                                 cv::Scalar(255, 255, 0), 1);

            circleCount++;
        }

        // Add measurements
        result.addMeasurement("circle_count", static_cast<double>(circleCount));

        if (circleCount > 0) {
            double totalRadius = 0;
            for (const auto& c : result.circles) {
                totalRadius += c.radiusPx;
            }
            result.addMeasurement("avg_radius_px", totalRadius / circleCount);
        }

        // Add metadata
        result.addMetadata("algorithm", "Halcon_EdgeBased");
        result.addMetadata("threshold", std::to_string(m_threshold));
        result.addMetadata("polarity", m_polarity == 0 ? "dark" : "bright");

        std::cout << "[HalconCircleDetection] Found " << circleCount << " circles" << std::endl;
        return true;
    }
    catch (HException& ex) {
        m_lastError = "Halcon error: " + std::string(ex.ErrorMessage().Text());
        std::cerr << "[HalconCircleDetection] " << m_lastError << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        m_lastError = "Error: " + std::string(e.what());
        std::cerr << "[HalconCircleDetection] " << m_lastError << std::endl;
        return false;
    }
}

bool HalconCircleDetectionPlugin::validate() const
{
    if (m_minRadius < 0) {
        m_lastError = "minRadius cannot be negative";
        return false;
    }
    if (m_maxRadius <= m_minRadius) {
        m_lastError = "maxRadius must be greater than minRadius";
        return false;
    }
    if (m_threshold < 0 || m_threshold > 255) {
        m_lastError = "threshold must be between 0 and 255";
        return false;
    }
    if (m_maxCircles <= 0) {
        m_lastError = "maxCircles must be positive";
        return false;
    }
    if (m_circularity < 0 || m_circularity > 1.0) {
        m_lastError = "circularity must be between 0 and 1";
        return false;
    }
    return true;
}

QWidget* HalconCircleDetectionPlugin::createConfigWidget(QWidget* parent)
{
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    QGroupBox* paramsGroup = new QGroupBox("Halcon Circle Detection Parameters");
    QFormLayout* formLayout = new QFormLayout(paramsGroup);

    // Polarity (Dark or Bright circles)
    m_polarityCombo = new QComboBox();
    m_polarityCombo->addItem("Dark circles", 0);
    m_polarityCombo->addItem("Bright circles", 1);
    m_polarityCombo->setCurrentIndex(m_polarity);
    m_polarityCombo->setToolTip("Select whether to detect dark circles on light background or bright circles on dark background");
    formLayout->addRow("Polarity:", m_polarityCombo);

    // Threshold
    m_thresholdSpin = new QSpinBox();
    m_thresholdSpin->setRange(0, 255);
    m_thresholdSpin->setSingleStep(5);
    m_thresholdSpin->setValue(m_threshold);
    m_thresholdSpin->setToolTip("Gray value threshold for segmentation (0-255).\nDark: pixels 0-threshold. Bright: pixels threshold-255.");
    formLayout->addRow("Threshold:", m_thresholdSpin);

    // Circularity
    m_circularitySpin = new QDoubleSpinBox();
    m_circularitySpin->setRange(0.0, 1.0);
    m_circularitySpin->setSingleStep(0.05);
    m_circularitySpin->setValue(m_circularity);
    m_circularitySpin->setToolTip("Minimum circularity (0-1, where 1 is perfect circle)");
    formLayout->addRow("Min Circularity:", m_circularitySpin);

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
    QObject::connect(m_polarityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int val) { m_polarity = val; });
    QObject::connect(m_thresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_threshold = val; });
    QObject::connect(m_circularitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_circularity = val; });
    QObject::connect(m_minRadiusSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_minRadius = val; });
    QObject::connect(m_maxRadiusSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxRadius = val; });
    QObject::connect(m_maxCirclesSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxCircles = val; });

    return m_configWidget;
}

nlohmann::json HalconCircleDetectionPlugin::getParameters() const
{
    nlohmann::json params;
    params["polarity"] = m_polarity;
    params["threshold"] = m_threshold;
    params["circularity"] = m_circularity;
    params["minRadius"] = m_minRadius;
    params["maxRadius"] = m_maxRadius;
    params["maxCircles"] = m_maxCircles;
    return params;
}

void HalconCircleDetectionPlugin::setParameters(const nlohmann::json& params)
{
    if (params.contains("polarity")) m_polarity = params["polarity"].get<int>();
    if (params.contains("threshold")) m_threshold = params["threshold"].get<int>();
    if (params.contains("circularity")) m_circularity = params["circularity"].get<double>();
    if (params.contains("minRadius")) m_minRadius = params["minRadius"].get<int>();
    if (params.contains("maxRadius")) m_maxRadius = params["maxRadius"].get<int>();
    if (params.contains("maxCircles")) m_maxCircles = params["maxCircles"].get<int>();

    // Update UI if widget exists
    if (m_polarityCombo) m_polarityCombo->setCurrentIndex(m_polarity);
    if (m_thresholdSpin) m_thresholdSpin->setValue(m_threshold);
    if (m_circularitySpin) m_circularitySpin->setValue(m_circularity);
    if (m_minRadiusSpin) m_minRadiusSpin->setValue(m_minRadius);
    if (m_maxRadiusSpin) m_maxRadiusSpin->setValue(m_maxRadius);
    if (m_maxCirclesSpin) m_maxCirclesSpin->setValue(m_maxCircles);
}

nlohmann::json HalconCircleDetectionPlugin::getDefaultParameters() const
{
    nlohmann::json params;
    params["polarity"] = 0;  // Dark circles by default
    params["threshold"] = 80;
    params["circularity"] = 0.8;
    params["minRadius"] = 10;
    params["maxRadius"] = 200;
    params["maxCircles"] = 10;
    return params;
}

ParameterSchema HalconCircleDetectionPlugin::getParameterSchema() const
{
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    // Segmentation Parameters
    schema.parameters.push_back(ParameterDescriptor::createChoice(
        "polarity", "Polarity",
        {"Dark circles", "Bright circles"}, {0, 1}, 0,
        "Dark: detect dark circles on light background. "
        "Bright: detect bright circles on dark background.",
        "Segmentation"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "threshold", "Threshold",
        0, 255, 80,
        "Gray value threshold for segmentation. "
        "Dark mode: pixels 0-threshold. Bright mode: pixels threshold-255.",
        "", 5,
        "Segmentation"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "circularity", "Min Circularity",
        0.0, 1.0, 0.8,
        "Minimum circularity to consider a region as a circle. "
        "1.0 = perfect circle, lower values accept more elliptical shapes.",
        "", 0.05,
        "Segmentation"
    ));

    // Size Constraints
    schema.parameters.push_back(ParameterDescriptor::createInt(
        "minRadius", "Min Radius",
        0, 10000, 10,
        "Minimum circle radius to detect.",
        "px", 5,
        "Size Constraints"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxRadius", "Max Radius",
        1, 10000, 200,
        "Maximum circle radius to detect.",
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

#endif // HAS_OPENCV && HAS_HALCON
