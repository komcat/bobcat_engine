// VisionResult.h
#pragma once

#ifdef HAS_OPENCV

#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <opencv2/core.hpp>

/**
 * @brief Result structures for vision processing
 */

// ============================================================================
// Geometric Results
// ============================================================================

struct Point2DResult {
    std::string name = "";
    cv::Point2d pixelCoord = {0, 0};
    cv::Point2d mmCoord = {0, 0};
    double confidence = 1.0;
};

struct LineResult {
    std::string name = "";
    cv::Point2d startPx = {0, 0};
    cv::Point2d endPx = {0, 0};
    cv::Point2d startMm = {0, 0};
    cv::Point2d endMm = {0, 0};
    double angleDeg = 0.0;
    double lengthPx = 0.0;
    double lengthMm = 0.0;
};

struct CircleResult {
    std::string name = "";
    cv::Point2d centerPx = {0, 0};
    cv::Point2d centerMm = {0, 0};
    double radiusPx = 0.0;
    double radiusMm = 0.0;
    double diameterPx = 0.0;
    double diameterMm = 0.0;
    double confidence = 1.0;
};

struct RectangleResult {
    std::string name = "";
    cv::Rect2d boundingBoxPx = {0, 0, 0, 0};
    cv::Rect2d boundingBoxMm = {0, 0, 0, 0};
    cv::Point2d centerPx = {0, 0};
    cv::Point2d centerMm = {0, 0};
    double angleDeg = 0.0;
    double areaPx = 0.0;
    double areaMm = 0.0;
    double widthPx = 0.0;
    double heightPx = 0.0;
    double widthMm = 0.0;
    double heightMm = 0.0;
};

// ============================================================================
// Overlay Drawing Commands
// ============================================================================

struct OverlayCommand {
    enum Type {
        DrawPoint,
        DrawLine,
        DrawCircle,
        DrawRectangle,
        DrawFilledRect,     // Semi-transparent filled rectangle (axis-aligned)
        DrawFilledPolygon,  // Semi-transparent filled polygon (rotated/tilted)
        DrawText,
        DrawCrosshair,
        DrawContour
    };

    Type type = DrawPoint;
    cv::Scalar color = cv::Scalar(0, 255, 0);  // Green default
    int thickness = 2;
    int fontScale = 1;
    double opacity = 1.0;  // Opacity for filled shapes (0.0-1.0)

    // Geometry data (used based on type)
    cv::Point2d p1 = {0, 0};
    cv::Point2d p2 = {0, 0};
    double radius = 0.0;
    cv::Rect2d rect = {0, 0, 0, 0};
    std::string text = "";
    std::vector<cv::Point> contour;
};

// ============================================================================
// Main Vision Result Structure
// ============================================================================

struct VisionResult {
    // Execution status
    bool success = false;
    std::string pluginName = "";
    std::string errorMessage = "";
    double processingTimeMs = 0.0;

    // Geometric results
    std::vector<Point2DResult> points;
    std::vector<LineResult> lines;
    std::vector<CircleResult> circles;
    std::vector<RectangleResult> rectangles;

    // Generic measurements (key-value pairs)
    std::map<std::string, double> measurements;
    // Examples: "count", "area_total", "avg_diameter", "max_radius"

    // Metadata
    std::map<std::string, std::string> metadata;
    // Examples: "algorithm", "confidence_level", "roi_used"

    // Overlay commands for visualization
    std::vector<OverlayCommand> overlays;

    // ========== Helper Methods ==========

    void clear() {
        success = false;
        pluginName.clear();
        errorMessage.clear();
        processingTimeMs = 0.0;
        points.clear();
        lines.clear();
        circles.clear();
        rectangles.clear();
        measurements.clear();
        metadata.clear();
        overlays.clear();
    }

    void addPoint(const std::string& name, const cv::Point2d& px, const cv::Point2d& mm, double conf = 1.0) {
        Point2DResult pt;
        pt.name = name;
        pt.pixelCoord = px;
        pt.mmCoord = mm;
        pt.confidence = conf;
        points.push_back(pt);
    }

    void addCircle(const std::string& name, const cv::Point2d& centerPx, double radiusPx,
                   const cv::Point2d& centerMm, double radiusMm, double conf = 1.0) {
        CircleResult c;
        c.name = name;
        c.centerPx = centerPx;
        c.centerMm = centerMm;
        c.radiusPx = radiusPx;
        c.radiusMm = radiusMm;
        c.diameterPx = radiusPx * 2.0;
        c.diameterMm = radiusMm * 2.0;
        c.confidence = conf;
        circles.push_back(c);
    }

