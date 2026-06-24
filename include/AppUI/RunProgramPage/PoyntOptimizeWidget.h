// PoyntOptimizeWidget.h - Poynt-optimization helpers tab.
//
// Embedded as a tab ("Poynt Optimize") in the F1 Run Program page.
// Lets the operator drive the four common scripted actions of the
// poynt-correction flow (DUT init / close, automation, single
// measurement) plus a translation/rotation jog ladder on hex-right
// that re-measures after each jog by running the baseline script.
//
// Top action row:
//   - DUT Init         -> dut_init.lua
//   - DUT Close        -> dut_close.lua
//   - Automate         -> CLAUDE_step_all.lua (greedy per-axis coordinate descent)
//   - Auto-correct v2  -> CLAUDE_autocorrect_v2.lua (Jacobian damped least-squares)
//   - Single measure   -> CLAUDE_step1_baseline.lua
//   - Stop             -> ScriptEngine::RequestStop() to break a running script
//
// Translation jog group (operates on hex-right X/Y/Z, units mm):
//   - Step display (in um); +/-0.5 buttons add/sub 0.5 um to the step
//   - +X / -X / +Y / -Y / +Z / -Z: relative move by step then run baseline.
//
// Rotation jog group (operates on hex-right U/V/W, units deg):
//   - Step display (in millideg); +/-10 buttons add/sub 10 millideg
//   - +U/-U/+V/-V/+W/-W: relative move by step then run baseline.
//
// While ANY script or jog-move is in flight every other button is
// disabled and Stop is enabled.
#pragma once

#include <QWidget>
#include <future>

class QLabel;
class QPushButton;
class QCheckBox;
class QGroupBox;
class QPlainTextEdit;
class ScriptWorker;
class M4AASerialDataWidget;
struct ApplicationContext;

class PoyntOptimizeWidget : public QWidget {
  Q_OBJECT
public:
  explicit PoyntOptimizeWidget(ApplicationContext* context, QWidget* parent = nullptr);
  ~PoyntOptimizeWidget() override;

private slots:
  void onDutInit();
  void onDutClose();
  void onAutomate();
  void onAutomateV2();
  void onSingleMeasurement();
  void onStop();

  void onTranslationStepInc();   // step += 0.5 um
  void onTranslationStepDec();   // step -= 0.5 um (clamped at 0)
  void onRotationStepInc();      // step += 10 millideg
  void onRotationStepDec();      // step -= 10 millideg (clamped at 0)

  // Each axis jog: relative move by current step on hex-right, then run baseline.
  void onJogXPlus();  void onJogXMinus();
  void onJogYPlus();  void onJogYMinus();
  void onJogZPlus();  void onJogZMinus();
  void onJogUPlus();  void onJogUMinus();
  void onJogVPlus();  void onJogVMinus();
  void onJogWPlus();  void onJogWMinus();

  void onMoveToMoeA1();
  void onMoveToMoeA2();

  // Capture current gantry-main position as the named MOE-A1 / MOE-A2 named
  // position. Always shows a confirmation dialog with the delta between the
  // existing saved position (if any) and the current position.
  void onSaveMoeA1();
  void onSaveMoeA2();

  // ScriptWorker signal handlers.
  void onScriptStarted();
  void onScriptCompleted(bool success, QString errorMessage);
  void onScriptOutput(QString message);

private:
  void setupUI();
  // Enable/disable the whole control surface in one shot. When busy=true,
  // only Stop stays enabled.
  void setBusy(bool busy, const QString& whatRunning = QString());
  // Resolve a script name (e.g. "dut_init.lua") to a path under
  // config/{product}/scripts/ if a product is loaded, else scripts/.
  QString resolveScriptPath(const QString& scriptFile) const;
  // Start a ScriptWorker for the resolved script. Returns false if the
  // engine is unavailable or another job is already running.
  // captureDir (relative, e.g. "data/{product}/{serial}/{stamp}") is exposed
  // to the script as the Lua global POYNT_CAPTURE_DIR so it can save the SWIR
  // image package per measurement location; empty disables capture.
  bool launchScript(const QString& scriptFile, const QString& busyLabel,
                    const QString& captureDir = QString());
  // Background-jog helper: relative move on hex-right, then chain into
  // CLAUDE_step1_baseline.lua via launchScript() on success.
  void jogThenBaseline(const QString& axis, double signedDistance,
                       const QString& busyLabel);
  // Background absolute move on gantry-main to a saved named position.
  // No baseline chain. Used by Move to MOE-A1 / MOE-A2.
  void moveToNamedGantryPosition(const char* name);
  // Compute the per-run capture directory ("data/{product}/{serial}/{stamp}",
  // relative to the exe working dir) for a single measurement, or an empty
  // string if no product/DUT is available. Stamp is filesystem-safe UTC.
  QString buildCaptureDir() const;
  // When "Capture images" is checked, called after a successful single
  // measurement: the script has already saved the per-location SWIR image
  // packages under runDir/{location}/; this writes runDir/metadata.json with
  // the camera names + exposure, PSU V/I, and beam snapshots. UI thread.
  void writeCaptureMetadata(const QString& runDir);
  // Save current gantry-main position as the given named position. Always
  // shows a modal dialog with the delta vs. the existing saved value (or
  // "(no previous value)" when the name is unknown) and only writes on
  // confirmation. Same persistence flow as M4 Angle's Save MOE-A{1,2}.
  void saveCurrentToNamedGantryPosition(const char* name);
  // Repaint the step-display labels.
  void refreshTranslationStepLabel();
  void refreshRotationStepLabel();

