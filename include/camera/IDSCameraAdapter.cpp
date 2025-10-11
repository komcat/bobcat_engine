// IDSCameraAdapter.cpp - FIXED COLOR FORMAT
#include "IDSCameraAdapter.h"
#include <iostream>
#include <chrono>
#include <thread>

IDSCameraAdapter::IDSCameraAdapter(const std::string& cameraId, int deviceId)
	: m_cameraId(cameraId)
	, m_deviceId(deviceId)
	, m_hCam(0)
	, m_isConnected(false)
	, m_isGrabbing(false)
	, m_isDeviceRemoved(false)
	, m_pImageMemory(nullptr)
	, m_memoryId(0)
	, m_threadRunning(false) {

	std::cout << "[INFO] IDSCameraAdapter created for camera: " << cameraId
		<< " (Device ID: " << deviceId << ")" << std::endl;
}

IDSCameraAdapter::~IDSCameraAdapter() {
	try {
		if (IsGrabbing()) {
			StopGrabbing();
		}
		if (IsConnected()) {
			Disconnect();
		}
	}
	catch (...) {
		// Ignore exceptions during destruction
	}

	std::cout << "[INFO] IDSCameraAdapter destroyed for camera: " << m_cameraId << std::endl;
}

bool IDSCameraAdapter::Initialize() {
	try {
		ClearLastError();
		std::cout << "[INFO] IDSCameraAdapter initialized for camera: " << m_cameraId << std::endl;
		return true;
	}
	catch (const std::exception& e) {
		SetError("Exception during initialization: " + std::string(e.what()));
		return false;
	}
}

bool IDSCameraAdapter::Connect() {
	try {
		if (m_isConnected.load()) {
			return true; // Already connected
		}

		std::cout << "[INFO] Attempting to connect IDS camera: " << m_cameraId
			<< " (Device ID: " << m_deviceId << ")" << std::endl;

		// Initialize camera
		m_hCam = m_deviceId;
		INT result = is_InitCamera(&m_hCam, NULL);

		if (!CheckIDSResult(result, "InitCamera")) {
			SetError("Failed to initialize IDS camera with device ID: " + std::to_string(m_deviceId));
			std::cerr << "[ERROR] is_InitCamera failed for device ID: " << m_deviceId
				<< " Error: " << GetIDSErrorString(result) << std::endl;
			return false;
		}

		std::cout << "[INFO] IDS camera is_InitCamera succeeded. Camera handle: "
			<< m_hCam << std::endl;

		// Set connected flag BEFORE trying to get camera properties
		m_isConnected.store(true);
		m_isDeviceRemoved.store(false);

		// Now get basic camera information
		if (!UpdateCameraProperties()) {
			std::cout << "[WARNING] Failed to get camera properties, but camera is connected" << std::endl;
		}

		// Initialize image memory
		if (!InitializeImageMemory()) {
			std::cout << "[WARNING] Failed to initialize image memory, but camera is connected" << std::endl;
		}

		ClearLastError();
		std::cout << "[INFO] IDS camera connected successfully: " << m_cameraId << std::endl;

		return true;
	}
	catch (const std::exception& e) {
		SetError("Exception during connection: " + std::string(e.what()));
		m_isConnected.store(false);
		return false;
	}
}

