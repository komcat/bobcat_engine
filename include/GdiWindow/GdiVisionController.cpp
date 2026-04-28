// GdiVisionController.cpp
#include "GdiVisionController.h"

#ifdef HAS_OPENCV

#include "vision/VisionManager.h"
#include "vision/VisionROI.h"

#include <opencv2/imgproc.hpp>
#include <nlohmann/json.hpp>

#include <QString>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>

// Menu IDs are shared with GdiCameraWindow.h
#include "GdiCameraWindow.h"

GdiVisionController::GdiVisionController(VisionResultOverlay* overlay)
  : m_overlay(overlay)
{
}

GdiVisionController::~GdiVisionController()
{
  Shutdown();
}

void GdiVisionController::Shutdown()
{
  StopProcessing();
}

HMENU GdiVisionController::BuildMenu(HMENU menuBar)
{
  m_menu = CreatePopupMenu();
  if (!m_menu) return nullptr;

  AppendMenuW(m_menu, MF_STRING, IDM_VISION_ENABLE, L"Enable Continuous Vision");
  AppendMenuW(m_menu, MF_STRING, IDM_VISION_REFRESH, L"Refresh Profile List");
  AppendMenuW(m_menu, MF_SEPARATOR, 0, nullptr);
  // Profile items are populated dynamically by UpdateMenu().

  if (menuBar) {
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)m_menu, L"Vision");
  }

  DiscoverProfiles();
  return m_menu;
}

void GdiVisionController::OnMenuOpening()
{
  // Refresh profile list on menu open (allows runtime updates)
  DiscoverProfiles();
  UpdateMenu();
}

bool GdiVisionController::HandleMenuCommand(WORD commandId)
{
  if (commandId == IDM_VISION_REFRESH) {
    DiscoverProfiles();
    UpdateMenu();
    std::cout << "[GdiVision] Found " << m_availableProfiles.size()
              << " profiles after refresh" << std::endl;
    return true;
  }

  if (commandId == IDM_VISION_ENABLE) {
    if (m_visionEnabled) {
      // Disable
      StopProcessing();
      m_visionEnabled = false;
      if (m_overlay) {
        m_overlay->SetEnabled(false);
        m_overlay->Clear();
      }
    } else {
      // Enable - need an active profile first
      std::lock_guard<std::mutex> lock(m_profileMutex);
      if (!m_activeProfile.empty()) {
        m_visionEnabled = true;
        if (m_overlay) m_overlay->SetEnabled(true);
        StartProcessing();
      } else {
        // Auto-select first profile if none selected
        if (!m_availableProfiles.empty()) {
          m_activeProfile = m_availableProfiles[0].fullPath;
          m_visionEnabled = true;
          if (m_overlay) m_overlay->SetEnabled(true);
          StartProcessing();
        }
      }
    }
    UpdateMenu();
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    return true;
  }

  if (commandId >= IDM_VISION_PROFILE_BASE && commandId < IDM_VISION_PROFILE_BASE + 100) {
    size_t index = commandId - IDM_VISION_PROFILE_BASE;
    if (index < m_availableProfiles.size()) {
      {
        std::lock_guard<std::mutex> lock(m_profileMutex);
        m_activeProfile = m_availableProfiles[index].fullPath;
      }
      std::cout << "[GdiVision] Profile selected: "
                << m_availableProfiles[index].displayName << std::endl;
      UpdateMenu();
    }
    return true;
  }

  return false;
}

void GdiVisionController::SetEnabled(bool enabled)
{
  if (enabled == m_visionEnabled.load()) return;

  if (enabled) {
    std::lock_guard<std::mutex> lock(m_profileMutex);
    if (m_activeProfile.empty()) {
      // Auto-select first profile if none selected
      if (!m_availableProfiles.empty()) {
        m_activeProfile = m_availableProfiles[0].fullPath;
      } else {
        std::cout << "[GdiVision] SetEnabled: No profile available" << std::endl;
        return;
      }
    }
    m_visionEnabled = true;
    if (m_overlay) m_overlay->SetEnabled(true);
    StartProcessing();
  } else {
    StopProcessing();
    m_visionEnabled = false;
    if (m_overlay) {
      m_overlay->SetEnabled(false);
      m_overlay->Clear();
    }
  }

  UpdateMenu();
  if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
  std::cout << "[GdiVision] SetEnabled(" << (enabled ? "true" : "false") << ")" << std::endl;
}

