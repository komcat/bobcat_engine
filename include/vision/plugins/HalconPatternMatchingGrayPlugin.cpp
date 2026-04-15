// HalconPatternMatchingGrayPlugin.cpp
// Gray-value template matching using Halcon's HTemplate (best_match / best_match_rot)

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "HalconPatternMatchingGrayPlugin.h"
#include "../VisionManager.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QThread>
#include <QCoreApplication>
#include <iostream>
#include <cmath>

using namespace HalconCpp;

HalconPatternMatchingGrayPlugin::HalconPatternMatchingGrayPlugin()
    : TemplateMatchingBase("PatternMatchGray (Halcon)", "PR",
                           "Gray-value template matching (Halcon best_match). Reports mean gray-value error; lower is better.")
{
    m_configWidget = nullptr;
    m_maxErrorSpin = nullptr;
    m_numLevelsSpin = nullptr;
    m_angleToleranceSpin = nullptr;
    m_angleToleranceSlider = nullptr;
    m_angleToleranceLabel = nullptr;
    m_optimizeCombo = nullptr;
    m_grayValuesCombo = nullptr;
}

HalconPatternMatchingGrayPlugin::~HalconPatternMatchingGrayPlugin() {
}

bool HalconPatternMatchingGrayPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) {
    if (!hasTemplate()) {
        m_lastError = "No template taught. Please teach a template first.";
        return false;
    }
    if (input.empty()) {
        m_lastError = "Input image is empty";
        return false;
    }

    try {
        cv::Mat gray;
        if (input.channels() == 3) {
            cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
        } else if (input.channels() == 4) {
            cv::cvtColor(input, gray, cv::COLOR_BGRA2GRAY);
        } else {
            gray = input.clone();
        }
        if (!gray.isContinuous()) gray = gray.clone();

        if (input.channels() == 1) {
            cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
        } else if (input.channels() == 4) {
            cv::cvtColor(input, output, cv::COLOR_BGRA2BGR);
        } else {
            output = input.clone();
        }

        HImage searchImage;
        searchImage.GenImage1("byte", static_cast<Hlong>(gray.cols), static_cast<Hlong>(gray.rows),
                              static_cast<void*>(gray.data));

        double angleStartRad = -m_angleTolerance * M_PI / 180.0;
        double angleExtentRad = 2.0 * m_angleTolerance * M_PI / 180.0;

        // Branch on whether rotation was taught. IMPORTANT: use the Mg
        // (multi-grid) variants — plain BestMatch / BestMatchRot search only
        // at the full-resolution level and are ~100x slower on large images.
        // The Mg variants use the pyramid that CreateTemplate(Rot) builds.
        //
        // NumLevels is the number of pyramid levels to search on and must be
        // >= 1. Passing 0 causes an access violation inside halcon.dll.
        // WhichLevels (-1) selects sub-pixel refinement automatically.
        double mRow = 0.0, mCol = 0.0, mAngle = 0.0, mError = 0.0;
        const Hlong numLevels = static_cast<Hlong>(std::max(1, m_actualLevels));
        const Hlong whichLevels = static_cast<Hlong>(-1);
        if (m_angleTolerance < 0.01) {
            m_template_handle.BestMatchMg(
                searchImage,
                m_maxError,
                HString(m_subPixel.c_str()),
                numLevels,
                whichLevels,
                &mRow, &mCol, &mError
            );
            mAngle = 0.0;
        } else {
            double bmRow = 0.0, bmCol = 0.0, bmAngle = 0.0, bmError = 0.0;
            // BestMatchRotMg does not take WhichLevels — only NumLevels.
            m_template_handle.BestMatchRotMg(
                searchImage,
                angleStartRad,
                angleExtentRad,
                m_maxError,
                HString(m_subPixel.c_str()),
                numLevels,
                &bmRow, &bmCol, &bmAngle, &bmError
            );
            mRow = bmRow; mCol = bmCol; mAngle = bmAngle; mError = bmError;
        }

        // Halcon returns sentinel values when no match is found. Depending
        // on the SDK version and the failure mode, we may see ANY of:
        //   - An exception thrown (handled in the outer catch block below)
        //   - mRow = -1 (classic sentinel)
        //   - mRow = 0, mCol = 0, mError = 255 (silent failure — the mg
        //     variants don't always throw when the search fails to find a
        //     minimum below MaxError)
        //   - mError >= the MaxError we passed in (above threshold → invalid)
        //
        // Previously we only checked `mRow < 0.0`, which let the second
        // sentinel form through and caused the plugin to report a bogus
        // match at (0,0) with score 0. Now we reject any result whose error
        // exceeds the threshold we specified.
        const double errorEpsilon = 1e-6;
        const bool noMatch =
            (mRow < 0.0) ||
            (mError > m_maxError + errorEpsilon) ||
            (mError >= 254.9);  // Halcon's max-error sentinel

        if (noMatch) {
            result.addMeasurement("num_matches", 0.0);
            result.addMetadata("algorithm", "Halcon_GrayTemplate");
            result.addMetadata("note", "No matches found below MaxError threshold");
            if (VisionManager::instance().isVerboseLogging()) {
                std::cout << "[HalconPatternMatchGray] No match: mRow=" << mRow
                          << " mCol=" << mCol << " mError=" << mError
                          << " (threshold=" << m_maxError << ")" << std::endl;
            }
            return true;
        }

        int numMatches = 1;

        if (VisionManager::instance().isVerboseLogging()) {
            std::cout << "[HalconPatternMatchGray] Match at row=" << mRow << " col=" << mCol
                      << " angle=" << (mAngle * 180.0 / M_PI) << " deg error=" << mError << std::endl;
        }

        double matchRow = mRow;
        double matchCol = mCol;
        double matchAngleRad = mAngle;
        double matchAngle = matchAngleRad * 180.0 / M_PI;
        double matchError = mError;

        cv::Point2d matchCenter(matchCol, matchRow);
        cv::Point2d anchorPoint = matchCenter;
        if (m_useCustomAnchor) {
            double cosA = std::cos(matchAngleRad);
            double sinA = std::sin(matchAngleRad);
            double rotatedOffsetX = m_anchorOffsetX * cosA - m_anchorOffsetY * sinA;
            double rotatedOffsetY = m_anchorOffsetX * sinA + m_anchorOffsetY * cosA;
            anchorPoint.x += rotatedOffsetX;
            anchorPoint.y += rotatedOffsetY;
        }

        // Rotated bounding box (green)
        if (m_templateWidth > 0 && m_templateHeight > 0) {
            double halfW = m_templateWidth / 2.0;
            double halfH = m_templateHeight / 2.0;
            double localX[4] = { -halfW,  halfW, halfW, -halfW };
            double localY[4] = { -halfH, -halfH, halfH,  halfH };
            double cosA = std::cos(matchAngleRad);
            double sinA = std::sin(matchAngleRad);
            std::vector<cv::Point2d> boxPts;
            boxPts.reserve(4);
            for (int k = 0; k < 4; ++k) {
                double rx = localX[k] * cosA - localY[k] * sinA;
                double ry = localX[k] * sinA + localY[k] * cosA;
                boxPts.emplace_back(matchCol + rx, matchRow + ry);
            }
            for (int k = 0; k < 4; ++k) {
                result.addLineOverlay(boxPts[k], boxPts[(k + 1) % 4], cv::Scalar(0, 255, 0), 1);
            }
        }

        // Yellow center crosshair
        OverlayCommand centerCmd;
        centerCmd.type = OverlayCommand::DrawCrosshair;
        centerCmd.p1 = matchCenter;
        centerCmd.radius = 2;
        centerCmd.color = cv::Scalar(0, 255, 255);
        centerCmd.thickness = 1;
        result.overlays.push_back(centerCmd);

        // Red anchor marker
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

        // Label shows error (lower = better) rather than score
        std::string label = "E=" + std::to_string(static_cast<int>(matchError)) + " A=" +
                           std::to_string(static_cast<int>(matchAngle)) + "deg";
        result.addTextOverlay(label, cv::Point2d(matchCol + 25, matchRow - 10), cv::Scalar(0, 0, 255), 1);

        // Record result. We fake a "score" = 1.0 - (error / 255) so that
        // downstream code comparing scores keeps working (higher = better),
        // but we also keep the raw error in measurements.
        double pseudoScore = 1.0 - (matchError / 255.0);
        if (pseudoScore < 0.0) pseudoScore = 0.0;

        std::string pointName = "match_0";
        result.addPoint(pointName, anchorPoint, cv::Point2d(0, 0), pseudoScore);
        result.measurements[pointName + "_score"] = pseudoScore;
        result.measurements[pointName + "_error"] = matchError;
        result.measurements[pointName + "_angle"] = matchAngle;
        result.measurements[pointName + "_row"] = matchRow;
        result.measurements[pointName + "_col"] = matchCol;
        // New naming (preferred): .x, .y, .p
        result.measurements[pointName + "_x"] = anchorPoint.x;
        result.measurements[pointName + "_y"] = anchorPoint.y;
        result.measurements[pointName + "_p"] = matchAngle;

        result.addMeasurement("num_matches", static_cast<double>(numMatches));
        result.addMeasurement("best_score", pseudoScore);
        result.addMeasurement("best_error", matchError);
        result.addMeasurement("best_angle", matchAngle);
        result.addMeasurement("best_x", anchorPoint.x);
        result.addMeasurement("best_y", anchorPoint.y);
        result.addMeasurement("best_p", matchAngle);

        result.addMetadata("algorithm", "Halcon_GrayTemplate");
        result.addMetadata("max_error", std::to_string(m_maxError));
        result.addMetadata("optimize", m_optimize);
        result.addMetadata("gray_values", m_grayValues);

        return true;

    } catch (HException& e) {
        // Halcon may throw when no match found — treat as "no match" not error
        const char* msg = e.ErrorMessage().Text();
        std::string msgStr = msg ? msg : "";
        if (msgStr.find("not found") != std::string::npos ||
            msgStr.find("no match") != std::string::npos ||
            e.ErrorCode() == 3562) {
            result.addMeasurement("num_matches", 0.0);
            result.addMetadata("algorithm", "Halcon_GrayTemplate");
            result.addMetadata("note", "No matches found below MaxError threshold");
            return true;
        }
        m_lastError = std::string("Halcon error: ") + msgStr;
        std::cerr << "[HalconPatternMatchGray] " << m_lastError << std::endl;
        return false;
    } catch (std::exception& e) {
        m_lastError = std::string("Error: ") + e.what();
        std::cerr << "[HalconPatternMatchGray] " << m_lastError << std::endl;
        return false;
    }
}

