// GdiBeamStreamController.cpp
#include "GdiBeamStreamController.h"

#include "customserver/BeamServerClient.h"

#include <QSettings>

#include <chrono>
#include <fstream>
#include <iostream>

// Menu IDs are shared with GdiCameraWindow.h
#include "GdiCameraWindow.h"

GdiBeamStreamController::GdiBeamStreamController(BeamStreamOverlay* overlay)
  : m_overlay(overlay)
{
}

GdiBeamStreamController::~GdiBeamStreamController()
{
  Shutdown();
}

void GdiBeamStreamController::Shutdown()
{
  StopStreaming();
}

HMENU GdiBeamStreamController::BuildMenu(HMENU menuBar)
{
  m_menu = CreatePopupMenu();
  if (!m_menu) return nullptr;

  AppendMenuA(m_menu, MF_STRING, IDM_BEAM_STREAM_ENABLE, "Enable Beam Stream");
  AppendMenuA(m_menu, MF_SEPARATOR, 0, nullptr);
  // Camera selection items populated dynamically by UpdateMenu().

  if (menuBar) {
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)m_menu, L"Beam Stream");
  }
  return m_menu;
}

void GdiBeamStreamController::OnMenuOpening(const std::vector<std::string>& cameraIds)
{
  UpdateMenu(cameraIds);
}

void GdiBeamStreamController::UpdateMenu(const std::vector<std::string>& cameraIds)
{
  if (!m_menu) return;

  // Remove old camera items (keep first 2: enable + separator)
  int count = GetMenuItemCount(m_menu);
  while (count > 2) {
    DeleteMenu(m_menu, count - 1, MF_BYPOSITION);
    count--;
  }

  // Update enable checkbox
  CheckMenuItem(m_menu, IDM_BEAM_STREAM_ENABLE,
                m_streamEnabled ? MF_CHECKED : MF_UNCHECKED);

  // Get current beam stream camera
  std::string streamCamera;
  {
    std::lock_guard<std::mutex> lock(m_streamCameraMutex);
    streamCamera = m_streamCameraId;
  }

  if (cameraIds.empty()) {
    AppendMenuW(m_menu, MF_STRING | MF_GRAYED, 0, L"(No cameras available)");
  } else {
    for (size_t i = 0; i < cameraIds.size() && i < 50; ++i) {
      std::wstring wCameraId(cameraIds[i].begin(), cameraIds[i].end());
      UINT flags = MF_STRING;
      if (cameraIds[i] == streamCamera) {
        flags |= MF_CHECKED;
      }
      AppendMenuW(m_menu, flags,
                  IDM_BEAM_STREAM_CAMERA_BASE + static_cast<UINT>(i),
                  wCameraId.c_str());
    }
  }

  if (m_hwnd) DrawMenuBar(m_hwnd);
}

bool GdiBeamStreamController::HandleMenuCommand(WORD commandId,
                                                const std::vector<std::string>& cameraIds)
{
  if (commandId == IDM_BEAM_STREAM_ENABLE) {
    if (m_streamEnabled) {
      StopStreaming();
    } else {
      // Auto-select first camera if none selected
      {
        std::lock_guard<std::mutex> lock(m_streamCameraMutex);
        if (m_streamCameraId.empty() && !cameraIds.empty()) {
          m_streamCameraId = cameraIds.front();
        }
      }
      StartStreaming();
    }
    UpdateMenu(cameraIds);
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    return true;
  }

  if (commandId >= IDM_BEAM_STREAM_CAMERA_BASE && commandId < IDM_BEAM_STREAM_CAMERA_BASE + 50) {
    size_t index = commandId - IDM_BEAM_STREAM_CAMERA_BASE;
    if (index < cameraIds.size()) {
      {
        std::lock_guard<std::mutex> lock(m_streamCameraMutex);
        m_streamCameraId = cameraIds[index];
      }
      std::cout << "[GdiBeamStream] camera set to: " << cameraIds[index] << std::endl;
      UpdateMenu(cameraIds);
    }
    return true;
  }

  return false;
}

bool GdiBeamStreamController::IsStreamCamera(const std::string& cameraId) const
{
  std::lock_guard<std::mutex> lock(m_streamCameraMutex);
  return !m_streamCameraId.empty() && m_streamCameraId == cameraId;
}