void GdiVisionController::SelectProfile(const std::string& profilePath)
{
  std::lock_guard<std::mutex> lock(m_profileMutex);
  m_activeProfile = profilePath;
  std::cout << "[GdiVision] SelectProfile: " << profilePath << std::endl;
}

void GdiVisionController::EnqueueFrame(const std::vector<uint8_t>& rgbData,
                                       uint32_t width, uint32_t height,
                                       int displayRotation)
{
  if (!m_visionEnabled.load() || !m_threadRunning.load()) return;

  std::lock_guard<std::mutex> lock(m_frameQueueMutex);
  // Keep max 2 frames in queue to avoid lag
  while (m_frameQueue.size() >= 2) {
    m_frameQueue.pop();
  }

  if (displayRotation != 0) {
    // Build rotated RGB so the vision thread sees what the user sees.
    const uint32_t srcW = width;
    const uint32_t srcH = height;
    const uint32_t rotW = srcH;
    const uint32_t rotH = srcW;
    std::vector<uint8_t> rotatedRgb(rotW * rotH * 3);
    const uint8_t* src = rgbData.data();
    for (uint32_t dy = 0; dy < rotH; ++dy) {
      for (uint32_t dx = 0; dx < rotW; ++dx) {
        uint32_t sx, sy;
        if (displayRotation == -90) {
          sx = srcW - 1 - dy;
          sy = dx;
        } else {  // +90
          sx = dy;
          sy = srcH - 1 - dx;
        }
        const size_t srcIdx = (sy * srcW + sx) * 3;
        const size_t dstIdx = (dy * rotW + dx) * 3;
        rotatedRgb[dstIdx + 0] = src[srcIdx + 0];
        rotatedRgb[dstIdx + 1] = src[srcIdx + 1];
        rotatedRgb[dstIdx + 2] = src[srcIdx + 2];
      }
    }
    m_frameQueue.push(std::move(rotatedRgb));
    m_queuedWidth  = rotW;
    m_queuedHeight = rotH;
  } else {
    m_frameQueue.push(rgbData);
    m_queuedWidth  = width;
    m_queuedHeight = height;
  }
  m_frameQueueCV.notify_one();
}

void GdiVisionController::DiscoverProfiles()
{
  m_availableProfiles.clear();

  // Look for vision profiles in config/vision directory
  std::vector<std::string> searchPaths = {
    "config/vision",
    "./config/vision",
    "../config/vision"
  };

  WIN32_FIND_DATAW findData;
  HANDLE hFind;

  for (const auto& basePath : searchPaths) {
    std::wstring searchPath(basePath.begin(), basePath.end());
    searchPath += L"/*.json";

    hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
      do {
        std::wstring fileName = findData.cFileName;
        // Skip recipe files
        if (fileName.find(L".recipe.json") != std::wstring::npos) {
          continue;
        }

        std::string narrowFileName(fileName.begin(), fileName.end());

        ProfileInfo info;
        info.fullPath = basePath + "/" + narrowFileName;
        info.displayName = narrowFileName;
        if (info.displayName.length() > 5 &&
            info.displayName.substr(info.displayName.length() - 5) == ".json") {
          info.displayName = info.displayName.substr(0, info.displayName.length() - 5);
        }

        m_availableProfiles.push_back(info);
      } while (FindNextFileW(hFind, &findData));

      FindClose(hFind);

      if (!m_availableProfiles.empty()) {
        std::cout << "[GdiVision] Found " << m_availableProfiles.size()
                  << " vision profiles in " << basePath << std::endl;
        break;
      }
    }
  }

  // Also scan machine-specific folders (config/MACHINE/vision)
  for (const auto& basePath : {"config", "./config", "../config"}) {
    std::wstring configPath(basePath, basePath + std::strlen(basePath));
    configPath += L"/*";

    WIN32_FIND_DATAW dirFindData;
    hFind = FindFirstFileW(configPath.c_str(), &dirFindData);
    if (hFind != INVALID_HANDLE_VALUE) {
      do {
        if (dirFindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
          std::wstring dirName = dirFindData.cFileName;
          if (dirName != L"." && dirName != L"..") {
            std::wstring visionPath = configPath.substr(0, configPath.length() - 1);
            visionPath += dirName + L"/vision/*.json";

            WIN32_FIND_DATAW fileFindData;
            HANDLE hFind2 = FindFirstFileW(visionPath.c_str(), &fileFindData);
            if (hFind2 != INVALID_HANDLE_VALUE) {
              do {
                std::wstring fileName = fileFindData.cFileName;
                if (fileName.find(L".recipe.json") != std::wstring::npos) {
                  continue;
                }

                std::string narrowFileName(fileName.begin(), fileName.end());
                std::string narrowDirName(dirName.begin(), dirName.end());

                ProfileInfo info;
                info.fullPath = std::string(basePath) + "/" + narrowDirName + "/vision/" + narrowFileName;
                info.displayName = narrowFileName;
                if (info.displayName.length() > 5 &&
                    info.displayName.substr(info.displayName.length() - 5) == ".json") {
                  info.displayName = info.displayName.substr(0, info.displayName.length() - 5);
                }

                bool exists = false;
                for (const auto& existing : m_availableProfiles) {
                  if (existing.displayName == info.displayName) {
                    exists = true;
                    break;
                  }
                }
                if (!exists) {
                  m_availableProfiles.push_back(info);
                }
              } while (FindNextFileW(hFind2, &fileFindData));
              FindClose(hFind2);
            }
          }
        }
      } while (FindNextFileW(hFind, &dirFindData));
      FindClose(hFind);
    }
  }

  std::sort(m_availableProfiles.begin(), m_availableProfiles.end(),
            [](const ProfileInfo& a, const ProfileInfo& b) {
              return a.displayName < b.displayName;
            });

  if (!m_availableProfiles.empty()) {
    std::cout << "[GdiVision] Total vision profiles discovered: "
              << m_availableProfiles.size() << std::endl;
  }
}

