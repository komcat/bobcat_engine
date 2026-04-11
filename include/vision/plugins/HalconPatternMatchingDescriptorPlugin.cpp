// HalconPatternMatchingDescriptorPlugin.cpp
// Feature-descriptor-based pattern matching using Halcon's uncalibrated descriptor model
//
// STATUS: DO NOT USE — see header file for full details.
// This plugin is built but NOT registered with VisionManager. It has
// unresolved issues producing bogus matches and failing to locate
// templates that NCC/Edge plugins handle correctly. Use those instead.

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "HalconPatternMatchingDescriptorPlugin.h"
#include "../VisionManager.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QThread>
#include <QCoreApplication>
#include <iostream>
#include <cmath>

using namespace HalconCpp;

HalconPatternMatchingDescriptorPlugin::HalconPatternMatchingDescriptorPlugin()
    : TemplateMatchingBase("PatternMatchDescriptor (Halcon)", "PR",
                           "Feature-descriptor-based pattern matching (Lepetit/Harris) — robust to scale, perspective, and clutter")
{
    m_configWidget = nullptr;
    m_minScoreSpin = nullptr;
    m_maxMatchesSpin = nullptr;
    m_detectorCombo = nullptr;
    m_scoreTypeCombo = nullptr;
}

HalconPatternMatchingDescriptorPlugin::~HalconPatternMatchingDescriptorPlugin() {
}

bool HalconPatternMatchingDescriptorPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) {
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

        // Run descriptor-based matching. Pass empty detector/descriptor param
        // tuples to use defaults. Uses the single-match overload — descriptor
        // matching is intended for single-instance detection on richly
        // textured targets, so we always request one match.
        HTuple emptyNames;
        HTuple emptyValues;
        double scoreVal = 0.0;
        HHomMat2D hom;
        bool matchFound = true;

        try {
            hom = m_descriptorModel.FindUncalibDescriptorModel(
                searchImage,
                emptyNames, emptyValues,            // DetectorParamName/Value
                emptyNames, emptyValues,            // DescriptorParamName/Value
                m_minScore,                         // MinScore
                static_cast<Hlong>(1),              // NumMatches (single-match overload)
                m_scoreType.c_str(),                // ScoreType
                &scoreVal
            );
        } catch (HException& findEx) {
            // Distinguish "no match found" (expected, return 0 matches and
            // success) from "parameter error" (report as real failure so the
            // user actually sees it in the UI instead of a silent 0 matches).
            // Halcon's documented "no instance found" error codes for the
            // find_*_model family are in the 8900-9000 range.
            const int code = static_cast<int>(findEx.ErrorCode());
            const bool isNoMatch = (code == 8510) || (code == 8511) ||
                                   (code == 9301) || (code == 9302) ||
                                   (code == 9303);
            if (isNoMatch) {
                if (VisionManager::instance().isVerboseLogging()) {
                    std::cout << "[HalconPatternMatchDescriptor] No match: "
                              << findEx.ErrorMessage().Text() << std::endl;
                }
                matchFound = false;
            } else {
                // Re-throw to outer catch so the error surfaces properly.
                throw;
            }
        }

        if (!matchFound || scoreVal <= 0.0) {
            result.addMeasurement("num_matches", 0.0);
            result.addMetadata("algorithm", "Halcon_DescriptorMatch");
            result.addMetadata("note", "No matches found above threshold");
            return true;
        }

        int numMatches = 1;

        if (VisionManager::instance().isVerboseLogging()) {
            std::cout << "[HalconPatternMatchDescriptor] Found 1 match, score=" << scoreVal << std::endl;
        }

        {
            const int i = 0;

            // find_uncalib_descriptor_model returns a PROJECTIVE homography
            // (full 3x3 with non-zero perspective row), not necessarily
            // affine. HomMat2dToAffinePar throws #3125 when the perspective
            // component is non-zero, so we:
            //  1. Transform points with ProjectiveTransPoint2d (Pw=1)
            //  2. Extract rotation manually from the 2x2 upper-left block
            //     via atan2, which is valid for the in-plane rotation
            //     component of any projective transform at a given point.
            //
            // Matrix layout from ConvertToTuple() (row-major, 9 elements):
            //   [a00 a01 a02]     row0 = [0,1,2]
            //   [a10 a11 a12]     row1 = [3,4,5]
            //   [a20 a21 a22]     row2 = [6,7,8]
            // For affine the bottom row is [0 0 1]; for projective any value.
            // Halcon row = Y, col = X, so:
            //   X' = a00*X + a01*Y + a02  (col-mapping)
            //   Y' = a10*X + a11*Y + a12  (row-mapping)
            // Rotation of the X-axis = atan2(a10, a00).
            HTuple matTuple = hom.ConvertToTuple();
            double a00 = 1.0, a10 = 0.0;
            double a20 = 0.0, a21 = 0.0, a22 = 1.0;
            if (matTuple.Length() >= 9) {
                a00 = matTuple[0].D();
                a10 = matTuple[3].D();
                a20 = matTuple[6].D();
                a21 = matTuple[7].D();
                a22 = matTuple[8].D();
            }

            // Sanity check on the homography's perspective row. A real
            // alignment between two similarly-sized views of the same
            // object should be approximately affine — the bottom row of
            // the matrix should be close to [0, 0, 1]. RANSAC overfits
            // from too few correspondences produce wildly projective
            // matrices whose bottom rows are far from that, and those
            // matrices collapse to bogus coordinates when we divide by Qw
            // in ProjectiveTransPoint2d (one of our observed failures
            // returned score=1.0 but mapped the template center to
            // (-371, -503)).
            //
            // Threshold values: a20 and a21 should be << 1 (typical real
            // homographies have |a20|, |a21| < 1e-4 for top-down cameras).
            // We allow up to 1e-3 (0.001) as a generous bound.
            const double projectiveMag = std::max(std::abs(a20), std::abs(a21));
            const bool nearAffine = (projectiveMag < 1e-3) && (std::abs(a22 - 1.0) < 0.1);
            if (!nearAffine) {
                result.addMeasurement("num_matches", 0.0);
                result.addMetadata("algorithm", "Halcon_DescriptorMatch");
                result.addMetadata("note",
                    "Rejected overfit homography: perspective row far from identity "
                    "(a20=" + std::to_string(a20) +
                    ", a21=" + std::to_string(a21) +
                    ", a22=" + std::to_string(a22) + ")");
                if (VisionManager::instance().isVerboseLogging()) {
                    std::cout << "[HalconPatternMatchDescriptor] Rejected overfit: "
                              << "a20=" << a20 << " a21=" << a21 << " a22=" << a22
                              << " (score=" << scoreVal << ")" << std::endl;
                }
                return true;
            }

            double matchAngleRad = std::atan2(a10, a00);
            double matchAngle = matchAngleRad * 180.0 / M_PI;

            // Transform template center through the projective homography.
            // Pw=1 (homogeneous w-coordinate for a finite point). Qw is the
            // resulting homogeneous w; divide Qx/Qy by Qw if Qw != 1, but
            // ProjectiveTransPoint2d already does that internally — the
            // returned Qx/Qy are already in pixel units.
            double centerRow = m_templateHeight / 2.0;
            double centerCol = m_templateWidth / 2.0;
            double outCol = 0.0;
            double outWq = 1.0;
            double outRow = hom.ProjectiveTransPoint2d(centerRow, centerCol, 1.0, &outCol, &outWq);

            double matchRow = outRow;
            double matchCol = outCol;
            double matchScore = scoreVal;

            // Sanity check: reject bogus RANSAC matches whose homography
            // maps the template center outside the search image. With low
            // inlier ratios (~0.2-0.3) on weakly textured templates, RANSAC
            // can fit a mathematically valid homography to a handful of
            // spurious correspondences, producing transformations that map
            // the template corners far outside the image. Such matches are
            // never real — reject them as "no match".
            const int imgWidth = gray.cols;
            const int imgHeight = gray.rows;
            const double edgeTolerance = std::max(m_templateWidth, m_templateHeight) * 0.1;
            const bool centerInsideImage =
                (matchCol >= -edgeTolerance) &&
                (matchCol <= imgWidth + edgeTolerance) &&
                (matchRow >= -edgeTolerance) &&
                (matchRow <= imgHeight + edgeTolerance);
            if (!centerInsideImage) {
                result.addMeasurement("num_matches", 0.0);
                result.addMetadata("algorithm", "Halcon_DescriptorMatch");
                result.addMetadata("note",
                    "Rejected bogus match: homography mapped template center outside image "
                    "(row=" + std::to_string(matchRow) + ", col=" + std::to_string(matchCol) + ")");
                if (VisionManager::instance().isVerboseLogging()) {
                    std::cout << "[HalconPatternMatchDescriptor] Rejected bogus match at row="
                              << matchRow << " col=" << matchCol
                              << " (image=" << imgWidth << "x" << imgHeight << ")" << std::endl;
                }
                return true;
            }

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

            // Transform the four template corners through the full projective
            // homography — this shows perspective distortion if any.
            if (m_templateWidth > 0 && m_templateHeight > 0) {
                // Template corners in (row, col) order
                double cornerRow[4] = { 0.0, 0.0, static_cast<double>(m_templateHeight - 1), static_cast<double>(m_templateHeight - 1) };
                double cornerCol[4] = { 0.0, static_cast<double>(m_templateWidth - 1), static_cast<double>(m_templateWidth - 1), 0.0 };
                std::vector<cv::Point2d> boxPts;
                boxPts.reserve(4);
                for (int k = 0; k < 4; ++k) {
                    double qCol = 0.0;
                    double qWq = 1.0;
                    double qRow = hom.ProjectiveTransPoint2d(cornerRow[k], cornerCol[k], 1.0, &qCol, &qWq);
                    boxPts.emplace_back(qCol, qRow);
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

            std::string label = "S=" + std::to_string(static_cast<int>(matchScore)) + " A=" +
                               std::to_string(static_cast<int>(matchAngle)) + "deg";
            result.addTextOverlay(label, cv::Point2d(matchCol + 25, matchRow - 10), cv::Scalar(0, 0, 255), 1);

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
                std::cout << "[HalconPatternMatchDescriptor] Match " << i << " at (" << anchorPoint.x << ", " << anchorPoint.y
                          << ") score=" << matchScore << " angle=" << matchAngle << " deg" << std::endl;
            }
        }

        result.addMeasurement("num_matches", static_cast<double>(numMatches));
        result.addMeasurement("best_score", scoreVal);
        {
            // Use the same projective-safe rotation extraction as above.
            HTuple matTuple = hom.ConvertToTuple();
            double a00 = 1.0, a10 = 0.0;
            if (matTuple.Length() >= 9) {
                a00 = matTuple[0].D();
                a10 = matTuple[3].D();
            }
            result.addMeasurement("best_angle", std::atan2(a10, a00) * 180.0 / M_PI);
        }
        if (result.measurements.count("match_0_x")) {
            result.addMeasurement("best_x", result.measurements["match_0_x"]);
            result.addMeasurement("best_y", result.measurements["match_0_y"]);
            result.addMeasurement("best_p", result.measurements["match_0_p"]);
        }

        result.addMetadata("algorithm", "Halcon_DescriptorMatch");
        result.addMetadata("detector", m_detectorType);
        result.addMetadata("score_type", m_scoreType);
        result.addMetadata("min_score", std::to_string(m_minScore));

        return true;

    } catch (HException& e) {
        m_lastError = std::string("Halcon error: ") + e.ErrorMessage().Text();
        std::cerr << "[HalconPatternMatchDescriptor] " << m_lastError << std::endl;
        return false;
    } catch (std::exception& e) {
        m_lastError = std::string("Error: ") + e.what();
        std::cerr << "[HalconPatternMatchDescriptor] " << m_lastError << std::endl;
        return false;
    }
}

