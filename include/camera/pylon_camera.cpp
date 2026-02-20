#include "../include/camera/pylon_camera.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>
#include <cmath>
#include <nlohmann/json.hpp>

// Helper function to load camera settle delay from config
static int loadCameraSettleDelay() {
    const int DEFAULT_SETTLE_MS = 300;
    try {
        std::ifstream file("config/SYSTEM/systemdelay.json");
        if (file.is_open()) {
            nlohmann::json config = nlohmann::json::parse(file);
            if (config.contains("camera_settle_ms")) {
                int delay = config["camera_settle_ms"].get<int>();
                std::cout << "[PylonCamera] Loaded camera_settle_ms from config: " << delay << "ms" << std::endl;
                return delay;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[PylonCamera] Failed to load systemdelay.json: " << e.what() << std::endl;
    }
    std::cout << "[PylonCamera] Using default camera_settle_ms: " << DEFAULT_SETTLE_MS << "ms" << std::endl;
    return DEFAULT_SETTLE_MS;
}

// Implementation of PylonDeviceRemovalHandler

PylonDeviceRemovalHandler::PylonDeviceRemovalHandler(PylonCamera* pOwner)
	: m_pOwner(pOwner)
{
}

PylonDeviceRemovalHandler::~PylonDeviceRemovalHandler()
{
	// No specific cleanup needed
}

void PylonDeviceRemovalHandler::OnCameraDeviceRemoved(Pylon::CInstantCamera& /*camera*/)
{
	if (m_pOwner)
	{
		std::cout << "PylonDeviceRemovalHandler::OnCameraDeviceRemoved called." << std::endl;
		// Notify the PylonCamera that the device has been removed
		m_pOwner->HandleDeviceRemoval();
	}
}

// Implementation of PylonCamera


PylonCamera::PylonCamera()
	: m_initialized(false)
	, m_connected(false)
	, m_deviceRemoved(false)
	, m_reconnecting(false)
	, m_threadRunning(false)
	, m_newFrameReady(false)
	, m_pDeviceRemovalHandler(nullptr)
	, m_handlerRegistered(false)  // Track handler registration state
	, m_deviceRemovalCallback(nullptr)
	, m_newFrameCallback(nullptr)
	, m_targetFPS(30)
	, m_lastIPAddress("")
	, m_lastDeviceIndex(-1)
	, m_hasLatestFrame(false)
	, m_cameraModel(BaslerCameraModel::UNKNOWN)  // Initialize camera model
{
	// Initialize Pylon runtime
	Pylon::PylonInitialize();

	// Set up format converter for GetLatestFrameData
	m_formatConverter.OutputPixelFormat = Pylon::PixelType_RGB8packed;
	m_formatConverter.OutputBitAlignment = Pylon::OutputBitAlignment_MsbAligned;

	// Create the device removal handler
	m_pDeviceRemovalHandler = new PylonDeviceRemovalHandler(this);
}


PylonCamera::~PylonCamera() {
	std::cout << "PylonCamera destructor called" << std::endl;

	try {
		// Stop thread FIRST
		bool wasRunning = m_threadRunning.exchange(false);
		if (wasRunning && m_grabThread.joinable()) {
			std::cout << "Joining grab thread..." << std::endl;
			m_grabThread.join();
			std::cout << "Grab thread joined" << std::endl;
		}

		// Stop grabbing if active
		if (m_connected && m_camera.IsGrabbing()) {
			std::cout << "Stopping grabbing..." << std::endl;
			m_camera.StopGrabbing();
		}

		// ✅ CRITICAL FIX: DEREGISTER handler BEFORE closing camera (must be done while camera is open)
		if (m_pDeviceRemovalHandler && m_handlerRegistered) {
			std::cout << "Deregistering event handler..." << std::endl;
			try {
				// Only deregister if camera is open and handler is registered
				if (m_camera.IsOpen()) {
					m_camera.DeregisterConfiguration(m_pDeviceRemovalHandler);
					m_handlerRegistered = false;
					std::cout << "Event handler deregistered successfully" << std::endl;
				}
				else {
					std::cout << "Camera not open, skipping deregistration (handler may already be detached)" << std::endl;
					m_handlerRegistered = false;  // Mark as unregistered anyway
				}
			}
			catch (const Pylon::GenericException& e) {
				std::cerr << "Warning: Failed to deregister handler: " << e.GetDescription() << std::endl;
				m_handlerRegistered = false;  // Mark as unregistered to prevent issues
			}
		}

		// Close camera if open (AFTER deregistering handler)
		if (m_camera.IsOpen()) {
			std::cout << "Closing camera..." << std::endl;
			m_camera.Close();
		}

		// Detach device
		if (m_camera.IsPylonDeviceAttached()) {
			std::cout << "Detaching camera device..." << std::endl;
			m_camera.DetachDevice();
		}

		// NOW safe to delete handler (after deregistration)
		if (m_pDeviceRemovalHandler) {
			std::cout << "Deleting device removal handler..." << std::endl;
			delete m_pDeviceRemovalHandler;
			m_pDeviceRemovalHandler = nullptr;
		}

		// Terminate Pylon
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		Pylon::PylonTerminate();
		std::cout << "Pylon terminated successfully" << std::endl;
	}
	catch (const std::exception& e) {
		std::cerr << "Error in PylonCamera destructor: " << e.what() << std::endl;
	}
	catch (...) {
		std::cerr << "Unknown error in PylonCamera destructor" << std::endl;
	}

	std::cout << "PylonCamera destructor completed" << std::endl;
}

bool PylonCamera::Initialize()
{
	if (m_initialized)
	{
		return true; // Already initialized
	}

	try
	{
		// Reset state
		m_deviceRemoved = false;
		m_initialized = true;

		return true;
	}
	catch (const Pylon::GenericException& e)
	{
		std::cerr << "Error initializing camera: " << e.GetDescription() << std::endl;
		return false;
	}
}

bool PylonCamera::Connect()
{
	if (!m_initialized)
	{
		if (!Initialize())
		{
			return false;
		}
	}

	// Already connected
	if (m_connected)
	{
		return true;
	}

	try
	{
		std::lock_guard<std::mutex> lock(m_cameraMutex);

		// Get the transport layer factory
		Pylon::CTlFactory& tlFactory = Pylon::CTlFactory::GetInstance();

		// Get all attached devices
		Pylon::DeviceInfoList_t devices;
		if (tlFactory.EnumerateDevices(devices) == 0)
		{
			std::cerr << "No camera found" << std::endl;
			return false; // No camera found
		}

		// Create device from first camera found
		m_camera.Attach(tlFactory.CreateDevice(devices[0]));

		// Store device info for reconnection if needed
		m_lastDeviceSerialNumber = m_camera.GetDeviceInfo().GetSerialNumber();
		m_lastDeviceClass = m_camera.GetDeviceInfo().GetDeviceClass();

		// Register the device removal handler with the camera (only if not already registered)
		if (!m_handlerRegistered) {
			m_camera.RegisterConfiguration(m_pDeviceRemovalHandler, Pylon::RegistrationMode_Append, Pylon::Cleanup_None);
			m_handlerRegistered = true;
		}

		// Open the camera
		m_camera.Open();

		// For GigE cameras, set a reasonable heartbeat timeout for faster device removal detection
		try
		{
			Pylon::CIntegerParameter heartbeat(m_camera.GetTLNodeMap(), "HeartbeatTimeout");
			heartbeat.TrySetValue(1000, Pylon::IntegerValueCorrection_Nearest);  // set to 1000 ms timeout if writable
		}
		catch (...)
		{
			// Ignore if not available for this camera
		}

		m_connected = true;
		m_deviceRemoved = false;

		std::cout << "Connected to camera: " << m_camera.GetDeviceInfo().GetModelName() << std::endl;
		std::cout << "Serial Number: " << m_lastDeviceSerialNumber << std::endl;

		// Detect camera model (ace vs ace2) for correct parameter usage
		DetectCameraModel();

		return true;
	}
	catch (const Pylon::GenericException& e)
	{
		std::cerr << "Error connecting to camera: " << e.GetDescription() << std::endl;
		return false;
	}
}

bool PylonCamera::ConnectToSerial(const std::string& serialNumber)
{
	if (!m_initialized)
	{
		if (!Initialize())
		{
			return false;
		}
	}

	// Already connected
	if (m_connected)
	{
		// Check if we're already connected to the requested camera
		// Convert Pylon::String_t to std::string for proper comparison
		std::string currentSerialNumber(m_lastDeviceSerialNumber.c_str());
		if (currentSerialNumber == serialNumber)
		{
			return true;
		}

		// Disconnect from the current camera
		Disconnect();
	}

	try
	{
		std::lock_guard<std::mutex> lock(m_cameraMutex);

		// Get the transport layer factory
		Pylon::CTlFactory& tlFactory = Pylon::CTlFactory::GetInstance();

		// Create device info object for the camera we want to connect to
		Pylon::CDeviceInfo info;
		info.SetSerialNumber(serialNumber.c_str());

		// Create a filter with the device info
		Pylon::DeviceInfoList_t filter;
		filter.push_back(info);

		// Try to find the camera
		Pylon::DeviceInfoList_t devices;
		if (tlFactory.EnumerateDevices(devices, filter) == 0)
		{
			std::cerr << "Camera with serial " << serialNumber << " not found" << std::endl;

			// Debug: List all available cameras to help diagnose the issue
			Pylon::DeviceInfoList_t allDevices;
			size_t numDevices = tlFactory.EnumerateDevices(allDevices);
			std::cerr << "Available cameras (" << numDevices << " found):" << std::endl;
			for (size_t i = 0; i < numDevices; i++) {
				std::cerr << "  [" << i << "] Model: " << allDevices[i].GetModelName()
				          << ", Serial: " << allDevices[i].GetSerialNumber()
				          << ", Class: " << allDevices[i].GetDeviceClass() << std::endl;
			}
			if (numDevices == 0) {
				std::cerr << "  (No cameras detected - check if Pylon Viewer or another app has exclusive access)" << std::endl;
			}

			return false; // Camera not found
		}

		// Create device from the found camera
		m_camera.Attach(tlFactory.CreateDevice(devices[0]));

		// Store device info for reconnection if needed
		m_lastDeviceSerialNumber = m_camera.GetDeviceInfo().GetSerialNumber();
		m_lastDeviceClass = m_camera.GetDeviceInfo().GetDeviceClass();

		// Register the device removal handler with the camera (only if not already registered)
		if (!m_handlerRegistered) {
			m_camera.RegisterConfiguration(m_pDeviceRemovalHandler, Pylon::RegistrationMode_Append, Pylon::Cleanup_None);
			m_handlerRegistered = true;
		}

		// Open the camera
		m_camera.Open();

		// For GigE cameras, set a reasonable heartbeat timeout for faster device removal detection
		try
		{
			Pylon::CIntegerParameter heartbeat(m_camera.GetTLNodeMap(), "HeartbeatTimeout");
			heartbeat.TrySetValue(1000, Pylon::IntegerValueCorrection_Nearest);  // set to 1000 ms timeout if writable
		}
		catch (...)
		{
			// Ignore if not available for this camera
		}

		m_connected = true;
		m_deviceRemoved = false;

		std::cout << "Connected to camera: " << m_camera.GetDeviceInfo().GetModelName() << std::endl;
		std::cout << "Serial Number: " << m_lastDeviceSerialNumber << std::endl;

		// Detect camera model (ace vs ace2) for correct parameter usage
		DetectCameraModel();

		return true;
	}
	catch (const Pylon::GenericException& e)
	{
		std::cerr << "Error connecting to camera: " << e.GetDescription() << std::endl;
		return false;
	}
}

void PylonCamera::Disconnect()
{
	if (!m_connected)
	{
		return; // Not connected
	}

	try
	{
		// First, stop grabbing if we're grabbing (BEFORE acquiring the lock)
		// StopGrabbing() has its own locking for the camera operations
		if (m_threadRunning.load())
		{
			StopGrabbing();
		}

		// Now acquire lock for camera close operation
		std::lock_guard<std::mutex> lock(m_cameraMutex);

		// ✅ CRITICAL: Deregister event handler BEFORE closing camera
		if (m_pDeviceRemovalHandler && m_handlerRegistered && m_camera.IsOpen())
		{
			try {
				m_camera.DeregisterConfiguration(m_pDeviceRemovalHandler);
				m_handlerRegistered = false;
				std::cout << "Event handler deregistered during disconnect" << std::endl;
			}
			catch (const Pylon::GenericException& e) {
				std::cerr << "Warning: Failed to deregister handler during disconnect: " << e.GetDescription() << std::endl;
			}
		}

		// Close camera
		if (m_camera.IsOpen())
		{
			m_camera.Close();
		}

		m_connected = false;
		std::cout << "Camera disconnected successfully" << std::endl;
	}
	catch (const Pylon::GenericException& e)
	{
		std::cerr << "Error disconnecting camera: " << e.GetDescription() << std::endl;
	}
}

bool PylonCamera::StartGrabbing()
{
	// Use a lock to ensure thread safety during camera configuration
	std::lock_guard<std::mutex> lock(m_cameraMutex);

	if (!m_connected || m_deviceRemoved)
	{
		return false;
	}

	// If already grabbing, stop first
	if (m_camera.IsGrabbing())
	{
		try {
			m_camera.StopGrabbing();

			// Wait for thread to terminate if it exists
			if (m_threadRunning.load())
			{
				m_threadRunning.store(false);
				if (m_grabThread.joinable())
				{
					m_grabThread.join();
				}
			}
		}
		catch (const Pylon::GenericException& e) {
			std::cerr << "Error stopping existing grab session: " << e.GetDescription() << std::endl;
		}
	}

	try
	{
		// Set acquisition parameters
		m_camera.MaxNumBuffer = 10;  // Increased buffer count

		// Set acquisition mode to continuous
		try {
			// First check if the camera supports setting acquisition mode
			if (m_camera.GetNodeMap().GetNode("AcquisitionMode")) {
				Pylon::CEnumParameter(m_camera.GetNodeMap(), "AcquisitionMode").SetValue("Continuous");
				std::cout << "Set acquisition mode to Continuous" << std::endl;
			}
		}
		catch (const Pylon::GenericException& e) {
			std::cerr << "Warning: Could not set acquisition mode: " << e.GetDescription() << std::endl;
		}

		// Disable trigger mode if camera supports it
		try {
			if (m_camera.GetNodeMap().GetNode("TriggerMode")) {
				Pylon::CEnumParameter(m_camera.GetNodeMap(), "TriggerMode").SetValue("Off");
				std::cout << "Disabled trigger mode" << std::endl;
			}
		}
		catch (const Pylon::GenericException& e) {
			std::cerr << "Warning: Could not disable trigger mode: " << e.GetDescription() << std::endl;
		}

		// For GigE cameras, try to optimize the packet size
		try {
			// Check if this is a GigE camera
			if (m_camera.GetTLNodeMap().GetNode("GevSCPSPacketSize")) {
				Pylon::CIntegerParameter packetSize(m_camera.GetTLNodeMap(), "GevSCPSPacketSize");
				// Get the maximum allowed packet size
				int64_t maxPacketSize = packetSize.GetMax();
				// Set to maximum value
				packetSize.SetValue(maxPacketSize);
				std::cout << "Set GigE packet size to maximum: " << packetSize.GetValue() << std::endl;
			}
		}
		catch (const Pylon::GenericException& e) {
			std::cerr << "Warning: Could not optimize packet size: " << e.GetDescription() << std::endl;
		}

		// Configure auto exposure and gain
		try {
			if (m_camera.GetNodeMap().GetNode("ExposureAuto")) {
				Pylon::CEnumParameter(m_camera.GetNodeMap(), "ExposureAuto").SetValue("Continuous");
				std::cout << "Set exposure to auto continuous" << std::endl;
			}
		}
		catch (const Pylon::GenericException& e) {
			std::cerr << "Warning: Could not set auto exposure: " << e.GetDescription() << std::endl;
		}

		try {
			if (m_camera.GetNodeMap().GetNode("GainAuto")) {
				Pylon::CEnumParameter(m_camera.GetNodeMap(), "GainAuto").SetValue("Continuous");
				std::cout << "Set gain to auto continuous" << std::endl;
			}
		}
		catch (const Pylon::GenericException& e) {
			std::cerr << "Warning: Could not set auto gain: " << e.GetDescription() << std::endl;
		}

		// Debug camera settings before starting grabbing
		DebugCameraSettings();

		// Make sure the thread is not running
		if (m_threadRunning.load() && m_grabThread.joinable())
		{
			m_threadRunning.store(false);
			m_grabThread.join();
		}

		// Start grabbing (continuous mode)
		// Use LatestImageOnly strategy to ensure we always get the latest image
		m_camera.StartGrabbing(Pylon::GrabStrategy_LatestImageOnly, Pylon::GrabLoop_ProvidedByUser);
		std::cout << "Started continuous grabbing" << std::endl;

		// Start the grab thread
		m_threadRunning.store(true);
		m_grabThread = std::thread(&PylonCamera::GrabThreadFunction, this);

		return true;
	}
	catch (const Pylon::GenericException& e)
	{
		std::cerr << "Error starting grabbing: " << e.GetDescription() << std::endl;
		return false;
	}
}

void PylonCamera::StopGrabbing()
{
	try
	{
		// First, if we have a grab thread running, stop it properly
		if (m_threadRunning.load())
		{
			std::cout << "Stopping grab thread..." << std::endl;
			m_threadRunning.store(false);

			// Wait for thread to complete
			if (m_grabThread.joinable())
			{
				m_grabThread.join();
				std::cout << "Grab thread joined successfully" << std::endl;
			}
		}

		// Then stop camera grabbing
		std::lock_guard<std::mutex> lock(m_cameraMutex);
		if (m_camera.IsGrabbing())
		{
			std::cout << "Stopping camera grabbing..." << std::endl;
			m_camera.StopGrabbing();
			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give it time to stop
		}
	}
	catch (const Pylon::GenericException& e)
	{
		std::cerr << "Error stopping grabbing: " << e.GetDescription() << std::endl;
	}
}




bool PylonCamera::DisconnectWithResult()
{
	if (!m_connected)
	{
		return true; // Already disconnected - success
	}

	bool success = true;

	try
	{
		// First, stop grabbing if we're grabbing (BEFORE acquiring the lock)
		// StopGrabbingWithResult() has its own locking for the camera operations
		if (m_threadRunning.load())
		{
			if (!StopGrabbingWithResult()) {
				success = false;
				std::cerr << "Warning: Failed to stop grabbing during disconnect" << std::endl;
			}
		}

		// Now acquire lock for camera close operation
		std::lock_guard<std::mutex> lock(m_cameraMutex);

		// ✅ CRITICAL: Deregister event handler BEFORE closing camera
		if (m_pDeviceRemovalHandler && m_handlerRegistered && m_camera.IsOpen())
		{
			try {
				m_camera.DeregisterConfiguration(m_pDeviceRemovalHandler);
				m_handlerRegistered = false;
				std::cout << "Event handler deregistered during disconnect" << std::endl;
			}
			catch (const Pylon::GenericException& e) {
				std::cerr << "Warning: Failed to deregister handler during disconnect: " << e.GetDescription() << std::endl;
			}
		}

		// Close camera
		if (m_camera.IsOpen())
		{
			m_camera.Close();
		}

		m_connected = false;

		if (success) {
			std::cout << "Camera disconnected successfully" << std::endl;
		}
		else {
			std::cout << "Camera disconnected with warnings" << std::endl;
		}

		return success;
	}
	catch (const Pylon::GenericException& e)
	{
		std::cerr << "Error disconnecting camera: " << e.GetDescription() << std::endl;
		m_connected = false; // Still mark as disconnected even if there was an error
		return false;
	}
}

// Also add a bool-returning version of StopGrabbing if needed:
bool PylonCamera::StopGrabbingWithResult()
{
	bool success = true;

	try
	{
		// First, if we have a grab thread running, stop it properly
		if (m_threadRunning.load())
		{
			std::cout << "Stopping grab thread..." << std::endl;
			m_threadRunning.store(false);

			// Wait for thread to complete
			if (m_grabThread.joinable())
			{
				m_grabThread.join();
				std::cout << "Grab thread joined successfully" << std::endl;
			}
		}

		// Then stop camera grabbing
		std::lock_guard<std::mutex> lock(m_cameraMutex);
		if (m_camera.IsGrabbing())
		{
			std::cout << "Stopping camera grabbing..." << std::endl;
			m_camera.StopGrabbing();
			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give it time to stop
		}

		return success;
	}
	catch (const Pylon::GenericException& e)
	{
		std::cerr << "Error stopping grabbing: " << e.GetDescription() << std::endl;
		return false;
	}
}


std::string PylonCamera::GetDeviceInfo() const
{
	if (!m_connected || !m_camera.IsPylonDeviceAttached())
	{
		return "No camera connected";
	}

	try
	{
		std::string info = "Camera: " + std::string(m_camera.GetDeviceInfo().GetModelName());
		info += ", S/N: " + std::string(m_camera.GetDeviceInfo().GetSerialNumber());
		return info;
	}
	catch (const Pylon::GenericException& e)
	{
		return "Error getting device info: " + std::string(e.GetDescription());
	}
}

bool PylonCamera::IsConnected() const
{
	return m_connected && !m_deviceRemoved && m_camera.IsPylonDeviceAttached();
}

bool PylonCamera::IsGrabbing() const
{
	return m_connected && !m_deviceRemoved && m_camera.IsGrabbing();
}

bool PylonCamera::IsCameraDeviceRemoved() const
{
	return m_deviceRemoved || (m_connected && m_camera.IsCameraDeviceRemoved());
}

void PylonCamera::HandleDeviceRemoval()
{
	std::cout << "Camera device removal detected!" << std::endl;

	// Set the device removed flag
	m_deviceRemoved = true;

	// Stop the grab thread
	if (m_threadRunning.load())
	{
		m_threadRunning.store(false);
		if (m_grabThread.joinable())
		{
			m_grabThread.join();
		}
	}

	// Update the connection state
	m_connected = false;

	// Call the device removal callback if registered
	if (m_deviceRemovalCallback)
	{
		m_deviceRemovalCallback();
	}
}


// REPLACE your existing TryReconnect method with this enhanced version:
bool PylonCamera::TryReconnect() {
	if (m_reconnecting.exchange(true)) {
		return false; // Already trying to reconnect
	}

	bool result = false;

	try {
		std::cout << "Attempting to reconnect to camera..." << std::endl;

		// Destroy the current device since it's no longer valid
		if (m_camera.IsPylonDeviceAttached()) {
			m_camera.DetachDevice();
		}
		m_camera.DestroyDevice();

		// Try different reconnection methods based on what we stored
		if (!m_lastIPAddress.empty()) {
			// Try IP-based reconnection (preferred for GigE cameras)
			std::cout << "Attempting IP reconnection to: " << m_lastIPAddress << std::endl;
			result = ConnectToIP(m_lastIPAddress);
		}
		else if (m_lastDeviceIndex >= 0) {
			// Try index-based reconnection
			std::cout << "Attempting index reconnection to: " << m_lastDeviceIndex << std::endl;
			result = ConnectToIndex(m_lastDeviceIndex);
		}
		else if (!m_lastDeviceSerialNumber.empty()) {
			// Try serial-based reconnection
			std::cout << "Attempting serial reconnection to: " << m_lastDeviceSerialNumber << std::endl;
			result = ConnectToSerial(std::string(m_lastDeviceSerialNumber.c_str()));
		}
		else {
			// Fallback to auto-connect
			std::cout << "Attempting auto reconnection" << std::endl;
			result = Connect();
		}

		if (result) {
			std::cout << "Successfully reconnected to camera" << std::endl;
		}
		else {
			std::cout << "Failed to reconnect to camera" << std::endl;
		}
	}
	catch (const Pylon::GenericException& e) {
		std::cerr << "Error during reconnection attempt: " << e.GetDescription() << std::endl;
	}

	m_reconnecting.store(false);
	return result;
}


void PylonCamera::SetDeviceRemovalCallback(DeviceRemovalCallback callback)
{
	m_deviceRemovalCallback = callback;
}

void PylonCamera::SetNewFrameCallback(NewFrameCallback callback)
{
	m_newFrameCallback = callback;
}

void PylonCamera::GrabThreadFunction()
{
	std::cout << "Grab thread started" << std::endl;
	int frameCounter = 0;

	// Frame rate control variables
	const std::chrono::microseconds frameDuration(1000000 / m_targetFPS);

	while (m_threadRunning.load() && m_camera.IsGrabbing())
	{
		// Record start time for frame rate control
		auto frameStart = std::chrono::steady_clock::now();

		try
		{
			// Retrieve with minimal timeout for responsiveness
			Pylon::CGrabResultPtr grabResult;

			if (m_camera.RetrieveResult(50, grabResult, Pylon::TimeoutHandling_Return) &&
				grabResult && grabResult->GrabSucceeded())
			{
				// Lock only when we have a valid frame
				{
					std::lock_guard<std::mutex> lock(m_cameraMutex);
					m_ptrGrabResult = grabResult;
					frameCounter++;
					m_newFrameReady.store(true);
				}

				// Call the new frame callback if registered, outside the lock
				if (m_newFrameCallback)
				{
					m_newFrameCallback(grabResult);
				}
			}
		}
		catch (const Pylon::GenericException& e)
		{
			std::cerr << "Error in grab thread: " << e.GetDescription() << std::endl;

			// Check if the camera has been removed
			if (m_camera.IsCameraDeviceRemoved())
			{
				HandleDeviceRemoval();
				break;
			}

			// Add a short delay after errors to prevent busy looping
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		// Calculate time spent in this frame
		auto frameEnd = std::chrono::steady_clock::now();
		auto frameTime = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart);

		// Sleep to maintain target frame rate (if we processed faster than target)
		if (frameTime < frameDuration)
		{
			std::this_thread::sleep_for(frameDuration - frameTime);
		}
		else {
			// Still yield to prevent tight CPU loop when we can't meet target framerate
			std::this_thread::yield();
		}
	}

	std::cout << "Grab thread exiting after grabbing " << frameCounter << " frames" << std::endl;
}

void PylonCamera::DebugCameraSettings()
{
	if (!m_connected || !m_camera.IsOpen()) {
		std::cerr << "Cannot debug settings - camera not connected or not open" << std::endl;
		return;
	}

	try {
		std::cout << "\n--- Camera Settings Debug ---" << std::endl;

		// Get camera model info
		std::cout << "Camera Model: " << m_camera.GetDeviceInfo().GetModelName() << std::endl;
		std::cout << "Camera S/N: " << m_camera.GetDeviceInfo().GetSerialNumber() << std::endl;

		// Check acquisition mode
		try {
			if (m_camera.GetNodeMap().GetNode("AcquisitionMode")) {
				Pylon::CEnumParameter acqMode(m_camera.GetNodeMap(), "AcquisitionMode");
				std::cout << "Acquisition Mode: " << acqMode.GetValue() << std::endl;
			}
		}
		catch (...) {
			std::cout << "Acquisition Mode: [Not available]" << std::endl;
		}

		// Check trigger mode
		try {
			if (m_camera.GetNodeMap().GetNode("TriggerMode")) {
				Pylon::CEnumParameter trigMode(m_camera.GetNodeMap(), "TriggerMode");
				std::cout << "Trigger Mode: " << trigMode.GetValue() << std::endl;

				// If trigger is enabled, check trigger source
				if (std::string(trigMode.GetValue()) == "On") {
					try {
						Pylon::CEnumParameter trigSrc(m_camera.GetNodeMap(), "TriggerSource");
						std::cout << "Trigger Source: " << trigSrc.GetValue() << std::endl;
					}
					catch (...) {
						std::cout << "Trigger Source: [Not available]" << std::endl;
					}
				}
			}
		}
		catch (...) {
			std::cout << "Trigger Mode: [Not available]" << std::endl;
		}

		// Check exposure settings
		try {
			if (m_camera.GetNodeMap().GetNode("ExposureAuto")) {
				Pylon::CEnumParameter expAuto(m_camera.GetNodeMap(), "ExposureAuto");
				std::cout << "Exposure Auto: " << expAuto.GetValue() << std::endl;
			}
		}
		catch (...) {
			std::cout << "Exposure Auto: [Not available]" << std::endl;
		}

		try {
			if (m_camera.GetNodeMap().GetNode("ExposureTime")) {
				Pylon::CFloatParameter expTime(m_camera.GetNodeMap(), "ExposureTime");
				std::cout << "Exposure Time: " << expTime.GetValue() << " μs" << std::endl;
			}
		}
		catch (...) {
			std::cout << "Exposure Time: [Not available]" << std::endl;
		}

		// Check gain settings
		try {
			if (m_camera.GetNodeMap().GetNode("GainAuto")) {
				Pylon::CEnumParameter gainAuto(m_camera.GetNodeMap(), "GainAuto");
				std::cout << "Gain Auto: " << gainAuto.GetValue() << std::endl;
			}
		}
		catch (...) {
			std::cout << "Gain Auto: [Not available]" << std::endl;
		}

		try {
			if (m_camera.GetNodeMap().GetNode("Gain")) {
				Pylon::CFloatParameter gain(m_camera.GetNodeMap(), "Gain");
				std::cout << "Gain: " << gain.GetValue() << " dB" << std::endl;
			}
		}
		catch (...) {
			std::cout << "Gain: [Not available]" << std::endl;
		}

		// Check packet size for GigE cameras
		try {
			if (m_camera.GetTLNodeMap().GetNode("GevSCPSPacketSize")) {
				Pylon::CIntegerParameter packetSize(m_camera.GetTLNodeMap(), "GevSCPSPacketSize");
				std::cout << "Packet Size: " << packetSize.GetValue() << " bytes" << std::endl;
			}
		}
		catch (...) {
			std::cout << "Packet Size: [Not available]" << std::endl;
		}

		// Check max buffer count
		std::cout << "Max Buffer Count: " << m_camera.MaxNumBuffer.GetValue() << std::endl;

		std::cout << "--- End Camera Settings ---\n" << std::endl;
	}
	catch (const Pylon::GenericException& e) {
		std::cerr << "Error getting camera settings: " << e.GetDescription() << std::endl;
	}
	catch (const std::exception& e) {
		std::cerr << "Error getting camera settings: " << e.what() << std::endl;
	}
}

// Add these new methods to your existing pylon_camera.cpp file

// Helper method to validate connection (non-const version)
bool PylonCamera::ValidateConnection() const {
	if (!m_connected) {
		m_lastErrorMessage = "Camera is not connected";
		std::cerr << "Camera operation failed: " << m_lastErrorMessage << std::endl;
		return false;
	}

	if (!m_camera.IsOpen()) {
		m_lastErrorMessage = "Camera is not open";
		std::cerr << "Camera operation failed: " << m_lastErrorMessage << std::endl;
		return false;
	}

	return true;
}

// Helper method to validate connection for read operations
bool PylonCamera::ValidateConnectionForRead() const {
	if (!m_connected) {
		//std::cerr << "Camera operation failed: Camera is not connected" << std::endl;
		return false;
	}

	if (!m_camera.IsOpen()) {
		std::cerr << "Camera operation failed: Camera is not open" << std::endl;
		return false;
	}

	return true;
}

// Helper method to set exposure mode
bool PylonCamera::SetExposureMode() {
	try {
		if (m_camera.GetNodeMap().GetNode("ExposureMode")) {
			Pylon::CEnumParameter exposureMode(m_camera.GetNodeMap(), "ExposureMode");
			exposureMode.SetValue("Timed");
			return true;
		}
	}
	catch (const Pylon::GenericException& e) {
		m_lastErrorMessage = std::string("Could not set exposure mode: ") + e.GetDescription();
		std::cerr << m_lastErrorMessage << std::endl;
	}
	return false;
}

// Set exposure time in microseconds
bool PylonCamera::SetExposureTime(double microseconds) {
	if (!ValidateConnection()) return false;

	try {
		// Set exposure mode first
		SetExposureMode();

		// Determine which parameter to use based on detected camera model
		std::string paramName;
		if (m_cameraModel == BaslerCameraModel::ACE2) {
			paramName = "ExposureTime";
		}
		else if (m_cameraModel == BaslerCameraModel::ACE) {
			paramName = "ExposureTimeAbs";
		}
		else {
			// UNKNOWN model - use fallback detection
			if (m_camera.GetNodeMap().GetNode("ExposureTime")) {
				paramName = "ExposureTime";
			}
			else if (m_camera.GetNodeMap().GetNode("ExposureTimeAbs")) {
				paramName = "ExposureTimeAbs";
			}
			else {
				m_lastErrorMessage = "Neither ExposureTime nor ExposureTimeAbs parameter found";
				std::cerr << m_lastErrorMessage << std::endl;
				return false;
			}
		}

		// Set exposure using the determined parameter name
		if (m_camera.GetNodeMap().GetNode(paramName.c_str())) {
			Pylon::CFloatParameter exposureTime(m_camera.GetNodeMap(), paramName.c_str());

			// Get valid range for this camera
			double minExposure = exposureTime.GetMin();
			double maxExposure = exposureTime.GetMax();

			// Clamp to valid range
			double clampedExposure = (std::max)(minExposure, (std::min)(maxExposure, microseconds));

			exposureTime.SetValue(clampedExposure);

			if (std::abs(clampedExposure - microseconds) > 1.0) {
				std::cout << "Warning: Exposure clamped from " << microseconds
					<< " to " << clampedExposure << " us" << std::endl;
			}

			m_lastErrorMessage.clear();
			return true;
		}
		else {
			m_lastErrorMessage = "Parameter " + paramName + " not found on camera";
			std::cerr << m_lastErrorMessage << std::endl;
			return false;
		}
	}
	catch (const Pylon::GenericException& e) {
		m_lastErrorMessage = std::string("Could not set exposure time: ") + e.GetDescription();
		std::cerr << m_lastErrorMessage << std::endl;
		return false;
	}
}

// Set gain value (0-10 scale, mapped to camera's range)
bool PylonCamera::SetGain(double gain) {
	if (!ValidateConnection()) return false;

	try {
		// First set GainSelector based on detected model
		if (m_camera.GetNodeMap().GetNode("GainSelector")) {
			Pylon::CEnumParameter gainSelector(m_camera.GetNodeMap(), "GainSelector");

			if (m_cameraModel == BaslerCameraModel::ACE2) {
				// ace2 uses "All"
				try { gainSelector.SetValue("All"); } catch (...) {}
			}
			else if (m_cameraModel == BaslerCameraModel::ACE) {
				// ace uses "AnalogAll"
				try { gainSelector.SetValue("AnalogAll"); } catch (...) {}
			}
			else {
				// UNKNOWN - try both
				try {
					gainSelector.SetValue("All");
				}
				catch (...) {
					try { gainSelector.SetValue("AnalogAll"); } catch (...) {}
				}
			}
		}

		// Determine which parameter to use based on detected camera model
		bool useFloatGain = false;
		bool useRawGain = false;

		if (m_cameraModel == BaslerCameraModel::ACE2) {
			useFloatGain = true;
		}
		else if (m_cameraModel == BaslerCameraModel::ACE) {
			useRawGain = true;
		}
		else {
			// UNKNOWN model - use fallback detection
			if (m_camera.GetNodeMap().GetNode("Gain")) {
				useFloatGain = true;
			}
			else if (m_camera.GetNodeMap().GetNode("GainRaw")) {
				useRawGain = true;
			}
		}

		if (useFloatGain && m_camera.GetNodeMap().GetNode("Gain")) {
			// ace2/newer cameras: Gain (float, in dB)
			Pylon::CFloatParameter gainParam(m_camera.GetNodeMap(), "Gain");

			// Get valid range for this camera
			double minGain = gainParam.GetMin();
			double maxGain = gainParam.GetMax();

			// Convert our "gain" setting (0-10 range) to camera's dB range
			double targetGain;
			if (gain <= 0) {
				targetGain = minGain;
			}
			else if (gain >= 10) {
				targetGain = maxGain;
			}
			else {
				// Linear interpolation between min and max
				double ratio = gain / 10.0;
				targetGain = minGain + (maxGain - minGain) * ratio;
			}

			gainParam.SetValue(targetGain);
			m_lastErrorMessage.clear();
			return true;
		}
		else if (useRawGain && m_camera.GetNodeMap().GetNode("GainRaw")) {
			// Older ace cameras: GainRaw (integer)
			Pylon::CIntegerParameter gainRaw(m_camera.GetNodeMap(), "GainRaw");

			// Get valid range for this camera
			int64_t minGain = gainRaw.GetMin();
			int64_t maxGain = gainRaw.GetMax();

			// Convert our "gain" setting (0-10 range) to camera's raw range
			int64_t rawGain;
			if (gain <= 0) {
				rawGain = minGain;
			}
			else if (gain >= 10) {
				rawGain = maxGain;
			}
			else {
				// Linear interpolation between min and max
				double ratio = gain / 10.0;
				rawGain = static_cast<int64_t>(minGain + (maxGain - minGain) * ratio);
			}

			gainRaw.SetValue(rawGain);
			m_lastErrorMessage.clear();
			return true;
		}
		else {
			m_lastErrorMessage = "Neither Gain nor GainRaw parameter found";
			std::cerr << m_lastErrorMessage << std::endl;
			return false;
		}
	}
	catch (const Pylon::GenericException& e) {
		m_lastErrorMessage = std::string("Could not set gain: ") + e.GetDescription();
		std::cerr << m_lastErrorMessage << std::endl;
		return false;
	}
}

// Enable/disable auto exposure
bool PylonCamera::SetExposureAuto(bool enabled) {
	if (!ValidateConnection()) return false;

	try {
		if (m_camera.GetNodeMap().GetNode("ExposureAuto")) {
			Pylon::CEnumParameter exposureAuto(m_camera.GetNodeMap(), "ExposureAuto");
			exposureAuto.SetValue(enabled ? "Continuous" : "Off");
			m_lastErrorMessage.clear();
			return true;
		}
		else {
			m_lastErrorMessage = "ExposureAuto parameter not found";
			std::cerr << m_lastErrorMessage << std::endl;
			return false;
		}
	}
	catch (const Pylon::GenericException& e) {
		m_lastErrorMessage = std::string("Could not set exposure auto: ") + e.GetDescription();
		std::cerr << m_lastErrorMessage << std::endl;
		return false;
	}
}

// Enable/disable auto gain
bool PylonCamera::SetGainAuto(bool enabled) {
	if (!ValidateConnection()) return false;

	try {
		if (m_camera.GetNodeMap().GetNode("GainAuto")) {
			Pylon::CEnumParameter gainAuto(m_camera.GetNodeMap(), "GainAuto");
			gainAuto.SetValue(enabled ? "Continuous" : "Off");
			m_lastErrorMessage.clear();
			return true;
		}
		else {
			m_lastErrorMessage = "GainAuto parameter not found";
			std::cerr << m_lastErrorMessage << std::endl;
			return false;
		}
	}
	catch (const Pylon::GenericException& e) {
		m_lastErrorMessage = std::string("Could not set gain auto: ") + e.GetDescription();
		std::cerr << m_lastErrorMessage << std::endl;
		return false;
	}
}

// Get current exposure time in microseconds
double PylonCamera::GetExposureTime() const {
	if (!ValidateConnectionForRead()) return -1.0;

	try {
		// Need to cast away const to access GetNodeMap() - this is safe for read operations
		auto& camera = const_cast<Pylon::CInstantCamera&>(m_camera);

		// Determine which parameter to use based on detected camera model
		std::string paramName;
		if (m_cameraModel == BaslerCameraModel::ACE2) {
			paramName = "ExposureTime";
		}
		else if (m_cameraModel == BaslerCameraModel::ACE) {
			paramName = "ExposureTimeAbs";
		}
		else {
			// UNKNOWN model - use fallback detection
			if (camera.GetNodeMap().GetNode("ExposureTime")) {
				paramName = "ExposureTime";
			}
			else if (camera.GetNodeMap().GetNode("ExposureTimeAbs")) {
				paramName = "ExposureTimeAbs";
			}
			else {
				return -1.0;
			}
		}

		if (camera.GetNodeMap().GetNode(paramName.c_str())) {
			Pylon::CFloatParameter exposureTime(camera.GetNodeMap(), paramName.c_str());
			if (exposureTime.IsReadable()) {
				return exposureTime.GetValue();
			}
		}
	}
	catch (const Pylon::GenericException& e) {
		std::cerr << "Could not read exposure time: " << e.GetDescription() << std::endl;
	}
	return -1.0;
}

// Get current gain value (0-10 scale)
double PylonCamera::GetGain() const {
	if (!ValidateConnectionForRead()) return -1.0;

	try {
		// Need to cast away const to access GetNodeMap() - this is safe for read operations
		auto& camera = const_cast<Pylon::CInstantCamera&>(m_camera);

		// Determine which parameter to use based on detected camera model
		bool useFloatGain = false;
		bool useRawGain = false;

		if (m_cameraModel == BaslerCameraModel::ACE2) {
			useFloatGain = true;
		}
		else if (m_cameraModel == BaslerCameraModel::ACE) {
			useRawGain = true;
		}
		else {
			// UNKNOWN model - use fallback detection
			if (camera.GetNodeMap().GetNode("Gain")) {
				useFloatGain = true;
			}
			else if (camera.GetNodeMap().GetNode("GainRaw")) {
				useRawGain = true;
			}
		}

		if (useFloatGain && camera.GetNodeMap().GetNode("Gain")) {
			// ace2/newer cameras: Gain (float, in dB)
			Pylon::CFloatParameter gainParam(camera.GetNodeMap(), "Gain");
			if (gainParam.IsReadable()) {
				// Get the value and convert back to 0-10 scale
				double gainValue = gainParam.GetValue();
				double minGain = gainParam.GetMin();
				double maxGain = gainParam.GetMax();

				// Convert back to 0-10 scale
				if (maxGain > minGain) {
					double ratio = (gainValue - minGain) / (maxGain - minGain);
					return ratio * 10.0;
				}
			}
		}
		else if (useRawGain && camera.GetNodeMap().GetNode("GainRaw")) {
			// Older ace cameras: GainRaw (integer)
			Pylon::CIntegerParameter gainRaw(camera.GetNodeMap(), "GainRaw");
			if (gainRaw.IsReadable()) {
				// Get the raw value and convert back to 0-10 scale
				int64_t rawGain = gainRaw.GetValue();
				int64_t minGain = gainRaw.GetMin();
				int64_t maxGain = gainRaw.GetMax();

				// Convert back to 0-10 scale
				if (maxGain > minGain) {
					double ratio = static_cast<double>(rawGain - minGain) / (maxGain - minGain);
					return ratio * 10.0;
				}
			}
		}
	}
	catch (const Pylon::GenericException& e) {
		std::cerr << "Could not read gain: " << e.GetDescription() << std::endl;
	}
	return -1.0;
}

// Check if auto exposure is enabled
// Check if auto exposure is enabled
bool PylonCamera::IsExposureAuto() const {
	if (!ValidateConnectionForRead()) return false;

	try {
		// Need to cast away const to access GetNodeMap() - this is safe for read operations
		auto& camera = const_cast<Pylon::CInstantCamera&>(m_camera);

		if (camera.GetNodeMap().GetNode("ExposureAuto")) {
			Pylon::CEnumParameter exposureAuto(camera.GetNodeMap(), "ExposureAuto");
			if (exposureAuto.IsReadable()) {
				// Fix: Convert Pylon::String_t to std::string using .c_str()
				std::string autoMode = std::string(exposureAuto.GetValue().c_str());
				return (autoMode == "Continuous" || autoMode == "Once");
			}
		}
	}
	catch (const Pylon::GenericException& e) {
		std::cerr << "Could not read exposure auto: " << e.GetDescription() << std::endl;
	}
	return false;
}
// Check if auto gain is enabled
bool PylonCamera::IsGainAuto() const {
	if (!ValidateConnectionForRead()) return false;

	try {
		// Need to cast away const to access GetNodeMap() - this is safe for read operations
		auto& camera = const_cast<Pylon::CInstantCamera&>(m_camera);

		if (camera.GetNodeMap().GetNode("GainAuto")) {
			Pylon::CEnumParameter gainAuto(camera.GetNodeMap(), "GainAuto");
			if (gainAuto.IsReadable()) {
				// Fix: Convert Pylon::String_t to std::string using .c_str()
				std::string autoMode = std::string(gainAuto.GetValue().c_str());
				return (autoMode == "Continuous" || autoMode == "Once");
			}
		}
	}
	catch (const Pylon::GenericException& e) {
		std::cerr << "Could not read gain auto: " << e.GetDescription() << std::endl;
	}
	return false;
}
// Apply multiple settings at once (more efficient)
// ✅ Adds 500ms settle delay after each exposure-related change for camera stability
bool PylonCamera::ApplyExposureSettings(const ExposureSettings& settings) {
	if (!ValidateConnection()) return false;

	// Load settle delay from config (cached after first call would be better, but simple for now)
	static const int SETTLE_DELAY_MS = loadCameraSettleDelay();

	std::cout << "Applying camera settings:" << std::endl;
	std::cout << "  Target Exposure: " << settings.exposure_time << " us" << std::endl;
	std::cout << "  Target Gain: " << settings.gain << " (0-10 scale)" << std::endl;
	std::cout << "  Exposure Auto: " << (settings.exposure_auto ? "On" : "Off") << std::endl;
	std::cout << "  Gain Auto: " << (settings.gain_auto ? "On" : "Off") << std::endl;
	std::cout << "  Target Frame Rate: " << settings.frame_rate << " fps" << std::endl;

	bool success = true;

	// Read current camera state to detect what actually needs changing
	bool wasExposureAuto = IsExposureAuto();
	bool wasGainAuto = IsGainAuto();
	double wasExposureTime = GetExposureTime();
	double wasGain = GetGain();

	std::cout << "  Current Exposure Auto: " << (wasExposureAuto ? "On" : "Off") << std::endl;
	std::cout << "  Current Gain Auto: " << (wasGainAuto ? "On" : "Off") << std::endl;
	std::cout << "  Current Exposure: " << wasExposureTime << " us" << std::endl;
	std::cout << "  Current Gain: " << wasGain << std::endl;

	// Fast path: skip entirely if all settings already match
	bool exposureAutoMatch = (wasExposureAuto == settings.exposure_auto);
	bool gainAutoMatch = (wasGainAuto == settings.gain_auto);
	bool exposureMatch = settings.exposure_auto || (std::abs(wasExposureTime - settings.exposure_time) < 1.0);
	bool gainMatch = settings.gain_auto || (std::abs(wasGain - settings.gain) < 0.01);

	if (exposureAutoMatch && gainAutoMatch && exposureMatch && gainMatch) {
		std::cout << "  [INFO] All camera settings unchanged - skipping (saved ~"
		          << (SETTLE_DELAY_MS * 2) << "ms)" << std::endl;
		return true;
	}

	// Set exposure mode first
	if (!SetExposureMode()) {
		success = false;
	}

	// Set auto-exposure mode — only wait for settle if it actually changed
	if (!SetExposureAuto(settings.exposure_auto)) {
		success = false;
	} else if (!exposureAutoMatch) {
		std::cout << "  [INFO] Exposure Auto changed: " << (wasExposureAuto ? "On" : "Off")
		          << " -> " << (settings.exposure_auto ? "On" : "Off") << std::endl;
		std::cout << "  [INFO] Waiting " << SETTLE_DELAY_MS << "ms after exposure auto change..." << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(SETTLE_DELAY_MS));
	}

	// Set auto-gain mode
	if (!SetGainAuto(settings.gain_auto)) {
		success = false;
	}

	// Set manual exposure value — only wait for settle if value actually changed
	if (!settings.exposure_auto) {
		if (!SetExposureTime(settings.exposure_time)) {
			success = false;
		} else if (!exposureMatch) {
			std::cout << "  [INFO] Exposure changed: " << wasExposureTime << " -> "
			          << settings.exposure_time << " us" << std::endl;
			std::cout << "  [INFO] Waiting " << SETTLE_DELAY_MS << "ms after exposure time change..." << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds(SETTLE_DELAY_MS));
		}
	}

	// Set manual gain value (only if auto is disabled)
	if (!settings.gain_auto) {
		if (!SetGain(settings.gain)) {
			success = false;
		}
	}

	// Set frame rate (0 means unlimited/max)
	if (settings.frame_rate > 0) {
		if (!SetFrameRate(settings.frame_rate)) {
			std::cout << "  [WARNING] Failed to set frame rate (camera may not support it)" << std::endl;
			// Don't fail the whole operation for frame rate
		}
	}

	if (success) {
		std::cout << "Camera settings applied successfully" << std::endl;
	}
	else {
		std::cout << "Some camera settings failed to apply" << std::endl;
	}

	return success;
}

