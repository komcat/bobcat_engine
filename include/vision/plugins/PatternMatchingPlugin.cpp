// PatternMatchingPlugin.cpp
#ifdef HAS_OPENCV

#include "PatternMatchingPlugin.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QPixmap>
#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <iostream>
#include <cmath>
#include <algorithm>

PatternMatchingPlugin::PatternMatchingPlugin()
    : VisionPluginBase("PatternMatching", "PR", "Template-based pattern matching using normalized cross-correlation")
{
}

bool PatternMatchingPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) {
    result.clear();
    result.pluginName = m_name;

    auto startTime = std::chrono::high_resolution_clock::now();

    // Validate template
    if (!hasTemplate()) {
        result.success = false;
        result.errorMessage = "No template loaded. Please teach a template first.";
        return false;
    }

    // Convert to grayscale if needed
    cv::Mat grayInput;
    if (input.channels() == 3) {
        cv::cvtColor(input, grayInput, cv::COLOR_BGR2GRAY);
    } else {
        grayInput = input.clone();
    }

    // Check if template is larger than input
    if (m_template.cols > grayInput.cols || m_template.rows > grayInput.rows) {
        result.success = false;
        result.errorMessage = "Template is larger than search image";
        return false;
    }

    output = input.clone();

    // Apply edge preprocessing if enabled
    cv::Mat searchImage = grayInput;
    if (m_useEdgeMatching) {
        cv::Mat blurred;
        if (m_gaussianBlurSize > 0 && m_gaussianBlurSize % 2 == 1) {
            cv::GaussianBlur(grayInput, blurred, cv::Size(m_gaussianBlurSize, m_gaussianBlurSize), 0);
        } else {
            blurred = grayInput;
        }
        cv::Canny(blurred, searchImage, m_cannyThreshold1, m_cannyThreshold2);
        result.addMetadata("preprocessing", "edge_based");
    } else {
        result.addMetadata("preprocessing", "grayscale");
    }

    // Find matches
    std::vector<MatchResult> matches = findMatches(searchImage);

    if (matches.empty()) {
        result.success = false;
        result.errorMessage = "No matches found above threshold";
        result.addMetadata("threshold", std::to_string(m_matchThreshold));

        auto endTime = std::chrono::high_resolution_clock::now();
        result.processingTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        return false;
    }

    // Process matches
    result.success = true;
    int matchIndex = 0;
    for (const auto& match : matches) {
        std::string matchName = "match_" + std::to_string(matchIndex);

        // Add point result (center of matched region)
        result.addPoint(matchName, match.location, cv::Point2d(0, 0), match.score);

        // Add measurements
        result.addMeasurement(matchName + "_score", match.score);
        result.addMeasurement(matchName + "_x", match.location.x);
        result.addMeasurement(matchName + "_y", match.location.y);

        if (m_searchRotation) {
            result.addMeasurement(matchName + "_angle", match.angle);
        }
        if (m_searchScale) {
            result.addMeasurement(matchName + "_scale", match.scale);
        }

        // Draw overlay - bounding box around match
        double halfW = m_template.cols / 2.0 * match.scale;
        double halfH = m_template.rows / 2.0 * match.scale;

        // Calculate rotated rectangle corners
        double angleRad = match.angle * CV_PI / 180.0;
        double cosA = std::cos(angleRad);
        double sinA = std::sin(angleRad);

        std::vector<cv::Point2d> corners(4);
        corners[0] = cv::Point2d(-halfW, -halfH);  // Top-left
        corners[1] = cv::Point2d(halfW, -halfH);   // Top-right
        corners[2] = cv::Point2d(halfW, halfH);    // Bottom-right
        corners[3] = cv::Point2d(-halfW, halfH);   // Bottom-left

        // Rotate and translate corners
        for (auto& corner : corners) {
            double rx = corner.x * cosA - corner.y * sinA;
            double ry = corner.x * sinA + corner.y * cosA;
            corner.x = match.location.x + rx;
            corner.y = match.location.y + ry;
        }

        // Draw rotated bounding box
        cv::Scalar boxColor = (matchIndex == 0) ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 255, 255);
        for (int i = 0; i < 4; i++) {
            result.addLineOverlay(corners[i], corners[(i + 1) % 4], boxColor, 2);
        }

        // Draw center crosshair
        result.addPointOverlay(match.location, cv::Scalar(0, 0, 255), 5);  // Red center
        double crossSize = std::min(halfW, halfH) * 0.3;
        result.addLineOverlay(
            cv::Point2d(match.location.x - crossSize, match.location.y),
            cv::Point2d(match.location.x + crossSize, match.location.y),
            cv::Scalar(255, 0, 0), 2);
        result.addLineOverlay(
            cv::Point2d(match.location.x, match.location.y - crossSize),
            cv::Point2d(match.location.x, match.location.y + crossSize),
            cv::Scalar(255, 0, 0), 2);

        // Draw score text
        char scoreText[32];
        std::snprintf(scoreText, sizeof(scoreText), "%.1f%%", match.score * 100);
        result.addTextOverlay(scoreText, cv::Point2d(match.location.x + halfW + 5, match.location.y - halfH),
                              cv::Scalar(0, 255, 255), 1);

        matchIndex++;
    }

    // Add metadata
    result.addMetadata("template_width", std::to_string(m_template.cols));
    result.addMetadata("template_height", std::to_string(m_template.rows));
    result.addMetadata("matches_found", std::to_string(matches.size()));
    result.addMetadata("threshold", std::to_string(m_matchThreshold));
    if (!m_templateFilePath.empty()) {
        result.addMetadata("template_file", m_templateFilePath);
    }

    result.addMeasurement("match_count", static_cast<double>(matches.size()));
    result.addMeasurement("best_score", matches[0].score);

    auto endTime = std::chrono::high_resolution_clock::now();
    result.processingTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return true;
}

