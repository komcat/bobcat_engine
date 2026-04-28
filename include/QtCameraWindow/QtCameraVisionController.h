// QtCameraVisionController.h
// Self-contained "Vision" group: an enable checkbox, a profile combo,
// and a refresh button. Owns the worker thread that consumes frames
// from a single-slot queue (fed by the camera window's OnNewFrame),
// runs the active profile through VisionManager, and hands the
// resulting VisionResult to the viewport for overlay rendering.
//
// External callers (e.g. VisionCommandHandler::execute_profile) can
// also push a one-shot overlay via `showExternalOverlay()` — the
// controller owns the auto-clear QTimer.
//
// Extracted from QtCameraWindow so the window class can stay focused
// on camera display + side-panel orchestration. Behaviour is preserved
// exactly:
//   * QSettings keys (`Vision[<instanceId>]/Enabled`,
//     `Vision[<instanceId>]/ActiveProfile`) are unchanged so saved
//     state survives the refactor.
//   * Profile discovery scans `config/vision/`, then per-machine
//     `config/<MACHINE>/vision/` if the standard path is empty.
//   * Frame queue depth capped at 2 (drops on overflow).
//
// Conditionally compiled: only declared/defined when HAS_OPENCV is set.
#pragma once

#ifdef HAS_OPENCV

#include "vision/VisionResult.h"

#include <QGroupBox>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class QCheckBox;
class QComboBox;
class QPushButton;
class QTimer;
class QtCameraViewport;

class QtCameraVisionController : public QGroupBox
{
  Q_OBJECT

public:
  QtCameraVisionController(QtCameraViewport* viewport,
                           std::string instanceId,
                           QWidget* parent = nullptr);
  ~QtCameraVisionController() override;

  // Frame ingestion (from QtCameraWindow::OnNewFrame on the camera
  // thread). The queue is single-producer/single-consumer with depth 2.
  void enqueueFrame(const std::vector<uint8_t>& rgbData,
                    uint32_t width, uint32_t height);

  // Push the latest VisionResult to the viewport for overlay. Called
  // every frame by the window's updateViewport().
  void pushResultToViewport();

  // Public API (mirrors legacy QtCameraWindow surface).
  bool isVisionProcessingActive() const;
  VisionResult getLatestVisionResult() const;
  uint64_t getVisionResultSequence() const;
  void setVisionEnabled(bool enabled);
  void selectVisionProfile(const std::string& profilePath);
  void showExternalOverlay(const VisionResult& result,
                           const std::string& profileName,
                           int durationMs);

  // Persistence (keys unchanged from legacy QtCameraWindow).
  void saveSettings() const;
  void loadSettings();

  // Synchronous shutdown — joins the worker thread. Call from owner's
  // destructor before its members go away.
  void shutdown();

private slots:
  void onEnableToggled(bool checked);
  void onProfileChanged(int index);
  void onRefreshProfiles();

private:
  void setupUI();
  void startProcessing();
  void stopProcessing();
  void threadLoop();
  void discoverProfiles();
  void populateProfileCombo();
  QString settingsKey(const QString& key) const;

  QtCameraViewport* m_viewport;
  std::string m_instanceId;

  // UI controls
  QCheckBox*   m_enableCheck   = nullptr;
  QComboBox*   m_profileCombo  = nullptr;
  QPushButton* m_refreshBtn    = nullptr;

  // Worker thread state
  std::atomic<bool>                m_visionEnabled{false};
  std::atomic<bool>                m_threadRunning{false};
  std::thread                      m_thread;
  std::queue<std::vector<uint8_t>> m_frameQueue;
  uint32_t                         m_queuedWidth  = 0;
  uint32_t                         m_queuedHeight = 0;
  std::mutex                       m_frameQueueMutex;
  std::condition_variable          m_frameQueueCV;

  // Profile state
  std::string        m_activeProfile;
  mutable std::mutex m_profileMutex;

  // Result state (thread-safe)
  VisionResult            m_lastResult;
  std::string             m_resultProfileName;
  mutable std::mutex      m_resultMutex;
  std::atomic<uint64_t>   m_resultSequence{0};

  // External overlay (auto-clear)
  QTimer* m_externalOverlayTimer = nullptr;

  // Profile discovery
  struct ProfileInfo { std::string fullPath; std::string displayName; };
  std::vector<ProfileInfo> m_availableProfiles;
};

#endif  // HAS_OPENCV
