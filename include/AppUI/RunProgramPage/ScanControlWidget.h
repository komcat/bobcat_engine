// ScanControlWidget.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>
#include <QGroupBox>

/**
 * @brief Pure UI widget for hexapod scanning controls
 *
 * Provides a clean interface for:
 * - Selecting data channel
 * - Displaying current channel value
 * - Triggering coarse/fine translation scans on left/right devices
 * - Triggering rotation optimization scans
 * - Stopping active scans
 *
 * Emits signals when user requests a scan - does NOT contain business logic
 */
class ScanControlWidget : public QWidget
{
  Q_OBJECT

public:
  explicit ScanControlWidget(QWidget* parent = nullptr);
  ~ScanControlWidget() = default;

  // Configuration
  void setAvailableChannels(const QStringList& channels);
  QString currentChannel() const;
  void setCurrentChannel(const QString& channel);

  // Value display
  void updateChannelValue(double value, const QString& unit = "");
  void setDisplayText(const QString& text);

  // Enable/disable controls
  void setEnabled(bool enabled);
  void setScanButtonsEnabled(bool enabled);
  void setStopButtonEnabled(bool enabled);

signals:
  /**
   * @brief Emitted when user requests a translation scan
   * @param deviceName Device to scan (e.g. "hex-left", "hex-right")
   * @param isCoarse True for coarse scan, false for fine scan
   * @param channel Selected data channel
   */
  void scanRequested(QString deviceName, bool isCoarse, QString channel);

  /**
   * @brief Emitted when user requests a rotation scan
   * @param deviceName Device to scan (e.g. "hex-right")
   * @param rotationAxis Rotation axis ("U", "V", or "W")
   * @param rotationStep Rotation step in degrees
   * @param threshold Improvement threshold (0.01 = 1%)
   * @param maxNoImprovement Max consecutive no-improvement steps
   * @param channel Selected data channel
   */
  void rotationScanRequested(QString deviceName, QString rotationAxis,
                              double rotationStep, double threshold,
                              int maxNoImprovement, QString channel);

  /**
   * @brief Emitted when user clicks stop button
   */
  void stopRequested();

  /**
   * @brief Emitted when user changes the selected channel
   * @param channel New channel name
   */
  void channelChanged(QString channel);

private slots:
  void onLeftCoarseClicked();
  void onLeftFineClicked();
  void onRightCoarseClicked();
  void onRightFineClicked();
  void onStopClicked();
  void onRotationScanClicked();
  void onRotationGroupToggled(bool checked);

private:
  void setupUI();
  void setupRotationUI(QVBoxLayout* parentLayout);
  void emitScanRequest(const QString& deviceName, bool isCoarse);
  void emitRotationScanRequest();

  // UI Components - Translation Scanning
  QComboBox* m_channelCombo;
  QLabel* m_valueLabel;
  QPushButton* m_leftCoarseButton;
  QPushButton* m_leftFineButton;
  QPushButton* m_rightCoarseButton;
  QPushButton* m_rightFineButton;
  QPushButton* m_stopButton;

  // UI Components - Rotation Scanning
  QGroupBox* m_rotationGroup;
  QWidget* m_rotationContent;
  QComboBox* m_rotationDeviceCombo;
  QComboBox* m_rotationAxisCombo;
  QDoubleSpinBox* m_rotationStepSpin;
  QDoubleSpinBox* m_thresholdSpin;
  QSpinBox* m_maxNoImprovementSpin;
  QPushButton* m_rotationScanButton;
};