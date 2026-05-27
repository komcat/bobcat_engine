// QtCameraVisionController.cpp
#include "QtCameraVisionController.h"

#ifdef HAS_OPENCV

#include "QtCameraViewport.h"
#include "vision/VisionManager.h"
#include "vision/VisionROI.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>

#include <opencv2/imgproc.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

namespace {

const char* kGroupStyle =
    "QGroupBox { color: #cccccc; border: 1px solid #3e3e42; border-radius: 4px; "
    "            margin-top: 6px; padding-top: 4px; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 8px; }";

const char* kButtonStyle =
    "QPushButton { background-color: #3e3e42; border: 1px solid #555; "
    "              color: #cccccc; padding: 2px 6px; border-radius: 2px; }"
    "QPushButton:hover { background-color: #505050; }"
    "QPushButton:pressed { background-color: #2d2d30; }";

const char* kComboStyle =
    "QComboBox { background-color: #3e3e42; border: 1px solid #555; "
    "            color: #cccccc; padding: 2px 4px; }"
    "QComboBox QAbstractItemView { background-color: #3e3e42; color: #cccccc; }";

}  // namespace

QtCameraVisionController::QtCameraVisionController(
    QtCameraViewport* viewport,
    std::string instanceId,
    QWidget* parent)
  : QGroupBox("Vision", parent)
  , m_viewport(viewport)
  , m_instanceId(std::move(instanceId))
{
  setStyleSheet(kGroupStyle);
  setupUI();
  discoverProfiles();
  populateProfileCombo();
}

QtCameraVisionController::~QtCameraVisionController()
{
  shutdown();
}

void QtCameraVisionController::shutdown()
{
  stopProcessing();
}

void QtCameraVisionController::setupUI()
{
  auto* layout = new QVBoxLayout(this);
  layout->setSpacing(4);
  layout->setContentsMargins(6, 8, 6, 6);

  m_enableCheck = new QCheckBox("Enable Vision");
  layout->addWidget(m_enableCheck);

  layout->addWidget(new QLabel("Profile:"));

  m_profileCombo = new QComboBox();
  m_profileCombo->setStyleSheet(kComboStyle);
  layout->addWidget(m_profileCombo);

  m_refreshBtn = new QPushButton("Refresh Profiles");
  m_refreshBtn->setStyleSheet(kButtonStyle);
  layout->addWidget(m_refreshBtn);

  connect(m_enableCheck, &QCheckBox::toggled,
          this, &QtCameraVisionController::onEnableToggled);
  connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &QtCameraVisionController::onProfileChanged);
  connect(m_refreshBtn, &QPushButton::clicked,
          this, &QtCameraVisionController::onRefreshProfiles);
}

void QtCameraVisionController::enqueueFrame(const std::vector<uint8_t>& rgbData,
                                            uint32_t width, uint32_t height)
{
  if (!m_visionEnabled.load() || !m_threadRunning.load()) return;

  std::lock_guard<std::mutex> vLock(m_frameQueueMutex);
  if (m_frameQueue.size() < 2) {
    m_frameQueue.push(rgbData);
    m_queuedWidth  = width;
    m_queuedHeight = height;
  }
  m_frameQueueCV.notify_one();
}

void QtCameraVisionController::pushResultToViewport()
{
  if (!m_viewport) return;
  if (m_visionEnabled.load()) {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    m_viewport->setVisionOverlayData(true, m_lastResult, m_resultProfileName);
  } else {
    VisionResult empty;
    m_viewport->setVisionOverlayData(false, empty, std::string());
  }
}

void QtCameraVisionController::startProcessing()
{
  if (m_threadRunning.load()) return;
  m_threadRunning.store(true);
  m_thread = std::thread(&QtCameraVisionController::threadLoop, this);
  std::cout << "[VisionController] Vision processing thread started" << std::endl;
}

void QtCameraVisionController::stopProcessing()
{
  if (!m_threadRunning.load()) return;
  m_threadRunning.store(false);
  m_frameQueueCV.notify_all();
  if (m_thread.joinable()) {
    m_thread.join();
  }
  // Clear frame queue
  {
    std::lock_guard<std::mutex> lock(m_frameQueueMutex);
    while (!m_frameQueue.empty()) m_frameQueue.pop();
  }
  std::cout << "[VisionController] Vision processing thread stopped" << std::endl;
}

