// DataStoreWidget.h - Real-time chart view for all DataStore channels
#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTimer>

#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>
#include <deque>
#include <map>
#include <set>
#include <string>

struct ApplicationContext;
class DataStore;
class TcpClientManager;

/**
 * @brief Widget displaying real-time charts for all DataStore channels
 * - Automatically detects channels from DataStore
 * - Real-time chart updates via TcpClientManager signals
 * - Circular buffer (last 1000 points per channel)
 * - Throttled rendering at ~30 FPS for smooth performance
 */
class DataStoreWidget : public QWidget {
  Q_OBJECT

public:
  explicit DataStoreWidget(ApplicationContext* context, QWidget* parent = nullptr);
  ~DataStoreWidget();

private slots:
  void onDataReceived(const std::string& clientId, float value, qint64 timestamp);
  void onDataStoreValueChanged(const std::string& channelId, float value);  // ✅ NEW: Direct DataStore updates
  void onRefreshChannels();
  void onUpdateCharts();  // Periodic chart update at ~30 FPS

private:
  void setupUI();
  void createChannelCard(const std::string& channelId, const std::string& clientId, const std::string& clientName);
  void updateChart(const std::string& channelId);
  void refreshChannelList();
  QString formatValueWithUnit(float value, const std::string& baseUnit) const;

  struct DataPoint {
    qint64 timestamp;
    float value;
  };

  struct ChannelCard {
    std::string channelId;
    std::string clientId;      // ADD: Store client ID
    std::string clientName;    // ADD: Store client name
    std::string unit;          // ADD: Store unit (e.g., "A", "V", "W")
    QWidget* cardWidget;
    QLabel* nameLabel;
    QLabel* valueLabel;
    QChartView* chartView;
    QChart* chart;
    QLineSeries* series;
    QValueAxis* axisX;
    QValueAxis* axisY;
    std::deque<DataPoint> dataBuffer;  // Circular buffer
  };

  ApplicationContext* m_context;
  DataStore* m_dataStore;
  TcpClientManager* m_tcpManager;

  QLabel* m_summaryLabel;
  QPushButton* m_refreshButton;
  QVBoxLayout* m_cardsLayout;
  QTimer* m_refreshTimer;
  QTimer* m_chartUpdateTimer;  // For throttled chart updates

  std::map<std::string, ChannelCard> m_channelCards;
  std::set<std::string> m_dirtyChannels;  // Tracks which charts need update

  static constexpr size_t MAX_POINTS = 1000;  // Buffer size
  static constexpr qint64 TIME_WINDOW_MS = 10000;  // 10 seconds window
  static constexpr int CHART_UPDATE_INTERVAL_MS = 16;  // 60 FPS (was 33)
};