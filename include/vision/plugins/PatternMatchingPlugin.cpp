// PatternMatchingPlugin.cpp
#ifdef HAS_OPENCV

#include "PatternMatchingPlugin.h"
#include "../ui/VisionManagerPage.h"
#include "../VisionManager.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QPixmap>
#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QPainter>
#include <QUuid>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <functional>

// Custom widget for anchor point selection with click interaction
// Note: Using eventFilter instead of Q_OBJECT to avoid MOC compilation issues
class AnchorPointWidget : public QLabel {
public:
    AnchorPointWidget(const QPixmap& templateImage, double initialOffsetX, double initialOffsetY, QWidget* parent = nullptr)
        : QLabel(parent), m_templatePixmap(templateImage), m_anchorOffsetX(initialOffsetX), m_anchorOffsetY(initialOffsetY)
    {
        setAlignment(Qt::AlignCenter);
        setCursor(Qt::CrossCursor);
        setMouseTracking(true);
        updateDisplay();
    }

    double getAnchorOffsetX() const { return m_anchorOffsetX; }
    double getAnchorOffsetY() const { return m_anchorOffsetY; }

    // Callback for anchor changes (replaces signal)
    std::function<void(double, double)> onAnchorChanged;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            updateAnchorFromMouse(event->pos());
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons() & Qt::LeftButton) {
            updateAnchorFromMouse(event->pos());
        }
    }

