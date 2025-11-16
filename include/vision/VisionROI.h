// VisionROI.h
#pragma once

#ifdef HAS_OPENCV

#include <opencv2/core.hpp>
#include "nlohmann/json.hpp"

/**
 * @brief Region of Interest (ROI) management for vision processing
 *
 * Defines a rectangular region for cropping images before processing.
 * Supports validation, cropping, and serialization.
 */
class VisionROI {
public:
    VisionROI() = default;

    VisionROI(int startX, int startY, int endX, int endY)
        : m_startX(startX), m_startY(startY), m_endX(endX), m_endY(endY) {}

    // ========== Accessors ==========

    int getStartX() const { return m_startX; }
    int getStartY() const { return m_startY; }
    int getEndX() const { return m_endX; }
    int getEndY() const { return m_endY; }

    void setStartX(int x) { m_startX = x; }
    void setStartY(int y) { m_startY = y; }
    void setEndX(int x) { m_endX = x; }
    void setEndY(int y) { m_endY = y; }

    void setStartCorner(int x, int y) { m_startX = x; m_startY = y; }
    void setEndCorner(int x, int y) { m_endX = x; m_endY = y; }

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // ========== Computed Properties ==========

    int getWidth() const { return std::abs(m_endX - m_startX); }
    int getHeight() const { return std::abs(m_endY - m_startY); }
    int getArea() const { return getWidth() * getHeight(); }

    cv::Point getTopLeft() const {
        return cv::Point(std::min(m_startX, m_endX), std::min(m_startY, m_endY));
    }

    cv::Point getBottomRight() const {
        return cv::Point(std::max(m_startX, m_endX), std::max(m_startY, m_endY));
    }

    cv::Rect toRect() const {
        auto tl = getTopLeft();
        return cv::Rect(tl.x, tl.y, getWidth(), getHeight());
    }

    cv::Rect2d toRect2d() const {
        auto tl = getTopLeft();
        return cv::Rect2d(tl.x, tl.y, getWidth(), getHeight());
    }

    // ========== Validation ==========

    /**
     * @brief Check if ROI is valid (positive dimensions)
     */
    bool isValid() const {
        return getWidth() > 0 && getHeight() > 0;
    }

    /**
     * @brief Check if ROI fits within given image dimensions
     * @param imageWidth Image width in pixels
     * @param imageHeight Image height in pixels
     * @return true if ROI is completely within image bounds
     */
    bool isWithinBounds(int imageWidth, int imageHeight) const {
        if (!isValid()) return false;
        auto tl = getTopLeft();
        auto br = getBottomRight();
        return tl.x >= 0 && tl.y >= 0 &&
               br.x <= imageWidth && br.y <= imageHeight;
    }

    /**
     * @brief Clamp ROI to fit within image bounds
     * @param imageWidth Image width
     * @param imageHeight Image height
     */
    void clampToBounds(int imageWidth, int imageHeight) {
        m_startX = std::clamp(m_startX, 0, imageWidth);
        m_startY = std::clamp(m_startY, 0, imageHeight);
        m_endX = std::clamp(m_endX, 0, imageWidth);
        m_endY = std::clamp(m_endY, 0, imageHeight);
    }

    // ========== Image Operations ==========

    /**
     * @brief Crop image to ROI
     * @param input Input image
     * @return Cropped image (clone), or empty Mat if invalid
     */
    cv::Mat cropImage(const cv::Mat& input) const {
        if (!m_enabled || input.empty()) {
            return input.clone();
        }

        if (!isValid()) {
            return cv::Mat();  // Invalid ROI
        }

        auto rect = toRect();

        // Clamp to image bounds
        rect.x = std::max(0, rect.x);
        rect.y = std::max(0, rect.y);
        rect.width = std::min(rect.width, input.cols - rect.x);
        rect.height = std::min(rect.height, input.rows - rect.y);

        if (rect.width <= 0 || rect.height <= 0) {
            return cv::Mat();  // ROI outside image
        }

        return input(rect).clone();
    }