bool IDSCameraAdapter::UpdateCameraProperties() {
	if (m_hCam == 0) {
		std::cerr << "[ERROR] UpdateCameraProperties: Invalid camera handle" << std::endl;
		return false;
	}

	try {
		std::lock_guard<std::mutex> lock(m_propertiesMutex);

		// Try to get camera information with error checking
		CAMINFO camInfo;
		memset(&camInfo, 0, sizeof(camInfo));

		INT result = is_GetCameraInfo(m_hCam, &camInfo);

		std::cout << "[INFO] is_GetCameraInfo result: " << result << std::endl;

		if (result == IS_SUCCESS) {
			try {
				m_modelName = "IDS_Camera_Connected";
				m_serialNumber = "DeviceID_" + std::to_string(m_deviceId);

				std::cout << "[INFO] Camera connected - using default names: Model: '"
					<< m_modelName << "', Serial: '" << m_serialNumber << "'" << std::endl;

				return true;
			}
			catch (const std::exception& e) {
				std::cerr << "[ERROR] Exception processing camera info: " << e.what() << std::endl;
				m_modelName = "IDS_Camera";
				m_serialNumber = "Unknown";
				return false;
			}
		}
		else {
			std::cerr << "[ERROR] is_GetCameraInfo failed with error: " << result << std::endl;
			m_modelName = "IDS_Camera";
			m_serialNumber = "Unknown";
			return false;
		}
	}
	catch (const std::exception& e) {
		std::cerr << "[ERROR] Exception in UpdateCameraProperties: " << e.what() << std::endl;
		m_modelName = "IDS_Camera";
		m_serialNumber = "Unknown";
		return false;
	}
	catch (...) {
		std::cerr << "[ERROR] Unknown exception in UpdateCameraProperties" << std::endl;
		m_modelName = "IDS_Camera";
		m_serialNumber = "Unknown";
		return false;
	}
}

bool IDSCameraAdapter::Disconnect() {
	try {
		if (!m_isConnected.load()) {
			return true; // Already disconnected
		}

		// Stop grabbing first if active
		if (IsGrabbing()) {
			StopGrabbing();
		}

		// Cleanup image memory
		CleanupImageMemory();

		// Close camera
		if (m_hCam != 0) {
			INT result = is_ExitCamera(m_hCam);
			CheckIDSResult(result, "ExitCamera");
			m_hCam = 0;
		}

		m_isConnected.store(false);
		ClearLastError();

		std::cout << "[INFO] IDS camera disconnected: " << m_cameraId << std::endl;

		return true;
	}
	catch (const std::exception& e) {
		SetError("Exception during disconnection: " + std::string(e.what()));
		return false;
	}
}

bool IDSCameraAdapter::StartGrabbing() {
	try {
		if (!m_isConnected.load()) {
			SetError("Camera not connected");
			return false;
		}

		if (m_isGrabbing.load()) {
			return true; // Already grabbing
		}

		// Start live video capture
		INT result = is_CaptureVideo(m_hCam, IS_DONT_WAIT);
		if (!CheckIDSResult(result, "CaptureVideo")) {
			return false;
		}

		// Start frame grabbing thread
		StartGrabThread();

		m_isGrabbing.store(true);
		ClearLastError();

		std::cout << "[INFO] IDS camera started grabbing: " << m_cameraId << std::endl;

		return true;
	}
	catch (const std::exception& e) {
		SetError("Exception during start grabbing: " + std::string(e.what()));
		return false;
	}
}

bool IDSCameraAdapter::StopGrabbing() {
	try {
		if (!m_isGrabbing.load()) {
			return true; // Already stopped
		}

		// Stop grabbing thread first
		StopGrabThread();

		// Stop video capture
		if (m_hCam != 0) {
			INT result = is_StopLiveVideo(m_hCam, IS_FORCE_VIDEO_STOP);
			CheckIDSResult(result, "StopLiveVideo");
		}

		m_isGrabbing.store(false);
		ClearLastError();

		std::cout << "[INFO] IDS camera stopped grabbing: " << m_cameraId << std::endl;

		return true;
	}
	catch (const std::exception& e) {
		SetError("Exception during stop grabbing: " + std::string(e.what()));
		return false;
	}
}

bool IDSCameraAdapter::IsConnected() const {
	return m_isConnected.load();
}

bool IDSCameraAdapter::IsGrabbing() const {
	return m_isGrabbing.load();
}

bool IDSCameraAdapter::IsDeviceRemoved() const {
	return m_isDeviceRemoved.load();
}

