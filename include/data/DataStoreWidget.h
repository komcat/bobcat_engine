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
#include <string>

struct ApplicationContext;
class DataStore;
class TcpClientManager;

/**
 * @brief Widget displaying real-time charts for all DataStore channels
 * - Automatically detects channels from DataStore
 * - Real-time chart updates via TcpClientManager signals
 * - Circular buffer (last 200 points per channel)
 */
class DataStoreWidget : public QWidget {
  Q_OBJECT

public:
  explicit DataStoreWidget(ApplicationContext* context, QWidget* parent = nullptr);
  ~DataStoreWidget();

private slots:
  void onDataReceived(const std::string& clientId, float value, qint64 timestamp);
  void onRefreshChannels();

private:
  void setupUI();
  void createChannelCard(const std::string& channelId);
  void updateChart(const std::string& channelId);
  void refreshChannelList();

  struct DataPoint {
    qint64 timestamp;
    float value;
  };

  struct ChannelCard {
    std::string channelId;
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

  std::map<std::string, ChannelCard> m_channelCards;

  static constexpr size_t MAX_POINTS = 200;  // Buffer size
  static constexpr qint64 TIME_WINDOW_MS = 30000;  // 30 seconds window
};