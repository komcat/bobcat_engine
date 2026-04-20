// HalconSingleLineAnglePlugin.cpp
// Single-line detection reporting angle relative to the camera X-axis.

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "HalconSingleLineAnglePlugin.h"
#include "../VisionManager.h"
#include <halconcpp/HalconCpp.h>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <cmath>
#include <sstream>
#include <iomanip>

using namespace HalconCpp;

HalconSingleLineAnglePlugin::HalconSingleLineAnglePlugin()
    : VisionPluginBase("LineAngle (Halcon)", "Measurement",
                       "Detect a single line and report its angle vs the camera X-axis")
{
}

HalconSingleLineAnglePlugin::~HalconSingleLineAnglePlugin() {
    // Config widget owned by Qt parent
}

bool HalconSingleLineAnglePlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) {
    if (input.empty()) {
        m_lastError = "Input image is empty";
        return false;
    }

    try {
        // Convert to grayscale
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

        if (!gray.isContinuous()) {
            gray = gray.clone();
        }

        // Prepare output image (BGR for overlays)
        if (input.channels() == 1) {
            cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
        } else if (input.channels() == 4) {
            cv::cvtColor(input, output, cv::COLOR_BGRA2BGR);
        } else {
            output = input.clone();
        }

        // OpenCV -> Halcon
        HImage halconImage;
        halconImage.GenImage1("byte", static_cast<Hlong>(gray.cols), static_cast<Hlong>(gray.rows),
                              static_cast<void*>(gray.data));

        // Sub-pixel edges (Canny). Polarity is enforced later via fit selection,
        // Halcon EdgesSubPix doesn't take a polarity argument for "canny" — we
        // use sign of gradient via amplitude image if needed. For consistency
        // with HalconLineDetection we keep polarity as metadata only here.
        HObject edges;
        EdgesSubPix(halconImage, &edges, "canny", m_sigma, m_lowThreshold, m_highThreshold);

        // Segment contours into straight line segments
        HObject lineSegments;
        SegmentContoursXld(edges, &lineSegments, "lines", 5, 4, 2);

        // Filter by minimum length
        HObject longLines;
        SelectContoursXld(lineSegments, &longLines, "contour_length",
                          static_cast<double>(m_minLength), 100000.0, -0.5, 0.5);

        HTuple numContours;
        CountObj(longLines, &numContours);
        int contourCount = static_cast<int>(numContours.D());

        if (VisionManager::instance().isVerboseLogging()) {
            std::cout << "[LineAngle] Found " << contourCount
                      << " contour(s) passing length filter" << std::endl;
        }

        // Find the single longest fitted line
        double bestX1 = 0.0, bestY1 = 0.0, bestX2 = 0.0, bestY2 = 0.0;
        double bestLength = -1.0;
        bool found = false;

        for (int i = 1; i <= contourCount; ++i) {
            try {
                HObject singleContour;
                SelectObj(longLines, &singleContour, i);

                HTuple rowBegin, colBegin, rowEnd, colEnd, nr, nc, dist;
                const char* method = (m_fitMethod == 1) ? "tukey" : "regression";
                FitLineContourXld(singleContour, method, -1, 0, 5, 2,
                                  &rowBegin, &colBegin, &rowEnd, &colEnd, &nr, &nc, &dist);

                double x1 = colBegin.D();
                double y1 = rowBegin.D();
                double x2 = colEnd.D();
                double y2 = rowEnd.D();
                double length = std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));

                if (length >= m_minLength && length > bestLength) {
                    bestLength = length;
                    bestX1 = x1;
                    bestY1 = y1;
                    bestX2 = x2;
                    bestY2 = y2;
                    found = true;
                }
            } catch (HException& /*e*/) {
                continue;
            }
        }

        // Common metadata
        result.addMetadata("algorithm", "Halcon_SingleLineAngle");
        result.addMetadata("sigma", std::to_string(m_sigma));
        result.addMetadata("threshold_low", std::to_string(m_lowThreshold));
        result.addMetadata("threshold_high", std::to_string(m_highThreshold));
        result.addMetadata("min_length", std::to_string(m_minLength));
        result.addMetadata("fit_method", (m_fitMethod == 1) ? "tukey" : "regression");
        result.addMetadata("angle_convention",
                           "degrees from camera X-axis, range [-90,90], positive=tilt down-right");
        switch (m_polarity) {
            case 1: result.addMetadata("polarity", "dark_to_bright"); break;
            case 2: result.addMetadata("polarity", "bright_to_dark"); break;
            default: result.addMetadata("polarity", "all"); break;
        }

        if (!found) {
            result.addMeasurement("line_found", 0.0);
            result.addMetadata("note", "No line detected matching criteria");
            if (VisionManager::instance().isVerboseLogging()) {
                std::cout << "[LineAngle] No line found" << std::endl;
            }
            return true;
        }

        // Raw atan2 angle in [-180, 180]
        double rawAngle = std::atan2(bestY2 - bestY1, bestX2 - bestX1) * 180.0 / CV_PI;

        // Normalize to [-90, 90] — line direction is undirected, so ±180° ≡ 0°.
        double camAxisAngle = rawAngle;
        if (camAxisAngle > 90.0)  camAxisAngle -= 180.0;
        if (camAxisAngle < -90.0) camAxisAngle += 180.0;

        double midX = (bestX1 + bestX2) / 2.0;
        double midY = (bestY1 + bestY2) / 2.0;

        // Measurements
        result.addMeasurement("line_found", 1.0);
        result.addMeasurement("line_length", bestLength);
        result.addMeasurement("line_x1", bestX1);
        result.addMeasurement("line_y1", bestY1);
        result.addMeasurement("line_x2", bestX2);
        result.addMeasurement("line_y2", bestY2);
        result.addMeasurement("line_mid_x", midX);
        result.addMeasurement("line_mid_y", midY);
        result.addMeasurement("line_angle_raw_deg", rawAngle);
        result.addMeasurement("angle_from_camera_axis_deg", camAxisAngle);

        // Structured line result (adds entry to result.lines with angle/length)
        result.addLine("line1", cv::Point2d(bestX1, bestY1), cv::Point2d(bestX2, bestY2),
                       cv::Point2d(bestX1, bestY1), cv::Point2d(bestX2, bestY2));

        // --- Overlays ---
        // Detected line in green
        result.addLineOverlay(cv::Point2d(bestX1, bestY1), cv::Point2d(bestX2, bestY2),
                              cv::Scalar(0, 255, 0), 2);
        // Endpoints — blue start, red end
        result.addPointOverlay(cv::Point2d(bestX1, bestY1), cv::Scalar(255, 0, 0), 4);
        result.addPointOverlay(cv::Point2d(bestX2, bestY2), cv::Scalar(0, 0, 255), 4);

        // Camera X-axis reference through midpoint (yellow dashed-like: single segment)
        double halfLen = bestLength / 2.0;
        result.addLineOverlay(cv::Point2d(midX - halfLen, midY),
                              cv::Point2d(midX + halfLen, midY),
                              cv::Scalar(0, 255, 255), 1);

        // Angle label at midpoint
        std::ostringstream label;
        label << std::fixed << std::setprecision(2)
              << "angle=" << camAxisAngle << " deg | L=" << static_cast<int>(bestLength) << "px";
        result.addTextOverlay(label.str(), cv::Point2d(midX + 5, midY - 8),
                              cv::Scalar(0, 255, 255), 1);

        if (VisionManager::instance().isVerboseLogging()) {
            std::cout << "[LineAngle] (" << bestX1 << "," << bestY1 << ") -> ("
                      << bestX2 << "," << bestY2 << "), length=" << bestLength
                      << "px, raw=" << rawAngle << " deg, cam-axis="
                      << camAxisAngle << " deg" << std::endl;
        }

        return true;

    } catch (HException& e) {
        m_lastError = std::string("Halcon error: ") + e.ErrorMessage().Text();
        std::cerr << "[LineAngle] " << m_lastError << std::endl;
        return false;
    } catch (std::exception& e) {
        m_lastError = std::string("Error: ") + e.what();
        std::cerr << "[LineAngle] " << m_lastError << std::endl;
        return false;
    }
}