void GdiBeamStreamController::EnqueueFrame(const std::vector<uint8_t>& rgbData,
                                           uint32_t width, uint32_t height,
                                           const std::string& cameraId)
{
  if (!m_streamEnabled.load() || !m_threadRunning.load()) return;

  std::string streamCamId;
  {
    std::lock_guard<std::mutex> lock(m_streamCameraMutex);
    streamCamId = m_streamCameraId;
  }
  if (cameraId != streamCamId) return;

  std::lock_guard<std::mutex> lock(m_frameQueueMutex);
  while (m_frameQueue.size() >= 2) {
    m_frameQueue.pop();
  }
  m_frameQueue.push(rgbData);
  m_queuedWidth  = width;
  m_queuedHeight = height;
  m_frameQueueCV.notify_one();
}

void GdiBeamStreamController::SetCurrentPrimaryFrameSize(uint32_t width, uint32_t height)
{
  m_currentPrimaryWidth.store(width);
  m_currentPrimaryHeight.store(height);
}

void GdiBeamStreamController::LoadConfig()
{
  // Primary: read from QSettings (Application Settings page)
  QSettings settings("Fabrinet", "BobcatEngine");
  if (settings.contains("BeamServer/Host")) {
    m_config.host = settings.value("BeamServer/Host", "127.0.0.1").toString().toStdString();
    m_config.port = settings.value("BeamServer/StreamPort", 5567).toInt();
    m_config.umPerPxX = settings.value("BeamServer/UmPerPxX", "0.25").toString().toDouble();
    m_config.umPerPxY = settings.value("BeamServer/UmPerPxY", "0.25").toString().toDouble();
    m_config.rotate180 = settings.value("BeamServer/Rotate180", "false").toString() == "true";
    m_config.intervalMs = settings.value("BeamServer/StreamIntervalMs", 100).toInt();
    std::cout << "[GdiBeamStream] Loaded beam config from Application Settings"
              << " (host=" << m_config.host
              << " port=" << m_config.port
              << " interval=" << m_config.intervalMs << "ms)" << std::endl;
    return;
  }

  // Fallback: read from beam_stream.json
  std::vector<std::string> configPaths = {
    "config/beam_stream.json",
    "./config/beam_stream.json",
    "../config/beam_stream.json"
  };

  for (const auto& path : configPaths) {
    std::ifstream file(path);
    if (file.is_open()) {
      try {
        nlohmann::json cfg;
        file >> cfg;
        m_config.host = cfg.value("host", "127.0.0.1");
        m_config.port = cfg.value("port", 5567);
        m_config.umPerPxX = cfg.value("um_per_px_x", 0.25);
        m_config.umPerPxY = cfg.value("um_per_px_y", 0.25);
        m_config.rotate180 = cfg.value("rotate_180", false);
        m_config.intervalMs = cfg.value("interval_ms", 100);
        std::cout << "[GdiBeamStream] Loaded beam_stream.json from " << path
                  << " (host=" << m_config.host
                  << " port=" << m_config.port
                  << " interval=" << m_config.intervalMs << "ms)" << std::endl;
        return;
      } catch (const nlohmann::json::exception& e) {
        std::cout << "[GdiBeamStream] Error parsing beam_stream.json: " << e.what() << std::endl;
      }
    }
  }
  std::cout << "[GdiBeamStream] beam_stream.json not found, using defaults" << std::endl;
}

void GdiBeamStreamController::StartStreaming()
{
  if (m_threadRunning) return;

  // Join previous thread if it ended on its own (e.g. connection lost)
  if (m_thread.joinable()) {
    m_thread.join();
  }

  LoadConfig();

  // Create dedicated stream client (separate from any command client)
  m_streamClient = std::make_unique<BeamServerClient>();

  std::cout << "[GdiBeamStream] Connecting to stream server at "
            << m_config.host << ":" << m_config.port << std::endl;
  if (!m_streamClient->Connect(m_config.host, m_config.port)) {
    std::cout << "[GdiBeamStream] Failed to connect to stream server: "
              << m_streamClient->GetLastError() << std::endl;
    m_streamClient.reset();
    return;
  }

  const uint32_t width  = m_currentPrimaryWidth.load();
  const uint32_t height = m_currentPrimaryHeight.load();

  if (width == 0 || height == 0) {
    std::cout << "[GdiBeamStream] Cannot start: no frame data yet" << std::endl;
    m_streamClient.reset();
    return;
  }

  // Send STREAM_START
  auto response = m_streamClient->SendStreamStart(
      static_cast<int>(width), static_cast<int>(height), "mono8",
      m_config.rotate180,
      m_config.umPerPxX, m_config.umPerPxY,
      m_config.intervalMs);

  if (response.empty() || response.contains("error")) {
    std::string err = response.value("error", m_streamClient->GetLastError());
    std::cout << "[GdiBeamStream] STREAM_START failed: " << err << std::endl;
    m_streamClient.reset();
    return;
  }

  m_threadRunning = true;
  m_streamEnabled = true;
  if (m_overlay) m_overlay->SetEnabled(true);

  m_thread = std::thread(&GdiBeamStreamController::ThreadLoop, this);
  std::cout << "[GdiBeamStream] Beam streaming started" << std::endl;
}

