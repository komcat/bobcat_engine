// ScanControlWidget.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QStringList>

/**
 * @brief Pure UI widget for hexapod scanning controls
 *
 * Provides a clean interface for:
 * - Selecting data channel
 * - Displaying current channel value
 * - Triggering coarse/fine scans on left/right devices
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
   * @brief Emitted when user requests a scan
   * @param deviceName Device to scan (e.g. "hex-left", "hex-right")
   * @param isCoarse True for coarse scan, false for fine scan
   * @param channel Selected data channel
   */
  void scanRequested(QString deviceName, bool isCoarse, QString channel);

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

private:
  void setupUI();
  void emitScanRequest(const QString& deviceName, bool isCoarse);

  // UI Components
  QComboBox* m_channelCombo;
  QLabel* m_valueLabel;
  QPushButton* m_leftCoarseButton;
  QPushButton* m_leftFineButton;
  QPushButton* m_rightCoarseButton;
  QPushButton* m_rightFineButton;
  QPushButton* m_stopButton;
};