std::vector<PatternMatchingPlugin::MatchResult> PatternMatchingPlugin::findMatches(const cv::Mat& searchImage) {
    std::vector<MatchResult> allMatches;

    // Determine rotation angles to search
    std::vector<double> angles = {0.0};
    if (m_searchRotation) {
        angles.clear();
        for (double a = -m_rotationRange; a <= m_rotationRange; a += m_rotationStep) {
            angles.push_back(a);
        }
    }

    // Determine scales to search
    std::vector<double> scales = {1.0};
    if (m_searchScale) {
        scales.clear();
        for (double s = m_scaleMin; s <= m_scaleMax; s += m_scaleStep) {
            scales.push_back(s);
        }
    }

    // Use pyramid search if enabled and we have rotation/scale search
    if (m_usePyramidSearch && (m_searchRotation || m_searchScale)) {
        // Coarse-to-fine pyramid search
        // First pass: coarse search with larger steps
        std::vector<double> coarseAngles = {0.0};
        std::vector<double> coarseScales = {1.0};

        if (m_searchRotation) {
            coarseAngles.clear();
            double coarseStep = m_rotationStep * 3.0;  // 3x coarser
            for (double a = -m_rotationRange; a <= m_rotationRange; a += coarseStep) {
                coarseAngles.push_back(a);
            }
        }

        if (m_searchScale) {
            coarseScales.clear();
            double coarseStep = m_scaleStep * 3.0;  // 3x coarser
            for (double s = m_scaleMin; s <= m_scaleMax; s += coarseStep) {
                coarseScales.push_back(s);
            }
        }

        // Find best coarse match
        MatchResult bestCoarse;
        bestCoarse.score = 0.0;
        for (double angle : coarseAngles) {
            for (double scale : coarseScales) {
                MatchResult match = findBestMatch(searchImage, angle, scale);
                if (match.score > bestCoarse.score) {
                    bestCoarse = match;
                }
            }
        }

        // Fine search around best coarse result
        if (bestCoarse.score > 0) {
            double fineAngleMin = bestCoarse.angle - m_rotationStep * 2.0;
            double fineAngleMax = bestCoarse.angle + m_rotationStep * 2.0;
            double fineScaleMin = bestCoarse.scale - m_scaleStep * 2.0;
            double fineScaleMax = bestCoarse.scale + m_scaleStep * 2.0;

            angles.clear();
            scales.clear();

            if (m_searchRotation) {
                for (double a = fineAngleMin; a <= fineAngleMax; a += m_rotationStep) {
                    if (a >= -m_rotationRange && a <= m_rotationRange) {
                        angles.push_back(a);
                    }
                }
            } else {
                angles.push_back(0.0);
            }

            if (m_searchScale) {
                for (double s = fineScaleMin; s <= fineScaleMax; s += m_scaleStep) {
                    if (s >= m_scaleMin && s <= m_scaleMax) {
                        scales.push_back(s);
                    }
                }
            } else {
                scales.push_back(1.0);
            }
        }
    }

    // Search all combinations (either full or refined from pyramid)
    for (double angle : angles) {
        for (double scale : scales) {
            MatchResult match = findBestMatch(searchImage, angle, scale);
            if (match.score >= m_matchThreshold) {
                allMatches.push_back(match);
            }
        }
    }

    // Sort by score descending
    std::sort(allMatches.begin(), allMatches.end(),
              [](const MatchResult& a, const MatchResult& b) {
                  return a.score > b.score;
              });

    // Remove overlapping matches (non-maximum suppression)
    std::vector<MatchResult> finalMatches;
    double minDist = std::min(m_template.cols, m_template.rows) * 0.5;

    for (const auto& match : allMatches) {
        bool tooClose = false;
        for (const auto& existing : finalMatches) {
            double dist = cv::norm(match.location - existing.location);
            if (dist < minDist) {
                tooClose = true;
                break;
            }
        }
        if (!tooClose) {
            finalMatches.push_back(match);
            if (static_cast<int>(finalMatches.size()) >= m_maxMatches) {
                break;
            }
        }
    }

    return finalMatches;
}

