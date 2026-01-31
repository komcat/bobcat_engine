// HalconLineDetectionPlugin.cpp
// Line detection using Halcon library - Edge-based straight line detection

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

// Prevent Windows min/max macros from conflicting with std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "HalconLineDetectionPlugin.h"
#include "../VisionManager.h"
#include <halconcpp/HalconCpp.h>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <cmath>

using namespace HalconCpp;

HalconLineDetectionPlugin::HalconLineDetectionPlugin()
    : VisionPluginBase("LineDetect (Halcon)", "PR",
                       "Detect straight lines using Halcon edge detection and line fitting")
{
}

HalconLineDetectionPlugin::~HalconLineDetectionPlugin() {
    // Config widget is owned by Qt parent, don't delete
}

bool HalconLineDetectionPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) {
    if (input.empty()) {
        m_lastError = "Input image is empty";
        return false;
    }

    try {
        // Convert to grayscale if needed
        cv::Mat gray;
        if (input.channels() == 3) {
            cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
        } else if (input.channels() == 4) {
            cv::cvtColor(input, gray, cv::COLOR_BGRA2GRAY);
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

        // Prepare output image
        if (input.channels() == 1) {
            cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
        } else if (input.channels() == 4) {
            cv::cvtColor(input, output, cv::COLOR_BGRA2BGR);
        } else {
            output = input.clone();
        }

        // Convert OpenCV Mat to Halcon HImage
        HImage halconImage;
        halconImage.GenImage1("byte", static_cast<Hlong>(gray.cols), static_cast<Hlong>(gray.rows),
                              static_cast<void*>(gray.data));

        // Determine edge filter based on polarity
        HTuple edgeType;
        switch (m_polarity) {
            case 1:  // Dark to Bright
                edgeType = "positive";
                break;
            case 2:  // Bright to Dark
                edgeType = "negative";
                break;
            default: // All edges
                edgeType = "all";
                break;
        }

        // Apply sub-pixel edge detection using Canny
        HObject edges;
        EdgesSubPix(halconImage, &edges, "canny", m_sigma, m_lowThreshold, m_highThreshold);

        // Segment contours into straight line segments
        HObject lineSegments;
        SegmentContoursXld(edges, &lineSegments, "lines", 5, 4, 2);

        // Select contours by length
        HObject longLines;
        SelectContoursXld(lineSegments, &longLines, "contour_length",
                          static_cast<double>(m_minLength), 100000.0, -0.5, 0.5);

        // Count the number of contours
        HTuple numContours;
        CountObj(longLines, &numContours);
        int contourCount = static_cast<int>(numContours.D());

        if (VisionManager::instance().isVerboseLogging()) {
            std::cout << "[HalconLineDetection] Found " << contourCount << " line segments after filtering" << std::endl;
        }

        // Fit lines and collect results
        std::vector<std::tuple<double, double, double, double, double>> detectedLines; // x1, y1, x2, y2, length

        for (int i = 1; i <= contourCount && static_cast<int>(detectedLines.size()) < m_maxLines; i++) {
            try {
                HObject singleContour;
                SelectObj(longLines, &singleContour, i);

                // Fit line to contour
                HTuple rowBegin, colBegin, rowEnd, colEnd, nr, nc, dist;

                if (m_fitMethod == 1) {
                    // Robust Tukey fitting
                    FitLineContourXld(singleContour, "tukey", -1, 0, 5, 2,
                                      &rowBegin, &colBegin, &rowEnd, &colEnd, &nr, &nc, &dist);
                } else {
                    // Standard regression
                    FitLineContourXld(singleContour, "regression", -1, 0, 5, 2,
                                      &rowBegin, &colBegin, &rowEnd, &colEnd, &nr, &nc, &dist);
                }

                double x1 = colBegin.D();
                double y1 = rowBegin.D();
                double x2 = colEnd.D();
                double y2 = rowEnd.D();

                // Calculate line length
                double length = std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));

                if (length >= m_minLength) {
                    detectedLines.push_back(std::make_tuple(x1, y1, x2, y2, length));
                }
            } catch (HException& e) {
                // Skip contours that fail to fit
                continue;
            }
        }

        // Sort by length (longest first)
        std::sort(detectedLines.begin(), detectedLines.end(),
                  [](const auto& a, const auto& b) { return std::get<4>(a) > std::get<4>(b); });

        // Limit to max lines
        if (static_cast<int>(detectedLines.size()) > m_maxLines) {
            detectedLines.resize(m_maxLines);
        }

        // Store results
        result.addMeasurement("line_count", static_cast<double>(detectedLines.size()));

        if (!detectedLines.empty()) {
            // Store first (longest) line details
            auto& firstLine = detectedLines[0];
            double x1 = std::get<0>(firstLine);
            double y1 = std::get<1>(firstLine);
            double x2 = std::get<2>(firstLine);
            double y2 = std::get<3>(firstLine);
            double length = std::get<4>(firstLine);

            // Calculate line angle
            double angle = std::atan2(y2 - y1, x2 - x1) * 180.0 / CV_PI;

            // Calculate midpoint
            double midX = (x1 + x2) / 2.0;
            double midY = (y1 + y2) / 2.0;

            result.addMeasurement("line1_x1", x1);
            result.addMeasurement("line1_y1", y1);
            result.addMeasurement("line1_x2", x2);
            result.addMeasurement("line1_y2", y2);
            result.addMeasurement("line1_length", length);
            result.addMeasurement("line1_angle", angle);
            result.addMeasurement("line1_mid_x", midX);
            result.addMeasurement("line1_mid_y", midY);

            // Draw all detected lines using overlays
            int lineIdx = 1;
            for (const auto& line : detectedLines) {
                double lx1 = std::get<0>(line);
                double ly1 = std::get<1>(line);
                double lx2 = std::get<2>(line);
                double ly2 = std::get<3>(line);
                double len = std::get<4>(line);

                // Draw line in green
                result.addLineOverlay(cv::Point2d(lx1, ly1), cv::Point2d(lx2, ly2),
                                     cv::Scalar(0, 255, 0), 2);

                // Draw endpoints - blue at start, red at end
                result.addPointOverlay(cv::Point2d(lx1, ly1), cv::Scalar(255, 0, 0), 4);
                result.addPointOverlay(cv::Point2d(lx2, ly2), cv::Scalar(0, 0, 255), 4);

                // Draw length label at midpoint
                double mx = (lx1 + lx2) / 2.0;
                double my = (ly1 + ly2) / 2.0;
                std::string label = "L" + std::to_string(lineIdx) + "=" + std::to_string(static_cast<int>(len)) + "px";
                result.addTextOverlay(label, cv::Point2d(mx + 5, my - 5), cv::Scalar(0, 255, 255), 1);

                lineIdx++;
            }

            if (VisionManager::instance().isVerboseLogging()) {
                std::cout << "[HalconLineDetection] Result: " << detectedLines.size() << " lines detected" << std::endl;
                std::cout << "[HalconLineDetection] Longest line: (" << x1 << "," << y1 << ") to ("
                          << x2 << "," << y2 << "), length=" << length << "px, angle=" << angle << " deg" << std::endl;
            }
        } else {
            result.addMetadata("note", "No lines detected");
            if (VisionManager::instance().isVerboseLogging()) {
                std::cout << "[HalconLineDetection] No lines found matching criteria" << std::endl;
            }
        }

        // Store metadata
        result.addMetadata("algorithm", "Halcon_EdgeLine");
        result.addMetadata("sigma", std::to_string(m_sigma));
        result.addMetadata("threshold_low", std::to_string(m_lowThreshold));
        result.addMetadata("threshold_high", std::to_string(m_highThreshold));
        result.addMetadata("min_length", std::to_string(m_minLength));
        result.addMetadata("fit_method", (m_fitMethod == 1) ? "tukey" : "regression");

        switch (m_polarity) {
            case 1: result.addMetadata("polarity", "dark_to_bright"); break;
            case 2: result.addMetadata("polarity", "bright_to_dark"); break;
            default: result.addMetadata("polarity", "all"); break;
        }

        return true;

    } catch (HException& e) {
        m_lastError = std::string("Halcon error: ") + e.ErrorMessage().Text();
        std::cerr << "[HalconLineDetection] " << m_lastError << std::endl;
        return false;
    } catch (std::exception& e) {
        m_lastError = std::string("Error: ") + e.what();
        std::cerr << "[HalconLineDetection] " << m_lastError << std::endl;
        return false;
    }
}

