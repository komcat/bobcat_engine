// HalconPatternMatchingEdgePlugin.cpp
// Edge-based pattern matching using Halcon's Shape-Based Matching

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

// Prevent Windows min/max macros from conflicting with std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "HalconPatternMatchingEdgePlugin.h"
#include "../VisionManager.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QThread>
#include <QCoreApplication>
#include <iostream>
#include <cmath>

using namespace HalconCpp;

HalconPatternMatchingEdgePlugin::HalconPatternMatchingEdgePlugin()
    : TemplateMatchingBase("PatternMatchEdge (Halcon)", "PR",
                           "Edge-based pattern matching using Halcon Shape-Based Matching")
{
    m_configWidget = nullptr;
    m_minScoreSpin = nullptr;
    m_maxMatchesSpin = nullptr;
    m_greedinessSpin = nullptr;
    m_numLevelsSpin = nullptr;
    m_angleToleranceSpin = nullptr;
    m_angleToleranceSlider = nullptr;
    m_angleToleranceLabel = nullptr;
    m_contrastSpin = nullptr;
    m_minContrastSpin = nullptr;
}

HalconPatternMatchingEdgePlugin::~HalconPatternMatchingEdgePlugin() {
    // Shape model is automatically cleaned up by HShapeModel destructor
}

bool HalconPatternMatchingEdgePlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) {
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

        // Find shape model
        HTuple row, col, angle, score;

        m_shapeModel.FindShapeModel(
            searchImage,
            angleStartRad,           // AngleStart (-tolerance)
            angleExtentRad,          // AngleExtent (2*tolerance)
            m_minScore,              // MinScore
            m_maxMatches,            // NumMatches
            0.5,                     // MaxOverlap
            "least_squares",         // SubPixel
            m_numLevels,             // NumLevels (0 = auto)
            m_greediness,            // Greediness
            &row, &col, &angle, &score
        );

        int numMatches = static_cast<int>(score.Length());

        if (VisionManager::instance().isVerboseLogging()) {
            std::cout << "[HalconPatternMatchEdge] Found " << numMatches << " matches" << std::endl;
        }

        if (numMatches == 0) {
            result.addMeasurement("num_matches", 0.0);
            result.addMetadata("algorithm", "Halcon_ShapeMatch");
            result.addMetadata("note", "No matches found above threshold");
            return true;
        }

        // Process each match
        for (int i = 0; i < numMatches; i++) {
            double matchRow = row[i].D();
            double matchCol = col[i].D();
            double matchAngle = angle[i].D() * 180.0 / M_PI;  // Convert to degrees
            double matchScore = score[i].D();

            // Apply anchor point offset if custom anchor is set
            cv::Point2d matchCenter(matchCol, matchRow);
            cv::Point2d anchorPoint = matchCenter;
            if (m_useCustomAnchor) {
                // Rotate offset by match angle
                double cosA = std::cos(angle[i].D());
                double sinA = std::sin(angle[i].D());
                double rotatedOffsetX = m_anchorOffsetX * cosA - m_anchorOffsetY * sinA;
                double rotatedOffsetY = m_anchorOffsetX * sinA + m_anchorOffsetY * cosA;
                anchorPoint.x += rotatedOffsetX;
                anchorPoint.y += rotatedOffsetY;
            }

            // Get model contours for visualization
            try {
                HXLDCont modelContours = m_shapeModel.GetShapeModelContours(1);

                // Transform contours to match position using HHomMat2D
                HHomMat2D homMat2D;
                homMat2D.HomMat2dIdentity();
                homMat2D = homMat2D.HomMat2dRotate(angle[i].D(), 0, 0);
                homMat2D = homMat2D.HomMat2dTranslate(matchRow, matchCol);

                HXLDCont transformedContours = modelContours.AffineTransContourXld(homMat2D);

                // Get number of contours in the object
                Hlong numContours = transformedContours.CountObj();

                // Process each contour separately
                for (Hlong c = 1; c <= numContours; c++) {
                    HXLDCont singleContour = transformedContours.SelectObj(c);

                    // Extract contour points
                    HTuple contourRows, contourCols;
                    singleContour.GetContourXld(&contourRows, &contourCols);

                    // Add contour as overlay (green)
                    if (contourRows.Length() > 1) {
                        std::vector<cv::Point> contourPoints;
                        for (Hlong j = 0; j < contourRows.Length(); j++) {
                            contourPoints.push_back(cv::Point(
                                static_cast<int>(contourCols[j].D()),
                                static_cast<int>(contourRows[j].D())
                            ));
                        }
                        OverlayCommand contourOverlay;
                        contourOverlay.type = OverlayCommand::DrawContour;
                        contourOverlay.color = cv::Scalar(0, 255, 0);  // Green
                        contourOverlay.thickness = 2;
                        contourOverlay.contour = contourPoints;
                        result.overlays.push_back(contourOverlay);
                    }
                }
            } catch (HException& contourEx) {
                // Contour visualization failed, but match was found - continue without visualization
                if (VisionManager::instance().isVerboseLogging()) {
                    std::cout << "[HalconPatternMatchEdge] Warning: Could not visualize contours: "
                              << contourEx.ErrorMessage().Text() << std::endl;
                }
            }

            // Add center crosshair (yellow)
            OverlayCommand centerCmd;
            centerCmd.type = OverlayCommand::DrawCrosshair;
            centerCmd.p1 = matchCenter;
            centerCmd.radius = 20;
            centerCmd.color = cv::Scalar(0, 255, 255);  // Yellow
            centerCmd.thickness = 2;
            result.overlays.push_back(centerCmd);

            // Always show the detected anchor point as a red filled dot +
            // small red crosshair. When a custom anchor is configured the
            // marker sits at the offset position; otherwise it overlays the
            // yellow center crosshair so the user can see the reported point.
            result.addPointOverlay(anchorPoint, cv::Scalar(0, 0, 255), 5);
            {
                const double ancCrossR = 8.0;
                result.addLineOverlay(
                    cv::Point2d(anchorPoint.x - ancCrossR, anchorPoint.y),
                    cv::Point2d(anchorPoint.x + ancCrossR, anchorPoint.y),
                    cv::Scalar(0, 0, 255), 2);
                result.addLineOverlay(
                    cv::Point2d(anchorPoint.x, anchorPoint.y - ancCrossR),
                    cv::Point2d(anchorPoint.x, anchorPoint.y + ancCrossR),
                    cv::Scalar(0, 0, 255), 2);
            }

            // Add text label with score and angle
            std::string label = "S=" + std::to_string(static_cast<int>(matchScore * 100)) + "% A=" +
                               std::to_string(static_cast<int>(matchAngle)) + "deg";
            result.addTextOverlay(label, cv::Point2d(matchCol + 25, matchRow - 10), cv::Scalar(0, 0, 255), 1);

            // Add point result
            std::string pointName = "match_" + std::to_string(i);
            result.addPoint(pointName, anchorPoint, cv::Point2d(0, 0), matchScore);
            result.measurements[pointName + "_score"] = matchScore;
            result.measurements[pointName + "_angle"] = matchAngle;
            result.measurements[pointName + "_row"] = matchRow;
            result.measurements[pointName + "_col"] = matchCol;

            if (VisionManager::instance().isVerboseLogging()) {
                std::cout << "[HalconPatternMatchEdge] Match " << i << " at (" << anchorPoint.x << ", " << anchorPoint.y
                          << ") score=" << matchScore << " angle=" << matchAngle << " deg" << std::endl;
            }
        }

        // Detected-edges visualization overlay (yellow contours of the input
        // image's edges). Independent of Halcon's shape matching — purely so
        // the user can see what edges exist in the image vs the green template
        // model contours drawn at each match location.
        {
            cv::Mat visEdges;
            cv::Mat visBlurred;
            const int visBlurSize = 5;       // fixed for visualization
            const double visT1 = 50.0;
            const double visT2 = 150.0;
            cv::GaussianBlur(gray, visBlurred, cv::Size(visBlurSize, visBlurSize), 0);
            cv::Canny(visBlurred, visEdges, visT1, visT2);

            std::vector<std::vector<cv::Point>> detectedContours;
            cv::findContours(visEdges, detectedContours, cv::RETR_LIST, cv::CHAIN_APPROX_NONE);
            for (const auto& contour : detectedContours) {
                if (contour.size() < 2) continue;
                OverlayCommand overlay;
                overlay.type = OverlayCommand::DrawContour;
                overlay.color = cv::Scalar(0, 255, 255);  // Yellow (BGR)
                overlay.thickness = 1;
                overlay.contour = contour;
                result.overlays.push_back(overlay);
            }
        }

        // Store overall results
        result.addMeasurement("num_matches", static_cast<double>(numMatches));
        result.addMeasurement("best_score", score[0].D());
        result.addMeasurement("best_angle", angle[0].D() * 180.0 / M_PI);

        result.addMetadata("algorithm", "Halcon_ShapeMatch");
        result.addMetadata("min_score", std::to_string(m_minScore));
        result.addMetadata("greediness", std::to_string(m_greediness));

        return true;

    } catch (HException& e) {
        m_lastError = std::string("Halcon error: ") + e.ErrorMessage().Text();
        std::cerr << "[HalconPatternMatchEdge] " << m_lastError << std::endl;
        return false;
    } catch (std::exception& e) {
        m_lastError = std::string("Error: ") + e.what();
        std::cerr << "[HalconPatternMatchEdge] " << m_lastError << std::endl;
        return false;
    }
}