void GdiVisionController::UpdateMenu()
{
  if (!m_menu) return;

  // Remove old profile items (keep first 3: enable / refresh / separator)
  int count = GetMenuItemCount(m_menu);
  while (count > 3) {
    DeleteMenu(m_menu, count - 1, MF_BYPOSITION);
    count--;
  }

  // Update enable checkbox
  CheckMenuItem(m_menu, IDM_VISION_ENABLE,
                m_visionEnabled ? MF_CHECKED : MF_UNCHECKED);

  // Get current active profile
  std::string activeProfile;
  {
    std::lock_guard<std::mutex> lock(m_profileMutex);
    activeProfile = m_activeProfile;
  }

  // Add profile items
  if (m_availableProfiles.empty()) {
    AppendMenuW(m_menu, MF_STRING | MF_GRAYED, 0, L"(No profiles found)");
  } else {
    for (size_t i = 0; i < m_availableProfiles.size() && i < 100; ++i) {
      std::wstring wName(m_availableProfiles[i].displayName.begin(),
                         m_availableProfiles[i].displayName.end());
      UINT flags = MF_STRING;
      if (m_availableProfiles[i].fullPath == activeProfile) {
        flags |= MF_CHECKED;
      }
      AppendMenuW(m_menu, flags, IDM_VISION_PROFILE_BASE + static_cast<UINT>(i), wName.c_str());
    }
  }

  if (m_hwnd) DrawMenuBar(m_hwnd);
}

void GdiVisionController::StartProcessing()
{
  if (m_threadRunning) return;

  // Suppress verbose logging during continuous vision
  VisionManager::instance().setVerboseLogging(false);

  m_threadRunning = true;
  m_thread = std::thread(&GdiVisionController::ThreadLoop, this);
  std::cout << "[GdiVision] Vision processing thread started" << std::endl;
}

void GdiVisionController::StopProcessing()
{
  if (!m_threadRunning) return;

  m_threadRunning = false;
  m_frameQueueCV.notify_all();

  if (m_thread.joinable()) {
    m_thread.join();
  }

  // Clear the frame queue
  {
    std::lock_guard<std::mutex> lock(m_frameQueueMutex);
    while (!m_frameQueue.empty()) {
      m_frameQueue.pop();
    }
  }

  // Restore verbose logging
  VisionManager::instance().setVerboseLogging(true);

  std::cout << "[GdiVision] Vision processing thread stopped" << std::endl;
}

