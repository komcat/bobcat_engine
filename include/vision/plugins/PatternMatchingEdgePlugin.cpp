// PatternMatchingEdgePlugin.cpp
#ifdef HAS_OPENCV

#include "PatternMatchingEdgePlugin.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QThread>
#include <QCoreApplication>
#include <iostream>
#include <cmath>
#include <algorithm>

PatternMatchingEdgePlugin::PatternMatchingEdgePlugin()
    : TemplateMatchingBase("PatternMatchingEdge", "PR", "Edge-based pattern matching using Canny edge detection")
{
    m_templateOrigin = cv::Point2d(0, 0);

    // Initialize all widget pointers to nullptr
    m_configWidget = nullptr;
    m_thresholdSpin = nullptr;
    m_subpixelCheck = nullptr;
    m_maxMatchesSpin = nullptr;
    m_cannyThreshold1Spin = nullptr;
    m_cannyThreshold2Spin = nullptr;
    m_gaussianBlurSizeSpin = nullptr;
}

bool PatternMatchingEdgePlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) {
    if (!hasTemplate()) {
        result.success = false;
        result.errorMessage = "No template taught. Please teach a template first.";
        return false;
    }

    if (input.empty()) {
        result.success = false;
        result.errorMessage = "Input image is empty";
        return false;
    }

    // Convert input to grayscale if needed
    cv::Mat searchGray;
    if (input.channels() == 3) {
        cv::cvtColor(input, searchGray, cv::COLOR_BGR2GRAY);
    } else {
        searchGray = input.clone();
    }

    // Apply Gaussian blur and Canny edge detection to search image
    cv::Mat searchEdge;
    cv::Mat blurred;
    if (m_gaussianBlurSize > 0 && m_gaussianBlurSize % 2 == 1) {
        cv::GaussianBlur(searchGray, blurred, cv::Size(m_gaussianBlurSize, m_gaussianBlurSize), 0);
    } else {
        blurred = searchGray;
    }
    cv::Canny(blurred, searchEdge, m_cannyThreshold1, m_cannyThreshold2);

    // Check template size
    if (m_templateEdge.cols > searchEdge.cols || m_templateEdge.rows > searchEdge.rows) {
        result.success = false;
        result.errorMessage = "Template is larger than search image";
        return false;
    }

    // Perform template matching on edge images
    cv::Mat matchResult;
    cv::matchTemplate(searchEdge, m_templateEdge, matchResult, cv::TM_CCOEFF_NORMED);

    // Find multiple match locations
    struct MatchLocation {
        cv::Point location;
        double score;
    };
    std::vector<MatchLocation> matches;

    // Find all matches above threshold
    cv::Mat matchResultCopy = matchResult.clone();
    int rejectedCount = 0;
    for (int i = 0; i < m_maxMatches * 3; i++) {  // Search more candidates
        double minVal, maxVal;
        cv::Point minLoc, maxLoc;
        cv::minMaxLoc(matchResultCopy, &minVal, &maxVal, &minLoc, &maxLoc);

        if (matches.size() >= (size_t)m_maxMatches) {
            break;  // Found enough matches
        }

        if (maxVal < m_matchThreshold) {
            std::cout << "[PatternMatchingEdge] Rejected match at (" << maxLoc.x << ", " << maxLoc.y
                      << ") with score " << maxVal << " (threshold: " << m_matchThreshold << ")" << std::endl;
            rejectedCount++;
            if (rejectedCount >= 5) {
                break;  // Stop after showing 5 rejections
            }
        } else {
            matches.push_back({maxLoc, maxVal});
            std::cout << "[PatternMatchingEdge] Accepted match " << matches.size() << " at (" << maxLoc.x << ", " << maxLoc.y
                      << ") with score " << maxVal << std::endl;
        }

        // Suppress nearby matches to avoid duplicates
        int suppressRadius = std::max(m_templateEdge.cols, m_templateEdge.rows) / 2;
        cv::rectangle(matchResultCopy,
                      cv::Point(std::max(0, maxLoc.x - suppressRadius),
                                std::max(0, maxLoc.y - suppressRadius)),
                      cv::Point(std::min(matchResultCopy.cols - 1, maxLoc.x + suppressRadius),
                                std::min(matchResultCopy.rows - 1, maxLoc.y + suppressRadius)),
                      cv::Scalar(0), cv::FILLED);
    }

    // Check if we found any matches
    if (matches.empty()) {
        result.success = false;
        result.errorMessage = "No match found above threshold";
        if (input.channels() == 1) {
            cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
        } else {
            output = input.clone();
        }
        return false;
    }

    std::cout << "[PatternMatchingEdge] Found " << matches.size() << " matches" << std::endl;

    // Create output visualization - just copy input to output
    if (input.channels() == 1) {
        cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
    } else {
        output = input.clone();
    }

    // Find contours from searchEdge (yellow) - only once
    std::vector<std::vector<cv::Point>> yellowContours;
    cv::findContours(searchEdge.clone(), yellowContours, cv::RETR_LIST, cv::CHAIN_APPROX_NONE);

    std::cout << "[PatternMatchingEdge] Found " << yellowContours.size() << " yellow edge contours" << std::endl;

    // Add yellow edge contours as overlays
    for (const auto& contour : yellowContours) {
        OverlayCommand overlay;
        overlay.type = OverlayCommand::DrawContour;
        overlay.color = cv::Scalar(0, 255, 255);  // Yellow in BGR
        overlay.thickness = 1;
        overlay.contour = contour;
        result.overlays.push_back(overlay);
    }

    // Process each match
    for (size_t i = 0; i < matches.size(); i++) {
        cv::Point bestLoc = matches[i].location;
        double matchScore = matches[i].score;

        // Convert to center coordinates
        cv::Point2d matchCenter(bestLoc.x + m_templateEdge.cols / 2.0,
                                bestLoc.y + m_templateEdge.rows / 2.0);

        // Subpixel refinement
        if (m_subpixelRefinement && bestLoc.x > 0 && bestLoc.y > 0 &&
            bestLoc.x < matchResult.cols - 1 && bestLoc.y < matchResult.rows - 1) {

            float left = matchResult.at<float>(bestLoc.y, bestLoc.x - 1);
            float center = matchResult.at<float>(bestLoc.y, bestLoc.x);
            float right = matchResult.at<float>(bestLoc.y, bestLoc.x + 1);
            float deltaX = (left - right) / (2.0f * (left - 2.0f * center + right));

            float top = matchResult.at<float>(bestLoc.y - 1, bestLoc.x);
            float bottom = matchResult.at<float>(bestLoc.y + 1, bestLoc.x);
            float deltaY = (top - bottom) / (2.0f * (top - 2.0f * center + bottom));

            if (std::abs(deltaX) < 1.0f && std::abs(deltaY) < 1.0f) {
                matchCenter.x += deltaX;
                matchCenter.y += deltaY;
            }
        }

        // Apply anchor point offset if custom anchor is set
        cv::Point2d anchorPoint = matchCenter;
        if (m_useCustomAnchor) {
            anchorPoint.x += m_anchorOffsetX;
            anchorPoint.y += m_anchorOffsetY;
        }

        // Find contours from templateEdge and offset them to match location (red)
        std::vector<std::vector<cv::Point>> redContours;
        cv::findContours(m_templateEdge.clone(), redContours, cv::RETR_LIST, cv::CHAIN_APPROX_NONE);

        // Add red template edge contours as overlays (offset to this match location)
        for (auto contour : redContours) {
            // Offset contour points to match location
            for (auto& pt : contour) {
                pt.x += bestLoc.x;
                pt.y += bestLoc.y;
            }
            OverlayCommand overlay;
            overlay.type = OverlayCommand::DrawContour;
            overlay.color = cv::Scalar(0, 0, 255);  // Red in BGR
            overlay.thickness = 1;
            overlay.contour = contour;
            result.overlays.push_back(overlay);
        }

        // Add overlay commands for this match
        OverlayCommand rectCmd;
        rectCmd.type = OverlayCommand::DrawRectangle;
        rectCmd.rect = cv::Rect(bestLoc.x, bestLoc.y, m_templateEdge.cols, m_templateEdge.rows);
        rectCmd.color = cv::Scalar(0, 255, 0);
        rectCmd.thickness = 2;
        result.overlays.push_back(rectCmd);

        OverlayCommand centerCmd;
        centerCmd.type = OverlayCommand::DrawCrosshair;
        centerCmd.p1 = matchCenter;
        centerCmd.radius = 20;
        centerCmd.color = cv::Scalar(0, 255, 255);
        centerCmd.thickness = 2;
        result.overlays.push_back(centerCmd);

        if (m_useCustomAnchor) {
            OverlayCommand anchorCmd;
            anchorCmd.type = OverlayCommand::DrawCrosshair;
            anchorCmd.p1 = anchorPoint;
            anchorCmd.radius = 20;
            anchorCmd.color = cv::Scalar(0, 0, 255);
            anchorCmd.thickness = 2;
            result.overlays.push_back(anchorCmd);
        }

        // Add point result
        std::string pointName = "match_" + std::to_string(i);
        result.addPoint(pointName, anchorPoint, cv::Point2d(0, 0), matchScore);
        result.measurements[pointName + "_score"] = matchScore;

        std::cout << "[PatternMatchingEdge] Match " << i << " found at (" << anchorPoint.x << ", " << anchorPoint.y
                  << ") with score " << matchScore << std::endl;
    }

    // Store overall result
    result.success = true;
    result.measurements["num_matches"] = static_cast<double>(matches.size());
    result.measurements["best_score"] = matches[0].score;
    result.errorMessage = "Found " + std::to_string(matches.size()) + " match(es)";

    return true;
}

