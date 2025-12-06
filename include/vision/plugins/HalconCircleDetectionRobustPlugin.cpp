// HalconCircleDetectionRobustPlugin.cpp
// Robust circle detection using Halcon - Blob analysis with largest contour fitting
#if defined(HAS_OPENCV) && defined(HAS_HALCON)

#include "HalconCircleDetectionRobustPlugin.h"
#include <opencv2/imgproc.hpp>
#include <halconcpp/HalconCpp.h>
#include <iostream>

using namespace HalconCpp;

HalconCircleDetectionRobustPlugin::HalconCircleDetectionRobustPlugin()
    : VisionPluginBase("CircleDetect Robust (Halcon)", "PR",
                       "Robust circle detection using blob analysis and Tukey circle fitting")
{
}

HalconCircleDetectionRobustPlugin::~HalconCircleDetectionRobustPlugin()
{
}

bool HalconCircleDetectionRobustPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result)
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
        halconImage.GenImage1("byte", static_cast<Hlong>(gray.cols), static_cast<Hlong>(gray.rows),
                              static_cast<void*>(gray.data));

        // Step 1: Threshold for dark or bright regions
        HRegion thresholdRegion;
        if (m_polarity == 0) {
            // Dark blobs: threshold(GrayImage, Region, 0, thresholdMax)
            thresholdRegion = halconImage.Threshold(m_thresholdMin, m_thresholdMax);
        } else {
            // Bright blobs: threshold for bright regions
            thresholdRegion = halconImage.Threshold(m_thresholdMax, 255);
        }

        // Step 2: Morphological closing to fill gaps
        // closing_circle(Region, Region, closingRadius)
        HRegion closedRegion = thresholdRegion.ClosingCircle(m_closingRadius);

        // Step 3: Connection - separate touching regions
        HRegion connectedRegions = closedRegion.Connection();

        // Step 4: Select regions by area and circularity
        // select_shape(ConnectedRegions, SelectedRegions, ['area', 'circularity'], 'and',
        //              [minArea, minCircularity], [maxArea, 1.0])
        HRegion areaFiltered = connectedRegions.SelectShape("area", "and",
                                                             static_cast<double>(m_minArea),
                                                             static_cast<double>(m_maxArea));
        HRegion selectedRegions = areaFiltered.SelectShape("circularity", "and",
                                                            m_minCircularity, 1.0);

        // Check if we have any regions
        Hlong numRegions = selectedRegions.CountObj();
        std::cout << "[HalconCircleRobust] Found " << numRegions << " circular regions" << std::endl;

        // Prepare output image
        if (input.channels() == 1) {
            cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
        } else {
            output = input.clone();
        }

        if (numRegions == 0) {
            result.addMeasurement("circle_count", 0.0);
            result.addMetadata("algorithm", "Halcon_RobustBlob");
            result.addMetadata("note", "No circular regions found");
            std::cout << "[HalconCircleRobust] No circular regions found" << std::endl;
            return true;
        }

        // Step 5: Select the largest region
        HRegion largestRegion;
        if (numRegions > 1) {
            // select_shape_std(SelectedRegions, LargestRegion, 'max_area', 70)
            largestRegion = selectedRegions.SelectShapeStd("max_area", 70);
        } else {
            largestRegion = selectedRegions;
        }

        // Step 6: Generate contour from region
        // gen_contour_region_xld(LargestRegion, Contour, 'border')
        HXLDCont contour = largestRegion.GenContourRegionXld("border");

        // Check contour length
        HTuple contourLength = contour.LengthXld();
        std::cout << "[HalconCircleRobust] Contour length: " << contourLength.D() << std::endl;

        if (contourLength.D() < 10) {
            result.addMeasurement("circle_count", 0.0);
            result.addMetadata("algorithm", "Halcon_RobustBlob");
            result.addMetadata("note", "Contour too short");
            return true;
        }

        // Step 7: Fit circle using robust Tukey estimator
        // fit_circle_contour_xld(Contour, 'atukey'/'geotukey', -1, 2, 0, 10, 1,
        //                        Row, Column, Radius, StartPhi, EndPhi, PointOrder)
        HTuple row, col, radius, startPhi, endPhi, pointOrder;

        std::string fitMethodStr = (m_fitMethod == 0) ? "atukey" : "geotukey";

        try {
            contour.FitCircleContourXld(fitMethodStr.c_str(), -1, 2, 0, 10, 1,
                                        &row, &col, &radius, &startPhi, &endPhi, &pointOrder);
        }
        catch (HException& ex) {
            result.addMeasurement("circle_count", 0.0);
            result.addMetadata("algorithm", "Halcon_RobustBlob");
            result.addMetadata("note", "Circle fitting failed: " + std::string(ex.ErrorMessage().Text()));
            std::cout << "[HalconCircleRobust] Circle fitting failed: " << ex.ErrorMessage().Text() << std::endl;
            return true;
        }

        // Validate result
        if (radius.Length() == 0 || radius.D() <= 0 || std::isnan(row.D()) || std::isnan(col.D())) {
            result.addMeasurement("circle_count", 0.0);
            result.addMetadata("algorithm", "Halcon_RobustBlob");
            result.addMetadata("note", "Invalid circle fit result");
            return true;
        }

        double centerRow = row.D();
        double centerCol = col.D();
        double circleRadius = radius.D();

        std::cout << "[HalconCircleRobust] Detected circle: center=(" << centerCol << ", " << centerRow
                  << "), radius=" << circleRadius << std::endl;

        // Create result (Halcon uses row,col - convert to x,y for OpenCV)
        cv::Point2d center(centerCol, centerRow);

        // Add to result
        CircleResult circleResult;
        circleResult.name = "circle_0";
        circleResult.centerPx = center;
        circleResult.radiusPx = circleRadius;
        circleResult.diameterPx = circleRadius * 2.0;
        circleResult.confidence = 1.0;
        result.circles.push_back(circleResult);

        // Add overlay - main circle (green)
        result.addCircleOverlay(center, circleRadius, cv::Scalar(0, 255, 0), 2);

        // Add center point (red)
        result.addPointOverlay(center, cv::Scalar(0, 0, 255), 5);

        // Add cross at center
        double crossSize = 20;
        result.addLineOverlay(cv::Point2d(center.x - crossSize, center.y),
                             cv::Point2d(center.x + crossSize, center.y),
                             cv::Scalar(0, 255, 255), 1);
        result.addLineOverlay(cv::Point2d(center.x, center.y - crossSize),
                             cv::Point2d(center.x, center.y + crossSize),
                             cv::Scalar(0, 255, 255), 1);

        // Add text label
        std::string label = "r=" + std::to_string(static_cast<int>(circleRadius)) + "px";
        result.addTextOverlay(label, cv::Point2d(center.x - circleRadius, center.y - circleRadius - 10),
                             cv::Scalar(255, 255, 0), 1);

        // Add measurements
        result.addMeasurement("circle_count", 1.0);
        result.addMeasurement("center_x", centerCol);
        result.addMeasurement("center_y", centerRow);
        result.addMeasurement("radius_px", circleRadius);
        result.addMeasurement("diameter_px", circleRadius * 2.0);

        // Add metadata
        result.addMetadata("algorithm", "Halcon_RobustBlob");
        result.addMetadata("fit_method", fitMethodStr);
        result.addMetadata("polarity", m_polarity == 0 ? "dark" : "bright");
        result.addMetadata("threshold_range", std::to_string(m_thresholdMin) + "-" + std::to_string(m_thresholdMax));
        result.addMetadata("closing_radius", std::to_string(m_closingRadius));

        std::cout << "[HalconCircleRobust] Successfully detected 1 circle" << std::endl;
        return true;
    }
    catch (HException& ex) {
        m_lastError = "Halcon error: " + std::string(ex.ErrorMessage().Text());
        std::cerr << "[HalconCircleRobust] " << m_lastError << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        m_lastError = "Error: " + std::string(e.what());
        std::cerr << "[HalconCircleRobust] " << m_lastError << std::endl;
        return false;
    }
}

