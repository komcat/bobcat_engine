// LiveChartWidget.h
#pragma once

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>
#include <deque>
#include <string>

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

private:
  void setupUI();
  void updateChart();
  void refreshChannelList();
  QString formatValueWithUnit(float value, const std::string& unit) const;

  struct DataPoint {
    qint64 timestamp;
    float value;
  };

  DataStore* m_dataStore;
  TcpClientManager* m_tcpManager;

  // UI Components
  QComboBox* m_channelSelector;
  QLabel* m_valueLabel;
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

  QTimer* m_chartUpdateTimer;
  QTimer* m_channelRefreshTimer;
  bool m_isDirty;

  static constexpr size_t MAX_POINTS = 300;
  static constexpr qint64 TIME_WINDOW_MS = 5000;  // 10 seconds
  static constexpr int CHART_UPDATE_INTERVAL_MS = 16;  // ~60 FPS
};