void GdiVisionController::ThreadLoop()
{
  std::cout << "[GdiVision] Vision background thread started (ID: "
            << std::this_thread::get_id() << ")" << std::endl;

  while (m_threadRunning) {
    std::vector<uint8_t> frameData;
    uint32_t frameWidth = 0;
    uint32_t frameHeight = 0;
    std::string profilePath;

    // Wait for a frame
    {
      std::unique_lock<std::mutex> lock(m_frameQueueMutex);
      m_frameQueueCV.wait_for(lock, std::chrono::milliseconds(100), [this] {
        return !m_frameQueue.empty() || !m_threadRunning;
      });

      if (!m_threadRunning) break;
      if (m_frameQueue.empty()) continue;

      frameData = std::move(m_frameQueue.front());
      m_frameQueue.pop();
      frameWidth = m_queuedWidth;
      frameHeight = m_queuedHeight;
    }

    // Get active profile
    {
      std::lock_guard<std::mutex> lock(m_profileMutex);
      profilePath = m_activeProfile;
    }

    if (profilePath.empty() || frameData.empty() || frameWidth == 0 || frameHeight == 0) {
      continue;
    }

    try {
      // Convert RGB frame to BGR cv::Mat for OpenCV
      cv::Mat bgrImage(frameHeight, frameWidth, CV_8UC3);
      const uint8_t* src = frameData.data();
      uint8_t* dst = bgrImage.data;

      for (uint32_t y = 0; y < frameHeight; ++y) {
        for (uint32_t x = 0; x < frameWidth; ++x) {
          size_t srcIdx = (y * frameWidth + x) * 3;
          size_t dstIdx = (y * frameWidth + x) * 3;
          // RGB to BGR
          dst[dstIdx + 0] = src[srcIdx + 2];  // B
          dst[dstIdx + 1] = src[srcIdx + 1];  // G
          dst[dstIdx + 2] = src[srcIdx + 0];  // R
        }
      }

      // Load vision profile JSON directly (not as a recipe)
      std::ifstream profileFile(profilePath);
      if (!profileFile.is_open()) {
        std::cerr << "[GdiVision] Cannot open profile: " << profilePath << std::endl;
        continue;
      }

      nlohmann::json profileJson;
      profileFile >> profileJson;
      profileFile.close();

      // Extract plugin name and parameters from profile
      std::string pluginName;
      nlohmann::json pluginParams;

      if (profileJson.contains("plugin") && profileJson["plugin"].is_object()) {
        pluginName = profileJson["plugin"].value("name", "");
        pluginParams = profileJson["plugin"].value("parameters", nlohmann::json::object());
      }

      if (pluginName.empty()) {
        std::cerr << "[GdiVision] Profile missing plugin name: " << profilePath << std::endl;
        continue;
      }

      // Set up ROI if specified in profile
      if (profileJson.contains("roi") && profileJson["roi"].is_object()) {
        const auto& roiJson = profileJson["roi"];
        bool roiEnabled = roiJson.value("enabled", false);
        if (roiEnabled) {
          int startX = roiJson.value("startX", 0);
          int startY = roiJson.value("startY", 0);
          int endX = roiJson.value("endX", 0);
          int endY = roiJson.value("endY", 0);
          VisionROI roi(startX, startY, endX, endY);
          roi.setEnabled(true);
          VisionManager::instance().setROI(roi);
        } else {
          VisionManager::instance().setROIEnabled(false);
        }
      } else {
        VisionManager::instance().setROIEnabled(false);
      }

      // Set input image
      VisionManager::instance().setInputImage(bgrImage);

      // Execute the plugin directly
      QString qPluginName = QString::fromStdString(pluginName);
      VisionResult result = VisionManager::instance().executePlugin(qPluginName, pluginParams);

      // Extract display name from path
      std::string displayName = profilePath;
      size_t lastSlash = displayName.find_last_of("/\\");
      if (lastSlash != std::string::npos) {
        displayName = displayName.substr(lastSlash + 1);
      }
      if (displayName.length() > 5 &&
          displayName.substr(displayName.length() - 5) == ".json") {
        displayName = displayName.substr(0, displayName.length() - 5);
      }

      // Update overlay (thread-safe via mutex in SetResult)
      if (m_overlay) {
        m_overlay->SetResult(result, displayName);
      }

      // Post message to UI thread for repaint (safer than direct InvalidateRect)
      if (m_hwnd) {
        PostMessage(m_hwnd, WM_VISION_RESULT_READY, 0, 0);
      }
    }
    catch (const std::exception& e) {
      std::cerr << "[GdiVision] Vision processing error: " << e.what() << std::endl;
    }
  }

  std::cout << "[GdiVision] Vision thread loop exiting" << std::endl;
}

#endif  // HAS_OPENCV