bool IDSCameraAdapter::CaptureFrame(CameraFrameData& frameData) {
	try {
		if (!m_isConnected.load()) {
			SetError("Camera not connected");
			return false;
		}

		if (!m_pImageMemory) {
			SetError("No image memory allocated");
			return false;
		}

		// Get image information
		IS_RECT rectAOI;
		INT result = is_AOI(m_hCam, IS_AOI_IMAGE_GET_AOI, (void*)&rectAOI, sizeof(rectAOI));
		if (!CheckIDSResult(result, "GetAOI")) {
			return false;
		}

		int width = rectAOI.s32Width;
		int height = rectAOI.s32Height;

		// For live mode, check if video has started and frames are available
		if (m_isGrabbing.load()) {
			BOOL videoStarted = FALSE;
			result = is_HasVideoStarted(m_hCam, &videoStarted);
			if (result != IS_SUCCESS || !videoStarted) {
				SetError("Video not started or no frames available");
				return false;
			}
		}

		// Prepare frame data
		frameData.cameraId = m_cameraId;
		frameData.width = width;
		frameData.height = height;
		frameData.channels = 3; // RGB
		frameData.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();

		// Get exposure settings for metadata
		auto exposure = GetExposureSettings();
		frameData.exposureTime = exposure.exposure_time;
		frameData.gain = exposure.gain;

		// Copy image data (already in RGB format)
		size_t imageSize = width * height * 3;
		frameData.imageData.resize(imageSize);
		std::memcpy(frameData.imageData.data(), m_pImageMemory, imageSize);

		ClearLastError();
		return true;
	}
	catch (const std::exception& e) {
		SetError("Exception during frame capture: " + std::string(e.what()));
		return false;
	}
}

bool IDSCameraAdapter::HasNewFrame() const {
	if (!m_isGrabbing.load()) {
		return false;
	}

	// Check if video has started and frames are available
	BOOL videoStarted = FALSE;
	INT result = is_HasVideoStarted(m_hCam, &videoStarted);
	return (result == IS_SUCCESS && videoStarted);
}

std::string IDSCameraAdapter::GetModelName() const {
	std::lock_guard<std::mutex> lock(m_propertiesMutex);
	return m_modelName;
}

std::string IDSCameraAdapter::GetSerialNumber() const {
	std::lock_guard<std::mutex> lock(m_propertiesMutex);
	return m_serialNumber;
}

bool IDSCameraAdapter::SetExposureSettings(const ExposureSettings& settings) {
	try {
		if (!m_isConnected.load()) {
			SetError("Camera not connected");
			return false;
		}

		bool success = true;

		// Set auto exposure
		if (!SetAutoExposure(settings.auto_exposure)) {
			success = false;
		}

		// Set exposure time (if not auto)
		if (!settings.auto_exposure && !SetExposureTime(settings.exposure_time)) {
			success = false;
		}

		// Set auto gain
		if (!SetAutoGain(settings.auto_gain)) {
			success = false;
		}

		// Set gain (if not auto)
		if (!settings.auto_gain && !SetGain(settings.gain)) {
			success = false;
		}

		if (success) {
			ClearLastError();
		}

		return success;
	}
	catch (const std::exception& e) {
		SetError("Exception setting exposure: " + std::string(e.what()));
		return false;
	}
}

ICameraHardware::ExposureSettings IDSCameraAdapter::GetExposureSettings() const {
	ExposureSettings settings;

	if (m_isConnected.load()) {
		settings.exposure_time = GetExposureTime();
		settings.gain = GetGain();
		settings.auto_exposure = GetAutoExposure();
		settings.auto_gain = GetAutoGain();
	}

	return settings;
}

void IDSCameraAdapter::SetFrameCallback(std::function<void(const CameraFrameData&)> callback) {
	std::lock_guard<std::mutex> lock(m_callbackMutex);
	m_frameCallback = callback;

	std::cout << "[INFO] Frame callback set for IDS camera: " << m_cameraId << std::endl;
}

void IDSCameraAdapter::ClearFrameCallback() {
	std::lock_guard<std::mutex> lock(m_callbackMutex);
	m_frameCallback = nullptr;

	std::cout << "[INFO] Frame callback cleared for IDS camera: " << m_cameraId << std::endl;
}

bool IDSCameraAdapter::SetConfiguration(const std::string& key, const std::string& value) {
	try {
		if (!m_isConnected.load()) {
			SetError("Camera not connected");
			return false;
		}

		if (key == "TriggerMode") {
			INT triggerMode = (value == "On") ? IS_SET_TRIGGER_SOFTWARE : IS_SET_TRIGGER_OFF;
			INT result = is_SetExternalTrigger(m_hCam, triggerMode);
			return CheckIDSResult(result, "SetTriggerMode");
		}

		SetError("Unknown configuration key: " + key);
		return false;
	}
	catch (const std::exception& e) {
		SetError("Exception setting configuration: " + std::string(e.what()));
		return false;
	}
}