bool PatternMatchingEdgePlugin::validate() const {
    if (!hasTemplate()) {
        return false;
    }
    if (m_matchThreshold < 0.1 || m_matchThreshold > 1.0) {
        return false;
    }
    return true;
}

bool PatternMatchingEdgePlugin::teachFromImage(const cv::Mat& templateImage) {
    if (templateImage.empty()) {
        std::cout << "[PatternMatchingEdge] Cannot teach from empty image" << std::endl;
        return false;
    }

    // Store original template (in base class)
    m_template = templateImage.clone();

    // Convert to grayscale if needed
    cv::Mat templateGray;
    if (templateImage.channels() == 3) {
        cv::cvtColor(templateImage, templateGray, cv::COLOR_BGR2GRAY);
    } else {
        templateGray = templateImage.clone();
    }

    // Apply Gaussian blur and Canny edge detection to template
    cv::Mat blurred;
    if (m_gaussianBlurSize > 0 && m_gaussianBlurSize % 2 == 1) {
        cv::GaussianBlur(templateGray, blurred, cv::Size(m_gaussianBlurSize, m_gaussianBlurSize), 0);
    } else {
        blurred = templateGray;
    }
    cv::Canny(blurred, m_templateEdge, m_cannyThreshold1, m_cannyThreshold2);

    // Set template origin to center
    m_templateOrigin = cv::Point2d(m_templateEdge.cols / 2.0, m_templateEdge.rows / 2.0);

    std::cout << "[PatternMatchingEdge] Template taught: " << m_templateEdge.cols << "x"
              << m_templateEdge.rows << " pixels" << std::endl;

    updatePreviewLabel();
    return true;
}