bool HalconCircleDetectionRobustPlugin::validate() const
{
    if (m_thresholdMin < 0 || m_thresholdMin > 255) {
        m_lastError = "thresholdMin must be between 0 and 255";
        return false;
    }
    if (m_thresholdMax < 0 || m_thresholdMax > 255) {
        m_lastError = "thresholdMax must be between 0 and 255";
        return false;
    }
    if (m_thresholdMin >= m_thresholdMax) {
        m_lastError = "thresholdMin must be less than thresholdMax";
        return false;
    }
    if (m_closingRadius < 0) {
        m_lastError = "closingRadius cannot be negative";
        return false;
    }
    if (m_minArea < 0) {
        m_lastError = "minArea cannot be negative";
        return false;
    }
    if (m_maxArea <= m_minArea) {
        m_lastError = "maxArea must be greater than minArea";
        return false;
    }
    if (m_minCircularity < 0 || m_minCircularity > 1.0) {
        m_lastError = "minCircularity must be between 0 and 1";
        return false;
    }
    return true;
}

QWidget* HalconCircleDetectionRobustPlugin::createConfigWidget(QWidget* parent)
{
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    QGroupBox* paramsGroup = new QGroupBox("Robust Circle Detection Parameters");
    QFormLayout* formLayout = new QFormLayout(paramsGroup);

    // Polarity
    m_polarityCombo = new QComboBox();
    m_polarityCombo->addItem("Dark blobs", 0);
    m_polarityCombo->addItem("Bright blobs", 1);
    m_polarityCombo->setCurrentIndex(m_polarity);
    m_polarityCombo->setToolTip("Detect dark blobs on light background or bright blobs on dark background");
    formLayout->addRow("Polarity:", m_polarityCombo);

    // Threshold Min
    m_thresholdMinSpin = new QSpinBox();
    m_thresholdMinSpin->setRange(0, 254);
    m_thresholdMinSpin->setSingleStep(5);
    m_thresholdMinSpin->setValue(m_thresholdMin);
    m_thresholdMinSpin->setToolTip("Minimum gray value for threshold");
    formLayout->addRow("Threshold Min:", m_thresholdMinSpin);

    // Threshold Max
    m_thresholdMaxSpin = new QSpinBox();
    m_thresholdMaxSpin->setRange(1, 255);
    m_thresholdMaxSpin->setSingleStep(5);
    m_thresholdMaxSpin->setValue(m_thresholdMax);
    m_thresholdMaxSpin->setToolTip("Maximum gray value for threshold");
    formLayout->addRow("Threshold Max:", m_thresholdMaxSpin);

    // Closing Radius
    m_closingRadiusSpin = new QDoubleSpinBox();
    m_closingRadiusSpin->setRange(0.0, 50.0);
    m_closingRadiusSpin->setSingleStep(0.5);
    m_closingRadiusSpin->setValue(m_closingRadius);
    m_closingRadiusSpin->setToolTip("Morphological closing radius to fill gaps in the blob");
    formLayout->addRow("Closing Radius:", m_closingRadiusSpin);

    // Min Area
    m_minAreaSpin = new QSpinBox();
    m_minAreaSpin->setRange(0, 9999999);
    m_minAreaSpin->setSingleStep(500);
    m_minAreaSpin->setValue(m_minArea);
    m_minAreaSpin->setToolTip("Minimum blob area in pixels");
    formLayout->addRow("Min Area (px):", m_minAreaSpin);

    // Max Area
    m_maxAreaSpin = new QSpinBox();
    m_maxAreaSpin->setRange(1, 9999999);
    m_maxAreaSpin->setSingleStep(10000);
    m_maxAreaSpin->setValue(m_maxArea);
    m_maxAreaSpin->setToolTip("Maximum blob area in pixels");
    formLayout->addRow("Max Area (px):", m_maxAreaSpin);

    // Min Circularity
    m_minCircularitySpin = new QDoubleSpinBox();
    m_minCircularitySpin->setRange(0.0, 1.0);
    m_minCircularitySpin->setSingleStep(0.05);
    m_minCircularitySpin->setValue(m_minCircularity);
    m_minCircularitySpin->setToolTip("Minimum circularity (0-1, where 1 is perfect circle)");
    formLayout->addRow("Min Circularity:", m_minCircularitySpin);

    // Fit Method
    m_fitMethodCombo = new QComboBox();
    m_fitMethodCombo->addItem("Algebraic Tukey (atukey)", 0);
    m_fitMethodCombo->addItem("Geometric Tukey (geotukey)", 1);
    m_fitMethodCombo->setCurrentIndex(m_fitMethod);
    m_fitMethodCombo->setToolTip("Circle fitting method:\n"
                                  "Algebraic Tukey: Faster, good for most cases\n"
                                  "Geometric Tukey: More accurate, handles outliers better");
    formLayout->addRow("Fit Method:", m_fitMethodCombo);

    mainLayout->addWidget(paramsGroup);
    mainLayout->addStretch();

    // Connect signals
    QObject::connect(m_polarityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int val) { m_polarity = val; });
    QObject::connect(m_thresholdMinSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_thresholdMin = val; });
    QObject::connect(m_thresholdMaxSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_thresholdMax = val; });
    QObject::connect(m_closingRadiusSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_closingRadius = val; });
    QObject::connect(m_minAreaSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_minArea = val; });
    QObject::connect(m_maxAreaSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxArea = val; });
    QObject::connect(m_minCircularitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_minCircularity = val; });
    QObject::connect(m_fitMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int val) { m_fitMethod = val; });

    return m_configWidget;
}