PatternMatchingPlugin::MatchResult PatternMatchingPlugin::findBestMatch(
    const cv::Mat& searchImage, double angle, double scale) {

    MatchResult result;
    result.score = 0.0;
    result.angle = angle;
    result.scale = scale;

    // Prepare template (rotate and scale if needed)
    cv::Mat searchTemplate = m_template.clone();

    if (std::abs(scale - 1.0) > 0.001) {
        searchTemplate = scaleTemplate(searchTemplate, scale);
    }
    if (std::abs(angle) > 0.001) {
        searchTemplate = rotateTemplate(searchTemplate, angle);
    }

    // Apply edge preprocessing to template if enabled
    if (m_useEdgeMatching) {
        cv::Mat blurred;
        if (m_gaussianBlurSize > 0 && m_gaussianBlurSize % 2 == 1) {
            cv::GaussianBlur(searchTemplate, blurred, cv::Size(m_gaussianBlurSize, m_gaussianBlurSize), 0);
        } else {
            blurred = searchTemplate;
        }
        cv::Canny(blurred, searchTemplate, m_cannyThreshold1, m_cannyThreshold2);
    }

    // Check bounds
    if (searchTemplate.cols > searchImage.cols || searchTemplate.rows > searchImage.rows) {
        return result;
    }

    // Perform template matching
    cv::Mat matchResult;
    cv::matchTemplate(searchImage, searchTemplate, matchResult, m_matchMethod);

    // Find best match location
    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(matchResult, &minVal, &maxVal, &minLoc, &maxLoc);

    // For TM_CCOEFF_NORMED and TM_CCORR_NORMED, higher is better
    cv::Point bestLoc = maxLoc;
    result.score = maxVal;

    // Convert to center coordinates
    result.location.x = bestLoc.x + searchTemplate.cols / 2.0;
    result.location.y = bestLoc.y + searchTemplate.rows / 2.0;

    // Subpixel refinement using parabola fitting
    if (m_subpixelRefinement && bestLoc.x > 0 && bestLoc.y > 0 &&
        bestLoc.x < matchResult.cols - 1 && bestLoc.y < matchResult.rows - 1) {

        // Fit parabola in X direction
        float left = matchResult.at<float>(bestLoc.y, bestLoc.x - 1);
        float center = matchResult.at<float>(bestLoc.y, bestLoc.x);
        float right = matchResult.at<float>(bestLoc.y, bestLoc.x + 1);
        float deltaX = (left - right) / (2.0f * (left - 2.0f * center + right));

        // Fit parabola in Y direction
        float top = matchResult.at<float>(bestLoc.y - 1, bestLoc.x);
        float bottom = matchResult.at<float>(bestLoc.y + 1, bestLoc.x);
        float deltaY = (top - bottom) / (2.0f * (top - 2.0f * center + bottom));

        // Apply subpixel offset (clamp to reasonable range)
        if (std::abs(deltaX) < 1.0f && std::abs(deltaY) < 1.0f) {
            result.location.x += deltaX;
            result.location.y += deltaY;
        }
    }

    return result;
}

cv::Mat PatternMatchingPlugin::rotateTemplate(const cv::Mat& templ, double angleDeg) {
    cv::Point2f center(templ.cols / 2.0f, templ.rows / 2.0f);
    cv::Mat rotMatrix = cv::getRotationMatrix2D(center, -angleDeg, 1.0);

    // Calculate new bounding size
    cv::Rect2f bbox = cv::RotatedRect(center, templ.size(), static_cast<float>(angleDeg)).boundingRect2f();

    // Adjust rotation matrix for new center
    rotMatrix.at<double>(0, 2) += bbox.width / 2.0 - center.x;
    rotMatrix.at<double>(1, 2) += bbox.height / 2.0 - center.y;

    cv::Mat rotated;
    cv::warpAffine(templ, rotated, rotMatrix, bbox.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);

    return rotated;
}

cv::Mat PatternMatchingPlugin::scaleTemplate(const cv::Mat& templ, double scaleFactor) {
    cv::Mat scaled;
    cv::resize(templ, scaled, cv::Size(), scaleFactor, scaleFactor, cv::INTER_LINEAR);
    return scaled;
}

bool PatternMatchingPlugin::teachFromImage(const cv::Mat& templateImage) {
    if (templateImage.empty()) {
        std::cerr << "PatternMatchingPlugin: Cannot teach from empty image" << std::endl;
        return false;
    }

    // Convert to grayscale
    if (templateImage.channels() == 3) {
        cv::cvtColor(templateImage, m_template, cv::COLOR_BGR2GRAY);
    } else {
        m_template = templateImage.clone();
    }

    // Set origin to center
    m_templateOrigin = cv::Point2d(m_template.cols / 2.0, m_template.rows / 2.0);

    std::cout << "PatternMatchingPlugin: Template taught successfully ("
              << m_template.cols << "x" << m_template.rows << " px)" << std::endl;

    // Update UI preview
    updatePreviewLabel();

    return true;
}

void PatternMatchingPlugin::clearTemplate() {
    m_template.release();
    m_templateFilePath.clear();
    m_templateOrigin = cv::Point2d(0, 0);

    updatePreviewLabel();
}

cv::Mat PatternMatchingPlugin::getTemplatePreview(int maxSize) const {
    if (m_template.empty()) {
        return cv::Mat();
    }

    cv::Mat preview;
    double scale = 1.0;

    if (m_template.cols > maxSize || m_template.rows > maxSize) {
        double scaleX = static_cast<double>(maxSize) / m_template.cols;
        double scaleY = static_cast<double>(maxSize) / m_template.rows;
        scale = std::min(scaleX, scaleY);
    }

    cv::resize(m_template, preview, cv::Size(), scale, scale, cv::INTER_LINEAR);
    return preview;
}

bool PatternMatchingPlugin::saveTemplateToFile(const std::string& filePath) {
    if (m_template.empty()) {
        std::cerr << "PatternMatchingPlugin: No template to save" << std::endl;
        return false;
    }

    bool success = cv::imwrite(filePath, m_template);
    if (success) {
        m_templateFilePath = filePath;
        std::cout << "PatternMatchingPlugin: Template saved to " << filePath << std::endl;
        updatePreviewLabel();
    } else {
        std::cerr << "PatternMatchingPlugin: Failed to save template to " << filePath << std::endl;
    }

    return success;
}

bool PatternMatchingPlugin::loadTemplateFromFile(const std::string& filePath) {
    cv::Mat loaded = cv::imread(filePath, cv::IMREAD_GRAYSCALE);
    if (loaded.empty()) {
        std::cerr << "PatternMatchingPlugin: Failed to load template from " << filePath << std::endl;
        return false;
    }

    m_template = loaded;
    m_templateFilePath = filePath;
    m_templateOrigin = cv::Point2d(m_template.cols / 2.0, m_template.rows / 2.0);

    std::cout << "PatternMatchingPlugin: Template loaded from " << filePath
              << " (" << m_template.cols << "x" << m_template.rows << " px)" << std::endl;

    updatePreviewLabel();

    return true;
}

