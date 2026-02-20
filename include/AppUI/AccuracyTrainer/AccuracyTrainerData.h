// AccuracyTrainerData.h - Data structures and transform math for Accuracy Trainer
#pragma once

#include <string>
#include <vector>
#include <map>
#include <cmath>

// ============================================================================
// Data Structures
// ============================================================================

struct ReferencePoint {
    std::string name;           // e.g., "Ref1", "Ref2"
    double globalX = 0.0;      // Machine coordinates when taught
    double globalY = 0.0;
    double globalZ = 0.0;
    std::string visionProfilePath;  // Associated vision profile for auto-detect (empty = manual only)
    bool valid = false;
};

struct BondingPoint {
    std::string name;           // e.g., "Bond1", "Bond2"
    // Corner positions used to compute center
    double cornerTL_X = 0.0, cornerTL_Y = 0.0;  // top-left
    double cornerBR_X = 0.0, cornerBR_Y = 0.0;  // bottom-right
    double cornerTR_X = 0.0, cornerTR_Y = 0.0;  // top-right (optional)
    bool hasThirdCorner = false;
    // Computed center (bonding position) in global coords
    double globalX = 0.0;
    double globalY = 0.0;
    double globalZ = 0.0;      // Z from first corner teach
    // Position in reference frame (local coords)
    double localX = 0.0;
    double localY = 0.0;
    bool valid = false;
};

struct HexDeviceTeachData {
    std::string deviceName;  // "hex-left", "hex-right"
    double hexX = 0.0, hexY = 0.0, hexZ = 0.0, hexU = 0.0, hexV = 0.0, hexW = 0.0;  // hex device 6-axis position
    double gantryViewX = 0.0, gantryViewY = 0.0, gantryViewZ = 0.0;  // gantry viewing position
    std::string visionProfilePath;  // Vision profile for auto-detect hex tip (empty = manual only)
    bool valid = false;
};

struct TrainingSet {
    std::string name;           // User-given name, e.g., "Die Attach Zone A"
    std::string deviceName;     // Motion device, e.g., "gantry-main"

    // Reference frame definition
    std::vector<ReferencePoint> referencePoints;  // 2 or 3

    // Reference frame axes (computed from reference points)
    double originX = 0.0, originY = 0.0;
    double xAxisX = 1.0, xAxisY = 0.0;
    double yAxisX = 0.0, yAxisY = 1.0;

    // Bonding points (all in the same reference frame)
    std::vector<BondingPoint> bondingPoints;

    // Taught hex device positions (for auto-navigate during multi-device alignment)
    std::vector<HexDeviceTeachData> hexDevicePositions;

    // Bonding point -> device assignment (key=bonding name, value=device name)
    // Default is "gantry-main" for any bonding point not in this map
    std::map<std::string, std::string> bondingDeviceAssignments;

    std::string createdAt;
    bool valid = false;
};

// ============================================================================
// Transform Math (pure functions, no external dependencies)
// ============================================================================

namespace AccuracyTrainerMath {

/**
 * @brief Compute coordinate axes from two points
 *
 * Origin = p1, X-axis = unit(p1->p2), Y-axis = perpendicular (CCW 90)
 */
inline void ComputeCoordinateAxes(
    double fid1X, double fid1Y,
    double fid2X, double fid2Y,
    double& originX, double& originY,
    double& xAxisX, double& xAxisY,
    double& yAxisX, double& yAxisY)
{
    originX = fid1X;
    originY = fid1Y;

    double dx = fid2X - fid1X;
    double dy = fid2Y - fid1Y;
    double len = std::sqrt(dx * dx + dy * dy);

    if (len < 1e-9) {
        // Degenerate: points coincide, use identity axes
        xAxisX = 1.0; xAxisY = 0.0;
        yAxisX = 0.0; yAxisY = 1.0;
        return;
    }

    xAxisX = dx / len;
    xAxisY = dy / len;

    // Y-axis: perpendicular, CCW 90 degrees
    yAxisX = -xAxisY;
    yAxisY = xAxisX;
}

/**
 * @brief Convert world coordinates to local coordinates
 */
inline void WorldToLocal(
    double worldX, double worldY,
    double originX, double originY,
    double xAxisX, double xAxisY,
    double yAxisX, double yAxisY,
    double& localX, double& localY)
{
    double dx = worldX - originX;
    double dy = worldY - originY;
    localX = dx * xAxisX + dy * xAxisY;
    localY = dx * yAxisX + dy * yAxisY;
}

/**
 * @brief Convert local coordinates to world coordinates
 */
inline void LocalToWorld(
    double localX, double localY,
    double originX, double originY,
    double xAxisX, double xAxisY,
    double yAxisX, double yAxisY,
    double& worldX, double& worldY)
{
    worldX = originX + localX * xAxisX + localY * yAxisX;
    worldY = originY + localX * xAxisY + localY * yAxisY;
}

/**
 * @brief Compute distance between two 2D points
 */
inline double Distance(double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace AccuracyTrainerMath
