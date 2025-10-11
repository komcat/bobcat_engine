#include "ids_camera_test.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <vector>
#include <thread>

IDSCameraTest::IDSCameraTest()
  : m_cameraHandle(0), m_isConnected(false), m_isGrabbing(false), m_cameraId(0),
  m_pImageMemory(nullptr), m_imageMemoryId(0), m_imageWidth(0), m_imageHeight(0),
  m_imageBitsPerPixel(0), m_hasNewFrame(false), m_threadRunning(false), m_targetFPS(30) {
}

IDSCameraTest::~IDSCameraTest() {
  Disconnect();
}

bool IDSCameraTest::ConnectById(int cameraId) {
  if (m_isConnected.load()) {
    SetError("Camera already connected. Disconnect first.");
    return false;
  }

  m_cameraId = cameraId;
  HIDS hCam = (HIDS)cameraId;

  LogStatus("Attempting to connect to camera ID: " + std::to_string(cameraId));

  // Initialize camera
  INT result = is_InitCamera(&hCam, nullptr);

  if (result == IS_SUCCESS) {
    m_cameraHandle = hCam;
    m_isConnected.store(true);
    LogStatus("Successfully connected to camera ID: " + std::to_string(cameraId));

    // Setup image memory after successful connection
    if (SetupImageMemory()) {
      LogStatus("Image memory setup successful");
    }
    else {
      LogStatus("Warning: Image memory setup failed");
    }

    return true;
  }
  else {
    std::string errorMsg = "Failed to connect to camera ID " + std::to_string(cameraId) +
      " (Error code: " + std::to_string(result) + ")";
    SetError(errorMsg);
    return false;
  }
}

bool IDSCameraTest::ConnectBySerial(const std::string& serialNumber) {
  if (m_isConnected.load()) {
    SetError("Camera already connected. Disconnect first.");
    return false;
  }

  LogStatus("Attempting to connect to camera with serial: " + serialNumber);

  // Get camera list to find the camera with matching serial
  INT numCameras = 0;
  is_GetNumberOfCameras(&numCameras);

  if (numCameras == 0) {
    SetError("No cameras found");
    return false;
  }

  // Create camera list
  UEYE_CAMERA_LIST* cameraList = (UEYE_CAMERA_LIST*)malloc(sizeof(DWORD) + numCameras * sizeof(UEYE_CAMERA_INFO));
  cameraList->dwCount = numCameras;

  if (is_GetCameraList(cameraList) == IS_SUCCESS) {
    for (DWORD i = 0; i < cameraList->dwCount; i++) {
      std::string camSerial(cameraList->uci[i].SerNo);
      if (camSerial == serialNumber) {
        free(cameraList);
        return ConnectById(cameraList->uci[i].dwCameraID);
      }
    }
  }

  free(cameraList);
  SetError("Camera with serial " + serialNumber + " not found");
  return false;
}

void IDSCameraTest::Disconnect() {
  if (m_isConnected.load() && m_cameraHandle != 0) {
    LogStatus("Disconnecting camera...");

    // Stop grabbing if active
    if (m_isGrabbing.load()) {
      StopGrabbing();
    }

    // Free image memory
    FreeImageMemory();

    // Exit camera
    is_ExitCamera(m_cameraHandle);
    m_cameraHandle = 0;
    m_isConnected.store(false);
    m_cameraId = 0;
    LogStatus("Camera disconnected");
  }
}

bool IDSCameraTest::CaptureImage() {
  if (!m_isConnected.load()) {
    SetError("Camera not connected");
    return false;
  }

  std::lock_guard<std::mutex> lock(m_imageMutex);

  if (!m_pImageMemory) {
    SetError("Image memory not allocated");
    return false;
  }

  LogStatus("Capturing single image...");

  // Freeze video (capture single frame)
  INT result = is_FreezeVideo(m_cameraHandle, IS_WAIT);

  if (result == IS_SUCCESS) {
    m_hasNewFrame.store(true);
    LogStatus("Image captured successfully");
    return true;
  }
  else {
    SetError("Failed to capture image (Error code: " + std::to_string(result) + ")");
    return false;
  }
}

