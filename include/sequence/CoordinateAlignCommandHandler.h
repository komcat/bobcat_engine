#pragma once

#include "ICommandHandler.h"
#include <map>
#include <mutex>
#include <array>
#include <filesystem>

struct ApplicationContext;

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

/**
 * @brief Command handler for 3-point vision-based coordinate alignment
 *
 * This handler enables automated coordinate system alignment using 3 fiducial
 * points detected via vision. It supports:
 *
 * Supported Methods:
 * - TeachCoordinateSystem: Create taught system from 3 fiducial vision results in context
 * - AlignLiveCoordinateSystem: Re-execute vision at taught positions, compute live frame
 * - TeachLocalPoint: Prompt user to move gantry, capture position as local coordinates
 * - TransformPoint: Transform a local point to live world coordinates
 * - GetTransform: Retrieve transformation parameters
 * - ClearCoordinateSystem: Remove a coordinate system by UID
 * - ListCoordinateSystems: List all stored systems
 * - ListLocalPoints: List all taught local points
 * - ClearLocalPoint: Remove a local point by UID
 */
class CoordinateAlignCommandHandler : public ICommandHandler
{
public:
    CoordinateAlignCommandHandler() = default;
    ~CoordinateAlignCommandHandler() override = default;

    bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context) override;

    std::string GetManagerType() const override { return "coordalign"; }

    /**
     * @brief Set the application context for accessing managers
     */
    void SetContext(ApplicationContext* context) { m_appContext = context; }

    /**
     * @brief Set the config path for persistence
     * @param configPath Base config directory (e.g., "config/")
     */
    void SetConfigPath(const std::string& configPath);

    /**
     * @brief Load all persisted coordinate systems and local points
     */
    void LoadAllFromDisk();