void PatternMatchingPlugin::updatePreviewLabel() {
    if (!m_previewLabel || !m_templateInfoLabel) {
        return;
    }

    if (m_template.empty()) {
        m_previewLabel->setText("No template");
        m_previewLabel->setPixmap(QPixmap());
        m_templateInfoLabel->setText("Size: N/A");
        if (m_templatePathLabel) {
            m_templatePathLabel->setText("File: Not saved");
        }
    } else {
        // Create preview image
        cv::Mat preview = getTemplatePreview(100);
        QImage qimg(preview.data, preview.cols, preview.rows,
                    static_cast<int>(preview.step), QImage::Format_Grayscale8);
        m_previewLabel->setPixmap(QPixmap::fromImage(qimg));
        m_previewLabel->setText("");

        // Update info
        QString info = QString("Size: %1 x %2 px").arg(m_template.cols).arg(m_template.rows);
        m_templateInfoLabel->setText(info);

        if (m_templatePathLabel) {
            if (m_templateFilePath.empty()) {
                m_templatePathLabel->setText("File: Not saved");
            } else {
                QString path = QString::fromStdString(m_templateFilePath);
                // Show just filename
                QFileInfo fileInfo(path);
                m_templatePathLabel->setText("File: " + fileInfo.fileName());
                m_templatePathLabel->setToolTip(path);
            }
        }
    }
}

bool PatternMatchingPlugin::validate() const {
    return true;  // Template can be empty during configuration
}