bool HalconPatternMatchingGrayPlugin::validate() const {
    if (!hasTemplate()) return false;
    if (m_maxError < 0.0 || m_maxError > 255.0) return false;
    return true;
}

bool HalconPatternMatchingGrayPlugin::teachFromImage(const cv::Mat& templateImage) {
    if (templateImage.empty()) {
        std::cout << "[HalconPatternMatchGray] Cannot teach from empty image" << std::endl;
        return false;
    }

    try {
        m_template = templateImage.clone();

        cv::Mat gray;
        if (templateImage.channels() == 3) {
            cv::cvtColor(templateImage, gray, cv::COLOR_BGR2GRAY);
        } else if (templateImage.channels() == 4) {
            cv::cvtColor(templateImage, gray, cv::COLOR_BGRA2GRAY);
        } else {
            gray = templateImage.clone();
        }
        if (!gray.isContinuous()) gray = gray.clone();

        m_templateWidth = gray.cols;
        m_templateHeight = gray.rows;

        HImage halconTemplate;
        halconTemplate.GenImage1("byte", static_cast<Hlong>(gray.cols), static_cast<Hlong>(gray.rows),
                                 static_cast<void*>(gray.data));

        double angleStartRad = -m_angleTolerance * M_PI / 180.0;
        double angleExtentRad = 2.0 * m_angleTolerance * M_PI / 180.0;

        // Determine pyramid levels. Halcon's CreateTemplateRot requires
        // NumLevel >= 1 — 0 is not valid. If user leaves m_numLevels at 0,
        // pick a reasonable default based on template size.
        Hlong numLevels = static_cast<Hlong>(m_numLevels);
        if (numLevels < 1) {
            int minDim = std::min(gray.cols, gray.rows);
            numLevels = 1;
            while (minDim >= 32 && numLevels < 4) {
                minDim /= 2;
                numLevels++;
            }
        }
        // Remember the depth so BestMatchMg / BestMatchRotMg can pass it
        // back in — NumLevels=0 at match time causes an access violation.
        m_actualLevels = static_cast<int>(numLevels);

        // Branch: if no rotation requested use CreateTemplate (the rot variant
        // rejects AngleStep=0, and there is no HTuple "auto" overload here).
        if (m_angleTolerance < 0.01) {
            // FirstError = 255 means "accept any gray-value difference" — we
            // control the real threshold at match time via BestMatch(MaxError).
            m_template_handle.CreateTemplate(
                halconTemplate,
                static_cast<Hlong>(255),            // FirstError (permissive)
                numLevels,                          // NumLevel
                HString(m_optimize.c_str()),        // Optimize
                HString(m_grayValues.c_str())       // GrayValues
            );
        } else {
            // AngleStep must be > 0. 1 degree is a good general-purpose
            // default — fine enough for industrial alignment, coarse enough
            // to keep teach time reasonable. A previous version used
            // asin(sqrt(2)/maxDim) which produced ~0.2 deg on a 355 px
            // template and made matching ~20x slower with no real benefit.
            const double angleStepRad = 1.0 * M_PI / 180.0;

            m_template_handle.CreateTemplateRot(
                halconTemplate,
                numLevels,
                angleStartRad,
                angleExtentRad,
                angleStepRad,
                HString(m_optimize.c_str()),
                HString(m_grayValues.c_str())
            );
        }

        m_hasModel = true;

        std::cout << "[HalconPatternMatchGray] Gray template created: " << gray.cols << "x"
                  << gray.rows << " pixels, levels=" << numLevels << std::endl;

        updatePreviewLabel();
        return true;

    } catch (HException& e) {
        m_lastError = std::string("Failed to create gray template: ") + e.ErrorMessage().Text();
        std::cerr << "[HalconPatternMatchGray] " << m_lastError << std::endl;
        m_hasModel = false;
        return false;
    }
}