bool HalconPatternMatchingEdgePlugin::validate() const {
    if (!hasTemplate()) {
        return false;
    }
    if (m_minScore < 0.1 || m_minScore > 1.0) {
        return false;
    }
    return true;
}

bool HalconPatternMatchingEdgePlugin::teachFromImage(const cv::Mat& templateImage) {
    if (templateImage.empty()) {
        std::cout << "[HalconPatternMatchEdge] Cannot teach from empty image" << std::endl;
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

        // Convert to Halcon image
        HImage halconTemplate;
        halconTemplate.GenImage1("byte", static_cast<Hlong>(gray.cols), static_cast<Hlong>(gray.rows),
                                 static_cast<void*>(gray.data));

        // Convert angle tolerance to start/extent for model creation
        // Tolerance of X degrees means model covers -X to +X (total extent = 2*X)
        double angleStartRad = -m_angleTolerance * M_PI / 180.0;
        double angleExtentRad = 2.0 * m_angleTolerance * M_PI / 180.0;

        // Create shape model
        m_shapeModel.CreateShapeModel(
            halconTemplate,
            m_numLevels,             // NumLevels (0 = auto)
            angleStartRad,           // AngleStart (-tolerance)
            angleExtentRad,          // AngleExtent (2*tolerance)
            "auto",                  // AngleStep (auto)
            "none",                  // Optimization
            "use_polarity",          // Metric - use edge polarity
            m_contrast,              // Contrast
            m_minContrast            // MinContrast
        );

        m_hasModel = true;

        std::cout << "[HalconPatternMatchEdge] Shape model created: " << gray.cols << "x"
                  << gray.rows << " pixels" << std::endl;

        updatePreviewLabel();
        return true;

    } catch (HException& e) {
        m_lastError = std::string("Failed to create shape model: ") + e.ErrorMessage().Text();
        std::cerr << "[HalconPatternMatchEdge] " << m_lastError << std::endl;
        m_hasModel = false;
        return false;
    }
}

void HalconPatternMatchingEdgePlugin::clearTemplate() {
    // Call base class implementation
    TemplateMatchingBase::clearTemplate();

    // Clear Halcon-specific data
    m_hasModel = false;
    m_shapeModel.Clear();
}