  void appendLog(const QString& line, const QString& color = QString());

  ApplicationContext* m_context = nullptr;

  // Top-row action buttons.
  QPushButton* m_dutInitBtn   = nullptr;
  QPushButton* m_dutCloseBtn  = nullptr;
  QPushButton* m_automateBtn  = nullptr;
  QPushButton* m_automateV2Btn = nullptr;  // "Auto-correct v2" — Jacobian solver
  QPushButton* m_singleBtn    = nullptr;
  QPushButton* m_stopBtn      = nullptr;
  QCheckBox*   m_captureImagesCheck = nullptr;  // capture+save images on single measure
  QLabel*      m_busyLabel    = nullptr;

  // Translation group.
  QGroupBox*   m_translationGroup = nullptr;
  QLabel*      m_translationStepLabel = nullptr;     // shows "0.5 um"
  QPushButton* m_translationStepPlusBtn  = nullptr;  // "+0.5"
  QPushButton* m_translationStepMinusBtn = nullptr;  // "-0.5"
  QPushButton* m_xPlusBtn  = nullptr;  QPushButton* m_xMinusBtn = nullptr;
  QPushButton* m_yPlusBtn  = nullptr;  QPushButton* m_yMinusBtn = nullptr;
  QPushButton* m_zPlusBtn  = nullptr;  QPushButton* m_zMinusBtn = nullptr;

  // Rotation group.
  QGroupBox*   m_rotationGroup = nullptr;
  QLabel*      m_rotationStepLabel = nullptr;     // shows "10 millideg"
  QPushButton* m_rotationStepPlusBtn  = nullptr;  // "+10"
  QPushButton* m_rotationStepMinusBtn = nullptr;  // "-10"
  QPushButton* m_uPlusBtn  = nullptr;  QPushButton* m_uMinusBtn = nullptr;
  QPushButton* m_vPlusBtn  = nullptr;  QPushButton* m_vMinusBtn = nullptr;
  QPushButton* m_wPlusBtn  = nullptr;  QPushButton* m_wMinusBtn = nullptr;

  // Saved-position quick-move group.
  QGroupBox*   m_positionsGroup  = nullptr;
  QPushButton* m_moveToMoeA1Btn  = nullptr;
  QPushButton* m_moveToMoeA2Btn  = nullptr;
  QPushButton* m_saveMoeA1Btn    = nullptr;
  QPushButton* m_saveMoeA2Btn    = nullptr;

  QPlainTextEdit* m_log = nullptr;

  // M4AA serial-data panel embedded in the bottom half of this tab so the
  // operator can see live poynt-X/Y measurements while running scripts here.
  // The widget polls the active product's data/{product}/ folder on its own
  // 1 Hz timer, so we don't need to push anything to it.
  M4AASerialDataWidget* m_m4aaPanel = nullptr;

  // Step state.
  // Translation step is kept in um for display; converted to mm for motion.
  double m_translationStepUm     = 0.5;
  // Rotation step is kept in millideg for display; converted to deg for motion.
  double m_rotationStepMilliDeg  = 10.0;

  // Set when a single measurement is launched with "Capture images" checked;
  // consumed in onScriptCompleted to write the run's metadata.json.
  bool             m_captureOnComplete = false;
  // Capture directory for the in-flight single measurement (relative path);
  // shared with the script via POYNT_CAPTURE_DIR and reused for metadata.
  QString          m_captureRunDir;

  // Async-state: at most one script OR one move is in flight at any time.
  bool             m_busy = false;
  ScriptWorker*    m_scriptWorker = nullptr;
  // Background relative-move future (auto-cleaned via QMetaObject::invokeMethod).
  std::future<void> m_moveFuture;
};