private:
    ApplicationContext* m_appContext = nullptr;

    // Base config path for persistence
    std::string m_configPath = "config/";

    // Thread-safe storage for coordinate systems and local points
    std::map<std::string, TaughtCoordinateSystem> m_taughtSystems;
    std::map<std::string, LiveCoordinateSystem> m_liveSystems;
    std::map<std::string, TaughtLocalPoint> m_localPoints;
    mutable std::mutex m_mutex;

    /**
     * @brief Create a Taught coordinate system from 3 fiducial vision results in context
     */
    bool HandleTeachCoordinateSystem(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Re-execute vision at taught positions and create Live coordinate system
     */
    bool HandleAlignLiveCoordinateSystem(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Transform a local point to live world coordinates
     */
    bool HandleTransformPoint(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Teach a local point by prompting user to move gantry
     *
     * Prompts user to manually jog gantry to center camera on desired point,
     * then captures current position and converts to local coordinates.
     */
    bool HandleTeachLocalPoint(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Transform a stored local point to live world coordinates
     */
    bool HandleTransformLocalPoint(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Transform local coordinates and move device to the resulting world position
     *
     * Takes local X/Y coordinates, transforms using live/taught coordinate system,
     * and moves the specified motion device to the world X/Y while keeping current Z.
     */
    bool HandleMoveToLocalXY(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief List all taught local points
     */
    bool HandleListLocalPoints(
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Remove a local point by UID
     */
    bool HandleClearLocalPoint(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage);

    /**
     * @brief Retrieve transformation parameters for a live system
     */
    bool HandleGetTransform(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Remove a coordinate system by UID
     */
    bool HandleClearCoordinateSystem(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage);

    /**
     * @brief List all stored coordinate systems
     */
    bool HandleListCoordinateSystems(
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Execute vision profile at given device position
     * @return true if vision succeeded and results are in context
     */
    bool ExecuteVisionAtPosition(
        const std::string& motionDevice,
        const std::string& cameraName,
        const std::string& visionProfilePath,
        double deviceX, double deviceY, double deviceZ,
        const std::string& captureAs,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Read fiducial data from context after vision execution
     */
    bool ReadFiducialFromContext(
        const std::string& prefix,
        FiducialPoint& outFiducial,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Compute 3-point best-fit rigid transformation
     *
     * Uses centroid-based algorithm to find optimal rotation, translation, and scale
     * that maps taught points to live points with minimum RMS error.
     */
    bool ComputeBestFitTransform(
        const std::array<std::pair<double, double>, 3>& taught,
        const std::array<std::pair<double, double>, 3>& live,
        double& rotation,
        double& translateX,
        double& translateY,
        double& scale,
        double& rmsError,
        double& maxError);

    /**
     * @brief Apply transformation to a single point
     */
    void TransformPointInternal(
        double inputX, double inputY,
        double rotation, double translateX, double translateY, double scale,
        double& outputX, double& outputY) const;

    /**
     * @brief Resolve a parameter value that may be a context reference
     * @param valueStr The parameter value (numeric or "$context_key")
     * @param context The sequence context
     * @return Resolved numeric value
     */
    double ResolveNumericValue(const std::string& valueStr, SequenceContext* context) const;

    /**
     * @brief Get current timestamp as ISO string
     */
    std::string GetCurrentTimestamp() const;

    /**
     * @brief Calculate distance between two points
     */
    double CalculateDistance(double x1, double y1, double x2, double y2) const;

    /**
     * @brief Check if three points are collinear (area < threshold)
     */
    bool ArePointsCollinear(
        double x1, double y1,
        double x2, double y2,
        double x3, double y3,
        double threshold_mm2 = 0.001) const;

    /**
     * @brief Compute coordinate axes from origin and second fiducial
     *
     * Sets up:
     * - origin at fid1 position
     * - X-axis as unit vector from fid1 to fid2
     * - Y-axis perpendicular to X (right-hand rule, counter-clockwise 90°)
     */
    void ComputeCoordinateAxes(
        double fid1X, double fid1Y,
        double fid2X, double fid2Y,
        double& originX, double& originY,
        double& xAxisX, double& xAxisY,
        double& yAxisX, double& yAxisY) const;

    /**
     * @brief Convert world coordinates to local coordinates
     *
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param originX Origin X (Fid1)
     * @param originY Origin Y (Fid1)
     * @param xAxisX X-axis unit vector X component
     * @param xAxisY X-axis unit vector Y component
     * @param yAxisX Y-axis unit vector X component
     * @param yAxisY Y-axis unit vector Y component
     * @param localX Output local X coordinate
     * @param localY Output local Y coordinate
     */
    void WorldToLocal(
        double worldX, double worldY,
        double originX, double originY,
        double xAxisX, double xAxisY,
        double yAxisX, double yAxisY,
        double& localX, double& localY) const;

    /**
     * @brief Convert local coordinates to world coordinates
     *
     * @param localX Local X coordinate (relative to origin along X-axis)
     * @param localY Local Y coordinate (relative to origin along Y-axis)
     * @param originX Origin X (Fid1)
     * @param originY Origin Y (Fid1)
     * @param xAxisX X-axis unit vector X component
     * @param xAxisY X-axis unit vector Y component
     * @param yAxisX Y-axis unit vector X component
     * @param yAxisY Y-axis unit vector Y component
     * @param worldX Output world X coordinate
     * @param worldY Output world Y coordinate
     */
    void LocalToWorld(
        double localX, double localY,
        double originX, double originY,
        double xAxisX, double xAxisY,
        double yAxisX, double yAxisY,
        double& worldX, double& worldY) const;

    // =========================================================================
    // Persistence Methods
    // =========================================================================

    /**
     * @brief Get the directory path for taught systems
     */
    std::filesystem::path GetTaughtSystemsDir() const;

    /**
     * @brief Get the directory path for local points
     */
    std::filesystem::path GetLocalPointsDir() const;

    /**
     * @brief Get the directory path for live systems
     */
    std::filesystem::path GetLiveSystemsDir() const;

    /**
     * @brief Save a taught coordinate system to disk
     * @param system The system to save
     * @return true if saved successfully
     */
    bool SaveTaughtSystemToDisk(const TaughtCoordinateSystem& system);

    /**
     * @brief Load a taught coordinate system from disk
     * @param uid The UID of the system to load
     * @param outSystem Output system
     * @return true if loaded successfully
     */
    bool LoadTaughtSystemFromDisk(const std::string& uid, TaughtCoordinateSystem& outSystem);

    /**
     * @brief Save a local point to disk
     * @param point The point to save
     * @return true if saved successfully
     */
    bool SaveLocalPointToDisk(const TaughtLocalPoint& point);

    /**
     * @brief Load a local point from disk
     * @param uid The UID of the point to load
     * @param outPoint Output point
     * @return true if loaded successfully
     */
    bool LoadLocalPointFromDisk(const std::string& uid, TaughtLocalPoint& outPoint);

    /**
     * @brief Save a live coordinate system to disk
     * @param system The live system to save
     * @return true if saved successfully
     */
    bool SaveLiveSystemToDisk(const LiveCoordinateSystem& system);

    /**
     * @brief Load a live coordinate system from disk
     * @param uid The UID of the live system to load
     * @param outSystem Output system
     * @return true if loaded successfully
     */
    bool LoadLiveSystemFromDisk(const std::string& uid, LiveCoordinateSystem& outSystem);

    /**
     * @brief Delete a taught system file from disk
     */
    bool DeleteTaughtSystemFromDisk(const std::string& uid);

    /**
     * @brief Delete a local point file from disk
     */
    bool DeleteLocalPointFromDisk(const std::string& uid);

    /**
     * @brief Delete a live system file from disk
     */
    bool DeleteLiveSystemFromDisk(const std::string& uid);
};
