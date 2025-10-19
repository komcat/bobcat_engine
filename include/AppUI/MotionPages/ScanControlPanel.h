// ScanControlPanel.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QGroupBox>
#include <QVBoxLayout>

/**
 * @brief Right control panel for scanning operations
 * - Device selection
 * - Preset selection (Coarse/Fine)
 * - Data channel selection
 * - Start/Stop scan buttons
 * - Status display
 */
class ScanControlPanel : public QWidget
{
  Q_OBJECT

public:
  explicit ScanControlPanel(QWidget* parent = nullptr);
  ~ScanControlPanel() = default;

  // Update UI with device list
  void setDevices(const QStringList& devices);

  // Update data channels list
  void setDataChannels(const QStringList& channels);

  // Get current selections
  QString getCurrentDevice() const;
  QString getCurrentPreset() const;
  QString getCurrentDataChannel() const;

  // Update status display
  void setStatus(const QString& status);
  void setProgress(double progress);  // 0.0 to 1.0

  // Enable/disable controls during scan
  void setScanActive(bool active);

signals:
  void deviceChanged(const QString& deviceName);
  void presetChanged(const QString& preset);
  void dataChannelChanged(const QString& channel);
  void startScanClicked();
  void stopScanClicked();

private:
  void setupUI();

  // Widgets
  QComboBox* m_deviceCombo;
  QComboBox* m_presetCombo;
  QComboBox* m_dataChannelCombo;

  QPushButton* m_startScanButton;
  QPushButton* m_stopScanButton;

  QLabel* m_statusLabel;
  QLabel* m_progressLabel;
};