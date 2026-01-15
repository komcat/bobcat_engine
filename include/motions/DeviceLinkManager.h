// DeviceLinkManager.h
// Manages spatial relationships between motion devices for linked movement
#pragma once

#include "CoordinateTransformer.h"
#include "TransformationMatrixLoader.h"
#include <string>
#include <map>
#include <vector>

// Forward declaration
class UnifiedMotionManager;

/**
 * @brief Represents a link between a primary device and a linked (follower) device
 *
 * The offset is stored in GLOBAL coordinates, representing the spatial relationship
 * between the two devices at registration time.
 */
struct DeviceLink {
    std::string primaryDevice;    // Reference device (e.g., "gantry-main")
    std::string linkedDevice;     // Follower device (e.g., "hex-left")
    CoordinateTransformer::Vector6D registeredOffset;  // T0: offset in global coords at registration
    std::string timestamp;        // When the link was registered
};

/**
 * @brief Manages device links for synchronized motion compensation
 *
 * This class allows registering spatial relationships between a primary device
 * (the "measurer", typically gantry-main) and linked devices (e.g., hexapods).
 * When the primary device moves, linked devices can be compensated to maintain
 * their original spatial relationship.
 *
 * Usage:
 * 1. RegisterLink() - Capture positions and store offset T0
 * 2. Later, call GetCompensationMove() to calculate required correction
 * 3. Apply the correction to restore the original relationship
 */
class DeviceLinkManager {
public:
    DeviceLinkManager();
    ~DeviceLinkManager() = default;

    /**
     * @brief Set the transformation matrix loader for coordinate conversions
     * @param loader Pointer to TransformationMatrixLoader (must remain valid)
     */
    void SetMatrixLoader(TransformationMatrixLoader* loader) { m_matrixLoader = loader; }

    /**
     * @brief Set the motion manager for position queries
     * @param manager Pointer to UnifiedMotionManager (must remain valid)
     */
    void SetMotionManager(UnifiedMotionManager* manager) { m_motionManager = manager; }

    // ============================================================================
    // LINK REGISTRATION
    // ============================================================================

    /**
     * @brief Register a link between two devices at their current positions
     *
     * Captures the current positions of both devices, transforms them to global
     * coordinates, and stores the offset T0 = primary_global - linked_global.
     *
     * @param primaryDevice The reference device name (e.g., "gantry-main")
     * @param linkedDevice The follower device name (e.g., "hex-left")
     * @return true if registration succeeded
     */
    bool RegisterLink(const std::string& primaryDevice, const std::string& linkedDevice);

    /**
     * @brief Register a link with explicit offset (for restoring from file)
     */
    bool RegisterLinkWithOffset(const std::string& primaryDevice,
                                const std::string& linkedDevice,
                                const CoordinateTransformer::Vector6D& offset,
                                const std::string& timestamp);

    /**
     * @brief Check if a link exists between two devices
     */
    bool HasLink(const std::string& primaryDevice, const std::string& linkedDevice) const;

    /**
     * @brief Get the stored link between two devices
     * @return Pointer to DeviceLink or nullptr if not found
     */
    const DeviceLink* GetLink(const std::string& primaryDevice,
                              const std::string& linkedDevice) const;

    /**
     * @brief Remove a link between two devices
     */
    bool RemoveLink(const std::string& primaryDevice, const std::string& linkedDevice);

    /**
     * @brief Get all registered links
     */
    std::vector<DeviceLink> GetAllLinks() const;

    // ============================================================================
    // COMPENSATION CALCULATION
    // ============================================================================

    /**
     * @brief Calculate the compensation move needed to restore original relationship
     *
     * This calculates how much the linked device needs to move (in its LOCAL coords)
     * to restore the original spatial relationship registered in T0.
     *
     * @param primaryDevice The reference device name
     * @param linkedDevice The follower device name
     * @param outCompensation Output: movement vector in linkedDevice's LOCAL coordinates
     * @param axes Which axes to compensate ("XY", "XYZ", or "XYZUVW")
     * @return true if calculation succeeded
     */
    bool GetCompensationMove(const std::string& primaryDevice,
                             const std::string& linkedDevice,
                             CoordinateTransformer::Vector6D& outCompensation,
                             const std::string& axes = "XY") const;

    /**
     * @brief Calculate the current offset drift from registered T0
     *
     * Returns T1 - T0 where T1 is current offset in global coords.
     * Positive values mean the linked device has drifted away.
     *
     * @param primaryDevice The reference device name
     * @param linkedDevice The follower device name
     * @param outDrift Output: drift vector in GLOBAL coordinates
     * @return true if calculation succeeded
     */
    bool GetCurrentDrift(const std::string& primaryDevice,
                         const std::string& linkedDevice,
                         CoordinateTransformer::Vector6D& outDrift) const;

    // ============================================================================
    // PERSISTENCE
    // ============================================================================

    /**
     * @brief Save all links to a JSON file
     * @param filePath Path to save file (e.g., "config/device_links.json")
     * @return true if save succeeded
     */
    bool SaveToFile(const std::string& filePath) const;

    /**
     * @brief Load links from a JSON file
     * @param filePath Path to load file
     * @return true if load succeeded (empty file is valid)
     */
    bool LoadFromFile(const std::string& filePath);

private:
    // Key format: "primaryDevice|linkedDevice"
    std::string MakeLinkKey(const std::string& primary, const std::string& linked) const;

    // Get current position of a device in GLOBAL coordinates
    bool GetPositionInGlobal(const std::string& deviceName,
                             CoordinateTransformer::Vector6D& outGlobalPos) const;

    // Storage for links
    std::map<std::string, DeviceLink> m_links;

    // Dependencies (must be set before use)
    TransformationMatrixLoader* m_matrixLoader = nullptr;
    UnifiedMotionManager* m_motionManager = nullptr;
};
