// CoordAlignMath.h
// Pure math used by all four CoordinateAlignCommandHandler families. Header-
// inline (mirrors the calibration/CalibYawMath.h pattern but bigger) — every
// function is short and pure, no state access. Lives in namespace
// coord_align so per-family helpers can call e.g.
//   coord_align::ComputeBestFitTransform(...)
// without dragging the orchestrator class in. Family B's
// AlignLiveCoordinateSystem additionally uses M_PI; that family's TU defines
// it locally if not already provided by the platform headers.
//
// Convention: all 2D, mm units. Coordinate frame from a 3-fiducial layout has
// origin at Fid1, X-axis pointing Fid1->Fid2, Y-axis perpendicular by the
// right-hand rule (counter-clockwise 90°).
#pragma once

#include <array>
#include <cmath>
#include <utility>

namespace coord_align {

inline double CalculateDistance(double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

inline bool ArePointsCollinear(
    double x1, double y1,
    double x2, double y2,
    double x3, double y3,
    double threshold_mm2 = 0.001)
{
    // Triangle area = 0.5 * |x1(y2 - y3) + x2(y3 - y1) + x3(y1 - y2)|
    double area = std::abs(x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2)) / 2.0;
    return area < threshold_mm2;
}

/**
 * @brief Build coordinate axes from origin (Fid1) and the second fiducial.
 *
 * Origin = Fid1; X-axis = unit(Fid1 -> Fid2); Y-axis = X-axis rotated +90°.
 * Degenerate case (Fid1 ≈ Fid2): X = (1,0), Y = (0,1).
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

    if (len < 1e-10) {
        xAxisX = 1.0; xAxisY = 0.0;
        yAxisX = 0.0; yAxisY = 1.0;
        return;
    }

    xAxisX = dx / len;
    xAxisY = dy / len;

    // Y = X rotated +90° (counter-clockwise): (x, y) -> (-y, x)
    yAxisX = -xAxisY;
    yAxisY = xAxisX;
}

inline void WorldToLocal(
    double worldX, double worldY,
    double originX, double originY,
    double xAxisX, double xAxisY,
    double yAxisX, double yAxisY,
    double& localX, double& localY)
{
    double dx = worldX - originX;
    double dy = worldY - originY;
    // Axes are orthonormal -> inverse transform = transpose (dot products).
    localX = dx * xAxisX + dy * xAxisY;
    localY = dx * yAxisX + dy * yAxisY;
}

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
 * @brief Apply a 2D similarity transform: T(p) = scale * R(rotation) * p + t.
 */
inline void TransformPointInternal(
    double inputX, double inputY,
    double rotation, double translateX, double translateY, double scale,
    double& outputX, double& outputY)
{
    double cosR = std::cos(rotation);
    double sinR = std::sin(rotation);

    outputX = scale * (cosR * inputX - sinR * inputY) + translateX;
    outputY = scale * (sinR * inputX + cosR * inputY) + translateY;
}

/**
 * @brief 3-point best-fit similarity (rotation + translation + scale).
 *
 * Centroid-based 2D fit. Returns the rigid + uniform-scale transform that
 * minimizes RMS error between the taught and live point sets.
 *
 * Always returns true (signature kept bool to mirror the original method;
 * caller can check rmsError / maxError for quality gating).
 */
inline bool ComputeBestFitTransform(
    const std::array<std::pair<double, double>, 3>& taught,
    const std::array<std::pair<double, double>, 3>& live,
    double& rotation,
    double& translateX,
    double& translateY,
    double& scale,
    double& rmsError,
    double& maxError)
{
    // 1. Centroids.
    double taughtCx = (taught[0].first + taught[1].first + taught[2].first) / 3.0;
    double taughtCy = (taught[0].second + taught[1].second + taught[2].second) / 3.0;
    double liveCx = (live[0].first + live[1].first + live[2].first) / 3.0;
    double liveCy = (live[0].second + live[1].second + live[2].second) / 3.0;

    // 2. Center the point sets.
    std::array<std::pair<double, double>, 3> taughtCentered;
    std::array<std::pair<double, double>, 3> liveCentered;
    for (int i = 0; i < 3; i++) {
        taughtCentered[i] = {taught[i].first - taughtCx, taught[i].second - taughtCy};
        liveCentered[i]   = {live[i].first   - liveCx,   live[i].second   - liveCy};
    }

    // 3. Cross-covariance matrix H (2x2).
    double H[2][2] = {{0, 0}, {0, 0}};
    for (int i = 0; i < 3; i++) {
        H[0][0] += taughtCentered[i].first  * liveCentered[i].first;
        H[0][1] += taughtCentered[i].first  * liveCentered[i].second;
        H[1][0] += taughtCentered[i].second * liveCentered[i].first;
        H[1][1] += taughtCentered[i].second * liveCentered[i].second;
    }

    // 4. Rotation from H (2D closed form).
    rotation = std::atan2(H[1][0] - H[0][1], H[0][0] + H[1][1]);

    // 5. Scale (ratio of centered norms).
    double taughtNorm = 0.0, liveNorm = 0.0;
    for (int i = 0; i < 3; i++) {
        taughtNorm += taughtCentered[i].first  * taughtCentered[i].first  +
                      taughtCentered[i].second * taughtCentered[i].second;
        liveNorm   += liveCentered[i].first    * liveCentered[i].first    +
                      liveCentered[i].second   * liveCentered[i].second;
    }
    scale = (taughtNorm > 1e-10) ? std::sqrt(liveNorm / taughtNorm) : 1.0;

    // 6. Translation.
    double cosR = std::cos(rotation);
    double sinR = std::sin(rotation);
    double rotatedTaughtCx = scale * (cosR * taughtCx - sinR * taughtCy);
    double rotatedTaughtCy = scale * (sinR * taughtCx + cosR * taughtCy);
    translateX = liveCx - rotatedTaughtCx;
    translateY = liveCy - rotatedTaughtCy;

    // 7. Quality (RMS + max).
    double sumSqError = 0.0;
    maxError = 0.0;
    for (int i = 0; i < 3; i++) {
        double transformedX = scale * (cosR * taught[i].first  - sinR * taught[i].second) + translateX;
        double transformedY = scale * (sinR * taught[i].first  + cosR * taught[i].second) + translateY;

        double errX = transformedX - live[i].first;
        double errY = transformedY - live[i].second;
        double err = std::sqrt(errX * errX + errY * errY);
        sumSqError += err * err;
        if (err > maxError) {
            maxError = err;
        }
    }
    rmsError = std::sqrt(sumSqError / 3.0);

    return true;
}

}  // namespace coord_align
