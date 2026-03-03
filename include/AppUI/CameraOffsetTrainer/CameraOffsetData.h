// CameraOffsetData.h - Data structures for camera offset calibration
#pragma once

#include <string>

/**
 * @brief XYZ position captured from gantry for a camera center.
 */
struct CameraPosition {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool valid = false;
};

/**
 * @brief A single camera offset calibration set.
 *
 * Stores the low-mag and high-mag camera positions on gantry-main,
 * the computed XYZ offset between them, and an optional test result.
 */
struct CameraOffsetCalibrationSet {
    std::string uid;
    std::string name;
    std::string createdAt;
    std::string lastModifiedAt;

    std::string deviceName;           // e.g., "gantry-main"
    std::string lowMagCameraId;       // camera ID from CameraManager
    std::string highMagCameraId;      // camera ID from CameraManager

    CameraPosition lowMagPosition;    // gantry position centered on low-mag fiducial
    CameraPosition highMagPosition;   // gantry position centered on high-mag fiducial

    double offsetX = 0.0;            // highMag.x - lowMag.x
    double offsetY = 0.0;            // highMag.y - lowMag.y
    double offsetZ = 0.0;            // highMag.z - lowMag.z
    bool offsetValid = false;

    std::string lowMagVisionProfilePath;   // optional vision recipe for auto-centering
    std::string highMagVisionProfilePath;  // optional vision recipe for auto-centering

    std::string pneumaticSlideName;        // optional pneumatic slide for high-mag lens tube (empty = none)

    /**
     * @brief Result of a test run comparing calibrated offset to actual.
     */
    struct TestResult {
        CameraPosition testedLowMagPos;
        CameraPosition testedHighMagPos;

        double testedOffsetX = 0.0;
        double testedOffsetY = 0.0;
        double testedOffsetZ = 0.0;

        double deviationX = 0.0;     // testedOffset - calibratedOffset
        double deviationY = 0.0;
        double deviationZ = 0.0;

        std::string lowMagImagePath;
        std::string highMagImagePath;
        std::string testedAt;
        bool valid = false;
    } lastTestResult;

    std::string sourceFilename;       // not serialized — tracks loaded file stem
};
