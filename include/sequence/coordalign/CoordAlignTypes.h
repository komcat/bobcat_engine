// CoordAlignTypes.h
// Data types for the CoordinateAlignCommandHandler family. Relocated out of
// the orchestrator header so per-family helpers + persistence + math can
// include just the types without dragging the handler class in.
#pragma once

#include <string>

/**
 * @brief Single fiducial point with world and device coordinates
 */
struct FiducialPoint {
    // Vision-detected world position (from calibrated camera)
    double worldX_mm = 0.0;
    double worldY_mm = 0.0;

    // Device position when fiducial was captured
    double deviceX_mm = 0.0;
    double deviceY_mm = 0.0;
    double deviceZ_mm = 0.0;

    // Vision profile used for detection
    std::string visionProfilePath;

    // Context prefix for this fiducial
    std::string contextPrefix;

    // Capture timestamp
    std::string timestamp;

    // Detection success flag
    bool valid = false;
};

/**
 * @brief A taught local point in the coordinate system
 *
 * Stored as local coordinates relative to Fid1 origin with Fid1->Fid2 as X-axis.
 * Can be transformed to live world coordinates after alignment.
 */
struct TaughtLocalPoint {
    std::string uid;                    // Unique identifier for this point
    std::string coordSystemUid;         // Reference to taught coordinate system

    // Local coordinates (relative to Fid1 origin)
    double localX_mm = 0.0;
    double localY_mm = 0.0;

    // Original world position when taught (for reference)
    double taughtWorldX_mm = 0.0;
    double taughtWorldY_mm = 0.0;

    // Device position when taught
    double deviceX_mm = 0.0;
    double deviceY_mm = 0.0;
    double deviceZ_mm = 0.0;

    // Metadata
    std::string description;
    std::string createdAt;
    bool valid = false;
};

/**
 * @brief Taught coordinate system from 3 fiducial vision results
 *
 * Created once when teaching a master part. Contains the reference
 * positions and vision profiles for later re-alignment.
 *
 * Coordinate system definition:
 * - Origin: Fid1 world position
 * - X-axis: Unit vector from Fid1 to Fid2
 * - Y-axis: Perpendicular to X-axis (right-hand rule)
 */
struct TaughtCoordinateSystem {
    std::string uid;                    // Unique identifier

    // Three fiducial reference points
    FiducialPoint fid1;
    FiducialPoint fid2;
    FiducialPoint fid3;

    // Hardware configuration
    std::string motionDevice;           // e.g., "gantry-main"
    std::string cameraName;             // e.g., "main_camera"

    // Metadata
    std::string createdAt;              // ISO timestamp
    std::string description;            // Optional description

    // Computed reference values (for quality checks)
    double refDistance12_mm = 0.0;      // Distance F1-F2
    double refDistance23_mm = 0.0;      // Distance F2-F3
    double refDistance31_mm = 0.0;      // Distance F3-F1

    // Coordinate system axes (computed from fiducials)
    // Origin = Fid1 world position
    double originX_mm = 0.0;            // = fid1.worldX_mm
    double originY_mm = 0.0;            // = fid1.worldY_mm

    // X-axis unit vector (Fid1 -> Fid2 direction)
    double xAxisX = 1.0;
    double xAxisY = 0.0;

    // Y-axis unit vector (perpendicular to X, right-hand rule)
    double yAxisX = 0.0;
    double yAxisY = 1.0;
};

/**
 * @brief Live coordinate system from re-alignment
 *
 * Created by re-executing vision at taught device positions.
 * Contains the live coordinate frame for transforming local points to world.
 *
 * Coordinate system definition (same as taught):
 * - Origin: Live Fid1 world position
 * - X-axis: Unit vector from live Fid1 to live Fid2
 * - Y-axis: Perpendicular to X-axis (right-hand rule)
 */
struct LiveCoordinateSystem {
    std::string uid;                    // Unique identifier for this live system
    std::string taughtUid;              // Reference to taught system

    // Three fiducial current positions
    FiducialPoint fid1;
    FiducialPoint fid2;
    FiducialPoint fid3;

    // Live coordinate system axes (computed from live fiducials)
    // Origin = Live Fid1 world position
    double originX_mm = 0.0;
    double originY_mm = 0.0;

    // X-axis unit vector (Live Fid1 -> Live Fid2 direction)
    double xAxisX = 1.0;
    double xAxisY = 0.0;

    // Y-axis unit vector (perpendicular to X, right-hand rule)
    double yAxisX = 0.0;
    double yAxisY = 1.0;

    // Rotation from taught to live (for diagnostics)
    double rotationRad = 0.0;           // Rotation in radians
    double rotationDeg = 0.0;           // Rotation in degrees

    // Scale factor (ratio of live distances to taught distances)
    double scale = 1.0;

    // Quality metrics
    double rmsError_mm = 0.0;           // RMS fitting error
    double maxError_mm = 0.0;           // Maximum point error

    // Timestamp
    std::string createdAt;

    // Validity flag
    bool transformValid = false;
};