// Get current settings as a structure
PylonCamera::ExposureSettings PylonCamera::GetCurrentExposureSettings() const {
	ExposureSettings settings;

	settings.exposure_time = GetExposureTime();
	settings.gain = GetGain();
	settings.exposure_auto = IsExposureAuto();
	settings.gain_auto = IsGainAuto();
	settings.frame_rate = GetFrameRate();

	return settings;
}

// Set frame rate (fps)
bool PylonCamera::SetFrameRate(double fps) {
	if (!ValidateConnection()) return false;

	try {
		std::lock_guard<std::mutex> lock(m_cameraMutex);

		// Try to enable frame rate control first
		if (GenApi::IsWritable(m_camera.GetNodeMap().GetNode("AcquisitionFrameRateEnable"))) {
			GenApi::CBooleanPtr pFrameRateEnable(m_camera.GetNodeMap().GetNode("AcquisitionFrameRateEnable"));
			if (pFrameRateEnable) {
				pFrameRateEnable->SetValue(true);
			}
		}

		// Try AcquisitionFrameRate first (ace2 cameras)
		GenApi::CFloatPtr pFrameRate(m_camera.GetNodeMap().GetNode("AcquisitionFrameRate"));
		if (pFrameRate && GenApi::IsWritable(pFrameRate)) {
			double minFps = pFrameRate->GetMin();
			double maxFps = pFrameRate->GetMax();
			double clampedFps = std::max(minFps, std::min(fps, maxFps));
			pFrameRate->SetValue(clampedFps);
			std::cout << "Frame rate set to: " << clampedFps << " fps (requested: " << fps << ", range: " << minFps << "-" << maxFps << ")" << std::endl;
			return true;
		}

		// Try AcquisitionFrameRateAbs (older ace cameras)
		GenApi::CFloatPtr pFrameRateAbs(m_camera.GetNodeMap().GetNode("AcquisitionFrameRateAbs"));
		if (pFrameRateAbs && GenApi::IsWritable(pFrameRateAbs)) {
			double minFps = pFrameRateAbs->GetMin();
			double maxFps = pFrameRateAbs->GetMax();
			double clampedFps = std::max(minFps, std::min(fps, maxFps));
			pFrameRateAbs->SetValue(clampedFps);
			std::cout << "Frame rate (Abs) set to: " << clampedFps << " fps (requested: " << fps << ", range: " << minFps << "-" << maxFps << ")" << std::endl;
			return true;
		}

		std::cerr << "Camera does not support frame rate control" << std::endl;
		return false;
	}
	catch (const Pylon::GenericException& e) {
		std::cerr << "Error setting frame rate: " << e.GetDescription() << std::endl;
		return false;
	}
}