bool IDSCameraTest::CaptureImageToDisk(const std::string& filename) {
  if (!CaptureImage()) {
    return false;
  }

  std::string saveFilename = filename.empty() ? GenerateTimestampFilename() : filename;

  LogStatus("Saving image to: " + saveFilename);

  // Use is_ImageFile to save image - more widely supported
  IMAGE_FILE_PARAMS imageFileParams;
  imageFileParams.pwchFileName = nullptr;
  imageFileParams.nFileType = IS_IMG_BMP;  // Save as BMP
  imageFileParams.ppcImageMem = nullptr;
  imageFileParams.pnImageID = nullptr;
  imageFileParams.nQuality = 0;

  // Convert filename to wide string for Windows
  std::wstring wideFilename(saveFilename.begin(), saveFilename.end());
  imageFileParams.pwchFileName = const_cast<wchar_t*>(wideFilename.c_str());

  INT result = is_ImageFile(m_cameraHandle, IS_IMAGE_FILE_CMD_SAVE,
    (void*)&imageFileParams, sizeof(imageFileParams));

  if (result == IS_SUCCESS) {
    LogStatus("Image saved successfully to: " + saveFilename);
    return true;
  }
  else {
    // If is_ImageFile fails, try alternative method using memory copy
    LogStatus("Primary save method failed, trying alternative...");
    return SaveImageAlternativeMethod(saveFilename);
  }
}

bool IDSCameraTest::StartGrabbing() {
  if (!m_isConnected.load()) {
    SetError("Camera not connected");
    return false;
  }

  if (m_isGrabbing.load()) {
    LogStatus("Already grabbing");
    return true;
  }

  std::lock_guard<std::mutex> lock(m_imageMutex);

  if (!m_pImageMemory) {
    SetError("Image memory not allocated");
    return false;
  }

  LogStatus("Starting continuous grabbing...");

  // Start live video capture
  INT result = is_CaptureVideo(m_cameraHandle, IS_DONT_WAIT);

  if (result == IS_SUCCESS) {
    m_isGrabbing.store(true);
    m_threadRunning.store(true);

    // Start grabbing thread
    m_grabThread = std::thread(&IDSCameraTest::GrabThreadFunction, this);

    LogStatus("Started grabbing successfully");
    return true;
  }
  else {
    SetError("Failed to start grabbing (Error code: " + std::to_string(result) + ")");
    return false;
  }
}

bool IDSCameraTest::StopGrabbing() {
  if (!m_isConnected.load()) {
    SetError("Camera not connected");
    return false;
  }

  if (!m_isGrabbing.load()) {
    LogStatus("Not currently grabbing");
    return true;
  }

  LogStatus("Stopping grabbing...");

  // Signal thread to stop
  m_threadRunning.store(false);

  // Wait for thread to finish
  if (m_grabThread.joinable()) {
    m_grabThread.join();
  }

  // Stop live video capture
  INT result = is_StopLiveVideo(m_cameraHandle, IS_WAIT);

  if (result == IS_SUCCESS) {
    m_isGrabbing.store(false);
    LogStatus("Stopped grabbing successfully");
    return true;
  }
  else {
    m_isGrabbing.store(false); // Set to false anyway
    SetError("Failed to stop grabbing (Error code: " + std::to_string(result) + ")");
    return false;
  }
}