nlohmann::json HalconCircleDetectionRobustPlugin::getParameters() const
{
    nlohmann::json params;
    params["polarity"] = m_polarity;
    params["thresholdMin"] = m_thresholdMin;
    params["thresholdMax"] = m_thresholdMax;
    params["closingRadius"] = m_closingRadius;
    params["minArea"] = m_minArea;
    params["maxArea"] = m_maxArea;
    params["minCircularity"] = m_minCircularity;
    params["fitMethod"] = m_fitMethod;
    return params;
}

void HalconCircleDetectionRobustPlugin::setParameters(const nlohmann::json& params)
{
    if (params.contains("polarity")) m_polarity = params["polarity"].get<int>();
    if (params.contains("thresholdMin")) m_thresholdMin = params["thresholdMin"].get<int>();
    if (params.contains("thresholdMax")) m_thresholdMax = params["thresholdMax"].get<int>();
    if (params.contains("closingRadius")) m_closingRadius = params["closingRadius"].get<double>();
    if (params.contains("minArea")) m_minArea = params["minArea"].get<int>();
    if (params.contains("maxArea")) m_maxArea = params["maxArea"].get<int>();
    if (params.contains("minCircularity")) m_minCircularity = params["minCircularity"].get<double>();
    if (params.contains("fitMethod")) m_fitMethod = params["fitMethod"].get<int>();

    // Update UI if widget exists
    if (m_polarityCombo) m_polarityCombo->setCurrentIndex(m_polarity);
    if (m_thresholdMinSpin) m_thresholdMinSpin->setValue(m_thresholdMin);
    if (m_thresholdMaxSpin) m_thresholdMaxSpin->setValue(m_thresholdMax);
    if (m_closingRadiusSpin) m_closingRadiusSpin->setValue(m_closingRadius);
    if (m_minAreaSpin) m_minAreaSpin->setValue(m_minArea);
    if (m_maxAreaSpin) m_maxAreaSpin->setValue(m_maxArea);
    if (m_minCircularitySpin) m_minCircularitySpin->setValue(m_minCircularity);
    if (m_fitMethodCombo) m_fitMethodCombo->setCurrentIndex(m_fitMethod);
}

