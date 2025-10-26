// unified_motion_manager.h (Refactored with Interface)
#pragma once

#include "IMotionControllerManager.h"
#include "acs_controller_manager.h"
#include "pi_controller_manager.h"
#include "MotionTypes.h"

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <queue>

/**
 * @brief Unified Motion Manager - Manages multiple controller brands
 *
 * Automatically routes devices to appropriate controller manager based on device type.
 * Supports easy addition of new controller brands through IMotionControllerManager interface.
 */
class UnifiedMotionManager {
public:
	UnifiedMotionManager();
	~UnifiedMotionManager();

	// ============================================================================
	// DEVICE MANAGEMENT
	// ============================================================================

	// Device management - automatically routes to correct manager based on Type
	void AddDevice(const MotionDevice& device);
	void RemoveDevice(const std::string& deviceName);
	bool HasDevice(const std::string& deviceName) const;

	// Get device type and configuration
	std::string GetDeviceType(const std::string& deviceName) const;
	bool GetDeviceConfig(const std::string& deviceName, MotionDevice& outDevice) const;
	std::string GetInstalledAxes(const std::string& deviceName) const;

	// ============================================================================
	// CONNECTION MANAGEMENT
	// ============================================================================

	bool ConnectAll();
	void DisconnectAll();
	bool ConnectDevice(const std::string& deviceName);
	bool DisconnectDevice(const std::string& deviceName);
	bool ReconnectDevice(const std::string& deviceName);
	int ConnectEnabledDevices();

	// ============================================================================
	// MOTION CONTROL METHODS
	// ============================================================================

	// Move to absolute position (multi-axis)
	bool MoveToAbs(const std::string& deviceName,
		double x, double y, double z,
		double velocity = 100.0,
		bool blocking = true);

	// Move single axis to position
	bool MoveToPosition(const std::string& deviceName,
		const std::string& axis,
		double position,
		bool blocking = true);

	// Move to named position
// Move to named position
	bool MoveToNamedPosition(const std::string& deviceName,
		const std::string& positionName,
		bool blocking = false);  // ✅ ADD blocking parameter with default false

	// Move relative (single axis)
	bool MoveRelative(const std::string& deviceName,
		const std::string& axis,
		double distance,
		bool blocking = true);

	// Emergency stop methods
	bool EmergencyStop(const std::string& deviceName);
	bool EmergencyStopAll();

	// Additional motion control
	bool StopAxis(const std::string& deviceName, const std::string& axis);
	bool HomeAxis(const std::string& deviceName, const std::string& axis);
	bool IsMoving(const std::string& deviceName, const std::string& axis);
	bool IsDeviceInMotion(const std::string& deviceName);

	// Position queries
	bool GetPosition(const std::string& deviceName,
		const std::string& axis,
		double& position);
	PositionStruct GetCurrentPosition(const std::string& deviceName);

	// Velocity control
	bool SetVelocity(const std::string& deviceName,
		const std::string& axis,
		double velocity);
	bool GetVelocity(const std::string& deviceName,
		const std::string& axis,
		double& velocity);

	// System velocity control (for PI hexapods - applies to all axes)
	bool SetSystemVelocity(const std::string& deviceName, double velocity);
	bool GetSystemVelocity(const std::string& deviceName, double& velocity);

	// Servo control
	bool EnableServo(const std::string& deviceName,
		const std::string& axis,
		bool enable);

	// ============================================================================
	// NAMED POSITION MANAGEMENT
	// ============================================================================

	void AddNamedPosition(const std::string& deviceName,
		const std::string& positionName,
		const PositionStruct& position);

	void AddNamedPosition(const std::string& deviceName,
		const std::string& positionName,
		double x, double y, double z,
		double u = 0.0, double v = 0.0, double w = 0.0);

	bool RemoveNamedPosition(const std::string& deviceName,
		const std::string& positionName);

	bool GetNamedPosition(const std::string& deviceName,
		const std::string& positionName,
		PositionStruct& outPosition) const;

	std::vector<std::string> GetNamedPositionList(const std::string& deviceName) const;

	// ============================================================================
	// DEVICE QUERIES
	// ============================================================================

	std::vector<std::string> GetAllDeviceNames() const;
	std::vector<std::string> GetDeviceNamesByType(const std::string& type) const;
	std::vector<std::string> GetConnectedDeviceNames() const;
	std::map<std::string, bool> GetConnectionStatus() const;

	// Statistics
	size_t GetTotalDeviceCount() const;
	size_t GetDeviceCountByType(const std::string& type) const;
	size_t GetConnectedDeviceCount() const;

	// Get list of supported controller types
	std::vector<std::string> GetSupportedControllerTypes() const;

	// ============================================================================
	// CONTROLLER ACCESS (Type-safe methods)
	// ============================================================================

	// Type-safe controller access
	ACSController* GetACSController(const std::string& deviceName);
	PIController* GetPIController(const std::string& deviceName);