void HalconPatternMatchingGrayPlugin::clearTemplate() {
    TemplateMatchingBase::clearTemplate();
    m_hasModel = false;
    m_templateWidth = 0;
    m_templateHeight = 0;
    try {
        m_template_handle.Clear();
    } catch (HException&) {
        // ignore
    }
}

QWidget* HalconPatternMatchingGrayPlugin::createConfigWidget(QWidget* parent) {
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    QObject::connect(m_configWidget, &QObject::destroyed, [this]() {
        m_configWidget = nullptr;
        m_maxErrorSpin = nullptr;
        m_numLevelsSpin = nullptr;
        m_angleToleranceSpin = nullptr;
        m_angleToleranceSlider = nullptr;
        m_angleToleranceLabel = nullptr;
        m_optimizeCombo = nullptr;
        m_grayValuesCombo = nullptr;
    });

    QGroupBox* templateGroup = createTemplateGroupWidget(m_configWidget);
    mainLayout->addWidget(templateGroup);

    QGroupBox* searchGroup = new QGroupBox("Search Parameters", m_configWidget);
    QFormLayout* searchLayout = new QFormLayout(searchGroup);

    m_maxErrorSpin = new QDoubleSpinBox(searchGroup);
    m_maxErrorSpin->setRange(0.0, 255.0);
    m_maxErrorSpin->setSingleStep(1.0);
    m_maxErrorSpin->setValue(m_maxError);
    m_maxErrorSpin->setDecimals(1);
    m_maxErrorSpin->setToolTip("Max mean gray-value difference (0-255). Lower = stricter. Typical: 20-40.");
    searchLayout->addRow("Max Error:", m_maxErrorSpin);

    mainLayout->addWidget(searchGroup);

    QGroupBox* transformGroup = new QGroupBox("Rotation Tolerance", m_configWidget);
    QVBoxLayout* transformLayout = new QVBoxLayout(transformGroup);
    QHBoxLayout* angleLayout = new QHBoxLayout();

    m_angleToleranceSlider = new QSlider(Qt::Horizontal, transformGroup);
    m_angleToleranceSlider->setRange(0, 90);
    m_angleToleranceSlider->setValue(static_cast<int>(m_angleTolerance));
    m_angleToleranceSlider->setTickPosition(QSlider::TicksBelow);
    m_angleToleranceSlider->setTickInterval(15);
    angleLayout->addWidget(m_angleToleranceSlider);

    m_angleToleranceSpin = new QDoubleSpinBox(transformGroup);
    m_angleToleranceSpin->setRange(0.0, 90.0);
    m_angleToleranceSpin->setSingleStep(1.0);
    m_angleToleranceSpin->setValue(m_angleTolerance);
    m_angleToleranceSpin->setDecimals(1);
    m_angleToleranceSpin->setSuffix(" deg");
    m_angleToleranceSpin->setFixedWidth(80);
    angleLayout->addWidget(m_angleToleranceSpin);
    transformLayout->addLayout(angleLayout);

    QString labelText = m_angleTolerance < 0.1 ? "Search range: no rotation" :
                        QString("Search range: +/-%1 deg").arg(m_angleTolerance, 0, 'f', 1);
    m_angleToleranceLabel = new QLabel(labelText, transformGroup);
    m_angleToleranceLabel->setStyleSheet("QLabel { color: #888; font-size: 10px; }");
    m_angleToleranceLabel->setAlignment(Qt::AlignCenter);
    transformLayout->addWidget(m_angleToleranceLabel);

    mainLayout->addWidget(transformGroup);

    QGroupBox* modelGroup = new QGroupBox("Model Parameters", m_configWidget);
    QFormLayout* modelLayout = new QFormLayout(modelGroup);

    m_grayValuesCombo = new QComboBox(modelGroup);
    m_grayValuesCombo->addItem("normalized");
    m_grayValuesCombo->addItem("not_normalized");
    m_grayValuesCombo->setCurrentText(QString::fromStdString(m_grayValues));
    m_grayValuesCombo->setToolTip("normalized: tolerates linear brightness change.\n"
                                   "not_normalized: strict pixel comparison.");
    modelLayout->addRow("Gray Values:", m_grayValuesCombo);

    m_optimizeCombo = new QComboBox(modelGroup);
    m_optimizeCombo->addItem("none");
    m_optimizeCombo->addItem("sort");
    m_optimizeCombo->addItem("point_reduction_low");
    m_optimizeCombo->addItem("point_reduction_high");
    m_optimizeCombo->setCurrentText(QString::fromStdString(m_optimize));
    m_optimizeCombo->setToolTip("Speed vs accuracy trade-off.");
    modelLayout->addRow("Optimize:", m_optimizeCombo);

    m_numLevelsSpin = new QSpinBox(modelGroup);
    m_numLevelsSpin->setRange(0, 10);
    m_numLevelsSpin->setValue(m_numLevels);
    m_numLevelsSpin->setToolTip("Pyramid levels (0=auto based on template size)");
    modelLayout->addRow("Pyramid Levels:", m_numLevelsSpin);

    mainLayout->addWidget(modelGroup);
    mainLayout->addStretch();

    QObject::connect(m_maxErrorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_maxError = val; });

    QObject::connect(m_angleToleranceSlider, &QSlider::valueChanged,
            [this](int val) {
                m_angleTolerance = static_cast<double>(val);
                if (m_angleToleranceSpin) m_angleToleranceSpin->setValue(m_angleTolerance);
                if (m_angleToleranceLabel) {
                    if (val == 0) m_angleToleranceLabel->setText("Search range: no rotation");
                    else m_angleToleranceLabel->setText(QString("Search range: +/-%1 deg").arg(val));
                }
                if (hasTemplate()) teachFromImage(m_template);
            });

    QObject::connect(m_angleToleranceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) {
                m_angleTolerance = val;
                if (m_angleToleranceSlider) m_angleToleranceSlider->setValue(static_cast<int>(val));
                if (m_angleToleranceLabel) {
                    if (val < 0.1) m_angleToleranceLabel->setText("Search range: no rotation");
                    else m_angleToleranceLabel->setText(QString("Search range: +/-%1 deg").arg(val, 0, 'f', 1));
                }
                if (hasTemplate()) teachFromImage(m_template);
            });

    QObject::connect(m_grayValuesCombo, &QComboBox::currentTextChanged,
            [this](const QString& text) {
                m_grayValues = text.toStdString();
                if (hasTemplate()) teachFromImage(m_template);
            });

    QObject::connect(m_optimizeCombo, &QComboBox::currentTextChanged,
            [this](const QString& text) {
                m_optimize = text.toStdString();
                if (hasTemplate()) teachFromImage(m_template);
            });

    QObject::connect(m_numLevelsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) {
                m_numLevels = val;
                if (hasTemplate()) teachFromImage(m_template);
            });

    return m_configWidget;
}

