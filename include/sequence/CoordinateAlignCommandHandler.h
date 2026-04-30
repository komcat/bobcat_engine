#pragma once

#include "ICommandHandler.h"
#include "coordalign/CoordAlignTypes.h"   // FiducialPoint, TaughtLocalPoint,
                                          // TaughtCoordinateSystem, LiveCoordinateSystem
#include <map>
#include <mutex>
#include <filesystem>
#include <utility>
#include <vector>

struct ApplicationContext;

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

    // =========================================================================
    // Cross-family utilities — public so per-family helpers under
    // include/sequence/coordalign/ can call them via deps.parent->...
    // (mirrors Calibration's GetCurrentDateTime exposure).
    // =========================================================================

    std::string GetCurrentTimestamp() const;

    bool ReadFiducialFromContext(
        const std::string& prefix,
        FiducialPoint& outFiducial,
        std::string& errorMessage,
        SequenceContext* context);

    // -------------------------------------------------------------------------
    // High-level state ops — locking + persistence encapsulated. Per-family
    // helpers hit these instead of poking the maps + mutex directly. As more
    // families migrate, more wrappers will land here. Final cleanup may
    // extract these into a separate CoordAlignStore class.
    // -------------------------------------------------------------------------

    /**
     * @brief Insert/replace a taught coordinate system in memory and persist.
     *
     * Best-effort persist (matches the pre-refactor behaviour where the
     * disk-write return value was discarded). Locks m_mutex internally.
     */
    void StoreTaughtSystem(const TaughtCoordinateSystem& sys);

    struct ClearCoordinateSystemResult {
        bool clearedTaught = false;
        bool clearedLive   = false;
    };

    /**
     * @brief Remove taught/live/both entries (memory + disk) for a uid.
     * @param type "taught" / "live" / "both"
     */
    ClearCoordinateSystemResult ClearCoordinateSystemEntries(
        const std::string& uid,
        const std::string& type);

    struct CoordinateSystemListing {
        // Taught is read from disk (filesystem is authoritative).
        std::vector<TaughtCoordinateSystem> taught;
        // Live is snapshotted from m_liveSystems under lock.
        std::vector<std::pair<std::string, LiveCoordinateSystem>> liveByUid;
    };

    CoordinateSystemListing ListCoordinateSystemsSnapshot() const;

    /**
     * @brief Load a taught coordinate system from disk by uid.
     */
    bool LoadTaughtSystem(const std::string& uid, TaughtCoordinateSystem& out);

    /**
     * @brief Insert/replace a live coordinate system in memory and persist.
     *
     * Best-effort persist (matches pre-refactor behaviour). Locks m_mutex.
     */
    void StoreLiveSystem(const LiveCoordinateSystem& live);

    /**
     * @brief Retrieve a live coordinate system: memory first, then disk.
     *
     * Returns true on success; false if neither memory nor disk has it.
     */
    bool GetLiveSystem(const std::string& uid, LiveCoordinateSystem& out);

    /**
     * @brief Load a live coordinate system from disk by uid (disk only,
     * does not consult the in-memory map). Use this when the caller needs
     * the persisted state explicitly — e.g. MoveToLocalXY, which gates on
     * disk presence + transformValid.
     */
    bool LoadLiveSystem(const std::string& uid, LiveCoordinateSystem& out);

    /**
     * @brief Resolve a parameter value that may be a context reference.
     *
     * Accepts "$key", "context:key", or a literal numeric string. Returns 0.0
     * for empty or unparseable input.
     */
    double ResolveNumericValue(const std::string& valueStr, SequenceContext* context) const;

    // -------------------------------------------------------------------------
    // Local-point state ops (Family C). Lock + persistence encapsulated.
    // -------------------------------------------------------------------------

    /**
     * @brief Load a taught local point from disk by uid.
     */
    bool LoadLocalPoint(const std::string& uid, TaughtLocalPoint& out);

    /**
     * @brief Insert/replace a local point in memory and persist.
     *
     * Best-effort persist (matches pre-refactor behaviour). Locks m_mutex.
     */
    void StoreLocalPoint(const TaughtLocalPoint& point);

    /**
     * @brief Remove a local point from disk and memory by uid.
     * @return true if the point was deleted from disk; false if not found.
     */
    bool RemoveLocalPoint(const std::string& uid);

    /**
     * @brief Snapshot of all local points (read from disk; filesystem
     * is authoritative for the persisted set).
     */
    std::vector<TaughtLocalPoint> ListLocalPointsSnapshot() const;

private:
    ApplicationContext* m_appContext = nullptr;

    // Base config path for persistence
    std::string m_configPath = "config/";

    // Thread-safe storage for coordinate systems and local points
    std::map<std::string, TaughtCoordinateSystem> m_taughtSystems;
    std::map<std::string, LiveCoordinateSystem> m_liveSystems;
    std::map<std::string, TaughtLocalPoint> m_localPoints;
    mutable std::mutex m_mutex;

    // =========================================================================
    // Persistence Methods (private — accessed only via the public state ops
    // declared above, which encapsulate locking + the in-memory map updates).
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