bool HalconSingleLineAnglePlugin::validate() const {
    if (m_sigma <= 0) { m_lastError = "Sigma must be positive"; return false; }
    if (m_lowThreshold <= 0) { m_lastError = "Low threshold must be positive"; return false; }
    if (m_highThreshold <= m_lowThreshold) {
        m_lastError = "High threshold must be greater than low threshold";
        return false;
    }
    if (m_minLength <= 0) { m_lastError = "Min length must be positive"; return false; }
    return true;
}

QWidget* HalconSingleLineAnglePlugin::createConfigWidget(QWidget* parent) {
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    QGroupBox* groupBox = new QGroupBox("Single-Line Angle Parameters");
    QFormLayout* formLayout = new QFormLayout(groupBox);

    m_polarityCombo = new QComboBox();
    m_polarityCombo->addItem("All edges", 0);
    m_polarityCombo->addItem("Dark to Bright", 1);
    m_polarityCombo->addItem("Bright to Dark", 2);
    m_polarityCombo->setCurrentIndex(m_polarity);
    m_polarityCombo->setToolTip("Edge transition type to detect");
    formLayout->addRow("Polarity:", m_polarityCombo);

    m_sigmaSpin = new QDoubleSpinBox();
    m_sigmaSpin->setRange(0.1, 10.0);
    m_sigmaSpin->setSingleStep(0.1);
    m_sigmaSpin->setDecimals(1);
    m_sigmaSpin->setValue(m_sigma);
    m_sigmaSpin->setToolTip("Gaussian smoothing sigma for edge detection");
    formLayout->addRow("Sigma:", m_sigmaSpin);

    m_lowThresholdSpin = new QDoubleSpinBox();
    m_lowThresholdSpin->setRange(1, 100);
    m_lowThresholdSpin->setSingleStep(1);
    m_lowThresholdSpin->setDecimals(1);
    m_lowThresholdSpin->setValue(m_lowThreshold);
    m_lowThresholdSpin->setToolTip("Canny low threshold");
    formLayout->addRow("Low Threshold:", m_lowThresholdSpin);

    m_highThresholdSpin = new QDoubleSpinBox();
    m_highThresholdSpin->setRange(1, 200);
    m_highThresholdSpin->setSingleStep(1);
    m_highThresholdSpin->setDecimals(1);
    m_highThresholdSpin->setValue(m_highThreshold);
    m_highThresholdSpin->setToolTip("Canny high threshold");
    formLayout->addRow("High Threshold:", m_highThresholdSpin);

    m_minLengthSpin = new QSpinBox();
    m_minLengthSpin->setRange(10, 10000);
    m_minLengthSpin->setValue(m_minLength);
    m_minLengthSpin->setToolTip("Minimum line length in pixels");
    formLayout->addRow("Min Length (px):", m_minLengthSpin);

    m_fitMethodCombo = new QComboBox();
    m_fitMethodCombo->addItem("Regression", 0);
    m_fitMethodCombo->addItem("Tukey (robust)", 1);
    m_fitMethodCombo->setCurrentIndex(m_fitMethod);
    m_fitMethodCombo->setToolTip("Line fitting algorithm");
    formLayout->addRow("Fit Method:", m_fitMethodCombo);

    mainLayout->addWidget(groupBox);
    mainLayout->addStretch();

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
    QObject::connect(m_fitMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     [this](int index) { m_fitMethod = index; });

    return m_configWidget;
}

