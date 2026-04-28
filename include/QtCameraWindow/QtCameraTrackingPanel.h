// QtCameraTrackingPanel.h
// Session-only device tracking overlay for the camera window:
//   1. User registers a gantry-frame zero point P0 from the live
//      gantry-main XY position.
//   2. User registers hex-left and hex-right "home" positions while at
//      reasonable XY offsets from P0.
//   3. As the hex devices are jogged, the panel transforms each hex's
//      local XYZ delta back to global gantry frame via
//      `transformation_matrix.json`, adds it to the registered offset,
//      and feeds the live (DX, DY) to the viewport overlay.
//   4. Distance hysteresis turns the registration buttons red when the
//      tracked device drifts past its alert threshold.
//
// Extracted from QtCameraWindow so the window class can stay focused on
// camera display + side-panel orchestration. Behaviour is preserved
// exactly:
//   * QSettings keys (`Tools[<instanceId>]/HexLeftAlertThreshold`, etc.)
//     are unchanged so saved thresholds survive the refactor.
//   * `transformation_matrix.json` is loaded from the same path.
//   * Polling at 10 Hz via internal QTimer.
#pragma once

#include <QGroupBox>
#include <QString>

#include <memory>
#include <string>

class QPushButton;
class QLabel;
class QTimer;
class UnifiedMotionManager;
class QtCameraViewport;
class TransformationMatrixLoader;

class QtCameraTrackingPanel : public QGroupBox
{
  Q_OBJECT

public:
  QtCameraTrackingPanel(QtCameraViewport* viewport,
                        std::string instanceId,
                        QWidget* parent = nullptr);
  ~QtCameraTrackingPanel() override;

  // The page wires the motion manager via dependency injection AFTER
  // the panel is constructed, so this setter must be reachable.
  void setMotionManager(UnifiedMotionManager* motionManager) {
    m_motionManager = motionManager;
  }

private slots:
  void onZeroP0Clicked();
  void onSetHexLeftClicked();
  void onSetHexRightClicked();
  void onClearTrackingClicked();
  void onTrackingTimerTick();
  void onSetHexLeftRightClicked();
  void onSetHexRightRightClicked();

private:
  void setupUI();
  bool getGantryMainXY(double& x, double& y) const;
  bool getHexLocalXYZ(const std::string& hexDevice,
                      double& lx, double& ly, double& lz) const;
  void updateStatusLabels();
  void applyHexButtonAlertStyle(QPushButton* btn, bool alert);
  void promptThresholdDialog(const QString& which, double& threshold);
  void loadSettings();
  void saveSettings();
  QString settingsKey(const QString& key) const;

  UnifiedMotionManager* m_motionManager = nullptr;
  QtCameraViewport* m_viewport = nullptr;
  std::string m_instanceId;
  std::unique_ptr<TransformationMatrixLoader> m_matrixLoader;
  QTimer* m_trackingTimer = nullptr;

  QPushButton* m_zeroP0Btn        = nullptr;
  QPushButton* m_setHexLeftBtn    = nullptr;
  QPushButton* m_setHexRightBtn   = nullptr;
  QPushButton* m_clearTrackingBtn = nullptr;
  QLabel*      m_p0StatusLabel       = nullptr;
  QLabel*      m_hexLeftStatusLabel  = nullptr;
  QLabel*      m_hexRightStatusLabel = nullptr;

  bool   m_p0Set = false;
  double m_p0X = 0.0, m_p0Y = 0.0;
  bool   m_hexLeftReg = false;
  double m_hexLeftRegDX = 0.0, m_hexLeftRegDY = 0.0;
  double m_hexLeftRegLocalX = 0.0, m_hexLeftRegLocalY = 0.0, m_hexLeftRegLocalZ = 0.0;
  bool   m_hexRightReg = false;
  double m_hexRightRegDX = 0.0, m_hexRightRegDY = 0.0;
  double m_hexRightRegLocalX = 0.0, m_hexRightRegLocalY = 0.0, m_hexRightRegLocalZ = 0.0;

  // Alert thresholds (Euclidean distance in mm). Persisted via QSettings.
  static constexpr double DEFAULT_HEX_ALERT_THRESHOLD = 0.005;
  double m_hexLeftAlertThreshold  = DEFAULT_HEX_ALERT_THRESHOLD;
  double m_hexRightAlertThreshold = DEFAULT_HEX_ALERT_THRESHOLD;
  bool   m_hexLeftAlertActive  = false;
  bool   m_hexRightAlertActive = false;
};