QWidget* HalconPatternMatchingEdgePlugin::createConfigWidget(QWidget* parent) {
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    // Connect to destroyed signal to reset our pointer
    QObject::connect(m_configWidget, &QObject::destroyed, [this]() {
        m_configWidget = nullptr;
        m_minScoreSpin = nullptr;
        m_maxMatchesSpin = nullptr;
        m_greedinessSpin = nullptr;
        m_numLevelsSpin = nullptr;
        m_angleToleranceSpin = nullptr;
        m_angleToleranceSlider = nullptr;
        m_angleToleranceLabel = nullptr;
        m_contrastSpin = nullptr;
        m_minContrastSpin = nullptr;
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
    m_minScoreSpin->setToolTip("Minimum match score (0.1-1.0)");
    searchLayout->addRow("Min Score:", m_minScoreSpin);

    m_maxMatchesSpin = new QSpinBox(searchGroup);
    m_maxMatchesSpin->setRange(1, 100);
    m_maxMatchesSpin->setValue(m_maxMatches);
    m_maxMatchesSpin->setToolTip("Maximum number of matches to find");
    searchLayout->addRow("Max Matches:", m_maxMatchesSpin);

    m_greedinessSpin = new QDoubleSpinBox(searchGroup);
    m_greedinessSpin->setRange(0.0, 1.0);
    m_greedinessSpin->setSingleStep(0.1);
    m_greedinessSpin->setValue(m_greediness);
    m_greedinessSpin->setDecimals(2);
    m_greedinessSpin->setToolTip("Search greediness (0=thorough/slow, 1=fast/may miss)");
    searchLayout->addRow("Greediness:", m_greedinessSpin);

    mainLayout->addWidget(searchGroup);

    // Rotation tolerance group
    QGroupBox* transformGroup = new QGroupBox("Rotation Tolerance", m_configWidget);
    QVBoxLayout* transformLayout = new QVBoxLayout(transformGroup);

    // Angle tolerance with slider and spinbox
    QHBoxLayout* angleLayout = new QHBoxLayout();

    m_angleToleranceSlider = new QSlider(Qt::Horizontal, transformGroup);
    m_angleToleranceSlider->setRange(0, 90);  // 0 to 90 degrees
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

    // Label showing the search range
    QString labelText = m_angleTolerance < 0.1 ? "Search range: no rotation" :
                        QString("Search range: +/-%1 deg").arg(m_angleTolerance, 0, 'f', 1);
    m_angleToleranceLabel = new QLabel(labelText, transformGroup);
    m_angleToleranceLabel->setStyleSheet("QLabel { color: #888; font-size: 10px; }");
    m_angleToleranceLabel->setAlignment(Qt::AlignCenter);
    transformLayout->addWidget(m_angleToleranceLabel);

    mainLayout->addWidget(transformGroup);

    // Model creation parameters
    QGroupBox* modelGroup = new QGroupBox("Model Parameters", m_configWidget);
    QFormLayout* modelLayout = new QFormLayout(modelGroup);

    m_contrastSpin = new QSpinBox(modelGroup);
    m_contrastSpin->setRange(1, 255);
    m_contrastSpin->setValue(m_contrast);
    m_contrastSpin->setToolTip("Edge contrast threshold for model creation");
    modelLayout->addRow("Contrast:", m_contrastSpin);

    m_minContrastSpin = new QSpinBox(modelGroup);
    m_minContrastSpin->setRange(1, 255);
    m_minContrastSpin->setValue(m_minContrast);
    m_minContrastSpin->setToolTip("Minimum contrast for matching");
    modelLayout->addRow("Min Contrast:", m_minContrastSpin);

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

    QObject::connect(m_greedinessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_greediness = val; });

    // Angle tolerance - sync slider and spinbox
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

    QObject::connect(m_contrastSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) {
                m_contrast = val;
                if (hasTemplate()) teachFromImage(m_template);
            });

    QObject::connect(m_minContrastSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) {
                m_minContrast = val;
                if (hasTemplate()) teachFromImage(m_template);
            });

    QObject::connect(m_numLevelsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) {
                m_numLevels = val;
                if (hasTemplate()) teachFromImage(m_template);
            });

    return m_configWidget;
}