nlohmann::json HalconCircleDetectionRobustPlugin::getDefaultParameters() const
{
    nlohmann::json params;
    params["polarity"] = 0;  // Dark blobs
    params["thresholdMin"] = 0;
    params["thresholdMax"] = 100;
    params["closingRadius"] = 10.5;
    params["minArea"] = 5000;
    params["maxArea"] = 999999;
    params["minCircularity"] = 0.7;
    params["fitMethod"] = 0;  // atukey
    return params;
}

ParameterSchema HalconCircleDetectionRobustPlugin::getParameterSchema() const
{
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    // Segmentation Parameters
    schema.parameters.push_back(ParameterDescriptor::createChoice(
        "polarity", "Polarity",
        {"Dark blobs", "Bright blobs"}, {0, 1}, 0,
        "Detect dark blobs on light background or vice versa.",
        "Segmentation"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "thresholdMin", "Threshold Min",
        0, 254, 0,
        "Minimum gray value for blob segmentation.",
        "", 5,
        "Segmentation"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "thresholdMax", "Threshold Max",
        1, 255, 100,
        "Maximum gray value for blob segmentation.",
        "", 5,
        "Segmentation"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "closingRadius", "Closing Radius",
        0.0, 50.0, 10.5,
        "Morphological closing radius to fill gaps in the blob edge.",
        "px", 0.5,
        "Morphology"
    ));

    // Blob Selection
    schema.parameters.push_back(ParameterDescriptor::createInt(
        "minArea", "Min Area",
        0, 9999999, 5000,
        "Minimum blob area in pixels.",
        "px", 500,
        "Blob Selection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxArea", "Max Area",
        1, 9999999, 999999,
        "Maximum blob area in pixels.",
        "px", 10000,
        "Blob Selection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "minCircularity", "Min Circularity",
        0.0, 1.0, 0.7,
        "Minimum circularity to consider a blob as circular (1.0 = perfect circle).",
        "", 0.05,
        "Blob Selection"
    ));

    // Circle Fitting
    schema.parameters.push_back(ParameterDescriptor::createChoice(
        "fitMethod", "Fit Method",
        {"Algebraic Tukey", "Geometric Tukey"}, {0, 1}, 0,
        "Algebraic: Faster, good for most cases. Geometric: More accurate with outliers.",
        "Circle Fitting"
    ));

    return schema;
}

#endif // HAS_OPENCV && HAS_HALCON
