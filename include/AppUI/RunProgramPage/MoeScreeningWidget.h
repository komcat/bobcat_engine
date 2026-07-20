// MoeScreeningWidget.h - "MOE Screening" tab for the F1 Run Program page.
//
// A sibling of PoyntOptimizeWidget for screening bare MOEs (identified by a
// MOE ID, NOT a DUT serial). Layout mirrors the operator sketch:
//
//   Top button grid (all act on gantry-main named positions):
//     [Gantry safe]
//     [Move to screener-A1] [Move screen-A1-IR] [Save screener-A1] [Save screen-A1-IR]
//     [Move to screener-A2] [Move screen-A2-IR] [Save screener-A2] [Save screen-A2-IR]
//     [Measure MOE]
//
//   [Measure MOE] prompts for a MOE ID then runs scripts/moe_screen_baseline.lua
//   (via ScriptWorker), which measures at screen-A1-IR / screen-A2-IR and appends
//   one CSV row per location to data/<product>/moe-screen/<MOE_ID>.csv.
//
//   Bottom read-back panel: polls data/<product>/moe-screen/ on a 1 Hz timer for
//   the newest *.csv and shows the A1/A2 x B0/B1 poynt X/Y grid (green if
//   |value| < 0.5, else red) plus a TX-RX dx/dy row per location. "Save final
//   measurement" exports the latest values to <MOE_ID>_final.json; "Open data
//   folder" opens the watch folder in Explorer.
//
// While any script or move is in flight every button except the panel's is
// disabled (m_busy guard), matching PoyntOptimizeWidget.
#pragma once

#include <QWidget>
#include <future>

class QLabel;
class QPushButton;
class QPlainTextEdit;
class QSpinBox;
class QTimer;
class ScriptWorker;
struct ApplicationContext;

class MoeScreeningWidget : public QWidget {
  Q_OBJECT
public:
  explicit MoeScreeningWidget(ApplicationContext* context, QWidget* parent = nullptr);
  ~MoeScreeningWidget() override;

private slots:
  void onMeasureMoe();

  // ScriptWorker signal handlers.
  void onScriptStarted();
  void onScriptCompleted(bool success, QString errorMessage);
  void onScriptOutput(QString message);

  // Read-back panel.
  void onTick();               // poll watch folder for newest CSV
  void onSaveFinalMeasurement();
  void onOpenDataFolder();

private:
  void setupUI();
  void setBusy(bool busy, const QString& whatRunning = QString());
  QString resolveScriptPath(const QString& scriptFile) const;
  bool launchScript(const QString& scriptFile, const QString& busyLabel);

  // gantry-main named-position helpers (ported from PoyntOptimizeWidget).
  void moveToNamedGantryPosition(const char* name);
  void saveCurrentToNamedGantryPosition(const char* name);

  // Live-apply the given exposure (µs) to the SWIR/IR camera right now.
  void applyExposure(int us);

  // Persist / restore the two per-position IR exposures for the active product
  // (config/<product>/moe_screen_exposure.json).
  QString exposureConfigPath() const;
  void    loadExposures();
  void    saveExposures();

  void appendLog(const QString& line, const QString& color = QString());

  // Read-back panel helpers.
  QString watchDir() const;    // applicationDir/data/<product>/moe-screen
  void    reloadFromCsv(const QString& path);
  void    clearPanel();

  ApplicationContext* m_context = nullptr;

  // Button grid.
  QPushButton* m_gantrySafeBtn = nullptr;
  QPushButton* m_moveScreenerA1Btn = nullptr;
  QPushButton* m_moveIrA1Btn       = nullptr;
  QPushButton* m_saveScreenerA1Btn = nullptr;
  QPushButton* m_saveIrA1Btn       = nullptr;
  QPushButton* m_moveScreenerA2Btn = nullptr;
  QPushButton* m_moveIrA2Btn       = nullptr;
  QPushButton* m_saveScreenerA2Btn = nullptr;
  QPushButton* m_saveIrA2Btn       = nullptr;
  QPushButton* m_measureBtn = nullptr;
  QLabel*      m_busyLabel  = nullptr;

  // Per-position IR camera exposure (µs) applied at the start of each run.
  QSpinBox*    m_expA1Spin = nullptr;   // at screen-A1-IR
  QSpinBox*    m_expA2Spin = nullptr;   // at screen-A2-IR
  QPushButton* m_applyA1Btn = nullptr;  // live-apply A1 exposure to the camera
  QPushButton* m_applyA2Btn = nullptr;  // live-apply A2 exposure to the camera

  QPlainTextEdit* m_log = nullptr;

  // Read-back panel.
  QLabel*  m_panelHeader = nullptr;
  QLabel*  m_panelStatus = nullptr;
  // Poynt value labels, indexed [loc 0=a1/1=a2][beam 0=b0/1=b1][axis 0=x/1=y].
  QLabel*  m_valueLabels[2][2][2] = {};
  // TX-RX delta labels, indexed [loc 0=a1/1=a2][axis 0=dx/1=dy] (µm).
  QLabel*  m_txRxLabels[2][2] = {};
  QPushButton* m_saveFinalBtn  = nullptr;
  QPushButton* m_openFolderBtn = nullptr;
  QTimer*      m_timer = nullptr;

  // Monitor state.
  QString   m_lastWatchDir;
  QString   m_currentPath;
  long long m_currentMtime = -1;
  QString   m_currentMoeId;   // moe_id parsed from the loaded CSV
  bool      m_haveData = false;

  // Async state: at most one script OR one move in flight.
  bool              m_busy = false;
  ScriptWorker*     m_scriptWorker = nullptr;
  std::future<void> m_moveFuture;
};