void PatternMatchingEdgePlugin::clearTemplate() {
    // Call base class implementation
    TemplateMatchingBase::clearTemplate();

    // Clear plugin-specific data
    m_templateEdge = cv::Mat();
    m_templateOrigin = cv::Point2d(0, 0);
}

QWidget* PatternMatchingEdgePlugin::createConfigWidget(QWidget* parent) {
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    // Connect to destroyed signal to reset our pointer
    QObject::connect(m_configWidget, &QObject::destroyed, [this]() {
        m_configWidget = nullptr;
        m_thresholdSpin = nullptr;
        m_subpixelCheck = nullptr;
        m_maxMatchesSpin = nullptr;
        m_cannyThreshold1Spin = nullptr;
        m_cannyThreshold2Spin = nullptr;
        m_gaussianBlurSizeSpin = nullptr;
    });

    // Add template group from base class
    QGroupBox* templateGroup = createTemplateGroupWidget(m_configWidget);
    mainLayout->addWidget(templateGroup);

    // Search parameters group
    QGroupBox* searchGroup = new QGroupBox("Search Parameters", m_configWidget);
    QFormLayout* searchLayout = new QFormLayout(searchGroup);

    m_thresholdSpin = new QDoubleSpinBox(searchGroup);
    m_thresholdSpin->setRange(0.1, 1.0);
    m_thresholdSpin->setSingleStep(0.05);
    m_thresholdSpin->setValue(m_matchThreshold);
    m_thresholdSpin->setDecimals(2);
    searchLayout->addRow("Match Threshold:", m_thresholdSpin);

    m_subpixelCheck = new QCheckBox("Subpixel Refinement", searchGroup);
    m_subpixelCheck->setChecked(m_subpixelRefinement);
    searchLayout->addRow("", m_subpixelCheck);

    m_maxMatchesSpin = new QSpinBox(searchGroup);
    m_maxMatchesSpin->setRange(1, 100);
    m_maxMatchesSpin->setSingleStep(1);
    m_maxMatchesSpin->setValue(m_maxMatches);
    m_maxMatchesSpin->setToolTip("Maximum number of matches to find (1 = best match only)");
    searchLayout->addRow("Max Matches:", m_maxMatchesSpin);

    mainLayout->addWidget(searchGroup);

    // Canny Edge Detection Parameters group
    QGroupBox* cannyGroup = new QGroupBox("Canny Edge Detection Parameters", m_configWidget);
    QFormLayout* cannyLayout = new QFormLayout(cannyGroup);

    m_cannyThreshold1Spin = new QDoubleSpinBox(cannyGroup);
    m_cannyThreshold1Spin->setRange(1.0, 500.0);
    m_cannyThreshold1Spin->setSingleStep(10.0);
    m_cannyThreshold1Spin->setValue(m_cannyThreshold1);
    m_cannyThreshold1Spin->setToolTip("Lower threshold for Canny edge detection");
    cannyLayout->addRow("Lower Threshold:", m_cannyThreshold1Spin);

    m_cannyThreshold2Spin = new QDoubleSpinBox(cannyGroup);
    m_cannyThreshold2Spin->setRange(1.0, 500.0);
    m_cannyThreshold2Spin->setSingleStep(10.0);
    m_cannyThreshold2Spin->setValue(m_cannyThreshold2);
    m_cannyThreshold2Spin->setToolTip("Upper threshold for Canny edge detection");
    cannyLayout->addRow("Upper Threshold:", m_cannyThreshold2Spin);

    m_gaussianBlurSizeSpin = new QSpinBox(cannyGroup);
    m_gaussianBlurSizeSpin->setRange(1, 15);
    m_gaussianBlurSizeSpin->setSingleStep(2);
    m_gaussianBlurSizeSpin->setValue(m_gaussianBlurSize);
    m_gaussianBlurSizeSpin->setToolTip("Aperture size for Gaussian blur (must be odd)");
    cannyLayout->addRow("Blur Kernel Size:", m_gaussianBlurSizeSpin);

    mainLayout->addWidget(cannyGroup);
    mainLayout->addStretch();

    // Connect signals
    QObject::connect(m_thresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_matchThreshold = val; });

    QObject::connect(m_subpixelCheck, &QCheckBox::toggled,
            [this](bool checked) { m_subpixelRefinement = checked; });

    QObject::connect(m_maxMatchesSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxMatches = val; });

    QObject::connect(m_cannyThreshold1Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) {
                m_cannyThreshold1 = val;
                // Re-generate edge template if we have one
                if (hasTemplate()) {
                    teachFromImage(m_template);
                }
            });

    QObject::connect(m_cannyThreshold2Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) {
                m_cannyThreshold2 = val;
                // Re-generate edge template if we have one
                if (hasTemplate()) {
                    teachFromImage(m_template);
                }
            });

    QObject::connect(m_gaussianBlurSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) {
                // Ensure odd value
                if (val % 2 == 0) val++;
                m_gaussianBlurSize = val;
                if (m_gaussianBlurSizeSpin) {
                    m_gaussianBlurSizeSpin->setValue(val);
                }
                // Re-generate edge template if we have one
                if (hasTemplate()) {
                    teachFromImage(m_template);
                }
            });

    return m_configWidget;
}