// Get current frame rate (fps)
double PylonCamera::GetFrameRate() const {
	if (!ValidateConnectionForRead()) return 30.0;  // Default value

	try {
		// Cast away const for lock (common pattern in Pylon code)
		PylonCamera* self = const_cast<PylonCamera*>(this);
		std::lock_guard<std::mutex> lock(self->m_cameraMutex);

		// Try AcquisitionFrameRate first (ace2 cameras)
		GenApi::CFloatPtr pFrameRate(self->m_camera.GetNodeMap().GetNode("AcquisitionFrameRate"));
		if (pFrameRate && GenApi::IsReadable(pFrameRate)) {
			return pFrameRate->GetValue();
		}

		// Try AcquisitionFrameRateAbs (older ace cameras)
		GenApi::CFloatPtr pFrameRateAbs(self->m_camera.GetNodeMap().GetNode("AcquisitionFrameRateAbs"));
		if (pFrameRateAbs && GenApi::IsReadable(pFrameRateAbs)) {
			return pFrameRateAbs->GetValue();
		}

		// Try ResultingFrameRate (read-only, actual achieved rate)
		GenApi::CFloatPtr pResultingRate(self->m_camera.GetNodeMap().GetNode("ResultingFrameRate"));
		if (pResultingRate && GenApi::IsReadable(pResultingRate)) {
			return pResultingRate->GetValue();
		}

		return 30.0;  // Default if not readable
	}
	catch (const Pylon::GenericException& e) {
		std::cerr << "Error getting frame rate: " << e.GetDescription() << std::endl;
		return 30.0;
	}
}