std::string IDSCameraAdapter::GetConfiguration(const std::string& key) const {
	try {
		if (!m_isConnected.load()) {
			return "";
		}

		if (key == "TriggerMode") {
			return "Off"; // Simplified - would need to query actual state
		}

		return "";
	}
	catch (...) {
		return "";
	}
}

// Frame grabbing thread methods
void IDSCameraAdapter::StartGrabThread() {
	if (!m_threadRunning.load()) {
		m_threadRunning.store(true);
		m_grabThread = std::thread(&IDSCameraAdapter::GrabThreadFunction, this);
	}
}

void IDSCameraAdapter::StopGrabThread() {
	if (m_threadRunning.load()) {
		m_threadRunning.store(false);
		if (m_grabThread.joinable()) {
			m_grabThread.join();
		}
	}
}

void IDSCameraAdapter::GrabThreadFunction() {
	std::cout << "[INFO] IDS grab thread started for " << m_cameraId << std::endl;

	int frameCounter = 0;
	const std::chrono::milliseconds frameDuration(33); // ~30 FPS

	while (m_threadRunning.load() && m_isConnected.load()) {
		auto frameStart = std::chrono::steady_clock::now();

		try {
			// Check if video has started and new frames are available
			BOOL videoStarted = FALSE;
			INT result = is_HasVideoStarted(m_hCam, &videoStarted);

			if (result == IS_SUCCESS && videoStarted) {
				// Capture frame data
				CameraFrameData frameData;
				if (CaptureFrame(frameData)) {
					frameCounter++;

					// Call callback if set (for broadcasting)
					{
						std::lock_guard<std::mutex> lock(m_callbackMutex);
						if (m_frameCallback) {
							m_frameCallback(frameData);
						}
					}
				}
			}
			else if (result != IS_SUCCESS) {
				// Error checking video status
				std::cout << "[WARNING] IDS video status check error: " << result
					<< " for " << m_cameraId << std::endl;
			}
		}
		catch (const std::exception& e) {
			std::cerr << "[ERROR] Error in IDS grab thread: " << e.what()
				<< " for " << m_cameraId << std::endl;
		}

		// Frame rate control
		auto frameEnd = std::chrono::steady_clock::now();
		auto frameTime = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart);

		if (frameTime < frameDuration) {
			std::this_thread::sleep_for(frameDuration - frameTime);
		}
		else {
			std::this_thread::yield();
		}
	}

	std::cout << "[INFO] IDS grab thread stopped for " << m_cameraId << std::endl;
}

// Private helper methods
void IDSCameraAdapter::SetError(const std::string& error) {
	m_lastError = error;
	std::cerr << "[ERROR] IDSCameraAdapter [" << m_cameraId << "]: " << error << std::endl;
}

// ============================================================================
// FIXED: Initialize Image Memory with RGB8
// ============================================================================
bool IDSCameraAdapter::InitializeImageMemory() {
	try {
		// Get image dimensions
		IS_RECT rectAOI;
		INT result = is_AOI(m_hCam, IS_AOI_IMAGE_GET_AOI, (void*)&rectAOI, sizeof(rectAOI));
		if (!CheckIDSResult(result, "GetAOI")) {
			return false;
		}

		int width = rectAOI.s32Width;
		int height = rectAOI.s32Height;

		// ✅ FIX: Set color mode to RGB8 (not MONO8!)
		std::cout << "[INFO] Setting IDS camera to RGB8 color mode..." << std::endl;
		result = is_SetColorMode(m_hCam, IS_CM_RGB8_PACKED);
		if (!CheckIDSResult(result, "SetColorMode RGB8")) {
			// Try BGR8 as alternative
			std::cout << "[INFO] RGB8 failed, trying BGR8..." << std::endl;
			result = is_SetColorMode(m_hCam, IS_CM_BGR8_PACKED);
			if (!CheckIDSResult(result, "SetColorMode BGR8")) {
				std::cerr << "[ERROR] Failed to set RGB/BGR color mode" << std::endl;
				return false;
			}
		}

		int bitsPerPixel = 24; // RGB8 = 24 bits per pixel

		// Allocate image memory
		result = is_AllocImageMem(m_hCam, width, height, bitsPerPixel, &m_pImageMemory, &m_memoryId);
		if (!CheckIDSResult(result, "AllocImageMem")) {
			return false;
		}

		// Set image memory as active
		result = is_SetImageMem(m_hCam, m_pImageMemory, m_memoryId);
		if (!CheckIDSResult(result, "SetImageMem")) {
			CleanupImageMemory();
			return false;
		}

		std::cout << "[INFO] IDS camera image memory initialized: "
			<< width << "x" << height << " RGB8" << std::endl;

		return true;
	}
	catch (...) {
		CleanupImageMemory();
		return false;
	}
}