    /**
     * @brief Get the offset to apply to coordinates from cropped image
     * @return Offset point (top-left corner of ROI)
     */
    cv::Point2d getOffset() const {
        auto tl = getTopLeft();
        return cv::Point2d(tl.x, tl.y);
    }

    // ========== Serialization ==========

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["enabled"] = m_enabled;
        j["startX"] = m_startX;
        j["startY"] = m_startY;
        j["endX"] = m_endX;
        j["endY"] = m_endY;
        return j;
    }

    static VisionROI fromJson(const nlohmann::json& j) {
        VisionROI roi;
        roi.m_enabled = j.value("enabled", false);
        roi.m_startX = j.value("startX", 0);
        roi.m_startY = j.value("startY", 0);
        roi.m_endX = j.value("endX", 0);
        roi.m_endY = j.value("endY", 0);
        return roi;
    }

    // ========== Utility ==========

    void reset() {
        m_startX = 0;
        m_startY = 0;
        m_endX = 0;
        m_endY = 0;
        m_enabled = false;
    }

    std::string toString() const {
        return "ROI[(" + std::to_string(m_startX) + "," + std::to_string(m_startY) +
               ") to (" + std::to_string(m_endX) + "," + std::to_string(m_endY) +
               ") " + std::to_string(getWidth()) + "x" + std::to_string(getHeight()) +
               " enabled=" + (m_enabled ? "true" : "false") + "]";
    }

private:
    int m_startX = 0;
    int m_startY = 0;
    int m_endX = 0;
    int m_endY = 0;
    bool m_enabled = false;
};

/**
 * @brief Search Box for iterating ROI across a 3x3 grid pattern
 *
 * Defines a larger search area where the ROI can move through 9 positions
 * (grid: 11,12,13,21,22,23,31,32,33) to find features that may be off-center.
 */
class VisionSearchBox {
public:
    VisionSearchBox() = default;

    VisionSearchBox(int startX, int startY, int endX, int endY)
        : m_startX(startX), m_startY(startY), m_endX(endX), m_endY(endY) {}

    // ========== Accessors ==========

    int getStartX() const { return m_startX; }
    int getStartY() const { return m_startY; }
    int getEndX() const { return m_endX; }
    int getEndY() const { return m_endY; }

    void setStartX(int x) { m_startX = x; }
    void setStartY(int y) { m_startY = y; }
    void setEndX(int x) { m_endX = x; }
    void setEndY(int y) { m_endY = y; }

    void setStartCorner(int x, int y) { m_startX = x; m_startY = y; }
    void setEndCorner(int x, int y) { m_endX = x; m_endY = y; }

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // ========== Computed Properties ==========

    int getWidth() const { return std::abs(m_endX - m_startX); }
    int getHeight() const { return std::abs(m_endY - m_startY); }

    cv::Point getTopLeft() const {
        return cv::Point(std::min(m_startX, m_endX), std::min(m_startY, m_endY));
    }

    cv::Point getBottomRight() const {
        return cv::Point(std::max(m_startX, m_endX), std::max(m_startY, m_endY));
    }

    cv::Rect toRect() const {
        auto tl = getTopLeft();
        return cv::Rect(tl.x, tl.y, getWidth(), getHeight());
    }

    // ========== Grid Search Operations ==========

    /**
     * @brief Get grid position label (11,12,13,21,22,23,31,32,33)
     * @param gridIndex 0-8 index
     * @return Grid label string
     */
    static std::string getGridLabel(int gridIndex) {
        static const std::string labels[] = {"11", "12", "13", "21", "22", "23", "31", "32", "33"};
        if (gridIndex >= 0 && gridIndex < 9) {
            return labels[gridIndex];
        }
        return "??";
    }

