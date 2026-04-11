// HalconPatternMatchingNCCPlugin.cpp
// Intensity-based pattern matching using Halcon's NCC Matching

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

// Prevent Windows min/max macros from conflicting with std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "HalconPatternMatchingNCCPlugin.h"
#include "../VisionManager.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QThread>
#include <QCoreApplication>
#include <iostream>
#include <cmath>

using namespace HalconCpp;

HalconPatternMatchingNCCPlugin::HalconPatternMatchingNCCPlugin()
    : TemplateMatchingBase("PatternMatchNCC (Halcon)", "PR",
                           "Intensity-based pattern matching using Halcon NCC (no edges required)")
{
    m_configWidget = nullptr;
    m_minScoreSpin = nullptr;
    m_maxMatchesSpin = nullptr;
    m_numLevelsSpin = nullptr;
    m_angleToleranceSpin = nullptr;
    m_angleToleranceSlider = nullptr;
    m_angleToleranceLabel = nullptr;
    m_metricCombo = nullptr;
}

HalconPatternMatchingNCCPlugin::~HalconPatternMatchingNCCPlugin() {
    // NCC model is automatically cleaned up by HNCCModel destructor
}

bool HalconPatternMatchingNCCPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) {
    if (!hasTemplate()) {
        m_lastError = "No template taught. Please teach a template first.";
        return false;
    }

    if (input.empty()) {
        m_lastError = "Input image is empty";
        return false;
    }

    try {
        // Convert input to grayscale if needed
        cv::Mat gray;
        if (input.channels() == 3) {
            cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
        } else if (input.channels() == 4) {
            cv::cvtColor(input, gray, cv::COLOR_BGRA2GRAY);
        } else {
            gray = input.clone();
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
        HImage searchImage;
        searchImage.GenImage1("byte", static_cast<Hlong>(gray.cols), static_cast<Hlong>(gray.rows),
                              static_cast<void*>(gray.data));

        // Convert angle tolerance to start/extent for Halcon
        // Tolerance of X degrees means search from -X to +X (total extent = 2*X)
        double angleStartRad = -m_angleTolerance * M_PI / 180.0;
        double angleExtentRad = 2.0 * m_angleTolerance * M_PI / 180.0;

        // Find NCC model
        HTuple row, col, angle, score;

        m_nccModel.FindNccModel(
            searchImage,
            angleStartRad,           // AngleStart (-tolerance)
            angleExtentRad,          // AngleExtent (2*tolerance)
            m_minScore,              // MinScore
            m_maxMatches,            // NumMatches
            0.5,                     // MaxOverlap
            "true",                  // SubPixel ("true" = sub-pixel accuracy)
            m_numLevels,             // NumLevels (0 = auto)
            &row, &col, &angle, &score
        );

        int numMatches = static_cast<int>(score.Length());

        if (VisionManager::instance().isVerboseLogging()) {
            std::cout << "[HalconPatternMatchNCC] Found " << numMatches << " matches" << std::endl;
        }

        if (numMatches == 0) {
            result.addMeasurement("num_matches", 0.0);
            result.addMetadata("algorithm", "Halcon_NCCMatch");
            result.addMetadata("note", "No matches found above threshold");
            return true;
        }

        // Process each match
        for (int i = 0; i < numMatches; i++) {
            double matchRow = row[i].D();
            double matchCol = col[i].D();
            double matchAngleRad = angle[i].D();
            double matchAngle = matchAngleRad * 180.0 / M_PI;  // Convert to degrees
            double matchScore = score[i].D();

            // Apply anchor point offset if custom anchor is set
            cv::Point2d matchCenter(matchCol, matchRow);
            cv::Point2d anchorPoint = matchCenter;
            if (m_useCustomAnchor) {
                // Rotate offset by match angle
                double cosA = std::cos(matchAngleRad);
                double sinA = std::sin(matchAngleRad);
                double rotatedOffsetX = m_anchorOffsetX * cosA - m_anchorOffsetY * sinA;
                double rotatedOffsetY = m_anchorOffsetX * sinA + m_anchorOffsetY * cosA;
                anchorPoint.x += rotatedOffsetX;
                anchorPoint.y += rotatedOffsetY;
            }

            // Draw rotated bounding box of the template at the matched pose (green)
            // NCC has no contours to draw, so we use the stored template dimensions.
            if (m_templateWidth > 0 && m_templateHeight > 0) {
                double halfW = m_templateWidth / 2.0;
                double halfH = m_templateHeight / 2.0;
                // 4 corners in template-local coords (centered)
                double localX[4] = { -halfW,  halfW, halfW, -halfW };
                double localY[4] = { -halfH, -halfH, halfH,  halfH };
                double cosA = std::cos(matchAngleRad);
                double sinA = std::sin(matchAngleRad);
                std::vector<cv::Point> boxPts;
                boxPts.reserve(4);
                for (int k = 0; k < 4; ++k) {
                    // Rotate then translate to match center. Note: Halcon row=Y, col=X.
                    double rx = localX[k] * cosA - localY[k] * sinA;
                    double ry = localX[k] * sinA + localY[k] * cosA;
                    boxPts.push_back(cv::Point(
                        static_cast<int>(matchCol + rx),
                        static_cast<int>(matchRow + ry)
                    ));
                }
                // Draw as 4 line segments so it renders consistently with the overlay pipeline
                for (int k = 0; k < 4; ++k) {
                    cv::Point2d a(boxPts[k].x, boxPts[k].y);
                    cv::Point2d b(boxPts[(k + 1) % 4].x, boxPts[(k + 1) % 4].y);
                    result.addLineOverlay(a, b, cv::Scalar(0, 255, 0), 1);
                }
            }

            // Add center crosshair (yellow)
            OverlayCommand centerCmd;
            centerCmd.type = OverlayCommand::DrawCrosshair;
            centerCmd.p1 = matchCenter;
            centerCmd.radius = 2;
            centerCmd.color = cv::Scalar(0, 255, 255);  // Yellow
            centerCmd.thickness = 1;
            result.overlays.push_back(centerCmd);

            // Always show the detected anchor point as a small red
            // crosshair. When a custom anchor is configured the marker sits
            // at the offset position; otherwise it overlays the yellow
            // center crosshair so the user can see the reported point.
            {
                const double ancCrossR = 2.0;
                result.addLineOverlay(
                    cv::Point2d(anchorPoint.x - ancCrossR, anchorPoint.y),
                    cv::Point2d(anchorPoint.x + ancCrossR, anchorPoint.y),
                    cv::Scalar(0, 0, 255), 1);
                result.addLineOverlay(
                    cv::Point2d(anchorPoint.x, anchorPoint.y - ancCrossR),
                    cv::Point2d(anchorPoint.x, anchorPoint.y + ancCrossR),
                    cv::Scalar(0, 0, 255), 1);
            }

            // Add text label with score and angle
            std::string label = "S=" + std::to_string(static_cast<int>(matchScore * 100)) + "% A=" +
                               std::to_string(static_cast<int>(matchAngle)) + "deg";
            result.addTextOverlay(label, cv::Point2d(matchCol + 25, matchRow - 10), cv::Scalar(0, 0, 255), 1);

            // Add point result. Keep the same measurement key naming as the edge
            // plugin so downstream consumers (execute_profile, calculate_world_position)
            // work without changes.
            std::string pointName = "match_" + std::to_string(i);
            result.addPoint(pointName, anchorPoint, cv::Point2d(0, 0), matchScore);
            result.measurements[pointName + "_score"] = matchScore;
            result.measurements[pointName + "_angle"] = matchAngle;
            result.measurements[pointName + "_row"] = matchRow;
            result.measurements[pointName + "_col"] = matchCol;
            // New naming (preferred): .x, .y, .p
            result.measurements[pointName + "_x"] = anchorPoint.x;
            result.measurements[pointName + "_y"] = anchorPoint.y;
            result.measurements[pointName + "_p"] = matchAngle;

            if (VisionManager::instance().isVerboseLogging()) {
                std::cout << "[HalconPatternMatchNCC] Match " << i << " at (" << anchorPoint.x << ", " << anchorPoint.y
                          << ") score=" << matchScore << " angle=" << matchAngle << " deg" << std::endl;
            }
        }

        // Store overall results
        result.addMeasurement("num_matches", static_cast<double>(numMatches));
        result.addMeasurement("best_score", score[0].D());
        result.addMeasurement("best_angle", angle[0].D() * 180.0 / M_PI);
        if (result.measurements.count("match_0_x")) {
            result.addMeasurement("best_x", result.measurements["match_0_x"]);
            result.addMeasurement("best_y", result.measurements["match_0_y"]);
            result.addMeasurement("best_p", result.measurements["match_0_p"]);
        }

        result.addMetadata("algorithm", "Halcon_NCCMatch");
        result.addMetadata("min_score", std::to_string(m_minScore));
        result.addMetadata("metric", m_metric);

        return true;

    } catch (HException& e) {
        m_lastError = std::string("Halcon error: ") + e.ErrorMessage().Text();
        std::cerr << "[HalconPatternMatchNCC] " << m_lastError << std::endl;
        return false;
    } catch (std::exception& e) {
        m_lastError = std::string("Error: ") + e.what();
        std::cerr << "[HalconPatternMatchNCC] " << m_lastError << std::endl;
        return false;
    }
}

bool HalconPatternMatchingNCCPlugin::validate() const {
    if (!hasTemplate()) {
        return false;
    }
    if (m_minScore < 0.1 || m_minScore > 1.0) {
        return false;
    }
    return true;
}

bool HalconPatternMatchingNCCPlugin::teachFromImage(const cv::Mat& templateImage) {
    if (templateImage.empty()) {
        std::cout << "[HalconPatternMatchNCC] Cannot teach from empty image" << std::endl;
        return false;
    }

    try {
        // Store original template (in base class)
        m_template = templateImage.clone();

        // Convert to grayscale if needed
        cv::Mat gray;
        if (templateImage.channels() == 3) {
            cv::cvtColor(templateImage, gray, cv::COLOR_BGR2GRAY);
        } else if (templateImage.channels() == 4) {
            cv::cvtColor(templateImage, gray, cv::COLOR_BGRA2GRAY);
        } else {
            gray = templateImage.clone();
        }

        // Ensure continuous data
        if (!gray.isContinuous()) {
            gray = gray.clone();
        }

        // Remember template size for pose-box visualization at match time
        m_templateWidth = gray.cols;
        m_templateHeight = gray.rows;

        // Convert to Halcon image
        HImage halconTemplate;
        halconTemplate.GenImage1("byte", static_cast<Hlong>(gray.cols), static_cast<Hlong>(gray.rows),
                                 static_cast<void*>(gray.data));

        // Convert angle tolerance to start/extent for model creation
        double angleStartRad = -m_angleTolerance * M_PI / 180.0;
        double angleExtentRad = 2.0 * m_angleTolerance * M_PI / 180.0;

        // Create NCC model. Unlike shape models, NCC has no contrast/minContrast
        // parameters — it works directly on grayscale intensities.
        // NOTE: AngleStep must be "auto" (string) or a positive double. The double
        // overload rejects sentinel values, so use the HTuple overload to pass "auto".
        m_nccModel.CreateNccModel(
            halconTemplate,
            HTuple(static_cast<Hlong>(m_numLevels)),  // NumLevels (0 = auto)
            angleStartRad,                             // AngleStart (-tolerance)
            angleExtentRad,                            // AngleExtent (2*tolerance)
            HTuple("auto"),                            // AngleStep (auto)
            HString(m_metric.c_str())                  // Metric: use_polarity or ignore_global_polarity
        );

        m_hasModel = true;

        std::cout << "[HalconPatternMatchNCC] NCC model created: " << gray.cols << "x"
                  << gray.rows << " pixels, metric=" << m_metric << std::endl;

        updatePreviewLabel();
        return true;

    } catch (HException& e) {
        m_lastError = std::string("Failed to create NCC model: ") + e.ErrorMessage().Text();
        std::cerr << "[HalconPatternMatchNCC] " << m_lastError << std::endl;
        m_hasModel = false;
        return false;
    }
}

void HalconPatternMatchingNCCPlugin::clearTemplate() {
    // Call base class implementation
    TemplateMatchingBase::clearTemplate();

    // Clear Halcon-specific data
    m_hasModel = false;
    m_templateWidth = 0;
    m_templateHeight = 0;
    m_nccModel.ClearNccModel();
}

QWidget* HalconPatternMatchingNCCPlugin::createConfigWidget(QWidget* parent) {
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    // Connect to destroyed signal to reset our pointer
    QObject::connect(m_configWidget, &QObject::destroyed, [this]() {
        m_configWidget = nullptr;
        m_minScoreSpin = nullptr;
        m_maxMatchesSpin = nullptr;
        m_numLevelsSpin = nullptr;
        m_angleToleranceSpin = nullptr;
        m_angleToleranceSlider = nullptr;
        m_angleToleranceLabel = nullptr;
        m_metricCombo = nullptr;
    });

    // Add template group from base class
    QGroupBox* templateGroup = createTemplateGroupWidget(m_configWidget);
    mainLayout->addWidget(templateGroup);

    // Search parameters group
    QGroupBox* searchGroup = new QGroupBox("Search Parameters", m_configWidget);
    QFormLayout* searchLayout = new QFormLayout(searchGroup);

    m_minScoreSpin = new QDoubleSpinBox(searchGroup);
    m_minScoreSpin->setRange(0.1, 1.0);
    m_minScoreSpin->setSingleStep(0.05);
    m_minScoreSpin->setValue(m_minScore);
    m_minScoreSpin->setDecimals(2);
    m_minScoreSpin->setToolTip("Minimum NCC score (0.1-1.0). NCC is stricter than edge matching; 0.7-0.8 is typical.");
    searchLayout->addRow("Min Score:", m_minScoreSpin);

    m_maxMatchesSpin = new QSpinBox(searchGroup);
    m_maxMatchesSpin->setRange(1, 100);
    m_maxMatchesSpin->setValue(m_maxMatches);
    m_maxMatchesSpin->setToolTip("Maximum number of matches to find");
    searchLayout->addRow("Max Matches:", m_maxMatchesSpin);

    mainLayout->addWidget(searchGroup);

    // Rotation tolerance group
    QGroupBox* transformGroup = new QGroupBox("Rotation Tolerance", m_configWidget);
    QVBoxLayout* transformLayout = new QVBoxLayout(transformGroup);

    QHBoxLayout* angleLayout = new QHBoxLayout();

    m_angleToleranceSlider = new QSlider(Qt::Horizontal, transformGroup);
    m_angleToleranceSlider->setRange(0, 90);
    m_angleToleranceSlider->setValue(static_cast<int>(m_angleTolerance));
    m_angleToleranceSlider->setTickPosition(QSlider::TicksBelow);
    m_angleToleranceSlider->setTickInterval(15);
    m_angleToleranceSlider->setToolTip("Angle tolerance: 0 = no rotation, higher = allow more rotation");
    angleLayout->addWidget(m_angleToleranceSlider);

    m_angleToleranceSpin = new QDoubleSpinBox(transformGroup);
    m_angleToleranceSpin->setRange(0.0, 90.0);
    m_angleToleranceSpin->setSingleStep(1.0);
    m_angleToleranceSpin->setValue(m_angleTolerance);
    m_angleToleranceSpin->setDecimals(1);
    m_angleToleranceSpin->setSuffix(" deg");
    m_angleToleranceSpin->setFixedWidth(80);
    m_angleToleranceSpin->setToolTip("Angle tolerance in degrees");
    angleLayout->addWidget(m_angleToleranceSpin);

    transformLayout->addLayout(angleLayout);

    QString labelText = m_angleTolerance < 0.1 ? "Search range: no rotation" :
                        QString("Search range: +/-%1 deg").arg(m_angleTolerance, 0, 'f', 1);
    m_angleToleranceLabel = new QLabel(labelText, transformGroup);
    m_angleToleranceLabel->setStyleSheet("QLabel { color: #888; font-size: 10px; }");
    m_angleToleranceLabel->setAlignment(Qt::AlignCenter);
    transformLayout->addWidget(m_angleToleranceLabel);

    mainLayout->addWidget(transformGroup);

    // Model parameters
    QGroupBox* modelGroup = new QGroupBox("Model Parameters", m_configWidget);
    QFormLayout* modelLayout = new QFormLayout(modelGroup);

    m_metricCombo = new QComboBox(modelGroup);
    m_metricCombo->addItem("ignore_global_polarity");
    m_metricCombo->addItem("use_polarity");
    m_metricCombo->setCurrentText(QString::fromStdString(m_metric));
    m_metricCombo->setToolTip("ignore_global_polarity: tolerant to lighting flips (brighter/darker).\n"
                               "use_polarity: stricter, requires same polarity as template.");
    modelLayout->addRow("Metric:", m_metricCombo);

    m_numLevelsSpin = new QSpinBox(modelGroup);
    m_numLevelsSpin->setRange(0, 10);
    m_numLevelsSpin->setValue(m_numLevels);
    m_numLevelsSpin->setToolTip("Number of pyramid levels (0=auto)");
    modelLayout->addRow("Pyramid Levels:", m_numLevelsSpin);

    mainLayout->addWidget(modelGroup);
    mainLayout->addStretch();

    // Connect signals
    QObject::connect(m_minScoreSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_minScore = val; });

    QObject::connect(m_maxMatchesSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxMatches = val; });

    QObject::connect(m_angleToleranceSlider, &QSlider::valueChanged,
            [this](int val) {
                m_angleTolerance = static_cast<double>(val);
                if (m_angleToleranceSpin) m_angleToleranceSpin->setValue(m_angleTolerance);
                if (m_angleToleranceLabel) {
                    if (val == 0) {
                        m_angleToleranceLabel->setText("Search range: no rotation");
                    } else {
                        m_angleToleranceLabel->setText(QString("Search range: +/-%1 deg").arg(val));
                    }
                }
                if (hasTemplate()) teachFromImage(m_template);
            });

    QObject::connect(m_angleToleranceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) {
                m_angleTolerance = val;
                if (m_angleToleranceSlider) m_angleToleranceSlider->setValue(static_cast<int>(val));
                if (m_angleToleranceLabel) {
                    if (val < 0.1) {
                        m_angleToleranceLabel->setText("Search range: no rotation");
                    } else {
                        m_angleToleranceLabel->setText(QString("Search range: +/-%1 deg").arg(val, 0, 'f', 1));
                    }
                }
                if (hasTemplate()) teachFromImage(m_template);
            });

    QObject::connect(m_metricCombo, &QComboBox::currentTextChanged,
            [this](const QString& text) {
                m_metric = text.toStdString();
                if (hasTemplate()) teachFromImage(m_template);
            });

    QObject::connect(m_numLevelsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) {
                m_numLevels = val;
                if (hasTemplate()) teachFromImage(m_template);
            });

    return m_configWidget;
}