private:
    void updateAnchorFromMouse(const QPoint& pos) {
        // Calculate where in the template image the click corresponds to
        QSize labelSize = size();
        QSize pixmapSize = m_templatePixmap.size();

        // Calculate the offset due to centering
        int offsetX = (labelSize.width() - pixmapSize.width()) / 2;
        int offsetY = (labelSize.height() - pixmapSize.height()) / 2;

        // Get position relative to template image
        double imgX = pos.x() - offsetX;
        double imgY = pos.y() - offsetY;

        // Clamp to template bounds
        imgX = std::max(0.0, std::min(imgX, static_cast<double>(pixmapSize.width() - 1)));
        imgY = std::max(0.0, std::min(imgY, static_cast<double>(pixmapSize.height() - 1)));

        // Calculate offset from center
        double centerX = pixmapSize.width() / 2.0;
        double centerY = pixmapSize.height() / 2.0;
        m_anchorOffsetX = imgX - centerX;
        m_anchorOffsetY = imgY - centerY;

        updateDisplay();

        // Call callback if set
        if (onAnchorChanged) {
            onAnchorChanged(m_anchorOffsetX, m_anchorOffsetY);
        }
    }

    void updateDisplay() {
        // Create a copy of the template with anchor point overlay
        QPixmap display = m_templatePixmap.copy();
        QPainter painter(&display);

        // Calculate anchor position in image coordinates
        double centerX = display.width() / 2.0;
        double centerY = display.height() / 2.0;
        double anchorX = centerX + m_anchorOffsetX;
        double anchorY = centerY + m_anchorOffsetY;

        // Draw center marker (yellow crosshair)
        painter.setPen(QPen(Qt::yellow, 1, Qt::DashLine));
        painter.drawLine(static_cast<int>(centerX - 10), static_cast<int>(centerY),
                         static_cast<int>(centerX + 10), static_cast<int>(centerY));
        painter.drawLine(static_cast<int>(centerX), static_cast<int>(centerY - 10),
                         static_cast<int>(centerX), static_cast<int>(centerY + 10));

        // Draw anchor point (red dot with blue crosshair)
        painter.setPen(QPen(Qt::blue, 2));
        painter.drawLine(static_cast<int>(anchorX - 15), static_cast<int>(anchorY),
                         static_cast<int>(anchorX + 15), static_cast<int>(anchorY));
        painter.drawLine(static_cast<int>(anchorX), static_cast<int>(anchorY - 15),
                         static_cast<int>(anchorX), static_cast<int>(anchorY + 15));

        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::red);
        painter.drawEllipse(QPointF(anchorX, anchorY), 4, 4);

        setPixmap(display);
    }

    QPixmap m_templatePixmap;
    double m_anchorOffsetX;
    double m_anchorOffsetY;
};

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

    // ✅ NEW: Apply binary preprocessing if enabled (more robust for high-contrast objects)
    cv::Mat searchImage = grayInput;
    std::string preprocessingDesc = "grayscale";

    if (m_useBinaryPreprocess) {
        cv::Mat binaryImage;

        if (m_binaryMethod == "otsu") {
            // Otsu's automatic thresholding
            cv::threshold(grayInput, binaryImage, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
            preprocessingDesc = "binary_otsu";
        } else if (m_binaryMethod == "adaptive") {
            // Adaptive thresholding (local threshold)
            cv::adaptiveThreshold(grayInput, binaryImage, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                                 cv::THRESH_BINARY, 11, 2);
            preprocessingDesc = "binary_adaptive";
        } else {
            // Fixed threshold
            cv::threshold(grayInput, binaryImage, m_binaryThreshold, 255, cv::THRESH_BINARY);
            preprocessingDesc = "binary_fixed";
        }

        // Invert if needed (for dark objects on bright background)
        if (m_binaryInvert) {
            cv::bitwise_not(binaryImage, binaryImage);
            preprocessingDesc += "_inverted";
        }

        searchImage = binaryImage;
    }

    // Apply edge detection if enabled (works on binary or grayscale)
    if (m_useEdgeMatching) {
        cv::Mat blurred;
        if (m_gaussianBlurSize > 0 && m_gaussianBlurSize % 2 == 1) {
            cv::GaussianBlur(searchImage, blurred, cv::Size(m_gaussianBlurSize, m_gaussianBlurSize), 0);
        } else {
            blurred = searchImage;
        }
        cv::Canny(blurred, searchImage, m_cannyThreshold1, m_cannyThreshold2);
        preprocessingDesc += "+edges";
    }

    result.addMetadata("preprocessing", preprocessingDesc);

    // Detected-edges visualization overlay (yellow contours of the input image's
    // edges). This is independent of m_useEdgeMatching — it's purely for the
    // user to see what edges actually exist in the image vs the red template
    // edges that are drawn at each match location.
    {
        cv::Mat visEdges;
        cv::Mat visBlurred;
        const int visBlurSize = 5;       // fixed for visualization
        const double visT1 = 50.0;
        const double visT2 = 150.0;
        cv::GaussianBlur(grayInput, visBlurred, cv::Size(visBlurSize, visBlurSize), 0);
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

        // Calculate anchor point position (center + offset, rotated if needed)
        cv::Point2d anchorPoint = match.location;
        if (m_useCustomAnchor) {
            double angleRad = match.angle * CV_PI / 180.0;
            double cosA = std::cos(angleRad);
            double sinA = std::sin(angleRad);

            // Apply anchor offset with rotation and scale
            double offsetX = m_anchorOffsetX * match.scale;
            double offsetY = m_anchorOffsetY * match.scale;
            double rotatedOffsetX = offsetX * cosA - offsetY * sinA;
            double rotatedOffsetY = offsetX * sinA + offsetY * cosA;

            anchorPoint.x = match.location.x + rotatedOffsetX;
            anchorPoint.y = match.location.y + rotatedOffsetY;
        }

        // Add point result (anchor point of matched region)
        result.addPoint(matchName, anchorPoint, cv::Point2d(0, 0), match.score);

        // Add measurements - report anchor point location
        result.addMeasurement(matchName + "_score", match.score);
        result.addMeasurement(matchName + "_x", match.location.x);  // Template center
        result.addMeasurement(matchName + "_y", match.location.y);  // Template center

        if (m_useCustomAnchor) {
            result.addMeasurement(matchName + "_anchor_x", anchorPoint.x);  // Anchor point
            result.addMeasurement(matchName + "_anchor_y", anchorPoint.y);  // Anchor point
        }

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

        // Draw anchor point crosshair (at the actual reported location)
        result.addPointOverlay(anchorPoint, cv::Scalar(0, 0, 255), 5);  // Red anchor dot
        double crossSize = std::min(halfW, halfH) * 0.3;
        result.addLineOverlay(
            cv::Point2d(anchorPoint.x - crossSize, anchorPoint.y),
            cv::Point2d(anchorPoint.x + crossSize, anchorPoint.y),
            cv::Scalar(255, 0, 0), 2);
        result.addLineOverlay(
            cv::Point2d(anchorPoint.x, anchorPoint.y - crossSize),
            cv::Point2d(anchorPoint.x, anchorPoint.y + crossSize),
            cv::Scalar(255, 0, 0), 2);

        // Draw template center marker (yellow dash) if using custom anchor
        if (m_useCustomAnchor) {
            result.addLineOverlay(
                cv::Point2d(match.location.x - 5, match.location.y),
                cv::Point2d(match.location.x + 5, match.location.y),
                cv::Scalar(0, 255, 255), 1);
            result.addLineOverlay(
                cv::Point2d(match.location.x, match.location.y - 5),
                cv::Point2d(match.location.x, match.location.y + 5),
                cv::Scalar(0, 255, 255), 1);
        }

        // ✅ NEW: Draw template edge overlay in red at detected location
        {
            // Create edge map of template
            cv::Mat templateEdges;
            cv::Mat templateForEdge = m_template.clone();

            // Extract edges for visualization
            cv::Mat blurred;
            int blurSize = 5;  // Fixed blur for visualization
            if (blurSize > 0 && blurSize % 2 == 1) {
                cv::GaussianBlur(templateForEdge, blurred, cv::Size(blurSize, blurSize), 0);
            } else {
                blurred = templateForEdge;
            }
            cv::Canny(blurred, templateEdges, 50, 150);  // Fixed thresholds for visualization

            // Scale template edges if needed
            cv::Mat scaledEdges = templateEdges;
            if (std::abs(match.scale - 1.0) > 0.001) {
                cv::resize(templateEdges, scaledEdges, cv::Size(), match.scale, match.scale, cv::INTER_NEAREST);
            }

            // Rotate template edges if needed
            cv::Mat transformedEdges = scaledEdges;
            if (std::abs(match.angle) > 0.001) {
                cv::Point2f center(scaledEdges.cols / 2.0f, scaledEdges.rows / 2.0f);
                cv::Mat rotMatrix = cv::getRotationMatrix2D(center, -match.angle, 1.0);

                // Calculate new bounding size
                cv::Rect2f bbox = cv::RotatedRect(center, scaledEdges.size(),
                                                   static_cast<float>(match.angle)).boundingRect2f();

                // Adjust rotation matrix for new center
                rotMatrix.at<double>(0, 2) += bbox.width / 2.0 - center.x;
                rotMatrix.at<double>(1, 2) += bbox.height / 2.0 - center.y;

                cv::warpAffine(scaledEdges, transformedEdges, rotMatrix, bbox.size(),
                              cv::INTER_NEAREST, cv::BORDER_CONSTANT, cv::Scalar(0));
            }

            // Calculate offset for placing edges at match location
            double offsetX = match.location.x - transformedEdges.cols / 2.0;
            double offsetY = match.location.y - transformedEdges.rows / 2.0;

            // Draw edge pixels directly on output image (before overlays are rendered)
            // This is more efficient than adding thousands of point overlays
            for (int y = 0; y < transformedEdges.rows; y++) {
                for (int x = 0; x < transformedEdges.cols; x++) {
                    if (transformedEdges.at<uchar>(y, x) > 128) {
                        int outputX = static_cast<int>(offsetX + x);
                        int outputY = static_cast<int>(offsetY + y);

                        // Check bounds
                        if (outputX >= 0 && outputX < output.cols &&
                            outputY >= 0 && outputY < output.rows) {

                            // Draw red pixel (works for both grayscale and color)
                            if (output.channels() == 3) {
                                // Color image - set to pure red (B=0, G=0, R=255)
                                output.at<cv::Vec3b>(outputY, outputX) = cv::Vec3b(0, 0, 255);
                            } else {
                                // Grayscale - set to white
                                output.at<uchar>(outputY, outputX) = 255;
                            }
                        }
                    }
                }
            }
        }

        // Draw score text
        char scoreText[32];
        std::snprintf(scoreText, sizeof(scoreText), "%.1f%%", match.score * 100);
        result.addTextOverlay(scoreText, cv::Point2d(match.location.x + halfW + 5, match.location.y - halfH),
                              cv::Scalar(0, 0, 255), 1);

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

    // ✅ Apply binary preprocessing to template (same as search image)
    if (m_useBinaryPreprocess) {
        cv::Mat binaryTemplate;

        if (m_binaryMethod == "otsu") {
            cv::threshold(searchTemplate, binaryTemplate, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        } else if (m_binaryMethod == "adaptive") {
            cv::adaptiveThreshold(searchTemplate, binaryTemplate, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                                 cv::THRESH_BINARY, 11, 2);
        } else {
            cv::threshold(searchTemplate, binaryTemplate, m_binaryThreshold, 255, cv::THRESH_BINARY);
        }

        if (m_binaryInvert) {
            cv::bitwise_not(binaryTemplate, binaryTemplate);
        }

        searchTemplate = binaryTemplate;
    }

    // Apply edge preprocessing to template if enabled (works on binary or grayscale)
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

    // Check if we're in the main (GUI) thread - if not, skip UI updates
    // This prevents crashes when setParameters is called from worker threads
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        std::cout << "[PatternMatchingPlugin] Skipping UI update from worker thread" << std::endl;
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
        // Create preview image with anchor point marker
        cv::Mat preview = getTemplatePreview(100);

        // Convert to RGB for colored markers
        cv::Mat previewRGB;
        cv::cvtColor(preview, previewRGB, cv::COLOR_GRAY2RGB);

        // Calculate scale factor for anchor point positioning
        double scale = static_cast<double>(preview.cols) / m_template.cols;

        // Draw center marker (yellow dashed crosshair)
        double centerX = preview.cols / 2.0;
        double centerY = preview.rows / 2.0;
        cv::line(previewRGB,
                 cv::Point(static_cast<int>(centerX - 5), static_cast<int>(centerY)),
                 cv::Point(static_cast<int>(centerX + 5), static_cast<int>(centerY)),
                 cv::Scalar(0, 255, 255), 1);  // Yellow in BGR
        cv::line(previewRGB,
                 cv::Point(static_cast<int>(centerX), static_cast<int>(centerY - 5)),
                 cv::Point(static_cast<int>(centerX), static_cast<int>(centerY + 5)),
                 cv::Scalar(0, 255, 255), 1);  // Yellow in BGR

        // Draw anchor point marker if custom anchor is set
        if (m_useCustomAnchor || (m_anchorOffsetX != 0.0 || m_anchorOffsetY != 0.0)) {
            double anchorX = centerX + m_anchorOffsetX * scale;
            double anchorY = centerY + m_anchorOffsetY * scale;

            // Blue crosshair
            cv::line(previewRGB,
                     cv::Point(static_cast<int>(anchorX - 8), static_cast<int>(anchorY)),
                     cv::Point(static_cast<int>(anchorX + 8), static_cast<int>(anchorY)),
                     cv::Scalar(255, 0, 0), 1);  // Blue in BGR
            cv::line(previewRGB,
                     cv::Point(static_cast<int>(anchorX), static_cast<int>(anchorY - 8)),
                     cv::Point(static_cast<int>(anchorX), static_cast<int>(anchorY + 8)),
                     cv::Scalar(255, 0, 0), 1);  // Blue in BGR

            // Red dot
            cv::circle(previewRGB, cv::Point(static_cast<int>(anchorX), static_cast<int>(anchorY)),
                      2, cv::Scalar(0, 0, 255), -1);  // Red filled circle in BGR
        }

        QImage qimg(previewRGB.data, previewRGB.cols, previewRGB.rows,
                    static_cast<int>(previewRGB.step), QImage::Format_RGB888);
        m_previewLabel->setPixmap(QPixmap::fromImage(qimg.rgbSwapped()));
        m_previewLabel->setText("");

        // Update info - include anchor point info if set
        QString info = QString("Size: %1 x %2 px").arg(m_template.cols).arg(m_template.rows);
        if (m_useCustomAnchor || (m_anchorOffsetX != 0.0 || m_anchorOffsetY != 0.0)) {
            info += QString("\nAnchor: (%.1f, %.1f)").arg(m_anchorOffsetX).arg(m_anchorOffsetY);
        }
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

void PatternMatchingPlugin::showAnchorPointDialog() {
    if (m_template.empty()) {
        return;
    }

    // Create dialog
    QDialog dialog(m_configWidget);
    dialog.setWindowTitle("Set Anchor Point");
    dialog.setMinimumSize(400, 450);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    // Instructions
    QLabel* instructions = new QLabel(
        "Click on the template to set the anchor point.\n"
        "The anchor point determines what location is reported when the pattern is found.\n\n"
        "Yellow dashed crosshair: Template center\n"
        "Blue crosshair with red dot: Current anchor point"
    );
    instructions->setWordWrap(true);
    layout->addWidget(instructions);

    // Convert template to QPixmap
    cv::Mat displayTemplate;
    if (m_template.channels() == 1) {
        cv::cvtColor(m_template, displayTemplate, cv::COLOR_GRAY2RGB);
    } else {
        displayTemplate = m_template.clone();
    }

    QImage qimg(displayTemplate.data, displayTemplate.cols, displayTemplate.rows,
                static_cast<int>(displayTemplate.step), QImage::Format_RGB888);
    QPixmap templatePixmap = QPixmap::fromImage(qimg.rgbSwapped());

    // Create anchor point widget
    AnchorPointWidget* anchorWidget = new AnchorPointWidget(templatePixmap, m_anchorOffsetX, m_anchorOffsetY);
    anchorWidget->setMinimumSize(templatePixmap.size());
    layout->addWidget(anchorWidget);

    // Offset display
    QLabel* offsetLabel = new QLabel(QString("Offset from center: (%.1f, %.1f) px")
                                     .arg(m_anchorOffsetX).arg(m_anchorOffsetY));
    layout->addWidget(offsetLabel);

    // Set callback to update offset display
    anchorWidget->onAnchorChanged = [offsetLabel](double x, double y) {
        offsetLabel->setText(QString("Offset from center: (%.1f, %.1f) px").arg(x).arg(y));
    };

    // Use custom anchor checkbox
    QCheckBox* useCustomCheck = new QCheckBox("Use custom anchor point (uncheck to use center)");
    useCustomCheck->setChecked(m_useCustomAnchor || (m_anchorOffsetX != 0.0 || m_anchorOffsetY != 0.0));
    layout->addWidget(useCustomCheck);

    // Buttons
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    // Show dialog
    if (dialog.exec() == QDialog::Accepted) {
        m_anchorOffsetX = anchorWidget->getAnchorOffsetX();
        m_anchorOffsetY = anchorWidget->getAnchorOffsetY();
        m_useCustomAnchor = useCustomCheck->isChecked();

        if (!m_useCustomAnchor) {
            // Reset to center
            m_anchorOffsetX = 0.0;
            m_anchorOffsetY = 0.0;
        }

        std::cout << "[PatternMatchingPlugin] Anchor point set: offset=(" << m_anchorOffsetX << ", "
                  << m_anchorOffsetY << "), useCustom=" << m_useCustomAnchor << std::endl;

        // ✅ Update preview to show anchor point marker
        updatePreviewLabel();
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

    m_teachBtn = new QPushButton("Select Template Area", templateGroup);
    m_teachBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; }");
    m_teachBtn->setToolTip("Draw a red rectangle on the image to select template area");
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

    // Anchor point button
    m_setAnchorBtn = new QPushButton("Set Anchor Point...", templateGroup);
    m_setAnchorBtn->setToolTip("Set custom anchor point for pattern match reporting");
    m_setAnchorBtn->setStyleSheet("QPushButton { background-color: #388E3C; color: white; }");
    templateLayout->addWidget(m_setAnchorBtn);

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

    // Binary preprocessing group
    QGroupBox* binaryGroup = new QGroupBox("Binary Preprocessing", m_configWidget);
    QFormLayout* binaryLayout = new QFormLayout(binaryGroup);

    m_useBinaryPreprocessCheck = new QCheckBox("Enable Binary Threshold", binaryGroup);
    m_useBinaryPreprocessCheck->setChecked(m_useBinaryPreprocess);
    m_useBinaryPreprocessCheck->setToolTip("Apply binary threshold before matching (more robust for high-contrast objects)");
    binaryLayout->addRow("", m_useBinaryPreprocessCheck);

    QLabel* binaryMethodLabel = new QLabel("Method:");
    m_binaryMethodCombo = new QComboBox(binaryGroup);
    m_binaryMethodCombo->addItem("Fixed Threshold", "fixed");
    m_binaryMethodCombo->addItem("Otsu (Auto)", "otsu");
    m_binaryMethodCombo->addItem("Adaptive", "adaptive");
    int binaryMethodIndex = (m_binaryMethod == "otsu") ? 1 : (m_binaryMethod == "adaptive") ? 2 : 0;
    m_binaryMethodCombo->setCurrentIndex(binaryMethodIndex);
    m_binaryMethodCombo->setEnabled(m_useBinaryPreprocess);
    m_binaryMethodCombo->setToolTip("Fixed: Manual threshold | Otsu: Automatic | Adaptive: Local threshold");
    binaryLayout->addRow(binaryMethodLabel, m_binaryMethodCombo);

    m_binaryThresholdSpin = new QSpinBox(binaryGroup);
    m_binaryThresholdSpin->setRange(0, 255);
    m_binaryThresholdSpin->setValue(m_binaryThreshold);
    m_binaryThresholdSpin->setEnabled(m_useBinaryPreprocess);
    m_binaryThresholdSpin->setToolTip("Threshold value for 'Fixed' method (0-255)");
    binaryLayout->addRow("Threshold:", m_binaryThresholdSpin);

    m_binaryInvertCheck = new QCheckBox("Invert Binary", binaryGroup);
    m_binaryInvertCheck->setChecked(m_binaryInvert);
    m_binaryInvertCheck->setEnabled(m_useBinaryPreprocess);
    m_binaryInvertCheck->setToolTip("Invert binary (use for dark objects on bright background)");
    binaryLayout->addRow("", m_binaryInvertCheck);

    mainLayout->addWidget(binaryGroup);

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

    // Binary preprocessing connections
    QObject::connect(m_useBinaryPreprocessCheck, &QCheckBox::toggled, [this](bool checked) {
        m_useBinaryPreprocess = checked;
        m_binaryMethodCombo->setEnabled(checked);
        m_binaryThresholdSpin->setEnabled(checked && m_binaryMethod == "fixed");
        m_binaryInvertCheck->setEnabled(checked);
    });

    QObject::connect(m_binaryMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        m_binaryMethod = m_binaryMethodCombo->itemData(index).toString().toStdString();
        // Only enable threshold spin for fixed method
        m_binaryThresholdSpin->setEnabled(m_useBinaryPreprocess && m_binaryMethod == "fixed");
    });

    QObject::connect(m_binaryThresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_binaryThreshold = val; });

    QObject::connect(m_binaryInvertCheck, &QCheckBox::toggled,
            [this](bool checked) { m_binaryInvert = checked; });

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
        // ✅ NEW: Use template selection mode instead of ROI
        if (!m_visionManagerPage) {
            QMessageBox::warning(m_configWidget, "Template Selection Error",
                                 "Vision system not available. Please try again.");
            return;
        }

        auto* visionPage = static_cast<VisionManagerPage*>(m_visionManagerPage);

        // Check if we're already in template selection mode
        if (m_inTemplateSelectionMode) {
            // Button clicked in "confirm" state - trigger confirmation
            visionPage->onTemplateSelectionConfirmed();
            return;
        }

        // First click - enter template selection mode
        m_inTemplateSelectionMode = true;
        m_teachBtn->setText("OK - Confirm Template");
        m_teachBtn->setStyleSheet("QPushButton { background-color: #2E7D32; color: white; font-weight: bold; }");

        // Start template selection mode with callbacks
        visionPage->startTemplateSelectionMode(
            // onConfirm callback - called when user finishes selection
            [this, visionPage](int startX, int startY, int endX, int endY) {
                std::cout << "[PatternMatchingPlugin] Template confirmed: ("
                          << startX << "," << startY << ") to (" << endX << "," << endY << ")" << std::endl;

                // Get the full image from VisionManager
                cv::Mat fullImage = VisionManager::instance().getInputImage();
                if (fullImage.empty()) {
                    QMessageBox::warning(m_configWidget, "Teaching Error",
                                         "No image available. Please capture an image first.");
                    m_teachBtn->setText("Select Template Area");
                    m_teachBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; }");
                    return;
                }

                // Extract template region
                cv::Rect templateROI(startX, startY, endX - startX, endY - startY);
                templateROI.x = std::max(0, templateROI.x);
                templateROI.y = std::max(0, templateROI.y);
                templateROI.width = std::min(fullImage.cols - templateROI.x, templateROI.width);
                templateROI.height = std::min(fullImage.rows - templateROI.y, templateROI.height);

                if (templateROI.width <= 0 || templateROI.height <= 0) {
                    QMessageBox::warning(m_configWidget, "Teaching Error",
                                         "Invalid template region.");
                    m_teachBtn->setText("Select Template Area");
                    m_teachBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; }");
                    return;
                }

                cv::Mat templateImage = fullImage(templateROI).clone();

                // Teach template
                if (teachFromImage(templateImage)) {
                    // Show anchor point dialog first
                    showAnchorPointDialog();

                    // Auto-save after anchor point is set
                    if (!m_templateDirectory.empty() && !m_profileName.empty()) {
                        QString templatesDir = QString::fromStdString(m_templateDirectory);
                        QDir dir(templatesDir);
                        if (!dir.exists()) dir.mkpath(".");

                        // Use UUID for unique template filenames (avoids overwriting on re-teach)
                        QString uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        QString templateFileName = uid + "_template.png";
                        QString fullPath = templatesDir + "/" + templateFileName;

                        if (saveTemplateToFile(fullPath.toStdString())) {
                            std::cout << "[PatternMatchingPlugin] Template auto-saved to: " << fullPath.toStdString() << std::endl;
                        }
                    }

                    // Update preview to show template thumbnail and file info
                    updatePreviewLabel();
                } else {
                    QMessageBox::warning(m_configWidget, "Teaching Error",
                                         "Failed to teach template.");
                }

                // Reset button and state
                m_inTemplateSelectionMode = false;
                m_teachBtn->setText("Select Template Area");
                m_teachBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; }");
            },
            // onCancel callback
            [this]() {
                m_inTemplateSelectionMode = false;
                m_teachBtn->setText("Select Template Area");
                m_teachBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; }");
            }
        );
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

    QObject::connect(m_setAnchorBtn, &QPushButton::clicked, [this]() {
        if (!hasTemplate()) {
            QMessageBox::warning(m_configWidget, "Anchor Point Error",
                                 "No template loaded. Please teach or load a template first.");
            return;
        }
        showAnchorPointDialog();
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

    // Binary preprocessing parameters
    params["useBinaryPreprocess"] = m_useBinaryPreprocess;
    params["binaryThreshold"] = m_binaryThreshold;
    params["binaryInvert"] = m_binaryInvert;
    params["binaryMethod"] = m_binaryMethod;

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

    // Anchor point parameters
    params["useCustomAnchor"] = m_useCustomAnchor;
    params["anchorOffsetX"] = m_anchorOffsetX;
    params["anchorOffsetY"] = m_anchorOffsetY;

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

    // Binary preprocessing parameters
    if (params.contains("useBinaryPreprocess")) {
        m_useBinaryPreprocess = params["useBinaryPreprocess"].get<bool>();
    }
    if (params.contains("binaryThreshold")) {
        m_binaryThreshold = params["binaryThreshold"].get<int>();
    }
    if (params.contains("binaryInvert")) {
        m_binaryInvert = params["binaryInvert"].get<bool>();
    }
    if (params.contains("binaryMethod")) {
        m_binaryMethod = params["binaryMethod"].get<std::string>();
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

    // Anchor point parameters
    if (params.contains("useCustomAnchor")) {
        m_useCustomAnchor = params["useCustomAnchor"].get<bool>();
    }
    if (params.contains("anchorOffsetX")) {
        m_anchorOffsetX = params["anchorOffsetX"].get<double>();
    }
    if (params.contains("anchorOffsetY")) {
        m_anchorOffsetY = params["anchorOffsetY"].get<double>();
    }

    // Update UI if widget exists - ONLY if we're on the main GUI thread
    // Skip UI updates if called from worker thread to prevent Qt threading errors
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        std::cout << "[PatternMatchingPlugin] Skipping UI updates - called from worker thread" << std::endl;
        return;
    }

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

    // Update binary preprocessing UI
    if (m_useBinaryPreprocessCheck) m_useBinaryPreprocessCheck->setChecked(m_useBinaryPreprocess);
    if (m_binaryMethodCombo) {
        int methodIndex = m_binaryMethodCombo->findData(QString::fromStdString(m_binaryMethod));
        if (methodIndex >= 0) {
            m_binaryMethodCombo->setCurrentIndex(methodIndex);
        }
        m_binaryMethodCombo->setEnabled(m_useBinaryPreprocess);
    }
    if (m_binaryThresholdSpin) {
        m_binaryThresholdSpin->setValue(m_binaryThreshold);
        m_binaryThresholdSpin->setEnabled(m_useBinaryPreprocess && m_binaryMethod == "fixed");
    }
    if (m_binaryInvertCheck) {
        m_binaryInvertCheck->setChecked(m_binaryInvert);
        m_binaryInvertCheck->setEnabled(m_useBinaryPreprocess);
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
    params["useBinaryPreprocess"] = false;
    params["binaryThreshold"] = 127;
    params["binaryInvert"] = false;
    params["binaryMethod"] = "fixed";
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