void QtCameraVisionController::threadLoop()
{
  for (int iter = 0; iter < 10000000 && m_threadRunning.load(); ++iter) {
    std::vector<uint8_t> frameData;
    uint32_t width  = 0;
    uint32_t height = 0;

    // Wait for a frame with 100ms timeout
    {
      std::unique_lock<std::mutex> lock(m_frameQueueMutex);
      m_frameQueueCV.wait_for(lock, std::chrono::milliseconds(100),
          [this] { return !m_frameQueue.empty() || !m_threadRunning.load(); });

      if (!m_threadRunning.load()) break;
      if (m_frameQueue.empty()) continue;

      frameData = std::move(m_frameQueue.front());
      m_frameQueue.pop();
      width  = m_queuedWidth;
      height = m_queuedHeight;
    }

    if (width == 0 || height == 0 || frameData.empty()) continue;

    // Get active profile path
    std::string profilePath;
    {
      std::lock_guard<std::mutex> lock(m_profileMutex);
      profilePath = m_activeProfile;
    }
    if (profilePath.empty()) continue;

    // Convert RGB to BGR cv::Mat
    cv::Mat rgbImage(static_cast<int>(height), static_cast<int>(width),
                     CV_8UC3, frameData.data());
    cv::Mat bgrImage;
    cv::cvtColor(rgbImage, bgrImage, cv::COLOR_RGB2BGR);

    // Load profile JSON (no exceptions from application code; wrap third-party parse)
    std::ifstream profileFile(profilePath);
    if (!profileFile.is_open()) {
      std::cerr << "[VisionController] Vision: cannot open profile: " << profilePath << std::endl;
      continue;
    }

    nlohmann::json profileJson;
    try {
      profileFile >> profileJson;
    } catch (const std::exception& ex) {
      std::cerr << "[VisionController] Vision: failed to parse profile JSON: "
                << ex.what() << std::endl;
      continue;
    }

    if (!profileJson.contains("plugin") || !profileJson["plugin"].contains("name")) {
      std::cerr << "[VisionController] Vision: profile missing plugin.name field" << std::endl;
      continue;
    }

    const std::string pluginName = profileJson["plugin"]["name"].get<std::string>();
    const nlohmann::json pluginParams = profileJson["plugin"].value("parameters",
                                                                     nlohmann::json::object());

    // Hand the FULL frame to VisionManager and let it apply the ROI itself.
    // (Previously we pre-cropped here and also enabled the ROI on VisionManager,
    // which double-applied the ROI and failed once the second crop fell off the
    // already-cropped image.)
    VisionManager::instance().setInputImage(bgrImage);

    if (profileJson.contains("roi") && profileJson["roi"].value("enabled", false)) {
      VisionROI roi = VisionROI::fromJson(profileJson["roi"]);
      if (roi.isValid()) {
        VisionManager::instance().setROI(roi);
        VisionManager::instance().setROIEnabled(true);
      } else {
        VisionManager::instance().setROIEnabled(false);
      }
    } else {
      VisionManager::instance().setROIEnabled(false);
    }

    VisionResult result = VisionManager::instance().executePlugin(
        QString::fromStdString(pluginName), pluginParams);

    // Extract display name from profile path (filename without extension)
    std::string displayName;
    {
      std::filesystem::path p(profilePath);
      displayName = p.stem().string();
    }

    // Store result
    {
      std::lock_guard<std::mutex> lock(m_resultMutex);
      m_lastResult = result;
      m_resultProfileName = displayName;
    }
    m_resultSequence.fetch_add(1, std::memory_order_relaxed);

    // Trigger viewport repaint on Qt main thread
    if (m_viewport) {
      QMetaObject::invokeMethod(m_viewport, "update", Qt::QueuedConnection);
    }
  }
}

