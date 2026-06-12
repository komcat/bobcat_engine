// M4AngleWidget.h - M4 angle (U / roll W) delta calculator.
//
// Embedded as a tab ("M4 Angle") in the F1 Run Program page, next to the
// M4AA tab. The user jogs gantry-main to four reference points and clicks
// the matching Capture button:
//   a1, a2   -> baseline line (nominal 2500 um apart)
//   ma1, ma2 -> measured line
//
// Captured X/Y/Z values are taken straight from gantry-main and shown in mm.
// The widget displays:
//   - XY angle a1-a2   = atan2(y_a1 - y_a2, x_a1 - x_a2)
//   - XY angle ma1-ma2 = atan2(y_ma1 - y_ma2, x_ma1 - x_ma2)
//   - delta U          = angle(a1a2) - angle(ma1ma2)         -> adjust hex-right U
//   - roll a1-a2       = atan2(z_a2 - z_a1, |a1-a2| in XY)
//   - roll ma1-ma2     = atan2(z_ma2 - z_ma1, |ma1-ma2| in XY)
//   - delta W          = roll(a1a2) - roll(ma1ma2)           -> adjust hex-right W
//
// The roll divisor is the actual XY distance between the two captured points
// (in mm), not a fixed nominal baseline. All angles in degrees. Correction is
// applied manually for now; the widget is calculation-only.
#pragma once
#include <QWidget>
#include <atomic>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class QLabel;
class QPushButton;
class QButtonGroup;
class QCheckBox;
class QHBoxLayout;
class QTimer;
class QtCameraWindow;
struct ApplicationContext;
struct PositionStruct;

class M4AngleWidget : public QWidget {
  Q_OBJECT
public:
  explicit M4AngleWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  // Re-sync from saved positions when the tab becomes visible IF nothing has
  // been populated yet (covers the case where the widget is constructed
  // before any product is loaded).
  void showEvent(QShowEvent* event) override;

private slots:
  void onCaptureA1();
  void onCaptureA2();
  void onCaptureMa1();
  void onCaptureMa2();
  void onMoveToA1();
  void onMoveToA2();
  void onMoveToMa1();
  void onMoveToMa2();
  void onMoveToMoeA1Nominal();   // a1 + (-1.50, 0, -0.15) mm; auto-captures ma1 on arrival
  void onMoveYPlus35();          // gantry-main relative Y +3.25 mm
  void onMoveYMinus35();         // gantry-main relative Y -3.25 mm
  void onApplyDeltaU();          // hex-right U -= delta_U at 0.1 vel
  void onApplyDeltaW();          // hex-right W -= delta_W at 0.1 vel
  void onSaveMoeA1();            // save gantry-main current pos as "MOE-A1" (tol-checked vs ma1)
  void onSaveMoeA2();            // save gantry-main current pos as "MOE-A2" (tol-checked vs ma2)
  void onSwirSet300us();         // swir_camera: exposure=300us, autoexposure=off
  void onSwirSetAutoExposure();  // swir_camera: autoexposure=on
  void onToggleBeamStream();     // enable/disable beam streaming on swir_camera
  // Keepalive tick: while auto-connect is desired, ensure the swir_camera beam
  // stream is enabled (the controller's worker thread does the actual connect +
  // reconnect, so this never blocks the UI). Also keeps the button label synced.
  void onBeamAutoTick();
  void onClearAll();

  // --- SWIR-source power-supply controls (mirrors the SWIR monitor window) ---
  void onPsuOutputToggle();      // connect+default / toggle output on selected PSU
  void onPsuCurrentStep(double deltaMa);  // adjust ch1 set-current by +/- deltaMa
  void onPsuTabSelected(int index);       // switch active PSU via its tab button
  void pollSelectedPsu();        // background-read Set/Voltage/Current of selected PSU

  // --- Auto-correction wizard --------------------------------------------------
  void onAutoStart();            // begin the guided correction sequence
  void onAutoCapture();          // operator confirm/capture for the current step
  void onAutoAbort();            // cancel the sequence and restore manual UI

private:
  struct Point {
    bool   captured = false;
    double x = 0.0, y = 0.0, z = 0.0;
  };