// NEW: Connect to camera by IP address (GigE cameras)
bool PylonCamera::ConnectToIP(const std::string& ipAddress) {
	if (!m_initialized) {
		if (!Initialize()) {
			return false;
		}
	}

	// Already connected - check if it's the same camera
	if (m_connected) {
		std::string currentIP = GetCurrentCameraIP();
		if (currentIP == ipAddress) {
			std::cout << "Already connected to camera at IP: " << ipAddress << std::endl;
			return true;
		}
		// Disconnect from current camera to connect to different one
		Disconnect();
	}

	try {
		std::lock_guard<std::mutex> lock(m_cameraMutex);

		std::cout << "Attempting to connect to camera at IP: " << ipAddress << std::endl;

		// Find device by IP address
		Pylon::CDeviceInfo deviceInfo;
		if (!FindDeviceByIP(ipAddress, deviceInfo)) {
			std::cerr << "Camera with IP " << ipAddress << " not found" << std::endl;
			return false;
		}

		// Create device from the found camera
		m_camera.Attach(Pylon::CTlFactory::GetInstance().CreateDevice(deviceInfo));

		// Store device info for reconnection
		m_lastDeviceSerialNumber = m_camera.GetDeviceInfo().GetSerialNumber();
		m_lastDeviceClass = m_camera.GetDeviceInfo().GetDeviceClass();
		m_lastIPAddress = ipAddress;  // Store IP for reconnection
		m_lastDeviceIndex = -1;       // Clear device index

		// Register the device removal handler (only if not already registered)
		if (!m_handlerRegistered) {
			m_camera.RegisterConfiguration(m_pDeviceRemovalHandler, Pylon::RegistrationMode_Append, Pylon::Cleanup_None);
			m_handlerRegistered = true;
		}

		// Open the camera
		m_camera.Open();

		// Configure camera settings
		ConfigureCamera();

		m_connected = true;
		m_deviceRemoved = false;

		std::cout << "Successfully connected to camera: " << m_camera.GetDeviceInfo().GetModelName()
			<< " at IP: " << ipAddress << std::endl;
		std::cout << "Serial Number: " << m_lastDeviceSerialNumber << std::endl;

		// Detect camera model (ace vs ace2) for correct parameter usage
		DetectCameraModel();

		return true;
	}
	catch (const Pylon::GenericException& e) {
		std::cerr << "Error connecting to camera at IP " << ipAddress << ": " << e.GetDescription() << std::endl;
		return false;
	}
}