nlohmann::json HalconPatternMatchingEdgePlugin::getParameters() const {
    nlohmann::json params;

    params["minScore"] = m_minScore;
    params["maxMatches"] = m_maxMatches;
    params["greediness"] = m_greediness;
    params["numLevels"] = m_numLevels;
    params["angleTolerance"] = m_angleTolerance;
    params["scaleMin"] = m_scaleMin;
    params["scaleMax"] = m_scaleMax;
    params["contrast"] = m_contrast;
    params["minContrast"] = m_minContrast;

    // Merge with base class template parameters
    nlohmann::json templateParams = getTemplateParameters();
    for (auto& [key, value] : templateParams.items()) {
        params[key] = value;
    }

    return params;
}

void HalconPatternMatchingEdgePlugin::setParameters(const nlohmann::json& params) {
    if (params.contains("minScore")) m_minScore = params["minScore"].get<double>();
    if (params.contains("maxMatches")) m_maxMatches = params["maxMatches"].get<int>();
    if (params.contains("greediness")) m_greediness = params["greediness"].get<double>();
    if (params.contains("numLevels")) m_numLevels = params["numLevels"].get<int>();
    if (params.contains("angleTolerance")) m_angleTolerance = params["angleTolerance"].get<double>();
    if (params.contains("scaleMin")) m_scaleMin = params["scaleMin"].get<double>();
    if (params.contains("scaleMax")) m_scaleMax = params["scaleMax"].get<double>();
    if (params.contains("contrast")) m_contrast = params["contrast"].get<int>();
    if (params.contains("minContrast")) m_minContrast = params["minContrast"].get<int>();

    // Set base class template parameters
    setTemplateParameters(params);

    // Update UI
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        return;
    }

    if (m_minScoreSpin) m_minScoreSpin->setValue(m_minScore);
    if (m_maxMatchesSpin) m_maxMatchesSpin->setValue(m_maxMatches);
    if (m_greedinessSpin) m_greedinessSpin->setValue(m_greediness);
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
    if (m_contrastSpin) m_contrastSpin->setValue(m_contrast);
    if (m_minContrastSpin) m_minContrastSpin->setValue(m_minContrast);

    updatePreviewLabel();
}

nlohmann::json HalconPatternMatchingEdgePlugin::getDefaultParameters() const {
    nlohmann::json params;
    params["minScore"] = 0.5;
    params["maxMatches"] = 1;
    params["greediness"] = 0.9;
    params["numLevels"] = 0;
    params["angleTolerance"] = 0.0;
    params["scaleMin"] = 1.0;
    params["scaleMax"] = 1.0;
    params["contrast"] = 30;
    params["minContrast"] = 10;
    params["useCustomAnchor"] = false;
    params["anchorOffsetX"] = 0.0;
    params["anchorOffsetY"] = 0.0;
    return params;
}

ParameterSchema HalconPatternMatchingEdgePlugin::getParameterSchema() const {
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "minScore", "Min Score", 0.1, 1.0, 0.5,
        "Minimum match score (0.1-1.0)", "", 0.05, "Search"));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxMatches", "Max Matches", 1, 100, 1,
        "Maximum number of matches to find", "", 1, "Search"));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "greediness", "Greediness", 0.0, 1.0, 0.9,
        "Search greediness (0=thorough, 1=fast)", "", 0.1, "Search"));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "angleTolerance", "Angle Tolerance", 0.0, 90.0, 0.0,
        "Rotation tolerance (+/- degrees, 0=no rotation)", "deg", 1.0, "Rotation"));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "contrast", "Contrast", 1, 255, 30,
        "Edge contrast threshold for model", "", 5, "Model"));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "minContrast", "Min Contrast", 1, 255, 10,
        "Minimum contrast for matching", "", 5, "Model"));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "numLevels", "Pyramid Levels", 0, 10, 0,
        "Number of pyramid levels (0=auto)", "", 1, "Model"));

    return schema;
}

#endif // HAS_OPENCV && HAS_HALCON