  void   setupUI();
  // Pull previously-saved named positions (a1/a2/ma1/ma2 on gantry-main) into
  // the in-memory Points so Move-to buttons work immediately on startup,
  // without requiring a fresh capture this session.
  void   loadFromSavedPositions();
  // Populate one row from a PositionStruct (used by loadFromSavedPositions).
  void   adoptPoint(Point& dst, QLabel* readout, QPushButton* moveBtn,
                    const PositionStruct& src);
  bool   capture(Point& dst, QLabel* readout, QPushButton* moveBtn, const char* tag);
  void   moveTo(const Point& src, const QString& tag);
  // Shared background-move primitive used by both the per-point "Move to *"
  // buttons and the MOE-A1 nominal button. Builds a task closure that does the
  // gantry-main MoveToAbs6 and hands it to runInBackground().
  void   startBackgroundMove(const PositionStruct& target,
                             const QString& movingMsg,
                             const QString& arrivedOkMsg,
                             const QString& arrivedFailMsg);
  void   setMoveButtonsEnabledForBusy(bool busy);   // disable all while a move is in flight
  void   refreshMoveButtonsFromCaptureState();      // re-enable based on captured flags
  void   refreshApplyButtonsEnabledState();         // enable Apply U/W only when delta is valid
  // Background hex-right axis correction: reads current hex-right position,
  // sets the axis to (current - delta), at system velocity 0.1, then restores
  // the previous velocity. axisLetter is 'U' or 'W'. Builds a task closure
  // and hands it to runInBackground(). onComplete (if set) runs on the UI
  // thread after the correction finishes (used by the auto-correction driver).
  void   applyHexRightCorrection(double deltaDeg, char axisLetter,
                                 std::function<void(bool)> onComplete = nullptr);
  // Save current gantry-main position as a named position (e.g. "MOE-A1").
  // When enforceTolerance is true (manual Save MOE-A1/A2 buttons), only saves
  // if the current pos is within kMoeSaveToleranceMm of the supplied reference
  // (ma1 / ma2), otherwise warns and refuses. When false (auto focus phase),
  // saves the current focused position unconditionally.
  void   saveMoeNamedPosition(const char* moeName,
                              const Point& referencePoint,
                              const char* referenceTag,
                              bool enforceTolerance = true);
  // Auto-detect the Qt camera monitor whose beam stream the M4 button should
  // drive: prefer one already streaming (so toggle-off always finds it), else
  // whichever window currently targets swir_camera (primary or secondary).
  // Returns nullptr if no candidate window exists.
  QtCameraWindow* findSwirBeamWindow() const;
  // Update the beam-stream button label/colour to reflect the given state.
  void   updateBeamStreamButton(bool on);

  // --- SWIR-source PSU helpers ------------------------------------------------
  // Populate the PSU selector from the PowerSupplyManager the first time any
  // device is registered, and kick a background auto-connect (same behaviour as
  // the SWIR monitor window). No-op once discovered.
  void   discoverPsuDevices();
  // (Re)build the row of PSU tab buttons from m_psuNames (one tab per device,
  // first one selected). Cheap; only called when the device list is discovered.
  void   rebuildPsuTabs();
  // Currently-selected PSU device name (empty if none / not discovered yet).
  std::string selectedPsuName() const;
  // Enable/disable the PSU buttons while a PSU action is in flight.
  void   setPsuControlsEnabled(bool enabled);
  // Refresh the Set/Voltage/Current labels and the output button from a reading
  // (runs on the UI thread). When disconnected, shows placeholders.
  void   updatePsuReadouts(bool connected, double voltageV, double currentA,
                           double setCurrentA, bool outputOn);
  void   updatePsuOutputButton(bool connected, bool outputOn);