QWidget* PatternMatchingPlugin::createConfigWidget(QWidget* parent) {
    if (m_configWidget) {
        return m_configWidget;
    }

    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    // Template group
    QGroupBox* templateGroup = new QGroupBox("Template", m_configWidget);
    QVBoxLayout* templateLayout = new QVBoxLayout(templateGroup);

    // Preview label
    m_previewLabel = new QLabel("No template", templateGroup);
    m_previewLabel->setMinimumSize(100, 100);
    m_previewLabel->setMaximumSize(150, 150);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet("QLabel { background-color: #2a2a2a; border: 1px solid #555; }");
    templateLayout->addWidget(m_previewLabel, 0, Qt::AlignCenter);

    // Template info
    m_templateInfoLabel = new QLabel("Size: N/A", templateGroup);
    m_templateInfoLabel->setAlignment(Qt::AlignCenter);
    templateLayout->addWidget(m_templateInfoLabel);

    m_templatePathLabel = new QLabel("File: Not saved", templateGroup);
    m_templatePathLabel->setAlignment(Qt::AlignCenter);
    m_templatePathLabel->setStyleSheet("QLabel { color: #888; font-size: 9px; }");
    templateLayout->addWidget(m_templatePathLabel);

    // Teaching buttons
    QHBoxLayout* teachButtonLayout = new QHBoxLayout();

    m_teachBtn = new QPushButton("Teach from ROI", templateGroup);
    m_teachBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; }");
    m_teachBtn->setToolTip("Capture current ROI as template pattern");
    teachButtonLayout->addWidget(m_teachBtn);

    m_clearTemplateBtn = new QPushButton("Clear", templateGroup);
    m_clearTemplateBtn->setStyleSheet("QPushButton { background-color: #C62828; color: white; }");
    teachButtonLayout->addWidget(m_clearTemplateBtn);

    templateLayout->addLayout(teachButtonLayout);

    // File management buttons
    QHBoxLayout* fileButtonLayout = new QHBoxLayout();

    m_saveTemplateBtn = new QPushButton("Save...", templateGroup);
    m_saveTemplateBtn->setToolTip("Save template to PNG file");
    fileButtonLayout->addWidget(m_saveTemplateBtn);

    m_loadTemplateBtn = new QPushButton("Load...", templateGroup);
    m_loadTemplateBtn->setToolTip("Load template from PNG file");
    fileButtonLayout->addWidget(m_loadTemplateBtn);

    templateLayout->addLayout(fileButtonLayout);

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

    m_maxMatchesSpin = new QSpinBox(searchGroup);
    m_maxMatchesSpin->setRange(1, 100);
    m_maxMatchesSpin->setValue(m_maxMatches);
    searchLayout->addRow("Max Matches:", m_maxMatchesSpin);

    m_subpixelCheck = new QCheckBox("Subpixel Refinement", searchGroup);
    m_subpixelCheck->setChecked(m_subpixelRefinement);
    searchLayout->addRow("", m_subpixelCheck);

    mainLayout->addWidget(searchGroup);

    // Rotation search group
    QGroupBox* rotationGroup = new QGroupBox("Rotation Search", m_configWidget);
    QFormLayout* rotationLayout = new QFormLayout(rotationGroup);

    m_searchRotationCheck = new QCheckBox("Enable", rotationGroup);
    m_searchRotationCheck->setChecked(m_searchRotation);
    rotationLayout->addRow("", m_searchRotationCheck);

    m_rotationRangeSpin = new QDoubleSpinBox(rotationGroup);
    m_rotationRangeSpin->setRange(1.0, 180.0);
    m_rotationRangeSpin->setSingleStep(5.0);
    m_rotationRangeSpin->setValue(m_rotationRange);
    m_rotationRangeSpin->setSuffix(" deg");
    m_rotationRangeSpin->setEnabled(m_searchRotation);
    rotationLayout->addRow("Range (±):", m_rotationRangeSpin);

    m_rotationStepSpin = new QDoubleSpinBox(rotationGroup);
    m_rotationStepSpin->setRange(0.5, 30.0);
    m_rotationStepSpin->setSingleStep(0.5);
    m_rotationStepSpin->setValue(m_rotationStep);
    m_rotationStepSpin->setSuffix(" deg");
    m_rotationStepSpin->setEnabled(m_searchRotation);
    rotationLayout->addRow("Step:", m_rotationStepSpin);

    mainLayout->addWidget(rotationGroup);

    // Scale search group
    QGroupBox* scaleGroup = new QGroupBox("Scale Search", m_configWidget);
    QFormLayout* scaleLayout = new QFormLayout(scaleGroup);

    m_searchScaleCheck = new QCheckBox("Enable", scaleGroup);
    m_searchScaleCheck->setChecked(m_searchScale);
    scaleLayout->addRow("", m_searchScaleCheck);

    m_scaleMinSpin = new QDoubleSpinBox(scaleGroup);
    m_scaleMinSpin->setRange(0.1, 2.0);
    m_scaleMinSpin->setSingleStep(0.05);
    m_scaleMinSpin->setValue(m_scaleMin);
    m_scaleMinSpin->setDecimals(2);
    m_scaleMinSpin->setEnabled(m_searchScale);
    scaleLayout->addRow("Min Scale:", m_scaleMinSpin);

    m_scaleMaxSpin = new QDoubleSpinBox(scaleGroup);
    m_scaleMaxSpin->setRange(0.1, 2.0);
    m_scaleMaxSpin->setSingleStep(0.05);
    m_scaleMaxSpin->setValue(m_scaleMax);
    m_scaleMaxSpin->setDecimals(2);
    m_scaleMaxSpin->setEnabled(m_searchScale);
    scaleLayout->addRow("Max Scale:", m_scaleMaxSpin);

    m_scaleStepSpin = new QDoubleSpinBox(scaleGroup);
    m_scaleStepSpin->setRange(0.01, 0.5);
    m_scaleStepSpin->setSingleStep(0.01);
    m_scaleStepSpin->setValue(m_scaleStep);
    m_scaleStepSpin->setDecimals(2);
    m_scaleStepSpin->setEnabled(m_searchScale);
    scaleLayout->addRow("Step:", m_scaleStepSpin);

    mainLayout->addWidget(scaleGroup);

    // Edge-based matching group
    QGroupBox* edgeGroup = new QGroupBox("Edge-Based Matching", m_configWidget);
    QFormLayout* edgeLayout = new QFormLayout(edgeGroup);

    m_useEdgeMatchingCheck = new QCheckBox("Enable (Faster)", edgeGroup);
    m_useEdgeMatchingCheck->setChecked(m_useEdgeMatching);
    m_useEdgeMatchingCheck->setToolTip("Convert images to edge maps before matching (faster, more robust to lighting)");
    edgeLayout->addRow("", m_useEdgeMatchingCheck);

    m_gaussianBlurSizeSpin = new QSpinBox(edgeGroup);
    m_gaussianBlurSizeSpin->setRange(1, 15);
    m_gaussianBlurSizeSpin->setSingleStep(2);
    m_gaussianBlurSizeSpin->setValue(m_gaussianBlurSize);
    m_gaussianBlurSizeSpin->setEnabled(m_useEdgeMatching);
    edgeLayout->addRow("Blur Size:", m_gaussianBlurSizeSpin);

    m_cannyThreshold1Spin = new QDoubleSpinBox(edgeGroup);
    m_cannyThreshold1Spin->setRange(1.0, 500.0);
    m_cannyThreshold1Spin->setSingleStep(10.0);
    m_cannyThreshold1Spin->setValue(m_cannyThreshold1);
    m_cannyThreshold1Spin->setEnabled(m_useEdgeMatching);
    edgeLayout->addRow("Canny Low:", m_cannyThreshold1Spin);

    m_cannyThreshold2Spin = new QDoubleSpinBox(edgeGroup);
    m_cannyThreshold2Spin->setRange(1.0, 500.0);
    m_cannyThreshold2Spin->setSingleStep(10.0);
    m_cannyThreshold2Spin->setValue(m_cannyThreshold2);
    m_cannyThreshold2Spin->setEnabled(m_useEdgeMatching);
    edgeLayout->addRow("Canny High:", m_cannyThreshold2Spin);

    mainLayout->addWidget(edgeGroup);

    // Pyramid search group
    QGroupBox* pyramidGroup = new QGroupBox("Pyramid Search", m_configWidget);
    QFormLayout* pyramidLayout = new QFormLayout(pyramidGroup);

    m_usePyramidSearchCheck = new QCheckBox("Enable (Coarse-to-Fine)", pyramidGroup);
    m_usePyramidSearchCheck->setChecked(m_usePyramidSearch);
    m_usePyramidSearchCheck->setToolTip("Use coarse-to-fine search for faster rotation/scale search");
    pyramidLayout->addRow("", m_usePyramidSearchCheck);

    m_pyramidLevelsSpin = new QSpinBox(pyramidGroup);
    m_pyramidLevelsSpin->setRange(2, 5);
    m_pyramidLevelsSpin->setValue(m_pyramidLevels);
    m_pyramidLevelsSpin->setEnabled(m_usePyramidSearch);
    pyramidLayout->addRow("Levels:", m_pyramidLevelsSpin);

    mainLayout->addWidget(pyramidGroup);

    mainLayout->addStretch();

    // Connect signals
    QObject::connect(m_thresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_matchThreshold = val; });

    QObject::connect(m_maxMatchesSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxMatches = val; });

    QObject::connect(m_subpixelCheck, &QCheckBox::toggled,
            [this](bool checked) { m_subpixelRefinement = checked; });

    QObject::connect(m_searchRotationCheck, &QCheckBox::toggled, [this](bool checked) {
        m_searchRotation = checked;
        m_rotationRangeSpin->setEnabled(checked);
        m_rotationStepSpin->setEnabled(checked);
    });

    QObject::connect(m_rotationRangeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_rotationRange = val; });

    QObject::connect(m_rotationStepSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_rotationStep = val; });

    QObject::connect(m_searchScaleCheck, &QCheckBox::toggled, [this](bool checked) {
        m_searchScale = checked;
        m_scaleMinSpin->setEnabled(checked);
        m_scaleMaxSpin->setEnabled(checked);
        m_scaleStepSpin->setEnabled(checked);
    });

    QObject::connect(m_scaleMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_scaleMin = val; });

    QObject::connect(m_scaleMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_scaleMax = val; });

    QObject::connect(m_scaleStepSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_scaleStep = val; });

    // Edge-based matching connections
    QObject::connect(m_useEdgeMatchingCheck, &QCheckBox::toggled, [this](bool checked) {
        m_useEdgeMatching = checked;
        m_gaussianBlurSizeSpin->setEnabled(checked);
        m_cannyThreshold1Spin->setEnabled(checked);
        m_cannyThreshold2Spin->setEnabled(checked);
    });

    QObject::connect(m_gaussianBlurSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) {
                // Ensure odd kernel size
                if (val % 2 == 0) {
                    m_gaussianBlurSizeSpin->setValue(val + 1);
                } else {
                    m_gaussianBlurSize = val;
                }
            });

    QObject::connect(m_cannyThreshold1Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_cannyThreshold1 = val; });

    QObject::connect(m_cannyThreshold2Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_cannyThreshold2 = val; });

    // Pyramid search connections
    QObject::connect(m_usePyramidSearchCheck, &QCheckBox::toggled, [this](bool checked) {
        m_usePyramidSearch = checked;
        m_pyramidLevelsSpin->setEnabled(checked);
    });

    QObject::connect(m_pyramidLevelsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_pyramidLevels = val; });

    // Teach button - use the source image's ROI
    QObject::connect(m_teachBtn, &QPushButton::clicked, [this]() {
        if (m_sourceImage.empty()) {
            QMessageBox::warning(m_configWidget, "Teaching Error",
                                 "No source image available. Please capture an image first.");
            return;
        }

        // The source image should already be the ROI cropped image
        if (teachFromImage(m_sourceImage)) {
            QString message = QString("Template successfully captured.\nSize: %1 x %2 pixels")
                                  .arg(m_template.cols).arg(m_template.rows);

            // Auto-save template if directory and profile name are set
            if (!m_templateDirectory.empty() && !m_profileName.empty()) {
                // Create templates subdirectory
                QString templatesDir = QString::fromStdString(m_templateDirectory) + "/templates";
                QDir dir(templatesDir);
                if (!dir.exists()) {
                    dir.mkpath(".");
                }

                // Generate filename based on profile name
                QString templateFileName = QString::fromStdString(m_profileName) + "_template.png";
                QString fullPath = templatesDir + "/" + templateFileName;

                if (saveTemplateToFile(fullPath.toStdString())) {
                    message += QString("\n\nTemplate auto-saved to:\n%1").arg(templateFileName);
                    std::cout << "PatternMatchingPlugin: Template auto-saved to " << fullPath.toStdString() << std::endl;
                } else {
                    message += "\n\nWarning: Failed to auto-save template.";
                }
            } else {
                message += "\n\nNote: Template is in memory only.\nUse 'Save...' to save to file.";
            }

            QMessageBox::information(m_configWidget, "Template Taught", message);
        } else {
            QMessageBox::warning(m_configWidget, "Teaching Error",
                                 "Failed to teach template from ROI.");
        }
    });

    QObject::connect(m_clearTemplateBtn, &QPushButton::clicked, [this]() {
        clearTemplate();
    });

    QObject::connect(m_saveTemplateBtn, &QPushButton::clicked, [this]() {
        if (!hasTemplate()) {
            QMessageBox::warning(m_configWidget, "Save Error",
                                 "No template to save. Please teach a template first.");
            return;
        }

        // Set default directory to templates folder
        QString defaultPath;
        if (!m_templateDirectory.empty()) {
            QString templatesDir = QString::fromStdString(m_templateDirectory) + "/templates";
            QDir dir(templatesDir);
            if (!dir.exists()) {
                dir.mkpath(".");
            }
            // Suggest filename based on profile name
            if (!m_profileName.empty()) {
                defaultPath = templatesDir + "/" + QString::fromStdString(m_profileName) + "_template.png";
            } else {
                defaultPath = templatesDir + "/template.png";
            }
        }

        QString filePath = QFileDialog::getSaveFileName(
            m_configWidget, "Save Template", defaultPath, "PNG Images (*.png);;All Files (*)");

        if (!filePath.isEmpty()) {
            if (!filePath.endsWith(".png", Qt::CaseInsensitive)) {
                filePath += ".png";
            }
            if (saveTemplateToFile(filePath.toStdString())) {
                QMessageBox::information(m_configWidget, "Save Successful",
                                         "Template saved successfully.");
            } else {
                QMessageBox::warning(m_configWidget, "Save Error",
                                     "Failed to save template file.");
            }
        }
    });

    QObject::connect(m_loadTemplateBtn, &QPushButton::clicked, [this]() {
        // Set default directory to templates folder
        QString defaultPath;
        if (!m_templateDirectory.empty()) {
            QString templatesDir = QString::fromStdString(m_templateDirectory) + "/templates";
            if (QDir(templatesDir).exists()) {
                defaultPath = templatesDir;
            } else {
                defaultPath = QString::fromStdString(m_templateDirectory);
            }
        }

        QString filePath = QFileDialog::getOpenFileName(
            m_configWidget, "Load Template", defaultPath, "PNG Images (*.png);;All Files (*)");

        if (!filePath.isEmpty()) {
            if (loadTemplateFromFile(filePath.toStdString())) {
                QMessageBox::information(m_configWidget, "Load Successful",
                                         QString("Template loaded successfully.\nSize: %1 x %2 pixels")
                                             .arg(m_template.cols).arg(m_template.rows));
            } else {
                QMessageBox::warning(m_configWidget, "Load Error",
                                     "Failed to load template file.");
            }
        }
    });

    // Update preview with current state
    updatePreviewLabel();

    return m_configWidget;
}

