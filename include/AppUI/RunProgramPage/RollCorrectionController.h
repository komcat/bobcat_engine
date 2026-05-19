// RollCorrectionController.h
// Owns the business logic behind the Run Program page's "Roll Correction"
// tab: hexapod W-axis roll correction. It drives the RollCorrectionWidget,
// runs the move/correction/scan worker threads, and owns the SequenceContext
// stop flags and the running state.
//
// The page keeps the RollCorrectionWidget itself (it's a tab body) and feeds
// it the DataStore channel list; this controller only coordinates "user
// clicks a button -> worker runs -> finishes -> log result".
//
// The page provides via Deps:
//   - dialogParent: parent widget for any dialogs (currently unused, kept for
//                   parity with ScanController)
//   - context:      managers needed by the workers (motion, sequence engine,
//                   datastore)
//   - widget:       the RollCorrectionWidget the controller should drive
//   - appendLog:    callback for surfacing worker messages in the page's
//                   execution log pane
#pragma once

#include "RollCorrectionWidget.h"

#include <QObject>
#include <QString>
#include <functional>
#include <memory>

struct ApplicationContext;
class SequenceContext;
class QWidget;

class RollCorrectionController : public QObject {
  Q_OBJECT
public:
  struct Deps {
    QWidget*               dialogParent = nullptr;
    ApplicationContext*    context      = nullptr;
    RollCorrectionWidget*  widget       = nullptr;
    std::function<void(const QString&)> appendLog;
  };

  explicit RollCorrectionController(Deps deps, QObject* parent = nullptr);
  ~RollCorrectionController() override;

private slots:
  void onRegisterCH1Requested(QString deviceName);
  void onRegisterCH2Requested(QString deviceName);
  void onMoveToCH1Requested(QString deviceName);
  void onMoveToCH2Requested(QString deviceName);
  void onCorrectWRequested(QString deviceName, QString channel,
                           double angleDeg, bool positiveW, double halfwayXmm);
  void onFineGridScanRequested(QString deviceName);
  void onStopGridScanRequested();
  void onGridScanFinished(bool success, QString deviceName, QString errorMessage);
  void onRollCorrectionFinished(bool success, QString deviceName, QString message);
  void onRollCorrectionClearRequested(QString deviceName);

private:
  void log(const QString& msg);

  Deps m_deps;
  std::unique_ptr<SequenceContext> m_correctionContext;  // stepwise W correction stop control
  std::unique_ptr<SequenceContext> m_gridScanContext;    // fine grid scan stop control
  bool m_correctionRunning = false;
};