// NEW: Connect to camera by device index
bool PylonCamera::ConnectToIndex(int deviceIndex) {
	if (!m_initialized) {
		if (!Initialize()) {
			return false;
		}
	}

	// Already connected - check if it's the same camera
	if (m_connected) {
		if (m_lastDeviceIndex == deviceIndex) {
			std::cout << "Already connected to camera at index: " << deviceIndex << std::endl;
			return true;
		}
		// Disconnect from current camera to connect to different one
		Disconnect();
	}

	try {
		std::lock_guard<std::mutex> lock(m_cameraMutex);

		std::cout << "Attempting to connect to camera at index: " << deviceIndex << std::endl;

		// Find device by index
		Pylon::CDeviceInfo deviceInfo;
		if (!FindDeviceByIndex(deviceIndex, deviceInfo)) {
			std::cerr << "Camera at index " << deviceIndex << " not found" << std::endl;
			return false;
		}

		// Create device from the found camera
		m_camera.Attach(Pylon::CTlFactory::GetInstance().CreateDevice(deviceInfo));

		// Store device info for reconnection
		m_lastDeviceSerialNumber = m_camera.GetDeviceInfo().GetSerialNumber();
		m_lastDeviceClass = m_camera.GetDeviceInfo().GetDeviceClass();
		m_lastDeviceIndex = deviceIndex;  // Store index for reconnection
		m_lastIPAddress = "";             // Clear IP address

		// Register the device removal handler (only if not already registered)
		if (!m_handlerRegistered) {
			m_camera.RegisterConfiguration(m_pDeviceRemovalHandler, Pylon::RegistrationMode_Append, Pylon::Cleanup_None);
			m_handlerRegistered = true;
		}

		// Open the camera
		m_camera.Open();

		// Configure camera settings
		ConfigureCamera();

		m_connected = true;
		m_deviceRemoved = false;

		std::cout << "Successfully connected to camera: " << m_camera.GetDeviceInfo().GetModelName()
			<< " at index: " << deviceIndex << std::endl;
		std::cout << "Serial Number: " << m_lastDeviceSerialNumber << std::endl;

		// Detect camera model (ace vs ace2) for correct parameter usage
		DetectCameraModel();

		return true;
	}
	catch (const Pylon::GenericException& e) {
		std::cerr << "Error connecting to camera at index " << deviceIndex << ": " << e.GetDescription() << std::endl;
		return false;
	}
}


