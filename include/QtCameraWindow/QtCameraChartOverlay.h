// QtCameraChartOverlay.h
// Self-contained "Chart Overlay" group: a checkbox + channel list + scale
// controls. Subscribes to a DataStore, accumulates per-channel ring
// buffers, and pushes the currently-enabled channels to the camera
// viewport for overlay rendering.
//
// Two push paths exist:
//   1. Throttled (~20 Hz) inside `onDataStoreValueChanged` — fires when
//      new data arrives on an enabled channel.
//   2. `pushSnapshotsToViewport()` — called once per frame by the
//      window's `updateViewport()` so the overlay stays in sync with
//      the camera frame cadence.
//
// Extracted from QtCameraWindow so the window class can stay focused on
// camera display + side-panel orchestration. Behaviour is preserved
// exactly:
//   * QSettings keys (`ChartOverlay[<instanceId>]/Enabled`, `Scale`,
//     `EnabledChannels`) are unchanged so saved state survives the
//     refactor.
//   * 20 Hz throttle, 200-point ring buffers per channel, scale clamp
//     [0.5, 3.0] are unchanged.
#pragma once

#include <QGroupBox>
#include <QString>

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>

class QCheckBox;
class QListWidget;
class QPushButton;
class DataStore;
class QtCameraViewport;

class QtCameraChartOverlay : public QGroupBox
{
  Q_OBJECT

public:
  QtCameraChartOverlay(QtCameraViewport* viewport,
                       std::string instanceId,
                       QWidget* parent = nullptr);
  ~QtCameraChartOverlay() override;

  // The page wires the data store via dependency injection AFTER the
  // panel is constructed; this setter registers the subscription.
  void setDataStore(DataStore* dataStore);

  // Public mirror of the legacy QtCameraWindow API.
  void setChartEnabled(bool enabled);
  bool isChartEnabled() const { return m_chartEnabled; }

  // Re-populate the channel list from the connected DataStore.
  // Page also calls this once after `initialize()` so saved channels
  // are reflected.
  void refreshChannelList();

  // Push currently-enabled channel snapshots to the viewport. Called
  // every frame by the window's updateViewport().
  void pushSnapshotsToViewport();

  // Persistence (keys unchanged from legacy QtCameraWindow).
  void saveSettings() const;
  void loadSettings();

private slots:
  void onChartEnableToggled(bool checked);
  void onChartScaleIncrease();
  void onChartScaleDecrease();
  void onChartRefreshChannels();

private:
  void setupUI();
  void onDataStoreValueChanged(const std::string& channelId, float value);
  QString settingsKey(const QString& key) const;

  QtCameraViewport* m_viewport;
  DataStore* m_dataStore = nullptr;
  std::string m_instanceId;

  // UI controls
  QCheckBox*   m_enableCheck   = nullptr;
  QListWidget* m_channelList   = nullptr;
  QPushButton* m_refreshBtn    = nullptr;
  QPushButton* m_scaleUpBtn    = nullptr;
  QPushButton* m_scaleDnBtn    = nullptr;

  // Channel data
  struct ChartChannelData {
    std::deque<double> values;
    double runningMin = 1e30;
    double runningMax = -1e30;
  };
  mutable std::mutex                       m_chartMutex;
  std::map<std::string, ChartChannelData>  m_chartChannels;
  std::set<std::string>                    m_chartEnabledChannels;

  bool  m_chartEnabled = false;
  float m_chartScale   = 1.0f;
  std::chrono::steady_clock::time_point m_lastChartRepaint;

  static constexpr size_t CHART_MAX_POINTS = 200;

  // Diagnostics: measure SMU sample rate, throttle decisions, and UI-thread
  // backlog (latency between scheduling a queued repaint and it actually
  // running). Emit a summary line every kDiagLogIntervalSec.
  // Counters are written from the DataStore subscriber callback (worker
  // thread) and from the queued lambda (UI thread); use atomics so the
  // updates are race-free without taking m_chartMutex.
  void maybeLogDiagnostics();
  std::atomic<uint64_t> m_diagSamplesTotal{0};
  std::atomic<uint64_t> m_diagSamplesEnabled{0};
  std::atomic<uint64_t> m_diagThrottleKeeps{0};
  std::atomic<uint64_t> m_diagThrottleDrops{0};
  std::atomic<uint64_t> m_diagQueuedExecuted{0};
  std::atomic<int64_t>  m_diagLatencyTotalUs{0};
  std::atomic<int64_t>  m_diagLatencyMaxUs{0};
  std::chrono::steady_clock::time_point m_diagWindowStart;
  std::chrono::steady_clock::time_point m_diagStartupTime;
  std::mutex m_diagLogMutex;
  static constexpr int kDiagLogIntervalSec = 5;
};