nlohmann::json PatternMatchingEdgePlugin::getParameters() const {
    nlohmann::json params;

    params["matchThreshold"] = m_matchThreshold;
    params["subpixelRefinement"] = m_subpixelRefinement;
    params["maxMatches"] = m_maxMatches;
    params["cannyThreshold1"] = m_cannyThreshold1;
    params["cannyThreshold2"] = m_cannyThreshold2;
    params["gaussianBlurSize"] = m_gaussianBlurSize;

    // Merge with base class template parameters
    nlohmann::json templateParams = getTemplateParameters();
    for (auto& [key, value] : templateParams.items()) {
        params[key] = value;
    }

    return params;
}

void PatternMatchingEdgePlugin::setParameters(const nlohmann::json& params) {
    if (params.contains("matchThreshold")) {
        m_matchThreshold = params["matchThreshold"].get<double>();
    }
    if (params.contains("subpixelRefinement")) {
        m_subpixelRefinement = params["subpixelRefinement"].get<bool>();
    }
    if (params.contains("maxMatches")) {
        m_maxMatches = params["maxMatches"].get<int>();
    }
    if (params.contains("cannyThreshold1")) {
        m_cannyThreshold1 = params["cannyThreshold1"].get<double>();
    }
    if (params.contains("cannyThreshold2")) {
        m_cannyThreshold2 = params["cannyThreshold2"].get<double>();
    }
    if (params.contains("gaussianBlurSize")) {
        m_gaussianBlurSize = params["gaussianBlurSize"].get<int>();
    }

    // Set base class template parameters
    setTemplateParameters(params);

    // Update UI if on main thread
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        return;
    }

    if (m_thresholdSpin) m_thresholdSpin->setValue(m_matchThreshold);
    if (m_subpixelCheck) m_subpixelCheck->setChecked(m_subpixelRefinement);
    if (m_maxMatchesSpin) m_maxMatchesSpin->setValue(m_maxMatches);
    if (m_cannyThreshold1Spin) m_cannyThreshold1Spin->setValue(m_cannyThreshold1);
    if (m_cannyThreshold2Spin) m_cannyThreshold2Spin->setValue(m_cannyThreshold2);
    if (m_gaussianBlurSizeSpin) m_gaussianBlurSizeSpin->setValue(m_gaussianBlurSize);

    updatePreviewLabel();
}

