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
#include <functional>
#include <future>
#include <utility>

class QLabel;
class QPushButton;
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
  void onMoveToMoeA1Nominal();   // a1 + (-1.50, 0, -0.15) mm
  void onApplyDeltaU();          // hex-right U -= delta_U at 0.1 vel
  void onApplyDeltaW();          // hex-right W -= delta_W at 0.1 vel
  void onSaveMoeA1();            // save gantry-main current pos as "MOE-A1" (tol-checked vs ma1)
  void onSaveMoeA2();            // save gantry-main current pos as "MOE-A2" (tol-checked vs ma2)
  void onSwirSet300us();         // swir_camera: exposure=300us, autoexposure=off
  void onSwirSetAutoExposure();  // swir_camera: autoexposure=on
  void onClearAll();

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
  // and hands it to runInBackground().
  void   applyHexRightCorrection(double deltaDeg, char axisLetter);
  // Save current gantry-main position as a named position (e.g. "MOE-A1"),
  // but only if it's within kMoeSaveToleranceMm of the supplied reference
  // (ma1 / ma2). Otherwise show a warning and refuse to save.
  void   saveMoeNamedPosition(const char* moeName,
                              const Point& referencePoint,
                              const char* referenceTag);

  // Generic "do work on a background thread, marshal result back to UI"
  // primitive shared by all motion actions in this widget. The worker
  // returns (ok, finalStatusMessage). While the task is in flight the
  // m_moveInFlight flag is set and every Move/Apply button is disabled;
  // both are restored on the UI thread when the task completes.
  using BackgroundTask = std::function<std::pair<bool, QString>()>;
  void   runInBackground(const QString& movingMsg, BackgroundTask task);
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
  QPushButton* m_moveToMoeA1NominalBtn = nullptr;
  QPushButton* m_applyDeltaUBtn = nullptr;
  QPushButton* m_applyDeltaWBtn = nullptr;
  QPushButton* m_saveMoeA1Btn = nullptr;
  QPushButton* m_saveMoeA2Btn = nullptr;

  QLabel* m_angleAXyLabel   = nullptr;
  QLabel* m_angleMaXyLabel  = nullptr;
  QLabel* m_deltaULabel     = nullptr;
  QLabel* m_rollAXzLabel    = nullptr;
  QLabel* m_rollMaXzLabel   = nullptr;
  QLabel* m_deltaWLabel     = nullptr;
  QLabel* m_advisoryLabel   = nullptr;
  QLabel* m_moveStatusLabel = nullptr;

  // Background-move state: a single move runs at a time; the future is kept
  // alive in this member so its destructor does not block the UI thread.
  std::future<void> m_moveFuture;
  bool              m_moveInFlight = false;
};