void IDSCameraAdapter::CleanupImageMemory() {
	if (m_hCam != 0 && m_pImageMemory) {
		is_FreeImageMem(m_hCam, m_pImageMemory, m_memoryId);
		m_pImageMemory = nullptr;
		m_memoryId = 0;
	}
}

// Core exposure/gain methods
bool IDSCameraAdapter::SetExposureTime(double exposureTimeUs) {
	if (!m_isConnected.load()) {
		return false;
	}

	// Get exposure range limits
	double minExposureMs = 0.0;
	double maxExposureMs = 0.0;
	double incrementMs = 0.0;

	INT result = is_Exposure(m_hCam, IS_EXPOSURE_CMD_GET_EXPOSURE_RANGE,
		&minExposureMs, sizeof(minExposureMs));
	if (result == IS_SUCCESS) {
		is_Exposure(m_hCam, IS_EXPOSURE_CMD_GET_EXPOSURE_RANGE_MAX,
			&maxExposureMs, sizeof(maxExposureMs));
		is_Exposure(m_hCam, IS_EXPOSURE_CMD_GET_EXPOSURE_RANGE_INC,
			&incrementMs, sizeof(incrementMs));
	}

	// Convert microseconds to milliseconds for IDS API
	double exposureMs = exposureTimeUs / 1000.0;

	// Clamp to valid range if we got the limits
	if (minExposureMs > 0 && maxExposureMs > 0) {
		if (exposureMs < minExposureMs) {
			exposureMs = minExposureMs;
			std::cout << "[WARNING] Exposure time clamped to minimum: "
				<< minExposureMs << " ms" << std::endl;
		}
		if (exposureMs > maxExposureMs) {
			exposureMs = maxExposureMs;
			std::cout << "[WARNING] Exposure time clamped to maximum: "
				<< maxExposureMs << " ms" << std::endl;
		}
	}

	result = is_Exposure(m_hCam, IS_EXPOSURE_CMD_SET_EXPOSURE,
		&exposureMs, sizeof(exposureMs));
	return CheckIDSResult(result, "SetExposureTime");
}

bool IDSCameraAdapter::SetGain(double gain) {
	if (!m_isConnected.load()) {
		return false;
	}

	// IDS gain is typically 0-100
	int idsGain = static_cast<int>(gain * 10); // Convert 0-10 scale to 0-100
	if (idsGain < 0) idsGain = 0;
	if (idsGain > 100) idsGain = 100;

	INT result = is_SetHardwareGain(m_hCam, idsGain, IS_IGNORE_PARAMETER, IS_IGNORE_PARAMETER, IS_IGNORE_PARAMETER);
	return CheckIDSResult(result, "SetGain");
}

bool IDSCameraAdapter::SetAutoExposure(bool enable) {
	if (!m_isConnected.load()) {
		return false;
	}

	double param1 = enable ? 1.0 : 0.0;
	double param2 = 0.0;

	INT result = is_SetAutoParameter(m_hCam, IS_SET_ENABLE_AUTO_SHUTTER,
		&param1, &param2);

	if (result == IS_SUCCESS && enable) {
		double targetBrightness = 128.0;
		is_SetAutoParameter(m_hCam, IS_SET_AUTO_REFERENCE,
			&targetBrightness, &param2);

		double speed = 50.0;
		is_SetAutoParameter(m_hCam, IS_SET_AUTO_SPEED, &speed, &param2);

		double hysteresis = 3.0;
		is_SetAutoParameter(m_hCam, IS_SET_AUTO_HYSTERESIS,
			&hysteresis, &param2);
	}

	return CheckIDSResult(result, "SetAutoExposure");
}