void GdiBeamStreamController::StopStreaming()
{
  m_threadRunning = false;
  m_frameQueueCV.notify_all();

  if (m_thread.joinable()) {
    m_thread.join();
  }

  // Send STREAM_STOP and disconnect dedicated stream client
  if (m_streamClient && m_streamClient->IsConnected()) {
    auto response = m_streamClient->SendStreamStop();
    if (response.contains("total_frames")) {
      std::cout << "[GdiBeamStream] Stream stopped. Total frames: "
                << response["total_frames"] << " avg_process_ms: "
                << response.value("avg_process_ms", 0.0) << std::endl;
    }
  }
  m_streamClient.reset();

  // Clear queue
  {
    std::lock_guard<std::mutex> lock(m_frameQueueMutex);
    while (!m_frameQueue.empty()) {
      m_frameQueue.pop();
    }
  }

  m_streamEnabled = false;
  if (m_overlay) {
    m_overlay->SetEnabled(false);
    m_overlay->Clear();
  }

  std::cout << "[GdiBeamStream] Beam streaming stopped" << std::endl;
}

void GdiBeamStreamController::ThreadLoop()
{
  std::cout << "[GdiBeamStream] thread started (ID: "
            << std::this_thread::get_id() << ")" << std::endl;

  static constexpr int MAX_STREAM_ITERATIONS = 10000000;
  uint32_t seqNum = 0;
  auto lastFrameTime = std::chrono::steady_clock::now();
  int frameCount = 0;

  for (int i = 0; i < MAX_STREAM_ITERATIONS && m_threadRunning; ++i) {
    std::vector<uint8_t> rgbData;
    uint32_t frameWidth = 0;
    uint32_t frameHeight = 0;

    // Wait for a frame
    {
      std::unique_lock<std::mutex> lock(m_frameQueueMutex);
      m_frameQueueCV.wait_for(lock,
          std::chrono::milliseconds(m_config.intervalMs),
          [this] { return !m_frameQueue.empty() || !m_threadRunning; });

      if (!m_threadRunning) break;
      if (m_frameQueue.empty()) continue;

      rgbData = std::move(m_frameQueue.front());
      m_frameQueue.pop();
      frameWidth = m_queuedWidth;
      frameHeight = m_queuedHeight;
    }

    if (rgbData.empty() || frameWidth == 0 || frameHeight == 0) continue;

    // Convert RGB to grayscale mono8
    size_t pixelCount = static_cast<size_t>(frameWidth) * frameHeight;
    std::vector<uint8_t> grayData(pixelCount);
    const uint8_t* src = rgbData.data();
    for (size_t j = 0; j < pixelCount; ++j) {
      size_t idx = j * 3;
      grayData[j] = static_cast<uint8_t>(
          (static_cast<int>(src[idx]) + static_cast<int>(src[idx + 1]) + static_cast<int>(src[idx + 2])) / 3);
    }

    // Send frame
    auto response = m_streamClient->SendStreamFrame(grayData, seqNum++);

    if (response.empty()) {
      std::cout << "[GdiBeamStream] Stream frame send failed: "
                << m_streamClient->GetLastError() << std::endl;
      // Connection lost - stop streaming
      m_threadRunning = false;
      break;
    }

    // Calculate FPS
    frameCount++;
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - lastFrameTime).count();
    if (elapsed >= 1.0) {
      double fps = frameCount / elapsed;
      if (m_overlay) m_overlay->SetFps(fps);
      frameCount = 0;
      lastFrameTime = now;
    }

    // Update overlay with result
    if (m_overlay) m_overlay->SetResult(response);

    // Trigger repaint
    if (m_hwnd) {
      PostMessage(m_hwnd, WM_BEAM_STREAM_RESULT_READY, 0, 0);
    }
  }

  std::cout << "[GdiBeamStream] thread loop exiting" << std::endl;
}
