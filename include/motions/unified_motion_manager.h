// unified_motion_manager.h (Refactored with Interface)
#pragma once

#include "IMotionControllerManager.h"
#include "acs_controller_manager.h"
#include "pi_controller_manager.h"
#include "MotionTypes.h"
#include "ILogger.h"

class InterlockManager;
class TransformationMatrixLoader;

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <queue>

// Forward declaration
class SafetyWatchManager;

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
	// SAFETY WATCH INTEGRATION
	// ============================================================================

	/**
	 * @brief Subscribe to SafetyWatchManager for emergency stop on critical safety
	 * @param safetyManager Pointer to SafetyWatchManager
	 */
	void SetSafetyWatchManager(SafetyWatchManager* safetyManager);

	// ============================================================================
	// LOGGER SUPPORT
	// ============================================================================

	/**
	 * @brief Set logger for this manager and propagate to child managers
	 * @param logger Pointer to ILogger interface (LogManager implements this)
	 */
	void SetLogger(ILogger* logger);

	// ============================================================================
	// DEBUG LOGGING CONTROL
	// ============================================================================

	/**
	 * @brief Enable/disable verbose debug logging for waypoint system
	 * @param enable true to enable detailed logging, false to suppress
	 */
	// ============================================================================
	// INTERLOCK SUPPORT
	// ============================================================================

	/**
	 * @brief Set interlock manager for pre-move position checks
	 * @param mgr Pointer to InterlockManager (owned externally)
	 */
	void SetInterlockManager(InterlockManager* mgr) { m_interlockManager = mgr; }
	InterlockManager* GetInterlockManager() const { return m_interlockManager; }

	/**
	 * @brief Set transformation matrix loader for global-frame relative moves
	 * @param loader Pointer to TransformationMatrixLoader (owned externally)
	 */
	void SetMatrixLoader(TransformationMatrixLoader* loader) { m_matrixLoader = loader; }
	TransformationMatrixLoader* GetMatrixLoader() const { return m_matrixLoader; }

	void SetDebugLogging(bool enable) { m_debugLogging = enable; }
	bool IsDebugLoggingEnabled() const { return m_debugLogging; }

	/**
	 * @brief Get string representation of waypoint state for debugging
	 */
	std::string GetWaypointStateString(const std::string& deviceName) const;

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

	[[nodiscard]] bool ConnectAll();
	void DisconnectAll();
	[[nodiscard]] bool ConnectDevice(const std::string& deviceName);
	bool DisconnectDevice(const std::string& deviceName);
	bool ReconnectDevice(const std::string& deviceName);
	int ConnectEnabledDevices();

	// ============================================================================
	// MOTION CONTROL METHODS
	// ============================================================================

	// Move to absolute position (XYZ only)
	[[nodiscard]] bool MoveToAbs(const std::string& deviceName,
		double x, double y, double z,
		double velocity = 100.0,
		bool blocking = true);

	// Move to absolute position (all 6 axes XYZUVW)
	[[nodiscard]] bool MoveToAbs6(const std::string& deviceName,
		const PositionStruct& position,
		bool blocking = true);

	// Move single axis to position
	[[nodiscard]] bool MoveToPosition(const std::string& deviceName,
		const std::string& axis,
		double position,
		bool blocking = true);

	// Move to named position