bool IDSCameraAdapter::SetAutoGain(bool enable) {
	if (!m_isConnected.load()) {
		return false;
	}

	double param1 = enable ? 1.0 : 0.0;
	double param2 = 0.0;

	INT result = is_SetAutoParameter(m_hCam, IS_SET_ENABLE_AUTO_GAIN, &param1, &param2);

	return CheckIDSResult(result, "SetAutoGain");
}

bool IDSCameraAdapter::GetAutoGain() const {
	if (!m_isConnected.load()) {
		return false;
	}

	double param1 = 0.0;
	double param2 = 0.0;

	INT result = is_SetAutoParameter(m_hCam, IS_GET_ENABLE_AUTO_GAIN, &param1, &param2);

	if (result == IS_SUCCESS) {
		return (param1 > 0.0);
	}

	return false;
}

double IDSCameraAdapter::GetExposureTime() const {
	if (!m_isConnected.load()) {
		return 0.0;
	}

	double exposureMs = 0.0;
	INT result = is_Exposure(m_hCam, IS_EXPOSURE_CMD_GET_EXPOSURE, &exposureMs, sizeof(exposureMs));
	if (CheckIDSResult(result, "GetExposureTime")) {
		return exposureMs * 1000.0; // Convert to microseconds
	}

	return 0.0;
}

double IDSCameraAdapter::GetGain() const {
	if (!m_isConnected.load()) {
		return 0.0;
	}

	int gain = is_SetHardwareGain(m_hCam, IS_GET_MASTER_GAIN, IS_IGNORE_PARAMETER, IS_IGNORE_PARAMETER, IS_IGNORE_PARAMETER);
	if (gain != IS_NO_SUCCESS && gain >= 0) {
		return static_cast<double>(gain) / 10.0; // Convert back to 0-10 scale
	}

	return 0.0;
}

bool IDSCameraAdapter::GetAutoExposure() const {
	if (!m_isConnected.load()) {
		return false;
	}

	double param1 = 0.0;
	double param2 = 0.0;

	INT result = is_SetAutoParameter(m_hCam, IS_GET_ENABLE_AUTO_SHUTTER, &param1, &param2);

	if (result == IS_SUCCESS) {
		return (param1 > 0.0);
	}

	return false;
}

std::string IDSCameraAdapter::GetIDSErrorString(INT result) const {
	switch (result) {
	case IS_SUCCESS: return "Success";
	case IS_INVALID_CAMERA_HANDLE: return "Invalid camera handle";
	case IS_IO_REQUEST_FAILED: return "IO request failed";
	case IS_CANT_OPEN_DEVICE: return "Cannot open device";
	case IS_CANT_CLOSE_DEVICE: return "Cannot close device";
	case IS_CANT_SETUP_MEMORY: return "Cannot setup memory";
	case IS_NO_IMAGE_MEM_ALLOCATED: return "No image memory allocated";
	case IS_CANT_CLEANUP_MEMORY: return "Cannot cleanup memory";
	case IS_CANT_COMMUNICATE_WITH_DRIVER: return "Cannot communicate with driver";
	case IS_FUNCTION_NOT_SUPPORTED_YET: return "Function not supported yet";
	case IS_INVALID_VIDEO_MODE: return "Invalid video mode";
	case IS_INVALID_COLOR_MODE: return "Invalid color mode";
	case IS_INVALID_MEMORY_POINTER: return "Invalid memory pointer";
	case IS_DEVICE_BUSY: return "Device busy";
	case IS_TIMED_OUT: return "Timed out";
	default:
		return "Unknown error (Code: " + std::to_string(result) + ")";
	}
}

bool IDSCameraAdapter::CheckIDSResult(INT result, const std::string& operation) const {
	if (result == IS_SUCCESS) {
		return true;
	}

	std::string errorMsg = operation + " failed: " + GetIDSErrorString(result);
	const_cast<IDSCameraAdapter*>(this)->SetError(errorMsg);
	return false;
}