void IDSCameraTest::GrabThreadFunction() {
  std::cout << "[INFO] IDS grab thread started" << std::endl;
  int frameCounter = 0;

  // Frame rate control variables
  const std::chrono::microseconds frameDuration(1000000 / m_targetFPS);

  while (m_threadRunning.load() && m_isConnected.load()) {
    auto frameStart = std::chrono::steady_clock::now();

    try {
      // For IDS cameras in live mode, we check if video has started and new frames are available
      BOOL videoStarted = FALSE;
      INT result = is_HasVideoStarted(m_cameraHandle, &videoStarted);

      if (result == IS_SUCCESS && videoStarted) {
        // Video is running, signal that we have frames available
        m_hasNewFrame.store(true);
        frameCounter++;

        // Debug logging every 30 frames
        if ((frameCounter % 30) == 0) {
          LogStatus("Grabbed " + std::to_string(frameCounter) + " frames");
        }
      }
      else if (result != IS_SUCCESS) {
        // Error checking video status
        std::cout << "[WARN] Video status check error: " << result << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      // If video hasn't started or no new frame, just continue the loop
    }
    catch (const std::exception& e) {
      std::cout << "[ERROR] Error in IDS grab thread: " << e.what() << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Frame rate control
    auto frameEnd = std::chrono::steady_clock::now();
    auto frameTime = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart);

    if (frameTime < frameDuration) {
      std::this_thread::sleep_for(frameDuration - frameTime);
    }
    else {
      std::this_thread::yield();
    }
  }

  std::cout << "[INFO] IDS grab thread exiting after grabbing " << frameCounter << " frames" << std::endl;
}

const char* IDSCameraTest::GetImageData() const {
  std::lock_guard<std::mutex> lock(m_imageMutex);
  return m_pImageMemory;
}

std::string IDSCameraTest::GetCameraInfo() const {
  if (!m_isConnected) {
    return "No camera connected";
  }

  CAMINFO cameraInfo;
  if (is_GetCameraInfo(m_cameraHandle, &cameraInfo) == IS_SUCCESS) {
    std::stringstream info;
    info << "Camera ID: " << m_cameraId << "\n";
    info << "Serial: " << cameraInfo.SerNo << "\n";
    info << "ID: " << cameraInfo.ID << "\n";
    info << "Version: " << cameraInfo.Version << "\n";
    info << "Date: " << cameraInfo.Date << "\n";
    info << "Type: " << cameraInfo.Type << "\n";
    info << "Image Size: " << m_imageWidth << "x" << m_imageHeight << "\n";
    info << "Bits Per Pixel: " << m_imageBitsPerPixel;
    return info.str();
  }

  return "Failed to get camera info";
}

std::vector<int> IDSCameraTest::GetAvailableCameraIds() {
  std::vector<int> cameraIds;

  INT numCameras = 0;
  is_GetNumberOfCameras(&numCameras);

  if (numCameras > 0) {
    UEYE_CAMERA_LIST* cameraList = (UEYE_CAMERA_LIST*)malloc(sizeof(DWORD) + numCameras * sizeof(UEYE_CAMERA_INFO));
    cameraList->dwCount = numCameras;

    if (is_GetCameraList(cameraList) == IS_SUCCESS) {
      for (DWORD i = 0; i < cameraList->dwCount; i++) {
        cameraIds.push_back(cameraList->uci[i].dwCameraID);
      }
    }

    free(cameraList);
  }

  return cameraIds;
}

std::vector<std::string> IDSCameraTest::GetAvailableCameraSerials() {
  std::vector<std::string> serials;

  INT numCameras = 0;
  is_GetNumberOfCameras(&numCameras);

  if (numCameras > 0) {
    UEYE_CAMERA_LIST* cameraList = (UEYE_CAMERA_LIST*)malloc(sizeof(DWORD) + numCameras * sizeof(UEYE_CAMERA_INFO));
    cameraList->dwCount = numCameras;

    if (is_GetCameraList(cameraList) == IS_SUCCESS) {
      for (DWORD i = 0; i < cameraList->dwCount; i++) {
        serials.push_back(std::string(cameraList->uci[i].SerNo));
      }
    }

    free(cameraList);
  }

  return serials;
}

bool IDSCameraTest::SetupImageMemory() {
  if (!m_isConnected) {
    return false;
  }

  // Free existing memory if any
  FreeImageMemory();

  // Get image size
  IS_SIZE_2D imageSize;
  INT result = is_AOI(m_cameraHandle, IS_AOI_IMAGE_GET_SIZE, (void*)&imageSize, sizeof(imageSize));

  if (result != IS_SUCCESS) {
    SetError("Failed to get image size");
    return false;
  }

  m_imageWidth = imageSize.s32Width;
  m_imageHeight = imageSize.s32Height;

  // Set color mode (assuming 8-bit mono for simplicity)
  result = is_SetColorMode(m_cameraHandle, IS_CM_MONO8);
  if (result != IS_SUCCESS) {
    LogStatus("Warning: Could not set mono8 mode, using default");
  }

  m_imageBitsPerPixel = 8; // Mono8

  // Allocate image memory
  result = is_AllocImageMem(m_cameraHandle, m_imageWidth, m_imageHeight,
    m_imageBitsPerPixel, &m_pImageMemory, &m_imageMemoryId);

  if (result != IS_SUCCESS) {
    SetError("Failed to allocate image memory");
    return false;
  }

  // Set image memory as active
  result = is_SetImageMem(m_cameraHandle, m_pImageMemory, m_imageMemoryId);

  if (result != IS_SUCCESS) {
    FreeImageMemory();
    SetError("Failed to set image memory");
    return false;
  }

  return true;
}

void IDSCameraTest::FreeImageMemory() {
  if (m_pImageMemory && m_cameraHandle) {
    is_FreeImageMem(m_cameraHandle, m_pImageMemory, m_imageMemoryId);
  }

  m_pImageMemory = nullptr;
  m_imageMemoryId = 0;
  m_imageWidth = 0;
  m_imageHeight = 0;
  m_imageBitsPerPixel = 0;
  m_hasNewFrame = false;
}

std::string IDSCameraTest::GenerateTimestampFilename(const std::string& extension) const {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);

  std::stringstream ss;
  std::tm timeinfo;
	localtime_s(&timeinfo, &time_t);
  ss << "ids_capture_" << std::put_time(&timeinfo, "%Y%m%d_%H%M%S") << extension;

  return ss.str();
}

