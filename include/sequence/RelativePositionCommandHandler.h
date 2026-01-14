#pragma once

#include "ICommandHandler.h"
#include "MotionTypes.h"
#include <map>
#include <mutex>

class UnifiedMotionManager;

/**
 * @brief Command handler for tracking relative positions between motion devices.
 *
 * This handler allows capturing a baseline relationship between a parent device
 * (e.g., gantry-main) and a child device (e.g., hex-left), storing the XYZ offset.
 * Later, the delta (drift) from this baseline can be computed and stored in
 * sequence context for use by other commands.
 *
 * Supported Methods:
 * - CaptureBaseline: Capture parent and child positions, store relationship with UID
 * - GetDelta: Compute current position delta from stored baseline
 * - ClearBaseline: Remove a specific baseline by UID
 * - ClearAllBaselines: Remove all stored baselines
 * - ListBaselines: Log all stored baseline UIDs
 */
class RelativePositionCommandHandler : public ICommandHandler
{
public:
    RelativePositionCommandHandler() = default;
    ~RelativePositionCommandHandler() override = default;

    bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context) override;

    std::string GetManagerType() const override { return "relpos"; }

    /**
     * @brief Set the motion manager for position queries
     */
    void SetMotionManager(UnifiedMotionManager* motionManager) { m_motionManager = motionManager; }

private:
    /**
     * @brief Stored baseline data for parent-child relationship
     */
    struct BaselineData {
        std::string parentDevice;
        std::string childDevice;
        PositionStruct parentPosition;   // Parent position at capture time
        PositionStruct childPosition;    // Child position at capture time
        PositionStruct relativeOffset;   // child - parent offset at baseline
        std::string timestamp;           // ISO timestamp when captured
    };

    // Thread-safe storage for baselines
    std::map<std::string, BaselineData> m_baselines;
    mutable std::mutex m_baselinesMutex;

    // Motion manager reference (set via SetMotionManager)
    UnifiedMotionManager* m_motionManager = nullptr;

    /**
     * @brief Capture baseline between parent and child devices
     * @param params Must contain: uid, parent, child
     * @param errorMessage Output error message on failure
     * @param context Sequence context (optional, for logging)
     * @return true on success
     */
    bool HandleCaptureBaseline(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Compute delta from stored baseline
     * @param params Must contain: uid, outputPrefix (optional, defaults to "delta")
     * @param errorMessage Output error message on failure
     * @param context Sequence context to store results
     * @return true on success
     */
    bool HandleGetDelta(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Clear a specific baseline by UID
     */
    bool HandleClearBaseline(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage);

    /**
     * @brief Clear all stored baselines
     */
    bool HandleClearAllBaselines(std::string& errorMessage);

    /**
     * @brief List all stored baseline UIDs
     */
    bool HandleListBaselines(std::string& errorMessage);

    /**
     * @brief Get current timestamp as ISO string
     */
    std::string GetCurrentTimestamp() const;
};