nlohmann::json HalconPatternMatchingNCCPlugin::getParameters() const {
    nlohmann::json params;

    params["minScore"] = m_minScore;
    params["maxMatches"] = m_maxMatches;
    params["numLevels"] = m_numLevels;
    params["angleTolerance"] = m_angleTolerance;
    params["metric"] = m_metric;

    // Merge with base class template parameters
    nlohmann::json templateParams = getTemplateParameters();
    for (auto& [key, value] : templateParams.items()) {
        params[key] = value;
    }

    return params;
}

void HalconPatternMatchingNCCPlugin::setParameters(const nlohmann::json& params) {
    if (params.contains("minScore")) m_minScore = params["minScore"].get<double>();
    if (params.contains("maxMatches")) m_maxMatches = params["maxMatches"].get<int>();
    if (params.contains("numLevels")) m_numLevels = params["numLevels"].get<int>();
    if (params.contains("angleTolerance")) m_angleTolerance = params["angleTolerance"].get<double>();
    if (params.contains("metric")) m_metric = params["metric"].get<std::string>();

    // Set base class template parameters
    setTemplateParameters(params);

    // Update UI
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        return;
    }

    if (m_minScoreSpin) m_minScoreSpin->setValue(m_minScore);
    if (m_maxMatchesSpin) m_maxMatchesSpin->setValue(m_maxMatches);
    if (m_numLevelsSpin) m_numLevelsSpin->setValue(m_numLevels);
    if (m_angleToleranceSpin) m_angleToleranceSpin->setValue(m_angleTolerance);
    if (m_angleToleranceSlider) m_angleToleranceSlider->setValue(static_cast<int>(m_angleTolerance));
    if (m_angleToleranceLabel) {
        if (m_angleTolerance < 0.1) {
            m_angleToleranceLabel->setText("Search range: no rotation");
        } else {
            m_angleToleranceLabel->setText(QString("Search range: +/-%1 deg").arg(m_angleTolerance, 0, 'f', 1));
        }
    }
    if (m_metricCombo) m_metricCombo->setCurrentText(QString::fromStdString(m_metric));

    updatePreviewLabel();
}

nlohmann::json HalconPatternMatchingNCCPlugin::getDefaultParameters() const {
    nlohmann::json params;
    params["minScore"] = 0.7;
    params["maxMatches"] = 1;
    params["numLevels"] = 0;
    params["angleTolerance"] = 0.0;
    params["metric"] = "ignore_global_polarity";
    params["useCustomAnchor"] = false;
    params["anchorOffsetX"] = 0.0;
    params["anchorOffsetY"] = 0.0;
    return params;
}

ParameterSchema HalconPatternMatchingNCCPlugin::getParameterSchema() const {
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "minScore", "Min Score", 0.1, 1.0, 0.7,
        "Minimum NCC score (0.1-1.0)", "", 0.05, "Search"));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxMatches", "Max Matches", 1, 100, 1,
        "Maximum number of matches to find", "", 1, "Search"));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "angleTolerance", "Angle Tolerance", 0.0, 90.0, 0.0,
        "Rotation tolerance (+/- degrees, 0=no rotation)", "deg", 1.0, "Rotation"));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "numLevels", "Pyramid Levels", 0, 10, 0,
        "Number of pyramid levels (0=auto)", "", 1, "Model"));

    return schema;
}

#endif // HAS_OPENCV && HAS_HALCON