bool IDSCameraTest::SaveImageAlternativeMethod(const std::string& filename) {
  if (!m_pImageMemory || m_imageWidth <= 0 || m_imageHeight <= 0) {
    SetError("No valid image data to save");
    return false;
  }

  // Simple BMP file creation
  try {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
      SetError("Failed to create file: " + filename);
      return false;
    }

    // Calculate image size and padding
    int bytesPerPixel = m_imageBitsPerPixel / 8;
    int rowSize = m_imageWidth * bytesPerPixel;
    int paddedRowSize = (rowSize + 3) & ~3; // Align to 4 bytes
    int imageSize = paddedRowSize * m_imageHeight;
    int fileSize = 54 + imageSize; // BMP header is 54 bytes

    // BMP Header (14 bytes)
    file.write("BM", 2);
    file.write(reinterpret_cast<const char*>(&fileSize), 4);
    int reserved = 0;
    file.write(reinterpret_cast<const char*>(&reserved), 4);
    int dataOffset = 54;
    file.write(reinterpret_cast<const char*>(&dataOffset), 4);

    // DIB Header (40 bytes)
    int dibHeaderSize = 40;
    file.write(reinterpret_cast<const char*>(&dibHeaderSize), 4);
    file.write(reinterpret_cast<const char*>(&m_imageWidth), 4);
    file.write(reinterpret_cast<const char*>(&m_imageHeight), 4);
    short planes = 1;
    file.write(reinterpret_cast<const char*>(&planes), 2);
    file.write(reinterpret_cast<const char*>(&m_imageBitsPerPixel), 2);
    int compression = 0;
    file.write(reinterpret_cast<const char*>(&compression), 4);
    file.write(reinterpret_cast<const char*>(&imageSize), 4);
    int xPixelsPerMeter = 2835; // 72 DPI
    int yPixelsPerMeter = 2835;
    file.write(reinterpret_cast<const char*>(&xPixelsPerMeter), 4);
    file.write(reinterpret_cast<const char*>(&yPixelsPerMeter), 4);
    int colorsUsed = 0;
    int colorsImportant = 0;
    file.write(reinterpret_cast<const char*>(&colorsUsed), 4);
    file.write(reinterpret_cast<const char*>(&colorsImportant), 4);

    // Write image data (BMP format is bottom-up, so write rows in reverse)
    std::vector<char> padding(paddedRowSize - rowSize, 0);
    for (int y = m_imageHeight - 1; y >= 0; y--) {
      const char* rowData = m_pImageMemory + (y * rowSize);
      file.write(rowData, rowSize);
      if (padding.size() > 0) {
        file.write(padding.data(), padding.size());
      }
    }

    file.close();
    LogStatus("Image saved using alternative method: " + filename);
    return true;
  }
  catch (const std::exception& e) {
    SetError("Failed to save image: " + std::string(e.what()));
    return false;
  }
}

void IDSCameraTest::LogStatus(const std::string& message) {
  if (m_statusCallback) {
    m_statusCallback(message);
  }
}

void IDSCameraTest::SetError(const std::string& error) {
  m_lastError = error;
  LogStatus("ERROR: " + error);
}