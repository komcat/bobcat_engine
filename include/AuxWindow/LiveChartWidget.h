// LiveChartWidget.h
#pragma once

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>
#include <deque>
#include <string>
#include <map>

class DataStore;
class TcpClientManager;

/**
 * @brief Reusable real-time chart widget with channel selector
 *
 * Features:
 * - Dropdown to select which channel to display
 * - Real-time chart updates (throttled at ~60 FPS)
 * - Circular buffer (last 1000 points)
 * - 10-second time window
 * - Auto-scaled Y-axis with SI prefixes
 *
 * Usage:
 *   LiveChartWidget* chart = new LiveChartWidget(dataStore, tcpManager);
 *   // User selects channel from dropdown
 */
class LiveChartWidget : public QWidget
{
  Q_OBJECT

public:
  explicit LiveChartWidget(DataStore* dataStore, TcpClientManager* tcpManager, QWidget* parent = nullptr);
  ~LiveChartWidget();

  // Get currently selected channel
  std::string getCurrentChannel() const;

  // Set which channel to display
  void setChannel(const std::string& channelId);

signals:
  void channelChanged(const std::string& channelId);

private slots:
  void onDataReceived(const std::string& clientId, float value, qint64 timestamp);
  void onChannelSelected(int index);
  void onUpdateChart();
  void onRefreshChannels();
  void onUpdateValueLabel();
  void onSetTarget();
  void onClearTarget();

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  void setupUI();
  void updateChart();
  void refreshChannelList();
  void updateValueFontSize();
  QColor getValueColor(float value) const;
  QColor getPercentageColor(float percentage) const;
  QString formatValueWithUnit(float value, const std::string& unit) const;
  void loadTargets();
  void saveTargets();
  void updateTargetButtonStates();

  struct DataPoint {
    qint64 timestamp;
    float value;
  };

  DataStore* m_dataStore;
  TcpClientManager* m_tcpManager;

  // UI Components
  QComboBox* m_channelSelector;
  QPushButton* m_refreshButton;
  QPushButton* m_setTargetButton;
  QPushButton* m_clearTargetButton;
  QLabel* m_valueLabel;
  QLabel* m_unitLabel;  // Separate label for unit
  QLabel* m_percentageLabel;  // Shows percentage when target is set
  QLabel* m_channelInfoLabel;
  QChartView* m_chartView;
  QChart* m_chart;
  QLineSeries* m_series;
  QValueAxis* m_axisX;
  QValueAxis* m_axisY;

  // Data
  std::string m_currentChannelId;
  std::string m_currentClientId;
  std::string m_currentUnit;
  std::deque<DataPoint> m_dataBuffer;

  // Target values (channel ID -> target value)
  std::map<std::string, float> m_targetValues;
  static constexpr const char* TARGET_CONFIG_FILE = "config/live_chart_targets.json";

  QTimer* m_chartUpdateTimer;
  QTimer* m_valueLabelUpdateTimer;
  bool m_isDirty;
  float m_latestValue;
  bool m_hasNewValue;

  static constexpr size_t MAX_POINTS = 2000;  // Support up to 200 Hz for 10 seconds
  static constexpr qint64 TIME_WINDOW_MS = 5000;  // 5 seconds display window
  static constexpr int CHART_UPDATE_INTERVAL_MS = 100;  // 10 Hz
  static constexpr int VALUE_LABEL_UPDATE_INTERVAL_MS = 100;  // 10 Hz (reduced throttle)

  // Y-axis hard limits to prevent unrealistic spike rescaling
  // Increased limits to support multi-channel data with various ranges (µA, mA, V, etc.)
  static constexpr float Y_AXIS_MAX_LIMIT = 1000.0f;  // Support up to 1000 in any unit
  static constexpr float Y_AXIS_MIN_LIMIT = -1000.0f; // Support down to -1000 in any unit
};