nlohmann::json HalconPatternMatchingGrayPlugin::getParameters() const {
    nlohmann::json params;
    params["maxError"] = m_maxError;
    params["numLevels"] = m_numLevels;
    params["angleTolerance"] = m_angleTolerance;
    params["optimize"] = m_optimize;
    params["grayValues"] = m_grayValues;

    nlohmann::json templateParams = getTemplateParameters();
    for (auto& [key, value] : templateParams.items()) {
        params[key] = value;
    }
    return params;
}

void HalconPatternMatchingGrayPlugin::setParameters(const nlohmann::json& params) {
    if (params.contains("maxError")) m_maxError = params["maxError"].get<double>();
    if (params.contains("numLevels")) m_numLevels = params["numLevels"].get<int>();
    if (params.contains("angleTolerance")) m_angleTolerance = params["angleTolerance"].get<double>();
    if (params.contains("optimize")) m_optimize = params["optimize"].get<std::string>();
    if (params.contains("grayValues")) m_grayValues = params["grayValues"].get<std::string>();

    setTemplateParameters(params);

    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        return;
    }

    if (m_maxErrorSpin) m_maxErrorSpin->setValue(m_maxError);
    if (m_numLevelsSpin) m_numLevelsSpin->setValue(m_numLevels);
    if (m_angleToleranceSpin) m_angleToleranceSpin->setValue(m_angleTolerance);
    if (m_angleToleranceSlider) m_angleToleranceSlider->setValue(static_cast<int>(m_angleTolerance));
    if (m_angleToleranceLabel) {
        if (m_angleTolerance < 0.1) m_angleToleranceLabel->setText("Search range: no rotation");
        else m_angleToleranceLabel->setText(QString("Search range: +/-%1 deg").arg(m_angleTolerance, 0, 'f', 1));
    }
    if (m_optimizeCombo) m_optimizeCombo->setCurrentText(QString::fromStdString(m_optimize));
    if (m_grayValuesCombo) m_grayValuesCombo->setCurrentText(QString::fromStdString(m_grayValues));

    updatePreviewLabel();
}

nlohmann::json HalconPatternMatchingGrayPlugin::getDefaultParameters() const {
    nlohmann::json params;
    params["maxError"] = 30.0;
    params["numLevels"] = 0;
    params["angleTolerance"] = 0.0;
    params["optimize"] = "sort";
    params["grayValues"] = "normalized";
    params["useCustomAnchor"] = false;
    params["anchorOffsetX"] = 0.0;
    params["anchorOffsetY"] = 0.0;
    return params;
}

ParameterSchema HalconPatternMatchingGrayPlugin::getParameterSchema() const {
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "maxError", "Max Error", 0.0, 255.0, 30.0,
        "Max mean gray-value difference (lower = stricter)", "", 1.0, "Search"));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "angleTolerance", "Angle Tolerance", 0.0, 90.0, 0.0,
        "Rotation tolerance (+/- degrees, 0=no rotation)", "deg", 1.0, "Rotation"));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "numLevels", "Pyramid Levels", 0, 10, 0,
        "Number of pyramid levels (0=auto)", "", 1, "Model"));

    return schema;
}

#endif // HAS_OPENCV && HAS_HALCON