// QUICK FIX: Update your GetNetworkInfo method to use Device NodeMap instead of TL NodeMap

std::string PylonCamera::GetNetworkInfo() const {
	if (!m_connected || !m_camera.IsOpen()) {
		return "Not connected";
	}

	try {
		std::string networkInfo;

		// FIXED: Use Device NodeMap instead of TL NodeMap (based on successful IP detection)
		auto& camera = const_cast<Pylon::CInstantCamera&>(m_camera);

		// Try Device NodeMap first (this is where we found the IP address)
		if (camera.GetNodeMap().GetNode("GevCurrentIPAddress")) {
			// Get IP address
			Pylon::CIntegerParameter ipParam(camera.GetNodeMap(), "GevCurrentIPAddress");
			if (ipParam.IsReadable()) {
				int64_t ipValue = ipParam.GetValue();

				// Convert to dotted decimal notation
				int a = (ipValue >> 24) & 0xFF;
				int b = (ipValue >> 16) & 0xFF;
				int c = (ipValue >> 8) & 0xFF;
				int d = ipValue & 0xFF;

				networkInfo = std::to_string(a) + "." + std::to_string(b) + "." +
					std::to_string(c) + "." + std::to_string(d);
			}

			// Try to get packet size from TL NodeMap
			if (camera.GetTLNodeMap().GetNode("GevSCPSPacketSize")) {
				Pylon::CIntegerParameter packetSizeParam(camera.GetTLNodeMap(), "GevSCPSPacketSize");
				if (packetSizeParam.IsReadable()) {
					networkInfo += " (Packet:" + std::to_string(packetSizeParam.GetValue()) + ")";
				}
			}

			// If we got network info, it's definitely a GigE camera
			if (!networkInfo.empty()) {
				return networkInfo;
			}
		}

		// Fallback: Try TL NodeMap
		if (camera.GetTLNodeMap().GetNode("GevCurrentIPAddress")) {
			Pylon::CIntegerParameter ipParam(camera.GetTLNodeMap(), "GevCurrentIPAddress");
			if (ipParam.IsReadable()) {
				int64_t ipValue = ipParam.GetValue();
				int a = (ipValue >> 24) & 0xFF;
				int b = (ipValue >> 16) & 0xFF;
				int c = (ipValue >> 8) & 0xFF;
				int d = ipValue & 0xFF;

				networkInfo = std::to_string(a) + "." + std::to_string(b) + "." +
					std::to_string(c) + "." + std::to_string(d);

				if (camera.GetTLNodeMap().GetNode("GevSCPSPacketSize")) {
					Pylon::CIntegerParameter packetSizeParam(camera.GetTLNodeMap(), "GevSCPSPacketSize");
					if (packetSizeParam.IsReadable()) {
						networkInfo += " (Packet:" + std::to_string(packetSizeParam.GetValue()) + ")";
					}
				}
				return networkInfo;
			}
		}

		// If we couldn't get IP info but we know it's connected via IP, show that
		if (!m_lastIPAddress.empty()) {
			return m_lastIPAddress + " (Connected)";
		}

		return "GigE camera (no network details)";
	}
	catch (const Pylon::GenericException& e) {
		return "Error: " + std::string(e.GetDescription());
	}
}