nlohmann::json PatternMatchingEdgePlugin::getDefaultParameters() const {
    nlohmann::json params;
    params["matchThreshold"] = 0.7;
    params["subpixelRefinement"] = true;
    params["maxMatches"] = 1;
    params["cannyThreshold1"] = 50.0;
    params["cannyThreshold2"] = 150.0;
    params["gaussianBlurSize"] = 5;
    params["useCustomAnchor"] = false;
    params["anchorOffsetX"] = 0.0;
    params["anchorOffsetY"] = 0.0;
    return params;
}

ParameterSchema PatternMatchingEdgePlugin::getParameterSchema() const {
    ParameterSchema schema;
    schema.pluginName = m_name;
    schema.pluginCategory = m_category;

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "matchThreshold", "Match Threshold", 0.1, 1.0, 0.7,
        "Minimum match score (0-1)", "", 0.05, "Search"));

    schema.parameters.push_back(ParameterDescriptor::createBool(
        "subpixelRefinement", "Subpixel Refinement", true,
        "Refine match position to subpixel accuracy", "Search"));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxMatches", "Max Matches", 1, 100, 1,
        "Maximum number of matches to find", "", 1, "Search"));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "cannyThreshold1", "Lower Threshold", 1.0, 500.0, 50.0,
        "Canny edge detection lower threshold", "", 10.0, "Canny Edge Detection"));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "cannyThreshold2", "Upper Threshold", 1.0, 500.0, 150.0,
        "Canny edge detection upper threshold", "", 10.0, "Canny Edge Detection"));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "gaussianBlurSize", "Blur Kernel Size", 1, 15, 5,
        "Gaussian blur kernel size (must be odd)", "px", 2, "Canny Edge Detection"));

    return schema;
}

#endif // HAS_OPENCV