bool HalconPatternMatchingDescriptorPlugin::validate() const {
    if (!hasTemplate()) return false;
    if (m_minScore < 0.0) return false;
    return true;
}

bool HalconPatternMatchingDescriptorPlugin::teachFromImage(const cv::Mat& templateImage) {
    if (templateImage.empty()) {
        std::cout << "[HalconPatternMatchDescriptor] Cannot teach from empty image" << std::endl;
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

        // Build permissive detector params. Halcon defaults are tuned for
        // high-texture targets (printed matter, textured packaging). Dim /
        // low-contrast industrial images need a looser threshold or the
        // detector reports HALCON error #8977 "too few feature points".
        //
        // For Lepetit: min_score default is 0.00061; we drop ~10x to 0.00005.
        // For Harris/Harris-binomial: threshold default is 100; we drop to 10.
        HTuple detNames;
        HTuple detValues;
        if (m_detectorType == "lepetit") {
            detNames.Append(HTuple("min_score"));
            detValues.Append(HTuple(0.00005));
        } else {
            // harris or harris_binomial
            detNames.Append(HTuple("threshold"));
            detValues.Append(HTuple(10.0));
        }

        // Descriptor params: use Halcon's full default rotation/scale range
        // so the matcher has the widest possible search space. Earlier I
        // constrained min_rot/max_rot/min_scale/max_scale for speed, but
        // those constraints caused Halcon to train on too narrow a domain
        // and produced bogus RANSAC overfits with score=1.0 at nonsense
        // coordinates. Training takes ~10-15 s instead of ~3 s, which is
        // acceptable for a once-per-profile teach operation.
        //
        // We still reduce number_ferns (50 -> 30) and depth (12 -> 10) to
        // shave a few seconds off training with negligible accuracy impact.
        HTuple descNames;
        HTuple descValues;
        descNames.Append(HTuple("number_ferns"));
        descValues.Append(HTuple(static_cast<Hlong>(30)));    // default ~50
        descNames.Append(HTuple("depth"));
        descValues.Append(HTuple(static_cast<Hlong>(10)));    // default 12

        m_descriptorModel.CreateUncalibDescriptorModel(
            halconTemplate,
            HString(m_detectorType.c_str()),
            detNames, detValues,
            descNames, descValues,
            static_cast<Hlong>(42)
        );

        m_hasModel = true;

        std::cout << "[HalconPatternMatchDescriptor] Descriptor model created: " << gray.cols << "x"
                  << gray.rows << " pixels, detector=" << m_detectorType << std::endl;

        updatePreviewLabel();
        return true;

    } catch (HException& e) {
        m_lastError = std::string("Failed to create descriptor model: ") + e.ErrorMessage().Text();
        std::cerr << "[HalconPatternMatchDescriptor] " << m_lastError << std::endl;
        m_hasModel = false;
        return false;
    }
}