bool HalconLineDetectionPlugin::validate() const {
    if (m_sigma <= 0) {
        m_lastError = "Sigma must be positive";
        return false;
    }
    if (m_lowThreshold <= 0) {
        m_lastError = "Low threshold must be positive";
        return false;
    }
    if (m_highThreshold <= m_lowThreshold) {
        m_lastError = "High threshold must be greater than low threshold";
        return false;
    }
    if (m_minLength <= 0) {
        m_lastError = "Min length must be positive";
        return false;
    }
    return true;
}

QWidget* HalconLineDetectionPlugin::createConfigWidget(QWidget* parent) {
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    QGroupBox* groupBox = new QGroupBox("Line Detection Parameters");
    QFormLayout* formLayout = new QFormLayout(groupBox);

    // Polarity selection
    m_polarityCombo = new QComboBox();
    m_polarityCombo->addItem("All edges", 0);
    m_polarityCombo->addItem("Dark to Bright", 1);
    m_polarityCombo->addItem("Bright to Dark", 2);
    m_polarityCombo->setCurrentIndex(m_polarity);
    m_polarityCombo->setToolTip("Edge transition type to detect");
    formLayout->addRow("Polarity:", m_polarityCombo);

    // Sigma for Gaussian smoothing
    m_sigmaSpin = new QDoubleSpinBox();
    m_sigmaSpin->setRange(0.1, 10.0);
    m_sigmaSpin->setSingleStep(0.1);
    m_sigmaSpin->setDecimals(1);
    m_sigmaSpin->setValue(m_sigma);
    m_sigmaSpin->setToolTip("Gaussian smoothing sigma for edge detection");
    formLayout->addRow("Sigma:", m_sigmaSpin);

    // Low threshold
    m_lowThresholdSpin = new QDoubleSpinBox();
    m_lowThresholdSpin->setRange(1, 100);
    m_lowThresholdSpin->setSingleStep(1);
    m_lowThresholdSpin->setDecimals(1);
    m_lowThresholdSpin->setValue(m_lowThreshold);
    m_lowThresholdSpin->setToolTip("Canny low threshold");
    formLayout->addRow("Low Threshold:", m_lowThresholdSpin);

    // High threshold
    m_highThresholdSpin = new QDoubleSpinBox();
    m_highThresholdSpin->setRange(1, 200);
    m_highThresholdSpin->setSingleStep(1);
    m_highThresholdSpin->setDecimals(1);
    m_highThresholdSpin->setValue(m_highThreshold);
    m_highThresholdSpin->setToolTip("Canny high threshold");
    formLayout->addRow("High Threshold:", m_highThresholdSpin);

    // Minimum line length
    m_minLengthSpin = new QSpinBox();
    m_minLengthSpin->setRange(10, 1000);
    m_minLengthSpin->setValue(m_minLength);
    m_minLengthSpin->setToolTip("Minimum line length in pixels");
    formLayout->addRow("Min Length (px):", m_minLengthSpin);

    // Maximum lines
    m_maxLinesSpin = new QSpinBox();
    m_maxLinesSpin->setRange(1, 100);
    m_maxLinesSpin->setValue(m_maxLines);
    m_maxLinesSpin->setToolTip("Maximum number of lines to detect");
    formLayout->addRow("Max Lines:", m_maxLinesSpin);

    // Fit method
    m_fitMethodCombo = new QComboBox();
    m_fitMethodCombo->addItem("Regression", 0);
    m_fitMethodCombo->addItem("Tukey (robust)", 1);
    m_fitMethodCombo->setCurrentIndex(m_fitMethod);
    m_fitMethodCombo->setToolTip("Line fitting algorithm");
    formLayout->addRow("Fit Method:", m_fitMethodCombo);

    mainLayout->addWidget(groupBox);
    mainLayout->addStretch();

    // Connect signals
    QObject::connect(m_polarityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     [this](int index) { m_polarity = index; });
    QObject::connect(m_sigmaSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                     [this](double value) { m_sigma = value; });
    QObject::connect(m_lowThresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                     [this](double value) { m_lowThreshold = value; });
    QObject::connect(m_highThresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                     [this](double value) { m_highThreshold = value; });
    QObject::connect(m_minLengthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                     [this](int value) { m_minLength = value; });
    QObject::connect(m_maxLinesSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                     [this](int value) { m_maxLines = value; });
    QObject::connect(m_fitMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     [this](int index) { m_fitMethod = index; });

    return m_configWidget;
}