void QtCameraVisionController::discoverProfiles()
{
  m_availableProfiles.clear();

  // Helper lambda: scan a directory for .json profiles (skip .recipe.json)
  auto scanDir = [this](const std::string& dir) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) return;

    for (const auto& entry : it) {
      if (!entry.is_regular_file()) continue;
      if (entry.path().extension() != ".json") continue;

      // Skip recipe files
      const std::string fname = entry.path().filename().string();
      if (fname.find(".recipe.json") != std::string::npos) continue;

      ProfileInfo info;
      info.fullPath    = entry.path().string();
      info.displayName = entry.path().stem().string();

      // Avoid duplicates
      bool exists = false;
      for (const auto& existing : m_availableProfiles) {
        if (existing.displayName == info.displayName) { exists = true; break; }
      }
      if (!exists) {
        m_availableProfiles.push_back(info);
      }
    }
  };

  // 1. Scan standard config/vision/ paths
  const std::vector<std::string> searchDirs = {
    "config/vision",
    "./config/vision",
    "../config/vision"
  };

  for (const auto& dir : searchDirs) {
    scanDir(dir);
    if (!m_availableProfiles.empty()) break;
  }

  // 2. Scan machine-specific paths: config/<MACHINE>/vision/
  const std::vector<std::string> configRoots = {"config", "./config", "../config"};
  for (const auto& root : configRoots) {
    std::error_code ec;
    std::filesystem::directory_iterator rootIt(root, ec);
    if (ec) continue;

    for (const auto& entry : rootIt) {
      if (!entry.is_directory()) continue;
      const std::string dirName = entry.path().filename().string();
      if (dirName == "." || dirName == ".." || dirName == "system" || dirName == "SYSTEM") continue;

      // Check if this subdirectory has a vision/ folder
      const std::string visionDir = entry.path().string() + "/vision";
      scanDir(visionDir);
    }

    if (!m_availableProfiles.empty()) break;
  }

  std::sort(m_availableProfiles.begin(), m_availableProfiles.end(),
      [](const ProfileInfo& a, const ProfileInfo& b) {
        return a.displayName < b.displayName;
      });

  std::cout << "[VisionController] Vision profiles discovered: "
            << m_availableProfiles.size() << std::endl;
}

void QtCameraVisionController::populateProfileCombo()
{
  if (!m_profileCombo) return;

  m_profileCombo->blockSignals(true);
  m_profileCombo->clear();
  for (const auto& profile : m_availableProfiles) {
    m_profileCombo->addItem(
        QString::fromStdString(profile.displayName),
        QString::fromStdString(profile.fullPath));
  }
  m_profileCombo->blockSignals(false);
}

QString QtCameraVisionController::settingsKey(const QString& key) const
{
  return QString("Vision%1/%2")
      .arg(QString::fromStdString(m_instanceId), key);
}

bool QtCameraVisionController::isVisionProcessingActive() const
{
  return m_visionEnabled.load() && m_threadRunning.load();
}

VisionResult QtCameraVisionController::getLatestVisionResult() const
{
  std::lock_guard<std::mutex> lock(m_resultMutex);
  return m_lastResult;
}

uint64_t QtCameraVisionController::getVisionResultSequence() const
{
  return m_resultSequence.load();
}

void QtCameraVisionController::setVisionEnabled(bool enabled)
{
  if (enabled) {
    m_visionEnabled.store(true);
    startProcessing();
    if (m_enableCheck) {
      m_enableCheck->blockSignals(true);
      m_enableCheck->setChecked(true);
      m_enableCheck->blockSignals(false);
    }
  } else {
    stopProcessing();
    m_visionEnabled.store(false);
    if (m_enableCheck) {
      m_enableCheck->blockSignals(true);
      m_enableCheck->setChecked(false);
      m_enableCheck->blockSignals(false);
    }
  }
}

void QtCameraVisionController::selectVisionProfile(const std::string& profilePath)
{
  std::lock_guard<std::mutex> lock(m_profileMutex);
  m_activeProfile = profilePath;
}