// Find device by IP address - uses CDeviceInfo directly without opening camera
bool PylonCamera::FindDeviceByIP(const std::string& ipAddress, Pylon::CDeviceInfo& deviceInfo) {
	try {
		Pylon::CTlFactory& tlFactory = Pylon::CTlFactory::GetInstance();
		Pylon::DeviceInfoList_t devices;

		// Enumerate all devices
		if (tlFactory.EnumerateDevices(devices) == 0) {
			std::cout << "No cameras found during IP search" << std::endl;
			return false;
		}

		std::cout << "=== IP SEARCH ===" << std::endl;
		std::cout << "Searching among " << devices.size() << " cameras for IP: " << ipAddress << std::endl;

		// Search through all devices for matching IP
		for (size_t i = 0; i < devices.size(); i++) {
			const auto& device = devices[i];

			try {
				std::cout << "\n--- Camera " << i << " ---" << std::endl;
				std::cout << "Model: " << device.GetModelName() << std::endl;
				std::cout << "Serial: " << device.GetSerialNumber() << std::endl;
				std::cout << "Class: " << device.GetDeviceClass() << std::endl;

				// Check if this is a GigE camera
				std::string deviceClass = std::string(device.GetDeviceClass().c_str());
				if (deviceClass.find("GigE") != std::string::npos || deviceClass.find("Gige") != std::string::npos) {
					std::cout << "This is a GigE camera, checking IP..." << std::endl;

					// METHOD 1: Get IP directly from CDeviceInfo (preferred - no camera open needed)
					std::string cameraIP = "";
					try {
						// For GigE cameras, the IP address is available in the device info
						Pylon::String_t ipStr = device.GetIpAddress();
						if (!ipStr.empty()) {
							cameraIP = std::string(ipStr.c_str());
							std::cout << "IP from DeviceInfo.GetIpAddress(): " << cameraIP << std::endl;
						}
					}
					catch (...) {
						std::cout << "GetIpAddress() not available" << std::endl;
					}

					// METHOD 2: Try GetPropertyValue for IP address
					if (cameraIP.empty()) {
						try {
							Pylon::String_t value;
							if (device.GetPropertyValue("IpAddress", value)) {
								cameraIP = std::string(value.c_str());
								std::cout << "IP from PropertyValue 'IpAddress': " << cameraIP << std::endl;
							}
						}
						catch (...) {
							// Property not available
						}
					}

					// METHOD 3: Try full device ID which often contains the IP
					if (cameraIP.empty()) {
						try {
							std::string fullName = std::string(device.GetFullName().c_str());
							std::cout << "FullName: " << fullName << std::endl;

							// Parse IP from full name - format is often like "Basler acA1920-40gm#00305337A40F#169.254.4.4:3956"
							// Look for IP pattern in the string
							size_t hashPos = fullName.rfind('#');
							if (hashPos != std::string::npos) {
								std::string afterHash = fullName.substr(hashPos + 1);
								size_t colonPos = afterHash.find(':');
								if (colonPos != std::string::npos) {
									cameraIP = afterHash.substr(0, colonPos);
									std::cout << "IP extracted from FullName: " << cameraIP << std::endl;
								}
							}
						}
						catch (...) {
							std::cout << "Could not parse IP from FullName" << std::endl;
						}
					}

					// METHOD 4: Check Address property
					if (cameraIP.empty()) {
						try {
							Pylon::String_t addr = device.GetAddress();
							if (!addr.empty()) {
								std::string addrStr = std::string(addr.c_str());
								std::cout << "Address: " << addrStr << std::endl;
								// Address might be in format "169.254.4.4:3956"
								size_t colonPos = addrStr.find(':');
								if (colonPos != std::string::npos) {
									cameraIP = addrStr.substr(0, colonPos);
								} else {
									cameraIP = addrStr;
								}
								std::cout << "IP from Address: " << cameraIP << std::endl;
							}
						}
						catch (...) {
							// Address not available
						}
					}

					// Check if we found the IP and if it matches
					if (!cameraIP.empty()) {
						std::cout << "Camera IP: " << cameraIP << std::endl;
						std::cout << "Looking for: " << ipAddress << std::endl;

						if (cameraIP == ipAddress) {
							std::cout << "IP MATCH FOUND!" << std::endl;
							deviceInfo = device;
							return true;
						}
						else {
							std::cout << "IP does not match" << std::endl;
						}
					}
					else {
						std::cout << "Could not detect IP address for this camera" << std::endl;
					}
				}
				else {
					std::cout << "Skipping non-GigE camera" << std::endl;
				}
			}
			catch (const Pylon::GenericException& e) {
				std::cout << "Error processing camera " << i << ": " << e.GetDescription() << std::endl;
				continue;
			}
		}

		std::cout << "\n=== SEARCH COMPLETED ===" << std::endl;
		std::cout << "No camera found with IP: " << ipAddress << std::endl;
		return false;
	}
	catch (const Pylon::GenericException& e) {
		std::cerr << "Error during IP search: " << e.GetDescription() << std::endl;
		return false;
	}
}


// NEW: Find device by index
bool PylonCamera::FindDeviceByIndex(int deviceIndex, Pylon::CDeviceInfo& deviceInfo) {
	try {
		Pylon::CTlFactory& tlFactory = Pylon::CTlFactory::GetInstance();
		Pylon::DeviceInfoList_t devices;

		// Enumerate all devices
		if (tlFactory.EnumerateDevices(devices) == 0) {
			std::cout << "No cameras found during index search" << std::endl;
			return false;
		}

		if (deviceIndex < 0 || deviceIndex >= static_cast<int>(devices.size())) {
			std::cout << "Device index " << deviceIndex << " out of range (0-"
				<< (devices.size() - 1) << ")" << std::endl;
			return false;
		}

		deviceInfo = devices[deviceIndex];
		std::cout << "Found camera at index " << deviceIndex << ": "
			<< deviceInfo.GetModelName() << " (Serial: " << deviceInfo.GetSerialNumber() << ")" << std::endl;
		return true;
	}
	catch (const Pylon::GenericException& e) {
		std::cerr << "Error during index search: " << e.GetDescription() << std::endl;
		return false;
	}
}


// FIX 4: ConfigureGigECamera method - use non-const reference 
void PylonCamera::ConfigureGigECamera() {
	try {
		std::cout << "Configuring GigE camera settings..." << std::endl;

		// Set heartbeat timeout for faster device removal detection
		if (m_camera.GetTLNodeMap().GetNode("HeartbeatTimeout")) {
			Pylon::CIntegerParameter heartbeat(m_camera.GetTLNodeMap(), "HeartbeatTimeout");
			heartbeat.TrySetValue(1000, Pylon::IntegerValueCorrection_Nearest);  // 1000ms
			std::cout << "  Set heartbeat timeout to 1000ms" << std::endl;
		}

		// Optimize GigE packet size
		if (m_camera.GetTLNodeMap().GetNode("GevSCPSPacketSize")) {
			Pylon::CIntegerParameter packetSize(m_camera.GetTLNodeMap(), "GevSCPSPacketSize");
			// Get the maximum allowed packet size
			int64_t maxPacketSize = packetSize.GetMax();
			// Set to maximum value for optimal performance
			packetSize.SetValue(maxPacketSize);
			std::cout << "  Set GigE packet size to maximum: " << packetSize.GetValue() << std::endl;
		}

		// Configure inter-packet delay if needed (helps with multiple cameras)
		if (m_camera.GetTLNodeMap().GetNode("GevSCPD")) {
			Pylon::CIntegerParameter interPacketDelay(m_camera.GetTLNodeMap(), "GevSCPD");
			// Set a small delay to prevent network congestion with multiple cameras
			interPacketDelay.TrySetValue(1000, Pylon::IntegerValueCorrection_Nearest);
			std::cout << "  Set inter-packet delay: " << interPacketDelay.GetValue() << std::endl;
		}
	}
	catch (const Pylon::GenericException& e) {
		std::cout << "Warning: Could not configure some GigE settings: " << e.GetDescription() << std::endl;
	}
}
// NEW: Configure general camera settings
void PylonCamera::ConfigureCamera() {
	try {
		// Set acquisition parameters
		m_camera.MaxNumBuffer = 5;

		// Configure trigger mode
		if (m_camera.GetNodeMap().GetNode("TriggerMode")) {
			Pylon::CEnumParameter(m_camera.GetNodeMap(), "TriggerMode").SetValue("Off");
			std::cout << "  Disabled trigger mode" << std::endl;
		}

		// If this is a GigE camera, apply GigE-specific configuration
		if (m_camera.GetDeviceInfo().GetDeviceClass() == "BaslerGigE") {
			ConfigureGigECamera();
		}
	}
	catch (const Pylon::GenericException& e) {
		std::cout << "Warning: Could not configure some camera settings: " << e.GetDescription() << std::endl;
	}
}