void HalconPatternMatchingDescriptorPlugin::clearTemplate() {
    TemplateMatchingBase::clearTemplate();
    m_hasModel = false;
    m_templateWidth = 0;
    m_templateHeight = 0;
    try {
        m_descriptorModel.ClearDescriptorModel();
    } catch (HException&) {
        // ignore — model may not have been created
    }
}

QWidget* HalconPatternMatchingDescriptorPlugin::createConfigWidget(QWidget* parent) {
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    QObject::connect(m_configWidget, &QObject::destroyed, [this]() {
        m_configWidget = nullptr;
        m_minScoreSpin = nullptr;
        m_maxMatchesSpin = nullptr;
        m_detectorCombo = nullptr;
        m_scoreTypeCombo = nullptr;
    });

    QGroupBox* templateGroup = createTemplateGroupWidget(m_configWidget);
    mainLayout->addWidget(templateGroup);

    QGroupBox* searchGroup = new QGroupBox("Search Parameters", m_configWidget);
    QFormLayout* searchLayout = new QFormLayout(searchGroup);

    m_minScoreSpin = new QDoubleSpinBox(searchGroup);
    m_minScoreSpin->setRange(0.0, 1.0);
    m_minScoreSpin->setSingleStep(0.05);
    m_minScoreSpin->setValue(m_minScore);
    m_minScoreSpin->setDecimals(3);
    m_minScoreSpin->setToolTip("Min score (0.0 - 1.0). Lower = more permissive.\n"
                                "Typical: 0.10 (permissive), 0.25 (default), 0.40+ (strict).\n"
                                "Halcon restricts MinScore to [0,1] regardless of score type.");
    searchLayout->addRow("Min Score:", m_minScoreSpin);

    m_maxMatchesSpin = new QSpinBox(searchGroup);
    m_maxMatchesSpin->setRange(1, 1);
    m_maxMatchesSpin->setValue(1);
    m_maxMatchesSpin->setEnabled(false);
    m_maxMatchesSpin->setToolTip("Descriptor matching is single-instance only.");
    searchLayout->addRow("Max Matches:", m_maxMatchesSpin);

    m_scoreTypeCombo = new QComboBox(searchGroup);
    m_scoreTypeCombo->addItem("inlier_ratio");
    m_scoreTypeCombo->addItem("num_points");
    m_scoreTypeCombo->setCurrentText(QString::fromStdString(m_scoreType));
    m_scoreTypeCombo->setToolTip("inlier_ratio: ratio 0.0-1.0 (recommended)\n"
                                  "num_points: count of inlier features (Halcon still caps MinScore to [0,1])");
    searchLayout->addRow("Score Type:", m_scoreTypeCombo);

    mainLayout->addWidget(searchGroup);

    QGroupBox* modelGroup = new QGroupBox("Model Parameters", m_configWidget);
    QFormLayout* modelLayout = new QFormLayout(modelGroup);

    m_detectorCombo = new QComboBox(modelGroup);
    m_detectorCombo->addItem("lepetit");
    m_detectorCombo->addItem("harris");
    m_detectorCombo->addItem("harris_binomial");
    m_detectorCombo->setCurrentText(QString::fromStdString(m_detectorType));
    m_detectorCombo->setToolTip("lepetit: fast, good for general use.\n"
                                 "harris: classic corner detector.\n"
                                 "harris_binomial: Harris with binomial smoothing.");
    modelLayout->addRow("Detector:", m_detectorCombo);

    mainLayout->addWidget(modelGroup);
    mainLayout->addStretch();

    QObject::connect(m_minScoreSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_minScore = val; });

    QObject::connect(m_maxMatchesSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxMatches = val; });

    QObject::connect(m_scoreTypeCombo, &QComboBox::currentTextChanged,
            [this](const QString& text) { m_scoreType = text.toStdString(); });

    QObject::connect(m_detectorCombo, &QComboBox::currentTextChanged,
            [this](const QString& text) {
                m_detectorType = text.toStdString();
                if (hasTemplate()) teachFromImage(m_template);
            });

    return m_configWidget;
}