  // --- Auto-correction wizard helpers -----------------------------------------
  // Ordered steps of the guided correction sequence (see M4AngleWidget.h header
  // comment / the per-step handling in autoEnterStep()).
  enum class AutoStep {
    Idle,
    TeachA1, TeachA2, TeachMa1, TeachMa2,   // operator-paced: move then wait for Capture
    ApplyU, ApplyW, SetExposure,            // automatic: run then advance
    FocusMoeA1, FocusMoeA2,                 // operator-paced: focus then save MOE
    Done
  };
  // Perform the automatic part of a step (move / apply / exposure) and, for
  // operator-paced steps, arm m_autoAwaitingCapture once the move settles.
  void   autoEnterStep(AutoStep step);
  // Drive gantry-main to target in the background; on arrival (if still active)
  // arm the operator capture with the given instruction. On failure, abort.
  void   autoMoveThenAwait(const PositionStruct& target, const QString& movingMsg,
                           const QString& awaitInstruction);
  // Advance from the current step to the next and enter it.
  void   autoAdvance();
  // Update the step chips + instruction label for the given step.
  void   updateAutoUi(AutoStep step, const QString& instruction);
  // Lock (true) / unlock (false) the manual controls while the wizard runs.
  void   setAutoModeUiLock(bool locked);
  // Capture a SWIR frame to <product>/m4_thumbnails/thumbnail_<tag>.bmp, honouring
  // the Overwrite checkbox (skips when unchecked and a file already exists).
  void   saveThumbnail(const char* tag);
  // Load <product>/m4_thumbnails/thumbnail_<tag>.bmp into the thumbnail box
  // (placeholder text when missing / no product).
  void   loadThumbnailForStep(const char* tag);
  // Show the most recently saved thumbnail (any of a1/a2/ma1/ma2) for the loaded
  // product, so the box isn't blank when the tab opens while the wizard is idle.
  void   loadLatestThumbnail();
  // Absolute path of the thumbnail directory for the loaded product ("" if none).
  std::string thumbnailDir() const;

  // --- Miniature gantry-main jog panel ----------------------------------------
  // Build the compact jog control (step-size selector + X/Y/Z arrow pads) shown
  // beside the computed deltas. Wires the member buttons and returns the panel.
  QWidget* buildGantryJogPanel();
  // Relative move of gantry-main on a single axis ("X"/"Y"/"Z") by deltaMm,
  // routed through runInBackground (so it respects the single-move-in-flight
  // guard). Stays usable during the auto-correction wizard.
  void   gantryJog(const std::string& axis, double deltaMm);
  // Highlight the active step-size button and update m_jogStepUm.
  void   selectJogStep(double stepUm);
  // Enable/disable the jog arrow pads (called from setMoveButtonsEnabledForBusy).
  void   setJogButtonsEnabled(bool enabled);

  // Generic "do work on a background thread, marshal result back to UI"
  // primitive shared by all motion actions in this widget. The worker
  // returns (ok, finalStatusMessage). While the task is in flight the
  // m_moveInFlight flag is set and every Move/Apply button is disabled;
  // both are restored on the UI thread when the task completes. onComplete, if
  // supplied, runs on the UI thread after the status update (used by the
  // auto-correction driver to chain to the next step).
  using BackgroundTask = std::function<std::pair<bool, QString>()>;
  void   runInBackground(const QString& movingMsg, BackgroundTask task,
                         std::function<void(bool)> onComplete = nullptr);
  void   recompute();
  static double lineAngleDegXY(const Point& p1, const Point& p2);
  static double lineRollDeg(const Point& p1, const Point& p2);

  ApplicationContext* m_context = nullptr;

  Point m_a1, m_a2, m_ma1, m_ma2;

  QLabel* m_a1Readout  = nullptr;
  QLabel* m_a2Readout  = nullptr;
  QLabel* m_ma1Readout = nullptr;
  QLabel* m_ma2Readout = nullptr;

  QPushButton* m_a1MoveBtn  = nullptr;
  QPushButton* m_a2MoveBtn  = nullptr;
  QPushButton* m_ma1MoveBtn = nullptr;
  QPushButton* m_ma2MoveBtn = nullptr;
  QPushButton* m_captureMa1Btn = nullptr;  // permanently disabled; ma1 is captured automatically after Move-to-MOE-A1-nominal
  QPushButton* m_yPlus35Btn  = nullptr;
  QPushButton* m_yMinus35Btn = nullptr;
  QPushButton* m_moveToMoeA1NominalBtn = nullptr;
  QPushButton* m_applyDeltaUBtn = nullptr;
  QPushButton* m_applyDeltaWBtn = nullptr;
  QPushButton* m_saveMoeA1Btn = nullptr;
  QPushButton* m_saveMoeA2Btn = nullptr;
  QPushButton* m_beamStreamBtn = nullptr;

  // Auto-connect the SWIR beam stream when the M4 tab is activated, and retain
  // it (reconnect) if the beam server restarts. m_beamAutoDesired is set true on
  // the first M4-tab activation and cleared only by a manual "Beam stream: OFF";
  // m_beamAutoTimer re-checks periodically so a SWIR monitor opened later still
  // gets streamed. The connect itself runs on the controller's worker thread.
  QTimer* m_beamAutoTimer   = nullptr;
  bool    m_beamAutoDesired = false;