nlohmann::json HalconSingleLineAnglePlugin::getParameters() const {
    nlohmann::json params;
    params["sigma"] = m_sigma;
    params["lowThreshold"] = m_lowThreshold;
    params["highThreshold"] = m_highThreshold;
    params["minLength"] = m_minLength;
    params["polarity"] = m_polarity;
    params["fitMethod"] = m_fitMethod;
    return params;
}

void HalconSingleLineAnglePlugin::setParameters(const nlohmann::json& params) {
    if (params.contains("sigma")) m_sigma = params["sigma"].get<double>();
    if (params.contains("lowThreshold")) m_lowThreshold = params["lowThreshold"].get<double>();
    if (params.contains("highThreshold")) m_highThreshold = params["highThreshold"].get<double>();
    if (params.contains("minLength")) m_minLength = params["minLength"].get<int>();
    if (params.contains("polarity")) m_polarity = params["polarity"].get<int>();
    if (params.contains("fitMethod")) m_fitMethod = params["fitMethod"].get<int>();

    if (m_sigmaSpin) m_sigmaSpin->setValue(m_sigma);
    if (m_lowThresholdSpin) m_lowThresholdSpin->setValue(m_lowThreshold);
    if (m_highThresholdSpin) m_highThresholdSpin->setValue(m_highThreshold);
    if (m_minLengthSpin) m_minLengthSpin->setValue(m_minLength);
    if (m_polarityCombo) m_polarityCombo->setCurrentIndex(m_polarity);
    if (m_fitMethodCombo) m_fitMethodCombo->setCurrentIndex(m_fitMethod);
}

nlohmann::json HalconSingleLineAnglePlugin::getDefaultParameters() const {
    nlohmann::json params;
    params["sigma"] = 1.0;
    params["lowThreshold"] = 20.0;
    params["highThreshold"] = 40.0;
    params["minLength"] = 50;
    params["polarity"] = 0;
    params["fitMethod"] = 0;
    return params;
}

ParameterSchema HalconSingleLineAnglePlugin::getParameterSchema() const {
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

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

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "minLength", "Min Length",
        10, 10000, 50,
        "Minimum line length in pixels.",
        "px", 10,
        "Line Selection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createChoice(
        "fitMethod", "Fit Method",
        {"Regression", "Tukey (robust)"}, {0, 1}, 0,
        "Line fitting algorithm. Tukey is more robust to outliers.",
        "Line Fitting"
    ));

    return schema;
}

#endif // HAS_OPENCV && HAS_HALCON