    void addLine(const std::string& name, const cv::Point2d& startPx, const cv::Point2d& endPx,
                 const cv::Point2d& startMm, const cv::Point2d& endMm) {
        LineResult l;
        l.name = name;
        l.startPx = startPx;
        l.endPx = endPx;
        l.startMm = startMm;
        l.endMm = endMm;
        l.lengthPx = cv::norm(endPx - startPx);
        l.lengthMm = cv::norm(endMm - startMm);
        l.angleDeg = std::atan2(endPx.y - startPx.y, endPx.x - startPx.x) * 180.0 / CV_PI;
        lines.push_back(l);
    }

    void addMeasurement(const std::string& key, double value) {
        measurements[key] = value;
    }

    void addMetadata(const std::string& key, const std::string& value) {
        metadata[key] = value;
    }

    // Overlay helpers
    void addPointOverlay(const cv::Point2d& pt, const cv::Scalar& color = cv::Scalar(0, 255, 0), int thickness = 3) {
        OverlayCommand cmd;
        cmd.type = OverlayCommand::DrawPoint;
        cmd.p1 = pt;
        cmd.color = color;
        cmd.thickness = thickness;
        overlays.push_back(cmd);
    }

    void addCircleOverlay(const cv::Point2d& center, double radius,
                          const cv::Scalar& color = cv::Scalar(0, 255, 0), int thickness = 2) {
        OverlayCommand cmd;
        cmd.type = OverlayCommand::DrawCircle;
        cmd.p1 = center;
        cmd.radius = radius;
        cmd.color = color;
        cmd.thickness = thickness;
        overlays.push_back(cmd);
    }

    void addLineOverlay(const cv::Point2d& start, const cv::Point2d& end,
                        const cv::Scalar& color = cv::Scalar(0, 255, 0), int thickness = 2) {
        OverlayCommand cmd;
        cmd.type = OverlayCommand::DrawLine;
        cmd.p1 = start;
        cmd.p2 = end;
        cmd.color = color;
        cmd.thickness = thickness;
        overlays.push_back(cmd);
    }

    void addTextOverlay(const std::string& text, const cv::Point2d& position,
                        const cv::Scalar& color = cv::Scalar(0, 255, 0), int fontScale = 1) {
        OverlayCommand cmd;
        cmd.type = OverlayCommand::DrawText;
        cmd.text = text;
        cmd.p1 = position;
        cmd.color = color;
        cmd.fontScale = fontScale;
        overlays.push_back(cmd);
    }

    void addRectOverlay(const cv::Rect2d& rect,
                        const cv::Scalar& color = cv::Scalar(0, 255, 0), int thickness = 2) {
        OverlayCommand cmd;
        cmd.type = OverlayCommand::DrawRectangle;
        cmd.rect = rect;
        cmd.color = color;
        cmd.thickness = thickness;
        overlays.push_back(cmd);
    }

    // Get summary string for display
    std::string getSummary() const {
        std::string summary;
        summary += "Plugin: " + pluginName + "\n";
        summary += "Status: " + std::string(success ? "SUCCESS" : "FAILED") + "\n";

        if (!success && !errorMessage.empty()) {
            summary += "Error: " + errorMessage + "\n";
        }

        summary += "Time: " + std::to_string(processingTimeMs) + " ms\n";
        summary += "\n";

        if (!points.empty()) {
            summary += "Points: " + std::to_string(points.size()) + "\n";
            for (const auto& pt : points) {
                summary += "  " + pt.name + ": (" +
                           std::to_string(pt.pixelCoord.x) + ", " +
                           std::to_string(pt.pixelCoord.y) + ") px\n";
            }
        }

        if (!circles.empty()) {
            summary += "Circles: " + std::to_string(circles.size()) + "\n";
            for (const auto& c : circles) {
                summary += "  " + c.name + ": center=(" +
                           std::to_string(c.centerPx.x) + ", " +
                           std::to_string(c.centerPx.y) + "), r=" +
                           std::to_string(c.radiusPx) + " px\n";
            }
        }

        if (!lines.empty()) {
            summary += "Lines: " + std::to_string(lines.size()) + "\n";
            for (const auto& l : lines) {
                summary += "  " + l.name + ": length=" +
                           std::to_string(l.lengthPx) + " px, angle=" +
                           std::to_string(l.angleDeg) + " deg\n";
            }
        }

        if (!measurements.empty()) {
            summary += "Measurements:\n";
            for (const auto& [key, val] : measurements) {
                char buf[64];
                // Use 6 decimals for mm_per_pixel, 4 for um_per_pixel, 4 for others
                if (key == "mm_per_pixel") {
                    snprintf(buf, sizeof(buf), "%.6f", val);
                } else if (key == "um_per_pixel" || key.find("_mm") != std::string::npos) {
                    snprintf(buf, sizeof(buf), "%.4f", val);
                } else {
                    snprintf(buf, sizeof(buf), "%.4f", val);
                }
                summary += "  " + key + ": " + std::string(buf) + "\n";
            }
        }

        return summary;
    }
};

#endif // HAS_OPENCV