nlohmann::json HalconPatternMatchingDescriptorPlugin::getParameters() const {
    nlohmann::json params;
    params["minScore"] = m_minScore;
    params["maxMatches"] = m_maxMatches;
    params["detectorType"] = m_detectorType;
    params["scoreType"] = m_scoreType;

    nlohmann::json templateParams = getTemplateParameters();
    for (auto& [key, value] : templateParams.items()) {
        params[key] = value;
    }
    return params;
}

void HalconPatternMatchingDescriptorPlugin::setParameters(const nlohmann::json& params) {
    if (params.contains("minScore")) m_minScore = params["minScore"].get<double>();
    if (params.contains("maxMatches")) m_maxMatches = params["maxMatches"].get<int>();
    if (params.contains("detectorType")) m_detectorType = params["detectorType"].get<std::string>();
    if (params.contains("scoreType")) m_scoreType = params["scoreType"].get<std::string>();

    setTemplateParameters(params);

    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        return;
    }

    if (m_minScoreSpin) m_minScoreSpin->setValue(m_minScore);
    if (m_maxMatchesSpin) m_maxMatchesSpin->setValue(m_maxMatches);
    if (m_detectorCombo) m_detectorCombo->setCurrentText(QString::fromStdString(m_detectorType));
    if (m_scoreTypeCombo) m_scoreTypeCombo->setCurrentText(QString::fromStdString(m_scoreType));

    updatePreviewLabel();
}

nlohmann::json HalconPatternMatchingDescriptorPlugin::getDefaultParameters() const {
    nlohmann::json params;
    params["minScore"] = 0.25;
    params["maxMatches"] = 1;
    params["detectorType"] = "lepetit";
    params["scoreType"] = "inlier_ratio";
    params["useCustomAnchor"] = false;
    params["anchorOffsetX"] = 0.0;
    params["anchorOffsetY"] = 0.0;
    return params;
}

ParameterSchema HalconPatternMatchingDescriptorPlugin::getParameterSchema() const {
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "minScore", "Min Score", 0.0, 1.0, 0.25,
        "Minimum score (0-1, lower = more permissive). 0.25 = Halcon default.", "", 0.05, "Search"));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxMatches", "Max Matches", 1, 100, 1,
        "Maximum number of matches to find", "", 1, "Search"));

    return schema;
}

#endif // HAS_OPENCV && HAS_HALCON