    /**
     * @brief Calculate ROI position for a given grid cell
     * @param gridIndex 0-8 (0=11, 1=12, 2=13, 3=21, 4=22, 5=23, 6=31, 7=32, 8=33)
     * @param roiWidth Width of ROI
     * @param roiHeight Height of ROI
     * @return ROI positioned at that grid cell
     */
    VisionROI getROIForGridCell(int gridIndex, int roiWidth, int roiHeight) const {
        if (!isValid() || roiWidth <= 0 || roiHeight <= 0) {
            return VisionROI();
        }

        int col = gridIndex % 3;  // 0, 1, 2
        int row = gridIndex / 3;  // 0, 1, 2

        auto searchTL = getTopLeft();
        int searchW = getWidth();
        int searchH = getHeight();

        // Calculate step size for ROI center movement
        // We position ROI center at 3 positions across search box
        double stepX = (searchW - roiWidth) / 2.0;
        double stepY = (searchH - roiHeight) / 2.0;

        // Ensure we don't go negative
        if (stepX < 0) stepX = 0;
        if (stepY < 0) stepY = 0;

        // Calculate ROI top-left position
        int roiX = searchTL.x + static_cast<int>(col * stepX);
        int roiY = searchTL.y + static_cast<int>(row * stepY);

        // Create ROI at this position
        VisionROI roi(roiX, roiY, roiX + roiWidth, roiY + roiHeight);
        roi.setEnabled(true);
        return roi;
    }

    /**
     * @brief Get all 9 ROI positions for grid search
     * @param roiWidth Width of ROI
     * @param roiHeight Height of ROI
     * @return Vector of 9 ROIs positioned at grid cells
     */
    std::vector<VisionROI> getAllGridROIs(int roiWidth, int roiHeight) const {
        std::vector<VisionROI> rois;
        for (int i = 0; i < 9; ++i) {
            rois.push_back(getROIForGridCell(i, roiWidth, roiHeight));
        }
        return rois;
    }

    // ========== Validation ==========

    bool isValid() const {
        return getWidth() > 0 && getHeight() > 0;
    }

    bool isWithinBounds(int imageWidth, int imageHeight) const {
        if (!isValid()) return false;
        auto tl = getTopLeft();
        auto br = getBottomRight();
        return tl.x >= 0 && tl.y >= 0 &&
               br.x <= imageWidth && br.y <= imageHeight;
    }

    void clampToBounds(int imageWidth, int imageHeight) {
        m_startX = std::clamp(m_startX, 0, imageWidth);
        m_startY = std::clamp(m_startY, 0, imageHeight);
        m_endX = std::clamp(m_endX, 0, imageWidth);
        m_endY = std::clamp(m_endY, 0, imageHeight);
    }

    // ========== Serialization ==========

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["enabled"] = m_enabled;
        j["startX"] = m_startX;
        j["startY"] = m_startY;
        j["endX"] = m_endX;
        j["endY"] = m_endY;
        return j;
    }

    static VisionSearchBox fromJson(const nlohmann::json& j) {
        VisionSearchBox box;
        box.m_enabled = j.value("enabled", false);
        box.m_startX = j.value("startX", 0);
        box.m_startY = j.value("startY", 0);
        box.m_endX = j.value("endX", 0);
        box.m_endY = j.value("endY", 0);
        return box;
    }

    void reset() {
        m_startX = 0;
        m_startY = 0;
        m_endX = 0;
        m_endY = 0;
        m_enabled = false;
    }

    std::string toString() const {
        return "SearchBox[(" + std::to_string(m_startX) + "," + std::to_string(m_startY) +
               ") to (" + std::to_string(m_endX) + "," + std::to_string(m_endY) +
               ") " + std::to_string(getWidth()) + "x" + std::to_string(getHeight()) +
               " enabled=" + (m_enabled ? "true" : "false") + "]";
    }

private:
    int m_startX = 0;
    int m_startY = 0;
    int m_endX = 0;
    int m_endY = 0;
    bool m_enabled = false;
};

#endif // HAS_OPENCV