nlohmann::json HalconLineDetectionPlugin::getParameters() const {
    nlohmann::json params;
    params["sigma"] = m_sigma;
    params["lowThreshold"] = m_lowThreshold;
    params["highThreshold"] = m_highThreshold;
    params["minLength"] = m_minLength;
    params["maxLines"] = m_maxLines;
    params["polarity"] = m_polarity;
    params["fitMethod"] = m_fitMethod;
    return params;
}

void HalconLineDetectionPlugin::setParameters(const nlohmann::json& params) {
    if (params.contains("sigma")) m_sigma = params["sigma"].get<double>();
    if (params.contains("lowThreshold")) m_lowThreshold = params["lowThreshold"].get<double>();
    if (params.contains("highThreshold")) m_highThreshold = params["highThreshold"].get<double>();
    if (params.contains("minLength")) m_minLength = params["minLength"].get<int>();
    if (params.contains("maxLines")) m_maxLines = params["maxLines"].get<int>();
    if (params.contains("polarity")) m_polarity = params["polarity"].get<int>();
    if (params.contains("fitMethod")) m_fitMethod = params["fitMethod"].get<int>();

    // Update UI if widget exists
    if (m_sigmaSpin) m_sigmaSpin->setValue(m_sigma);
    if (m_lowThresholdSpin) m_lowThresholdSpin->setValue(m_lowThreshold);
    if (m_highThresholdSpin) m_highThresholdSpin->setValue(m_highThreshold);
    if (m_minLengthSpin) m_minLengthSpin->setValue(m_minLength);
    if (m_maxLinesSpin) m_maxLinesSpin->setValue(m_maxLines);
    if (m_polarityCombo) m_polarityCombo->setCurrentIndex(m_polarity);
    if (m_fitMethodCombo) m_fitMethodCombo->setCurrentIndex(m_fitMethod);
}