// Move to named position
	[[nodiscard]] bool MoveToNamedPosition(const std::string& deviceName,
		const std::string& positionName,
		bool blocking = false);  // ✅ ADD blocking parameter with default false

	// Move relative (single axis)
	[[nodiscard]] bool MoveRelative(const std::string& deviceName,
		const std::string& axis,
		double distance,
		bool blocking = true);

	// Move relative expressed in the GLOBAL frame.
	// The (dx, dy, dz) delta is transformed into the device's local axes via
	// the per-device 3x3 matrix from transformation_matrix.json and dispatched
	// as a single simultaneous multi-axis relative move. Fails if the device
	// is not registered in the matrix file.
	[[nodiscard]] bool MoveRelativeGlobal(const std::string& deviceName,
		double dx, double dy, double dz,
		bool blocking = true);

	// Convenience wrapper: relative move along a single GLOBAL axis ("X"|"Y"|"Z").
	[[nodiscard]] bool MoveRelativeGlobalAxis(const std::string& deviceName,
		const std::string& globalAxis,
		double distance,
		bool blocking = true);

	// Emergency stop methods
	[[nodiscard]] bool EmergencyStop(const std::string& deviceName);
	[[nodiscard]] bool EmergencyStopAll();

	// Additional motion control
	[[nodiscard]] bool StopAxis(const std::string& deviceName, const std::string& axis);
	[[nodiscard]] bool HomeAxis(const std::string& deviceName, const std::string& axis);
	bool IsMoving(const std::string& deviceName, const std::string& axis);
	bool IsDeviceInMotion(const std::string& deviceName);

	// Position queries
	[[nodiscard]] bool GetPosition(const std::string& deviceName,
		const std::string& axis,
		double& position);
	PositionStruct GetCurrentPosition(const std::string& deviceName);

	// Velocity control
	[[nodiscard]] bool SetVelocity(const std::string& deviceName,
		const std::string& axis,
		double velocity);
	bool GetVelocity(const std::string& deviceName,
		const std::string& axis,
		double& velocity);

	// System velocity control (for PI hexapods - applies to all axes)
	bool SetSystemVelocity(const std::string& deviceName, double velocity);
	bool GetSystemVelocity(const std::string& deviceName, double& velocity);

	// Servo control
	[[nodiscard]] bool EnableServo(const std::string& deviceName,
		const std::string& axis,
		bool enable);

	bool IsServoEnabled(const std::string& deviceName,
		const std::string& axis);

	// Error information (for debugging)
	int GetLastErrorCode(const std::string& deviceName);
	std::string GetLastErrorMessage(const std::string& deviceName);

	// ============================================================================
	// PI FAST SCAN / ACTIVE ALIGNMENT (analog feedback based)
	// ============================================================================

	/**
	 * @brief Fast Line Maximum - 1D scan to find position of maximum analog signal
	 * Moves to the position where maximum signal is found after scan completes
	 * @param deviceName PI device name
	 * @param axis Axis to scan ("X", "Y", "Z", etc.)
	 * @param length Scan range in mm
	 * @param threshold Minimum signal threshold (voltage)
	 * @param analogInput Analog input channel number
	 * @param direction Scan direction: +1 (positive) or -1 (negative)
	 * @return true if scan started successfully
	 */
	bool FLM(const std::string& deviceName, const std::string& axis,
		double length, double threshold, int analogInput, int direction = 1);

	/**
	 * @brief Fast Line Scan - 1D scan along axis (returns to start after scan)
	 * @param deviceName PI device name
	 * @param axis Axis to scan
	 * @param length Scan range in mm
	 * @param threshold Minimum signal threshold (voltage)
	 * @param analogInput Analog input channel number
	 * @param direction Scan direction: +1 (positive) or -1 (negative)
	 * @return true if scan started successfully
	 */
	bool FLS(const std::string& deviceName, const std::string& axis,
		double length, double threshold, int analogInput, int direction = 1);

	/**
	 * @brief Fast Scan Area - 2D area scan with alignment
	 * @param deviceName PI device name
	 * @param axis1 First axis to scan
	 * @param length1 Scan range for first axis (mm)
	 * @param axis2 Second axis to scan
	 * @param length2 Scan range for second axis (mm)
	 * @param threshold Minimum signal threshold (voltage)
	 * @param distance Step distance between scan lines (mm)
	 * @param alignStep Alignment step size (mm)
	 * @param analogInput Analog input channel number
	 * @return true if scan started successfully
	 */
	bool FSA(const std::string& deviceName,
		const std::string& axis1, double length1,
		const std::string& axis2, double length2,
		double threshold, double distance, double alignStep, int analogInput);

	/**
	 * @brief Fast Scan Circle - 2D circular scan pattern
	 * @param deviceName PI device name
	 * @param axis1 First axis
	 * @param length1 Scan range for first axis (mm)
	 * @param axis2 Second axis
	 * @param length2 Scan range for second axis (mm)
	 * @param threshold Minimum signal threshold (voltage)
	 * @param distance Circle radius/distance (mm)
	 * @param analogInput Analog input channel number
	 * @return true if scan started successfully
	 */
	bool FSC(const std::string& deviceName,
		const std::string& axis1, double length1,
		const std::string& axis2, double length2,
		double threshold, double distance, int analogInput);

	/**
	 * @brief Fast Scan Maximum - 2D scan to find maximum signal position
	 * @param deviceName PI device name
	 * @param axis1 First axis
	 * @param length1 Scan range for first axis (mm)
	 * @param axis2 Second axis
	 * @param length2 Scan range for second axis (mm)
	 * @param threshold Minimum signal threshold (voltage)
	 * @param distance Step distance (mm)
	 * @param analogInput Analog input channel number
	 * @return true if scan started successfully
	 */
	bool FSM(const std::string& deviceName,
		const std::string& axis1, double length1,
		const std::string& axis2, double length2,
		double threshold, double distance, int analogInput);

	/**
	 * @brief Query fast scan status
	 * @param deviceName PI device name
	 * @param status Output: 0=idle, >0=running, <0=error
	 * @return true if query successful
	 */
	bool GetFastScanStatus(const std::string& deviceName, int& status);

	/**
	 * @brief Wait for fast scan to complete
	 * @param deviceName PI device name
	 * @param timeoutSeconds Maximum time to wait
	 * @return true if scan completed successfully, false on timeout or error
	 */
	bool WaitForFastScanComplete(const std::string& deviceName, double timeoutSeconds = 30.0);

	// ============================================================================
	// ACS BUFFER PROGRAM CONTROL
	// ============================================================================

	/**
	 * @brief Run a buffer program on an ACS controller
	 * @param deviceName Name of the ACS device
	 * @param bufferNumber Buffer number (0-63)
	 * @param labelName Optional label name to start from
	 * @return true if successful
	 */
	bool RunBuffer(const std::string& deviceName, int bufferNumber,
		const std::string& labelName = "");

	/**
	 * @brief Stop a buffer program on an ACS controller
	 * @param deviceName Name of the ACS device
	 * @param bufferNumber Buffer number (0-63)
	 * @return true if successful
	 */
	bool StopBuffer(const std::string& deviceName, int bufferNumber);

	/**
	 * @brief Stop all buffer programs on an ACS controller
	 * @param deviceName Name of the ACS device
	 * @return true if successful
	 */
	bool StopAllBuffers(const std::string& deviceName);

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
	// ✅ NEW - POSITION SAVE TO FILE (for RetraceEdit feature)
	// ============================================================================

	/**
	 * @brief Save a named position to the local config file (motor_devices.local.json)
	 *
	 * Used by RetraceEdit to persist position adjustments. This method:
	 * 1. Reads motor_devices.local.json (creates from .json if not exists)
	 * 2. Updates the position coordinates for the specified position
	 * 3. Preserves existing waypoints if preserveWaypoints=true
	 * 4. Writes back to motor_devices.local.json
	 * 5. Updates the runtime m_namedPositions map
	 *
	 * @param deviceName Device name (e.g., "gantry-main", "hex-left")
	 * @param positionName Position name (e.g., "lensgrip", "safe")
	 * @param newPosition New coordinates to save
	 * @param preserveWaypoints If true, keeps existing waypoints; if false, clears them
	 * @return true if save succeeded, false on error
	 */
	bool SaveNamedPositionToFile(
		const std::string& deviceName,
		const std::string& positionName,
		const PositionStruct& newPosition,
		bool preserveWaypoints = true
	);

	/**
	 * @brief Get the config file path currently in use
	 * @return Path to motor_devices.local.json or motor_devices.json
	 */
	std::string GetConfigFilePath() const { return m_configFilePath; }

	/**
	 * @brief Set the config file path (called during loading)
	 */
	void SetConfigFilePath(const std::string& path) { m_configFilePath = path; }

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
	 * @brief Get pending target position for a device (set when non-blocking move starts)
	 * @return Target position name, or empty string if no pending move
	 */
	std::string GetPendingTarget(const std::string& deviceName) const;

	/**
	 * @brief Clear pending target for a device (call when move is confirmed complete)
	 */
	void ClearPendingTarget(const std::string& deviceName);

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

	// Interlock manager (owned externally)
	InterlockManager* m_interlockManager = nullptr;

	// Transformation matrix loader (owned externally) - used by MoveRelativeGlobal
	TransformationMatrixLoader* m_matrixLoader = nullptr;

	// Debug logging flag
	bool m_debugLogging = false;  // Set to true to enable verbose waypoint logging

	// Logger for spdlog integration
	ILogger* m_logger = nullptr;

	// Config file path (set during loading)
	std::string m_configFilePath;

	// Pending target tracking for non-blocking moves
	// Stores the target position name for each device when a non-blocking move starts
	std::map<std::string, std::string> m_pendingTargets;  // deviceName -> targetPositionName

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

};