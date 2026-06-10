#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <string>

// Point types captured for the PIC adjust feature:
//   S1, S2   = reference line endpoints (focus)
//   P1..P4   = PIC die corners (top-left, top-right, bottom-left, bottom-right)
// Indexed slot for arrays:
//   0 = S1, 1 = S2, 2 = P1, 3 = P2, 4 = P3, 5 = P4
enum class PicPointId : int {
    S1 = 0, S2, P1, P2, P3, P4,
    Count
};

constexpr int kPicPointCount = static_cast<int>(PicPointId::Count);

inline const char* PicPointName(PicPointId id) {
    switch (id) {
        case PicPointId::S1: return "S1";
        case PicPointId::S2: return "S2";
        case PicPointId::P1: return "P1";
        case PicPointId::P2: return "P2";
        case PicPointId::P3: return "P3";
        case PicPointId::P4: return "P4";
        default: return "?";
    }
}

struct PicPoint {
    bool   captured = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct PicAdjustSet {
    std::array<PicPoint, kPicPointCount> points{};

    const PicPoint& Get(PicPointId id) const { return points[static_cast<int>(id)]; }
    PicPoint&       Get(PicPointId id)       { return points[static_cast<int>(id)]; }
};

// Persisted UI/state settings — keep alongside the captured points so the
// user's sign convention and approach distance survive across runs.
struct PicAdjustSettings {
    bool invertU = true;
    bool invertV = true;
    bool invertW = true;
    std::string targetDevice = "hex-right";
    double targetDistanceUm = 100.0;   // PIC die -> S1-S2 reference line, in Z (µm)
    bool invertDistance = false;        // flip the sign of the computed hex Z move
    bool lockSigns = false;             // when true, invert-sign checkboxes are read-only
};

struct PicDistanceInfo {
    // Signed perpendicular distance from each corner to the S1->S2 line, in the
    // XY plane (mm). Sign convention: +X side of the line direction = positive.
    std::optional<double> perpP2Mm;       // P2 (top-right) to the line, paired with S1
    std::optional<double> perpP4Mm;       // P4 (bottom-right) to the line, paired with S2
    std::optional<double> currentGapMm;   // average of the two perpendicular distances
};

// All math results in degrees.
struct PicAdjustResult {
    // Reference line (S1->S2)
    std::optional<double> uRefDeg;       // atan2(dx, dy)  -- deviation from +Y axis (yaw)
    std::optional<double> wRefDeg;       // atan2(dz, dy)  -- height slope along the line (roll-equivalent)

    // PIC die (computed from corners)
    std::optional<double> uPicDeg;       // from right edge P2->P4: atan2(dx, dy)
    std::optional<double> wPicDeg;       // from right edge P2->P4 Z slope (canonical W_pic, mirrors U_pic edge)
    std::optional<double> wRollLeftDeg;  // P1->P3 Z slope (cross-check)
    std::optional<double> vPitchTopDeg;  // P1->P2 Z slope
    std::optional<double> vPitchBotDeg;  // P3->P4 Z slope

    // Deltas vs reference
    std::optional<double> deltaUDeg;     // uPic - uRef   (correction about Z)
    std::optional<double> deltaWDeg;     // wPic - wRef   (correction about X)
    std::optional<double> vAvgDeg;       // avg(vPitch{Top,Bot})  -- V pitch suggestion
};

namespace PicAdjustMath {

inline double Rad2Deg(double r) { return r * (180.0 / 3.14159265358979323846); }

// Yaw of line a->b measured as deviation from +Y axis (positive = rotated CW
// looking down -Z). For two nearly-vertical edges this is the small-angle yaw.
inline double YawFromXY(const PicPoint& a, const PicPoint& b) {
    return Rad2Deg(std::atan2(b.x - a.x, b.y - a.y));
}

// Slope angle from a->b in Z vs Y plane.
inline double SlopeZY(const PicPoint& a, const PicPoint& b) {
    return Rad2Deg(std::atan2(b.z - a.z, b.y - a.y));
}

// Slope angle from a->b in Z vs X plane.
inline double SlopeZX(const PicPoint& a, const PicPoint& b) {
    return Rad2Deg(std::atan2(b.z - a.z, b.x - a.x));
}

inline PicAdjustResult Compute(const PicAdjustSet& s) {
    PicAdjustResult r;
    const auto& s1 = s.Get(PicPointId::S1);
    const auto& s2 = s.Get(PicPointId::S2);
    const auto& p1 = s.Get(PicPointId::P1);
    const auto& p2 = s.Get(PicPointId::P2);
    const auto& p3 = s.Get(PicPointId::P3);
    const auto& p4 = s.Get(PicPointId::P4);

    if (s1.captured && s2.captured) {
        r.uRefDeg = YawFromXY(s1, s2);
        r.wRefDeg = SlopeZY(s1, s2);
    }
    if (p2.captured && p4.captured) {
        r.uPicDeg = YawFromXY(p2, p4);
        r.wPicDeg = SlopeZY(p2, p4);
    }
    if (p1.captured && p3.captured) {
        r.wRollLeftDeg = SlopeZY(p1, p3);
    }
    if (p1.captured && p2.captured) {
        r.vPitchTopDeg = SlopeZX(p1, p2);
    }
    if (p3.captured && p4.captured) {
        r.vPitchBotDeg = SlopeZX(p3, p4);
    }

    if (r.uPicDeg && r.uRefDeg) {
        r.deltaUDeg = *r.uPicDeg - *r.uRefDeg;
    }
    if (r.wPicDeg && r.wRefDeg) {
        r.deltaWDeg = *r.wPicDeg - *r.wRefDeg;
    }
    if (r.vPitchTopDeg || r.vPitchBotDeg) {
        double sum = 0.0; int n = 0;
        if (r.vPitchTopDeg) { sum += *r.vPitchTopDeg; ++n; }
        if (r.vPitchBotDeg) { sum += *r.vPitchBotDeg; ++n; }
        r.vAvgDeg = sum / n;
    }
    return r;
}

inline PicDistanceInfo ComputeDistance(const PicAdjustSet& s) {
    PicDistanceInfo info;
    const auto& s1 = s.Get(PicPointId::S1);
    const auto& s2 = s.Get(PicPointId::S2);
    const auto& p2 = s.Get(PicPointId::P2);
    const auto& p4 = s.Get(PicPointId::P4);
    if (!s1.captured || !s2.captured) return info;

    // Line direction S1->S2 in the XY plane; perpendicular is the 90° CCW rotation.
    const double dx = s2.x - s1.x;
    const double dy = s2.y - s1.y;
    const double L  = std::sqrt(dx*dx + dy*dy);
    if (L < 1e-12) return info;   // S1 and S2 coincident; no defined line
    const double nx = -dy / L;
    const double ny =  dx / L;

    auto perp = [&](const PicPoint& p) {
        return (p.x - s1.x) * nx + (p.y - s1.y) * ny;
    };
    if (p2.captured) info.perpP2Mm = perp(p2);
    if (p4.captured) info.perpP4Mm = perp(p4);
    if (info.perpP2Mm && info.perpP4Mm) {
        info.currentGapMm = (*info.perpP2Mm + *info.perpP4Mm) / 2.0;
    } else if (info.perpP2Mm) {
        info.currentGapMm = *info.perpP2Mm;   // fall back to single corner if only one is captured
    } else if (info.perpP4Mm) {
        info.currentGapMm = *info.perpP4Mm;
    }
    return info;
}

} // namespace PicAdjustMath