	// Generic controller access - check type first
	void* GetController(const std::string& deviceName, std::string& outType);

	// ============================================================================
	// MANAGER ACCESS (for advanced usage)
	// ============================================================================

	// Get manager by type
	IMotionControllerManager* GetManagerByType(const std::string& type);

	// Direct manager access (legacy compatibility)
	ACSControllerManager* GetACSManager();
	PIControllerManager* GetPIManager();


	// Reload all named positions from config file
	bool ReloadNamedPositions(const std::string& configPath);

	// Clear all named positions for a device
	void ClearNamedPositions(const std::string& deviceName);

	// ============================================================================
	// ✅ NEW - WAYPOINT SYSTEM (Non-blocking with automatic entry/exit handling)
	// ============================================================================

	/**
	 * @brief Update waypoint state machines - MUST be called periodically (~100ms)
	 * Call this from a QTimer in your UI to process waypoint sequences
	 */
	void Update();

	/**
	 * @brief Get current position name (where device is located)
	 * Returns empty string if not at any known position
	 */
	std::string GetCurrentPositionName(const std::string& deviceName) const;

	/**
	 * @brief Manually set current position name
	 * Use this after manual jogging or homing to inform the system where device is
	 */
	void SetCurrentPositionName(const std::string& deviceName,
		const std::string& positionName);

	/**
	 * @brief Check if waypoint system is active for a device
	 */
	bool IsWaypointSystemActive(const std::string& deviceName) const;

	/**
	 * @brief Cancel ongoing waypoint sequence
	 */
	void CancelWaypointSequence(const std::string& deviceName);

	/**
 * @brief Get all node names for a specific device
 * @param deviceName Name of the motion device
 * @return Vector of all saved position names for that device
 */
	std::vector<std::string> GetAllNodeNames(const std::string& deviceName) const;

private:
	// Map of controller type to manager
	std::map<std::string, std::unique_ptr<IMotionControllerManager>> m_managers;

	// Track device types for routing
	std::map<std::string, std::string> m_deviceTypes;  // deviceName -> type

	// Store full device configurations
	std::map<std::string, MotionDevice> m_deviceConfigs;  // deviceName -> MotionDevice

	// Named positions storage: deviceName -> (positionName -> PositionStruct)
	std::map<std::string, std::map<std::string, PositionStruct>> m_namedPositions;

	// Helper method to get manager for a device
	IMotionControllerManager* GetManagerForDevice(const std::string& deviceName);

	// Helper to normalize type strings (case-insensitive)
	std::string NormalizeType(const std::string& type) const;

	// Register a new controller manager
	void RegisterManager(const std::string& type, std::unique_ptr<IMotionControllerManager> manager);


	// ============================================================================
	// ✅ NEW - WAYPOINT STATE MACHINE
	// ============================================================================

	enum class WaypointState {
		Idle,
		MovingThroughExitWaypoints,
		MovingThroughEntryWaypoints,
		MovingToFinalTarget
	};

	struct DeviceWaypointState {
		WaypointState state = WaypointState::Idle;
		std::string currentPositionName;  // Where device is now
		std::string targetPositionName;   // Where we're going

		// Waypoint queue
		std::queue<std::string> waypointQueue;
		size_t exitWaypointCount = 0;
		size_t exitWaypointsProcessed = 0;
	};

	// Waypoint state for each device: deviceName -> state
	std::map<std::string, DeviceWaypointState> m_waypointStates;

	// Internal waypoint methods
	bool startWaypointSequence(const std::string& deviceName);
	void updateDeviceWaypoints(const std::string& deviceName);
	void stopWaypointSequence(const std::string& deviceName);

	// Position detection
	std::string detectCurrentPositionName(const std::string& deviceName,
		double tolerance = 0.5);

	// Execute low-level move (used internally by waypoint system)
	bool executeMoveToPosition(const std::string& deviceName,
		const std::string& positionName,
		bool blocking);


	// ============================================================================
	// ✅ NEW - CALCULATED POSITIONS
	// ============================================================================

	/**
	 * @brief Parse position name for offset notation (e.g. "lensgrip+Y1" or "safe+X-2+Z5")
	 * @param positionName Full position name with offsets
	 * @param baseName Output: base position name
	 * @param offsetX Output: X offset in mm
	 * @param offsetY Output: Y offset in mm
	 * @param offsetZ Output: Z offset in mm
	 * @return true if position has offset notation, false if normal position
	 */
	bool ParseCalculatedPosition(const std::string& positionName,
		std::string& baseName,
		double& offsetX,
		double& offsetY,
		double& offsetZ);

	/**
	 * @brief Get position with runtime offset calculation
	 * @param deviceName Device name
	 * @param positionName Position name (can include offsets like "base+Y1")
	 * @param outPosition Output position
	 * @return true if position found/calculated successfully
	 */
	bool GetPositionWithCalculation(const std::string& deviceName,
		const std::string& positionName,
		PositionStruct& outPosition);
};