// FIX 2: GetCurrentCameraIP method - handle const correctly
std::string PylonCamera::GetCurrentCameraIP() const {
	if (!m_connected || !m_camera.IsOpen()) {
		return "";
	}

	try {
		// FIXED: Cast away const for GetTLNodeMap() - this is safe for read operations
		auto& camera = const_cast<Pylon::CInstantCamera&>(m_camera);

		if (camera.GetTLNodeMap().GetNode("GevCurrentIPAddress")) {
			Pylon::CIntegerParameter ipParam(camera.GetTLNodeMap(), "GevCurrentIPAddress");
			if (ipParam.IsReadable()) {
				int64_t ipValue = ipParam.GetValue();

				// Convert to dotted decimal notation
				int a = (ipValue >> 24) & 0xFF;
				int b = (ipValue >> 16) & 0xFF;
				int c = (ipValue >> 8) & 0xFF;
				int d = ipValue & 0xFF;

				return std::to_string(a) + "." + std::to_string(b) + "." +
					std::to_string(c) + "." + std::to_string(d);
			}
		}
	}
	catch (...) {
		// Ignore errors
	}

	return "";
}




// ADD to pylon_camera.cpp:
void PylonCamera::DebugAllCameras() {
	std::cout << "\n=== DEBUG ALL DETECTED CAMERAS ===" << std::endl;

	try {
		Pylon::CTlFactory& tlFactory = Pylon::CTlFactory::GetInstance();
		Pylon::DeviceInfoList_t devices;

		if (tlFactory.EnumerateDevices(devices) == 0) {
			std::cout << "No cameras detected at all!" << std::endl;
			return;
		}

		std::cout << "Total cameras detected: " << devices.size() << std::endl;

		for (size_t i = 0; i < devices.size(); i++) {
			const auto& device = devices[i];

			std::cout << "\n=== Camera " << i << " ===" << std::endl;
			std::cout << "Model: " << device.GetModelName() << std::endl;
			std::cout << "Serial: " << device.GetSerialNumber() << std::endl;
			std::cout << "Vendor: " << device.GetVendorName() << std::endl;
			std::cout << "Class: " << device.GetDeviceClass() << std::endl;
			std::cout << "User ID: " << device.GetUserDefinedName() << std::endl;

			// Try to connect and get IP for GigE cameras
			try {
				std::string deviceClass = std::string(device.GetDeviceClass().c_str());
				if (deviceClass.find("GigE") != std::string::npos || deviceClass.find("Gige") != std::string::npos) {
					Pylon::CInstantCamera tempCamera;
					tempCamera.Attach(tlFactory.CreateDevice(device));
					tempCamera.Open();

					if (tempCamera.GetTLNodeMap().GetNode("GevCurrentIPAddress")) {
						Pylon::CIntegerParameter ipParam(tempCamera.GetTLNodeMap(), "GevCurrentIPAddress");
						if (ipParam.IsReadable()) {
							int64_t ipValue = ipParam.GetValue();
							int a = (ipValue >> 24) & 0xFF;
							int b = (ipValue >> 16) & 0xFF;
							int c = (ipValue >> 8) & 0xFF;
							int d = ipValue & 0xFF;

							std::string cameraIP = std::to_string(a) + "." + std::to_string(b) + "." +
								std::to_string(c) + "." + std::to_string(d);
							std::cout << "IP Address: " << cameraIP << std::endl;
						}
					}
					tempCamera.Close();
				}
			}
			catch (...) {
				std::cout << "Could not read IP address" << std::endl;
			}
		}

		std::cout << "\n=== END DEBUG ===" << std::endl;
	}
	catch (const Pylon::GenericException& e) {
		std::cout << "Debug error: " << e.GetDescription() << std::endl;
	}
}


// Detect camera model from model name prefix
void PylonCamera::DetectCameraModel() {
	if (!m_connected || !m_camera.IsOpen()) {
		m_cameraModel = BaslerCameraModel::UNKNOWN;
		return;
	}

	try {
		std::string modelName = std::string(m_camera.GetDeviceInfo().GetModelName().c_str());
		std::cout << "Detecting camera model from: " << modelName << std::endl;

		// Check model name prefix to determine ace vs ace2
		// ace cameras: acA prefix (e.g., acA1300-60gm, acA1920-40gc)
		// ace2 cameras: a2A prefix (e.g., a2A1920-51gcPRO, a2A2590-22gcPRO)
		if (modelName.length() >= 3) {
			std::string prefix = modelName.substr(0, 3);

			if (prefix == "a2A" || prefix == "a2a") {
				m_cameraModel = BaslerCameraModel::ACE2;
				std::cout << "Detected camera model: ACE2 (uses ExposureTime, Gain)" << std::endl;
			}
			else if (prefix == "acA" || prefix == "aca") {
				m_cameraModel = BaslerCameraModel::ACE;
				std::cout << "Detected camera model: ACE (uses ExposureTimeAbs, GainRaw)" << std::endl;
			}
			else {
				// Unknown prefix - will use try/fallback approach
				m_cameraModel = BaslerCameraModel::UNKNOWN;
				std::cout << "Unknown camera model prefix: " << prefix << " (will use fallback detection)" << std::endl;
			}
		}
		else {
			m_cameraModel = BaslerCameraModel::UNKNOWN;
			std::cout << "Model name too short to detect: " << modelName << std::endl;
		}
	}
	catch (const Pylon::GenericException& e) {
		std::cerr << "Error detecting camera model: " << e.GetDescription() << std::endl;
		m_cameraModel = BaslerCameraModel::UNKNOWN;
	}
}

// Get camera model as string for logging
std::string PylonCamera::GetCameraModelString() const {
	switch (m_cameraModel) {
	case BaslerCameraModel::ACE:
		return "ACE (legacy parameters)";
	case BaslerCameraModel::ACE2:
		return "ACE2 (SFNC parameters)";
	case BaslerCameraModel::UNKNOWN:
	default:
		return "UNKNOWN (fallback mode)";
	}
}

// Add to pylon_camera.cpp:
bool PylonCamera::ConnectToIPWithFallback(const std::string& ipAddress, const std::string& fallbackSerial) {
	std::cout << "Attempting IP connection with fallback to serial..." << std::endl;

	// First try IP connection
	if (ConnectToIP(ipAddress)) {
		std::cout << "✓ IP connection successful" << std::endl;
		return true;
	}

	std::cout << "⚠ IP connection failed, trying fallback..." << std::endl;

	// If IP fails and we have a fallback serial, try that
	if (!fallbackSerial.empty()) {
		std::cout << "Trying connection by serial: " << fallbackSerial << std::endl;
		if (ConnectToSerial(fallbackSerial)) {
			std::cout << "✓ Serial connection successful" << std::endl;
			return true;
		}
	}

	// Final fallback: try auto-connection
	std::cout << "Trying auto-connection as final fallback..." << std::endl;
	if (Connect()) {
		std::cout << "✓ Auto-connection successful" << std::endl;
		return true;
	}

	std::cout << "❌ All connection methods failed" << std::endl;
	return false;
}



bool PylonCamera::GetLatestFrameData(uint8_t*& imageData, uint32_t& width, uint32_t& height, bool& newFrame) {
	std::lock_guard<std::mutex> lock(m_frameDataMutex);

	// Initialize output parameters
	imageData = nullptr;
	width = 0;
	height = 0;
	newFrame = false;

	if (!m_connected || !IsGrabbing()) {
		return false;
	}

	// Check if we have a valid grab result from the grabbing thread
	{
		std::lock_guard<std::mutex> cameraLock(m_cameraMutex);

		if (!m_ptrGrabResult || !m_ptrGrabResult->GrabSucceeded()) {
			return false;
		}

		// Check if this is a new frame
		newFrame = m_newFrameReady.load();

		try {
			// Get frame dimensions
			width = m_ptrGrabResult->GetWidth();
			height = m_ptrGrabResult->GetHeight();

			// Convert the grab result to RGB format
			// Release previous images
			if (m_latestPylonImage.IsValid()) {
				m_latestPylonImage.Release();
			}
			if (m_latestConvertedImage.IsValid()) {
				m_latestConvertedImage.Release();
			}

			// Attach grab result to pylon image
			m_latestPylonImage.AttachGrabResultBuffer(m_ptrGrabResult);

			// Convert to RGB format for easier use
			m_formatConverter.Convert(m_latestConvertedImage, m_latestPylonImage);

			// Get pointer to the converted image data
			imageData = static_cast<uint8_t*>(m_latestConvertedImage.GetBuffer());

			m_hasLatestFrame.store(true);

			// Mark that we've consumed this frame
			if (newFrame) {
				m_newFrameReady.store(false);
			}

			return (imageData != nullptr && width > 0 && height > 0);
		}
		catch (const Pylon::GenericException& e) {
			std::cerr << "Error converting frame data: " << e.GetDescription() << std::endl;
			return false;
		}
	}
}