nlohmann::json HalconLineDetectionPlugin::getDefaultParameters() const {
    nlohmann::json params;
    params["sigma"] = 1.0;
    params["lowThreshold"] = 20.0;
    params["highThreshold"] = 40.0;
    params["minLength"] = 50;
    params["maxLines"] = 10;
    params["polarity"] = 0;
    params["fitMethod"] = 0;
    return params;
}

ParameterSchema HalconLineDetectionPlugin::getParameterSchema() const {
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    // Edge Detection Parameters
    schema.parameters.push_back(ParameterDescriptor::createChoice(
        "polarity", "Polarity",
        {"All edges", "Dark to Bright", "Bright to Dark"}, {0, 1, 2}, 0,
        "Edge transition type to detect.",
        "Edge Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "sigma", "Sigma",
        0.1, 10.0, 1.0,
        "Gaussian smoothing sigma for edge detection.",
        "", 0.1,
        "Edge Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "lowThreshold", "Low Threshold",
        1.0, 100.0, 20.0,
        "Canny low threshold for edge detection.",
        "", 1.0,
        "Edge Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "highThreshold", "High Threshold",
        1.0, 200.0, 40.0,
        "Canny high threshold for edge detection.",
        "", 1.0,
        "Edge Detection"
    ));

    // Line Selection Parameters
    schema.parameters.push_back(ParameterDescriptor::createInt(
        "minLength", "Min Length",
        10, 1000, 50,
        "Minimum line length in pixels.",
        "px", 10,
        "Line Selection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxLines", "Max Lines",
        1, 100, 10,
        "Maximum number of lines to detect.",
        "", 1,
        "Line Selection"
    ));

    // Line Fitting
    schema.parameters.push_back(ParameterDescriptor::createChoice(
        "fitMethod", "Fit Method",
        {"Regression", "Tukey (robust)"}, {0, 1}, 0,
        "Line fitting algorithm. Tukey is more robust to outliers.",
        "Line Fitting"
    ));

    return schema;
}

#endif // HAS_OPENCV && HAS_HALCON