nlohmann::json PatternMatchingPlugin::getParameters() const {
    nlohmann::json params;

    params["matchThreshold"] = m_matchThreshold;
    params["maxMatches"] = m_maxMatches;
    params["subpixelRefinement"] = m_subpixelRefinement;
    params["searchRotation"] = m_searchRotation;
    params["rotationRange"] = m_rotationRange;
    params["rotationStep"] = m_rotationStep;
    params["searchScale"] = m_searchScale;
    params["scaleMin"] = m_scaleMin;
    params["scaleMax"] = m_scaleMax;
    params["scaleStep"] = m_scaleStep;

    // Edge-based matching parameters
    params["useEdgeMatching"] = m_useEdgeMatching;
    params["cannyThreshold1"] = m_cannyThreshold1;
    params["cannyThreshold2"] = m_cannyThreshold2;
    params["gaussianBlurSize"] = m_gaussianBlurSize;

    // Pyramid search parameters
    params["usePyramidSearch"] = m_usePyramidSearch;
    params["pyramidLevels"] = m_pyramidLevels;

    // Template file path (relative or absolute)
    params["templateFile"] = m_templateFilePath;

    // Template origin
    params["templateOriginX"] = m_templateOrigin.x;
    params["templateOriginY"] = m_templateOrigin.y;

    return params;
}