  // SWIR-source PSU controls (channel 1) — minimal mirror of the SWIR monitor.
  // The device selector is rendered as a row of tab buttons (cyan = active)
  // instead of a combo box; the active device is tracked by index.
  std::vector<std::string>  m_psuNames;          // discovered device names (tab order)
  std::vector<QPushButton*> m_psuTabs;           // one tab button per device
  QButtonGroup* m_psuTabGroup    = nullptr;      // exclusive group over the tabs
  QHBoxLayout*  m_psuTabLayout   = nullptr;      // layout the tab buttons live in
  int           m_psuSelectedIndex = -1;         // active device index (-1 = none)
  QPushButton* m_psuOutputBtn   = nullptr;       // full-width On/Off bar
  QPushButton* m_psuPlus10Btn   = nullptr;
  QPushButton* m_psuMinus10Btn  = nullptr;
  QPushButton* m_psuPlus5Btn    = nullptr;
  QPushButton* m_psuMinus5Btn   = nullptr;
  QLabel*      m_psuSetLabel     = nullptr;
  QLabel*      m_psuVoltageLabel = nullptr;
  QLabel*      m_psuCurrentLabel = nullptr;
  QTimer*      m_psuPollTimer    = nullptr;
  bool         m_psuDiscovered   = false;
  // PSU work runs off the UI thread. Actions (set-current / output) and the
  // periodic poll each have their own future + in-flight guard; a single
  // hardware mutex serialises the actual VISA calls so an action and a poll
  // never touch the device concurrently.
  std::future<void> m_psuActionFuture;
  std::future<void> m_psuPollFuture;
  std::future<void> m_psuConnectFuture;
  std::atomic<bool> m_psuActionInFlight{false};
  std::atomic<bool> m_psuPollInFlight{false};
  mutable std::mutex m_psuHwMutex;

  QLabel* m_angleAXyLabel   = nullptr;
  QLabel* m_angleMaXyLabel  = nullptr;
  QLabel* m_deltaULabel     = nullptr;
  QLabel* m_rollAXzLabel    = nullptr;
  QLabel* m_rollMaXzLabel   = nullptr;
  QLabel* m_deltaWLabel     = nullptr;
  QLabel* m_advisoryLabel   = nullptr;
  QLabel* m_moveStatusLabel = nullptr;

  // Auto-correction wizard UI + state.
  QPushButton* m_autoStartBtn        = nullptr;
  QPushButton* m_autoCaptureBtn      = nullptr;
  QPushButton* m_autoAbortBtn        = nullptr;
  QLabel*      m_autoInstructionLabel = nullptr;
  QLabel*      m_autoThumbnailLabel   = nullptr;
  QCheckBox*   m_autoOverwriteChk     = nullptr;
  QLabel*      m_autoStepChips[4]     = {nullptr, nullptr, nullptr, nullptr};  // A1, A2, MA1, MA2
  AutoStep     m_autoStep             = AutoStep::Idle;
  bool         m_autoActive           = false;
  bool         m_autoAwaitingCapture  = false;
  // Group boxes locked (disabled) while the wizard owns the sequence. Stored as
  // QWidget* so the header needs no QGroupBox include. (The computed-deltas group
  // is deliberately not locked — the jog panel beside it must stay usable.)
  QWidget*     m_captureGroupBox = nullptr;
  QWidget*     m_psuGroupBox     = nullptr;

  // Miniature gantry-main jog panel (beside the computed deltas). Stays enabled
  // during the wizard so the operator can fine-tune / focus gantry-main.
  double       m_jogStepUm = 2.0;                 // active step size in microns
  QPushButton* m_jogStepBtns[3] = {nullptr, nullptr, nullptr};  // 2um / 5um / 10um
  QPushButton* m_jogXPlusBtn  = nullptr;
  QPushButton* m_jogXMinusBtn = nullptr;
  QPushButton* m_jogYPlusBtn  = nullptr;
  QPushButton* m_jogYMinusBtn = nullptr;
  QPushButton* m_jogZPlusBtn  = nullptr;
  QPushButton* m_jogZMinusBtn = nullptr;

  // Background-move state: a single move runs at a time; the future is kept
  // alive in this member so its destructor does not block the UI thread.
  std::future<void> m_moveFuture;
  bool              m_moveInFlight = false;
  // Optional continuation invoked on the UI thread after the current background
  // task finishes (set by runInBackground, consumed once).
  std::function<void(bool)> m_moveOnComplete;
};
