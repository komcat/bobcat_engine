// ScanControlWidget.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
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
 * - Stopping active scans
 *
 * Emits signals when user requests a scan - does NOT contain business logic.
 */
class ScanControlWidget : public QWidget
{
  Q_OBJECT

public:
  // Scan mode enumeration
  enum class ScanMode { Coarse, Fine, UltraFine };

  explicit ScanControlWidget(QWidget* parent = nullptr);
  ~ScanControlWidget() = default;

  // Configuration
  void setAvailableChannels(const QStringList& channels);
  QString currentChannel() const;
  void setCurrentChannel(const QString& channel);

  // Value display
  void updateChannelValue(double value, const QString& unit = "");
  void setDisplayText(const QString& text);

  /**
   * @brief Show the selected channel's publish rate
   * @param hz Measured rate; pass a negative value for "unknown"
   *
   * This is the DataStore publish rate, not the UI refresh rate. The scan's
   * fresh-sample gate waits on new publishes, so this number sets the floor
   * on how fast a scan can step.
   */
  void setDataRate(double hz);

  // Enable/disable controls
  void setEnabled(bool enabled);
  void setScanButtonsEnabled(bool enabled);
  void setStopButtonEnabled(bool enabled);

signals:
  /**
   * @brief Emitted when user requests a translation scan
   * @param deviceName Device to scan (e.g. "hex-left", "hex-right")
   * @param mode Scan mode (Coarse, Fine, or UltraFine)
   * @param channel Selected data channel
   */
  void scanRequested(QString deviceName, ScanControlWidget::ScanMode mode, QString channel);

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
  void onLeftUltraFineClicked();
  void onRightCoarseClicked();
  void onRightFineClicked();
  void onRightUltraFineClicked();
  void onStopClicked();

private:
  void setupUI();
  void emitScanRequest(const QString& deviceName, ScanMode mode);

  // UI Components - Translation Scanning
  QComboBox* m_channelCombo;
  QLabel* m_valueLabel;
  QLabel* m_rateLabel;
  QPushButton* m_leftCoarseButton;
  QPushButton* m_leftFineButton;
  QPushButton* m_leftUltraFineButton;
  QPushButton* m_rightCoarseButton;
  QPushButton* m_rightFineButton;
  QPushButton* m_rightUltraFineButton;
  QPushButton* m_stopButton;
};