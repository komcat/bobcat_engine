// RollCorrectionWidget.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QGroupBox>

/**
 * @brief Pure UI widget for roll correction angle calculation
 *
 * Provides interface for:
 * - Registering CH1/CH2 peak positions for hex-left and hex-right
 * - Displaying registered position data (X and full XYZUVW)
 * - Setting fiber spacing parameter
 * - Showing computed roll angle and correction direction
 *
 * Emits signals when user clicks register buttons - does NOT contain business logic.
 */
class RollCorrectionWidget : public QWidget
{
  Q_OBJECT

public:
  explicit RollCorrectionWidget(QWidget* parent = nullptr);
  ~RollCorrectionWidget() = default;

  // Data setters (called by RunProgramPage after capturing position)
  void setCH1Position(const QString& deviceName, double x, double y, double z,
                      double u, double v, double w);
  void setCH2Position(const QString& deviceName, double x, double y, double z,
                      double u, double v, double w);

  // Clear registered data for a device
  void clearPositions(const QString& deviceName);

  // Get spacing value in microns
  double spacing() const;

  // Scanning state control
  void setScanRunning(bool running);

  // Position getters for move-to functionality (returns stored XYZUVW)
  struct StoredPosition {
    bool valid = false;
    double x = 0.0, y = 0.0, z = 0.0, u = 0.0, v = 0.0, w = 0.0;
  };
  StoredPosition getCH1Position(const QString& deviceName) const;
  StoredPosition getCH2Position(const QString& deviceName) const;

signals:
  /** Emitted when user clicks "Register CH1 Peak" for a device */
  void registerCH1Requested(QString deviceName);

  /** Emitted when user clicks "Register CH2 Peak" for a device */
  void registerCH2Requested(QString deviceName);

  /** Emitted when user clicks "Move to CH1" for a device */
  void moveToCH1Requested(QString deviceName);

  /** Emitted when user clicks "Move to CH2" for a device */
  void moveToCH2Requested(QString deviceName);

  /** Emitted when user clicks "Correct W" - moves to CH1 peak, applies halfway X + W correction */
  void correctWRequested(QString deviceName, double angleDeg, bool positiveW, double halfwayXmm);

  /** Emitted when user clicks "Fine Grid Scan" for a device */
  void fineGridScanRequested(QString deviceName);

  /** Emitted when user clicks "Stop Scan" */
  void stopScanRequested();

  /** Emitted when user clicks "Clear" for a device */
  void clearRequested(QString deviceName);

private:
  void setupUI();
  QGroupBox* createDeviceGroup(const QString& deviceName, const QString& displayName);
  void computeAndDisplayResults(const QString& deviceName);

  // Shared spacing input
  QLineEdit* m_spacingInput;
  QPushButton* m_stopScanButton = nullptr;

  // Per-device UI elements
  struct DeviceUI {
    QGroupBox* group = nullptr;
    QPushButton* registerCH1Button = nullptr;
    QPushButton* moveToCH1Button = nullptr;
    QPushButton* registerCH2Button = nullptr;
    QPushButton* moveToCH2Button = nullptr;
    QPushButton* correctWButton = nullptr;
    QPushButton* fineGridScanButton = nullptr;
    QPushButton* clearButton = nullptr;
    QLabel* ch1PositionLabel = nullptr;
    QLabel* ch1FullPosLabel = nullptr;
    QLabel* ch2PositionLabel = nullptr;
    QLabel* ch2FullPosLabel = nullptr;
    QTextEdit* resultDisplay = nullptr;

    // Stored position data
    bool ch1Registered = false;
    bool ch2Registered = false;
    double ch1X = 0.0, ch1Y = 0.0, ch1Z = 0.0, ch1U = 0.0, ch1V = 0.0, ch1W = 0.0;
    double ch2X = 0.0, ch2Y = 0.0, ch2Z = 0.0, ch2U = 0.0, ch2V = 0.0, ch2W = 0.0;

    // Computed correction values (valid when both registered)
    double correctionAngleDeg = 0.0;
    bool correctionPositiveW = false;
    double correctionHalfwayXmm = 0.0;
  };

  DeviceUI m_leftDevice;
  DeviceUI m_rightDevice;

  DeviceUI& getDeviceUI(const QString& deviceName);
  const DeviceUI& getDeviceUI(const QString& deviceName) const;
};