void PatternMatchingPlugin::setParameters(const nlohmann::json& params) {
    if (params.contains("matchThreshold")) {
        m_matchThreshold = params["matchThreshold"].get<double>();
    }
    if (params.contains("maxMatches")) {
        m_maxMatches = params["maxMatches"].get<int>();
    }
    if (params.contains("subpixelRefinement")) {
        m_subpixelRefinement = params["subpixelRefinement"].get<bool>();
    }
    if (params.contains("searchRotation")) {
        m_searchRotation = params["searchRotation"].get<bool>();
    }
    if (params.contains("rotationRange")) {
        m_rotationRange = params["rotationRange"].get<double>();
    }
    if (params.contains("rotationStep")) {
        m_rotationStep = params["rotationStep"].get<double>();
    }
    if (params.contains("searchScale")) {
        m_searchScale = params["searchScale"].get<bool>();
    }
    if (params.contains("scaleMin")) {
        m_scaleMin = params["scaleMin"].get<double>();
    }
    if (params.contains("scaleMax")) {
        m_scaleMax = params["scaleMax"].get<double>();
    }
    if (params.contains("scaleStep")) {
        m_scaleStep = params["scaleStep"].get<double>();
    }

    // Edge-based matching parameters
    if (params.contains("useEdgeMatching")) {
        m_useEdgeMatching = params["useEdgeMatching"].get<bool>();
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

    // Pyramid search parameters
    if (params.contains("usePyramidSearch")) {
        m_usePyramidSearch = params["usePyramidSearch"].get<bool>();
    }
    if (params.contains("pyramidLevels")) {
        m_pyramidLevels = params["pyramidLevels"].get<int>();
    }

    // Load template file if specified
    if (params.contains("templateFile")) {
        std::string templatePath = params["templateFile"].get<std::string>();
        if (!templatePath.empty()) {
            loadTemplateFromFile(templatePath);
        }
    }

    if (params.contains("templateOriginX") && params.contains("templateOriginY")) {
        m_templateOrigin.x = params["templateOriginX"].get<double>();
        m_templateOrigin.y = params["templateOriginY"].get<double>();
    }

    // Update UI if widget exists
    if (m_thresholdSpin) m_thresholdSpin->setValue(m_matchThreshold);
    if (m_maxMatchesSpin) m_maxMatchesSpin->setValue(m_maxMatches);
    if (m_subpixelCheck) m_subpixelCheck->setChecked(m_subpixelRefinement);
    if (m_searchRotationCheck) m_searchRotationCheck->setChecked(m_searchRotation);
    if (m_rotationRangeSpin) {
        m_rotationRangeSpin->setValue(m_rotationRange);
        m_rotationRangeSpin->setEnabled(m_searchRotation);
    }
    if (m_rotationStepSpin) {
        m_rotationStepSpin->setValue(m_rotationStep);
        m_rotationStepSpin->setEnabled(m_searchRotation);
    }
    if (m_searchScaleCheck) m_searchScaleCheck->setChecked(m_searchScale);
    if (m_scaleMinSpin) {
        m_scaleMinSpin->setValue(m_scaleMin);
        m_scaleMinSpin->setEnabled(m_searchScale);
    }
    if (m_scaleMaxSpin) {
        m_scaleMaxSpin->setValue(m_scaleMax);
        m_scaleMaxSpin->setEnabled(m_searchScale);
    }
    if (m_scaleStepSpin) {
        m_scaleStepSpin->setValue(m_scaleStep);
        m_scaleStepSpin->setEnabled(m_searchScale);
    }

    // Update edge-based matching UI
    if (m_useEdgeMatchingCheck) m_useEdgeMatchingCheck->setChecked(m_useEdgeMatching);
    if (m_gaussianBlurSizeSpin) {
        m_gaussianBlurSizeSpin->setValue(m_gaussianBlurSize);
        m_gaussianBlurSizeSpin->setEnabled(m_useEdgeMatching);
    }
    if (m_cannyThreshold1Spin) {
        m_cannyThreshold1Spin->setValue(m_cannyThreshold1);
        m_cannyThreshold1Spin->setEnabled(m_useEdgeMatching);
    }
    if (m_cannyThreshold2Spin) {
        m_cannyThreshold2Spin->setValue(m_cannyThreshold2);
        m_cannyThreshold2Spin->setEnabled(m_useEdgeMatching);
    }

    // Update pyramid search UI
    if (m_usePyramidSearchCheck) m_usePyramidSearchCheck->setChecked(m_usePyramidSearch);
    if (m_pyramidLevelsSpin) {
        m_pyramidLevelsSpin->setValue(m_pyramidLevels);
        m_pyramidLevelsSpin->setEnabled(m_usePyramidSearch);
    }

    updatePreviewLabel();
}

nlohmann::json PatternMatchingPlugin::getDefaultParameters() const {
    nlohmann::json params;
    params["matchThreshold"] = 0.7;
    params["maxMatches"] = 1;
    params["subpixelRefinement"] = true;
    params["searchRotation"] = false;
    params["rotationRange"] = 15.0;
    params["rotationStep"] = 5.0;
    params["searchScale"] = false;
    params["scaleMin"] = 0.9;
    params["scaleMax"] = 1.1;
    params["scaleStep"] = 0.05;
    params["useEdgeMatching"] = false;
    params["cannyThreshold1"] = 50.0;
    params["cannyThreshold2"] = 150.0;
    params["gaussianBlurSize"] = 5;
    params["usePyramidSearch"] = false;
    params["pyramidLevels"] = 3;
    params["templateFile"] = "";
    params["templateOriginX"] = 0.0;
    params["templateOriginY"] = 0.0;
    return params;
}

ParameterSchema PatternMatchingPlugin::getParameterSchema() const {
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    // Match parameters
    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "matchThreshold", "Match Threshold",
        0.1, 1.0, 0.7,
        "Minimum score (0-1) for a match to be accepted",
        "", 0.05,
        "Search Parameters"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxMatches", "Max Matches",
        1, 100, 1,
        "Maximum number of matches to find",
        "", 1,
        "Search Parameters"
    ));

    schema.parameters.push_back(ParameterDescriptor::createBool(
        "subpixelRefinement", "Subpixel Refinement",
        true,
        "Enable subpixel accuracy for match location",
        "Search Parameters"
    ));

    // Rotation search
    schema.parameters.push_back(ParameterDescriptor::createBool(
        "searchRotation", "Search Rotation",
        false,
        "Search for rotated instances of template",
        "Rotation Search"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "rotationRange", "Rotation Range",
        1.0, 180.0, 15.0,
        "Search rotation range in degrees (±)",
        "deg", 5.0,
        "Rotation Search"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "rotationStep", "Rotation Step",
        0.5, 30.0, 5.0,
        "Rotation step size in degrees",
        "deg", 0.5,
        "Rotation Search"
    ));

    // Scale search
    schema.parameters.push_back(ParameterDescriptor::createBool(
        "searchScale", "Search Scale",
        false,
        "Search for scaled instances of template",
        "Scale Search"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "scaleMin", "Min Scale",
        0.1, 2.0, 0.9,
        "Minimum scale factor",
        "", 0.05,
        "Scale Search"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "scaleMax", "Max Scale",
        0.1, 2.0, 1.1,
        "Maximum scale factor",
        "", 0.05,
        "Scale Search"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "scaleStep", "Scale Step",
        0.01, 0.5, 0.05,
        "Scale step size",
        "", 0.01,
        "Scale Search"
    ));

    // Edge-based matching
    schema.parameters.push_back(ParameterDescriptor::createBool(
        "useEdgeMatching", "Edge-Based Matching",
        false,
        "Convert images to edges before matching (faster, lighting invariant)",
        "Edge Matching"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "gaussianBlurSize", "Gaussian Blur Size",
        1, 15, 5,
        "Blur kernel size before edge detection (odd number)",
        "px", 2,
        "Edge Matching"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "cannyThreshold1", "Canny Low Threshold",
        1.0, 500.0, 50.0,
        "Lower threshold for Canny edge detection",
        "", 10.0,
        "Edge Matching"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "cannyThreshold2", "Canny High Threshold",
        1.0, 500.0, 150.0,
        "Upper threshold for Canny edge detection",
        "", 10.0,
        "Edge Matching"
    ));

    // Pyramid search
    schema.parameters.push_back(ParameterDescriptor::createBool(
        "usePyramidSearch", "Pyramid Search",
        false,
        "Use coarse-to-fine search for faster rotation/scale",
        "Pyramid Search"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "pyramidLevels", "Pyramid Levels",
        2, 5, 3,
        "Number of pyramid levels for coarse-to-fine search",
        "", 1,
        "Pyramid Search"
    ));

    return schema;
}

#endif // HAS_OPENCV