void QtCameraVisionController::showExternalOverlay(const VisionResult& result,
                                                   const std::string& profileName,
                                                   int durationMs)
{
  // Marshal to the GUI thread. AutoConnection resolves to direct invoke if
  // already on the GUI thread, queued otherwise. Lambda captures by value
  // so the VisionResult survives until the slot runs.
  QMetaObject::invokeMethod(this,
      [this, result, profileName, durationMs]() {
        if (!m_viewport) return;
        // Lazy-init timer on the GUI thread so it's parented to this
        // QObject and lives on the correct thread for QTimer operations.
        if (!m_externalOverlayTimer) {
          m_externalOverlayTimer = new QTimer(this);
          m_externalOverlayTimer->setSingleShot(true);
          connect(m_externalOverlayTimer, &QTimer::timeout, this, [this]() {
            if (m_viewport) {
              VisionResult empty;
              m_viewport->setVisionOverlayData(false, empty, std::string());
            }
          });
        }
        m_viewport->setVisionOverlayData(true, result, profileName);
        m_externalOverlayTimer->start(durationMs > 0 ? durationMs : 500);
      }, Qt::AutoConnection);
}

void QtCameraVisionController::onEnableToggled(bool checked)
{
  if (checked) {
    // Sync m_activeProfile with whatever the combo is currently showing.
    // populateProfileCombo() blocks signals around addItem(), so the
    // first-item auto-selection never fires onProfileChanged — without
    // this sync the vision thread reads an empty profilePath and silently
    // skips every frame, leaving the overlay stuck on the default
    // "FAILED, 0.0 ms" VisionResult even though the user has clearly
    // selected a profile in the combo.
    if (m_profileCombo && m_profileCombo->count() > 0) {
      const int idx = std::max(0, m_profileCombo->currentIndex());
      const std::string fullPath =
          m_profileCombo->itemData(idx).toString().toStdString();
      std::lock_guard<std::mutex> lock(m_profileMutex);
      if (!fullPath.empty()) {
        m_activeProfile = fullPath;
      }
    }
    m_visionEnabled.store(true);
    startProcessing();
  } else {
    stopProcessing();
    m_visionEnabled.store(false);
  }
  std::cout << "[VisionController] Vision processing "
            << (checked ? "enabled" : "disabled") << std::endl;
}

void QtCameraVisionController::onProfileChanged(int index)
{
  if (!m_profileCombo || index < 0) return;
  const std::string fullPath =
      m_profileCombo->itemData(index).toString().toStdString();
  {
    std::lock_guard<std::mutex> lock(m_profileMutex);
    m_activeProfile = fullPath;
  }
  std::cout << "[VisionController] Vision profile selected: " << fullPath << std::endl;
}

void QtCameraVisionController::onRefreshProfiles()
{
  discoverProfiles();
  populateProfileCombo();
}

void QtCameraVisionController::saveSettings() const
{
  QSettings settings("Fabrinet", "BobcatEngine");
  settings.setValue(settingsKey("Enabled"), m_visionEnabled.load());
  {
    std::lock_guard<std::mutex> lock(m_profileMutex);
    settings.setValue(settingsKey("ActiveProfile"),
                      QString::fromStdString(m_activeProfile));
  }
}

void QtCameraVisionController::loadSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  const bool visionEnabled = settings.value(settingsKey("Enabled"), false).toBool();
  const QString savedProfile = settings.value(settingsKey("ActiveProfile"), "").toString();

  if (!savedProfile.isEmpty()) {
    std::lock_guard<std::mutex> lock(m_profileMutex);
    m_activeProfile = savedProfile.toStdString();
  }

  if (m_profileCombo && !savedProfile.isEmpty()) {
    for (int i = 0; i < m_profileCombo->count(); ++i) {
      if (m_profileCombo->itemData(i).toString() == savedProfile) {
        m_profileCombo->blockSignals(true);
        m_profileCombo->setCurrentIndex(i);
        m_profileCombo->blockSignals(false);
        break;
      }
    }
  }

  if (m_enableCheck) {
    m_enableCheck->blockSignals(true);
    m_enableCheck->setChecked(visionEnabled);
    m_enableCheck->blockSignals(false);
  }
  if (visionEnabled) {
    m_visionEnabled.store(true);
    startProcessing();
  }
}

#endif  // HAS_OPENCV
