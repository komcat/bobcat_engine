// DataStoreWidget.cpp
#include "DataStoreWidget.h"
#include "ApplicationContext.h"
#include "DataStore.h"
#include "TcpClientManager.h"
#include <QScrollArea>
#include <QDateTime>

#include <iostream>
#include <algorithm>

DataStoreWidget::DataStoreWidget(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_dataStore(context->dataStore)
  , m_tcpManager(context->tcpClientManager)
{
  setupUI();

  // Connect to TcpClientManager signal for real-time updates
  connect(m_tcpManager, &TcpClientManager::dataReceived,
    this, &DataStoreWidget::onDataReceived);

  // Auto-refresh channel list every 5 seconds to detect new channels
  m_refreshTimer = new QTimer(this);
  connect(m_refreshTimer, &QTimer::timeout, this, &DataStoreWidget::refreshChannelList);
  m_refreshTimer->start(5000);

  // Chart update timer - smooth ~30 FPS updates
  m_chartUpdateTimer = new QTimer(this);
  connect(m_chartUpdateTimer, &QTimer::timeout, this, &DataStoreWidget::onUpdateCharts);
  m_chartUpdateTimer->start(CHART_UPDATE_INTERVAL_MS);

  // Initial channel list
  refreshChannelList();

  std::cout << "[DataStoreWidget] Created" << std::endl;
}

DataStoreWidget::~DataStoreWidget()
{
  if (m_refreshTimer) {
    m_refreshTimer->stop();
  }
  if (m_chartUpdateTimer) {
    m_chartUpdateTimer->stop();
  }
}

void DataStoreWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(15);

  // Header
  QWidget* headerWidget = new QWidget();
  headerWidget->setStyleSheet("QWidget { background-color: #1e1e1e; border-radius: 8px; }");
  QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(20, 15, 20, 15);

  QVBoxLayout* titleLayout = new QVBoxLayout();
  QLabel* titleLabel = new QLabel("Data Store - Real-Time Charts");
  titleLabel->setStyleSheet("QLabel { font-size: 28px; font-weight: bold; color: #ffffff; background: transparent; }");
  titleLayout->addWidget(titleLabel);

  m_summaryLabel = new QLabel("Channels: 0");
  m_summaryLabel->setStyleSheet("QLabel { color: #aaaaaa; font-size: 14px; background: transparent; }");
  titleLayout->addWidget(m_summaryLabel);

  headerLayout->addLayout(titleLayout);
  headerLayout->addStretch();

  // Refresh button
  m_refreshButton = new QPushButton("Refresh Channels");
  m_refreshButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #0078d4; "
    "  color: white; "
    "  padding: 10px 20px; "
    "  font-size: 14px; "
    "  font-weight: 600; "
    "  border: none; "
    "  border-radius: 6px; "
    "} "
    "QPushButton:hover { background-color: #1084d8; }");
  connect(m_refreshButton, &QPushButton::clicked, this, &DataStoreWidget::onRefreshChannels);
  headerLayout->addWidget(m_refreshButton);

  mainLayout->addWidget(headerWidget);

  // Scrollable area for channel cards
  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setStyleSheet("QScrollArea { background-color: transparent; border: none; }");

  QWidget* scrollContent = new QWidget();
  m_cardsLayout = new QVBoxLayout(scrollContent);
  m_cardsLayout->setSpacing(15);
  m_cardsLayout->setContentsMargins(0, 0, 0, 0);
  m_cardsLayout->addStretch();

  scrollArea->setWidget(scrollContent);
  mainLayout->addWidget(scrollArea);
}

void DataStoreWidget::createChannelCard(const std::string& channelId)
{
  // Check if card already exists
  if (m_channelCards.find(channelId) != m_channelCards.end()) {
    return;
  }

  ChannelCard card;
  card.channelId = channelId;

  // Card container
  card.cardWidget = new QWidget();
  card.cardWidget->setStyleSheet(
    "QWidget { "
    "  background-color: #2d2d2d; "
    "  border: 1px solid #404040; "
    "  border-radius: 8px; "
    "}");

  QVBoxLayout* cardLayout = new QVBoxLayout(card.cardWidget);
  cardLayout->setContentsMargins(20, 16, 20, 16);
  cardLayout->setSpacing(12);

  // Header row: Channel name and current value
  QHBoxLayout* headerRow = new QHBoxLayout();

  card.nameLabel = new QLabel(QString("Channel: %1").arg(QString::fromStdString(channelId)));
  card.nameLabel->setStyleSheet("QLabel { color: #ffffff; font-size: 18px; font-weight: bold; background: transparent; }");
  headerRow->addWidget(card.nameLabel);

  headerRow->addStretch();

  card.valueLabel = new QLabel("--");
  card.valueLabel->setStyleSheet(
    "QLabel { "
    "  color: #00d9ff; "
    "  font-size: 20px; "
    "  font-weight: 600; "
    "  background: transparent; "
    "}");
  headerRow->addWidget(card.valueLabel);

  cardLayout->addLayout(headerRow);

  // Chart
  card.series = new QLineSeries();
  card.series->setColor(QColor(0, 217, 255));  // Cyan color

  card.chart = new QChart();
  card.chart->addSeries(card.series);
  card.chart->legend()->hide();
  card.chart->setBackgroundBrush(QBrush(QColor(30, 30, 30)));
  card.chart->setMargins(QMargins(0, 0, 0, 0));

  // Axes
  card.axisX = new QValueAxis();
  card.axisX->setLabelFormat("%.0f");
  card.axisX->setTitleText("Seconds Ago");
  card.axisX->setTitleBrush(QBrush(Qt::white));
  card.axisX->setLabelsBrush(QBrush(QColor(170, 170, 170)));
  card.axisX->setGridLineColor(QColor(70, 70, 70));
  card.axisX->setTickCount(7);
  card.chart->addAxis(card.axisX, Qt::AlignBottom);
  card.series->attachAxis(card.axisX);

  card.axisY = new QValueAxis();
  card.axisY->setLabelFormat("%.3f");
  card.axisY->setTitleText("Value");
  card.axisY->setTitleBrush(QBrush(Qt::white));
  card.axisY->setLabelsBrush(QBrush(QColor(170, 170, 170)));
  card.axisY->setGridLineColor(QColor(70, 70, 70));
  card.chart->addAxis(card.axisY, Qt::AlignLeft);
  card.series->attachAxis(card.axisY);

  card.chartView = new QChartView(card.chart);
  card.chartView->setRenderHint(QPainter::Antialiasing);
  card.chartView->setMinimumHeight(250);
  cardLayout->addWidget(card.chartView);

  // Add card to layout (before stretch)
  int count = m_cardsLayout->count();
  m_cardsLayout->insertWidget(count - 1, card.cardWidget);

  // Store card
  m_channelCards[channelId] = card;

  std::cout << "[DataStoreWidget] Created card for channel: " << channelId << std::endl;
}

void DataStoreWidget::onDataReceived(const std::string& clientId, float value, qint64 timestamp)
{
  // Find which channel this client writes to
  auto config = m_tcpManager->GetClientConfig(clientId);
  std::string channelId = config.dataStoreChannel;

  // Get or create card for this channel
  auto it = m_channelCards.find(channelId);
  if (it == m_channelCards.end()) {
    createChannelCard(channelId);
    it = m_channelCards.find(channelId);
  }

  auto& card = it->second;

  // Add data point to buffer
  DataPoint point;
  point.timestamp = timestamp;
  point.value = value;
  card.dataBuffer.push_back(point);

  // Keep only last MAX_POINTS
  if (card.dataBuffer.size() > MAX_POINTS) {
    card.dataBuffer.pop_front();
  }

  // Update value label immediately for responsiveness
  card.valueLabel->setText(QString::number(value, 'f', 6));

  // Mark channel as dirty for next chart update (throttled)
  m_dirtyChannels.insert(channelId);
}

void DataStoreWidget::updateChart(const std::string& channelId)
{
  auto it = m_channelCards.find(channelId);
  if (it == m_channelCards.end()) return;

  auto& card = it->second;
  if (card.dataBuffer.empty()) return;

  card.series->clear();

  qint64 latestTime = card.dataBuffer.back().timestamp;

  QVector<QPointF> points;
  points.reserve(card.dataBuffer.size());

  float minValue = (std::numeric_limits<float>::max)();
  float maxValue = std::numeric_limits<float>::lowest();

  for (const auto& point : card.dataBuffer) {
    // Relative time in seconds (negative = past)
    double relativeTime = (point.timestamp - latestTime) / 1000.0;
    points.append(QPointF(relativeTime, point.value));

    minValue = (std::min)(minValue, point.value);
    maxValue = (std::max)(maxValue, point.value);
  }

  card.series->replace(points);

  // X-axis: configurable time window
  double timeRange = TIME_WINDOW_MS / 1000.0;
  card.axisX->setRange(-timeRange, 0);

  // Y-axis with 10% margin
  if (minValue < maxValue) {
    float margin = (maxValue - minValue) * 0.1f;
    card.axisY->setRange(minValue - margin, maxValue + margin);
  }
  else {
    // Single value case
    card.axisY->setRange(minValue * 0.9, minValue * 1.1);
  }
}

void DataStoreWidget::onUpdateCharts()
{
  // Update only dirty channels (throttled at ~30 FPS)
  for (const auto& channelId : m_dirtyChannels) {
    updateChart(channelId);
  }
  m_dirtyChannels.clear();
}

void DataStoreWidget::onRefreshChannels()
{
  std::cout << "[DataStoreWidget] Manual refresh requested" << std::endl;
  refreshChannelList();
}

void DataStoreWidget::refreshChannelList()
{
  if (!m_dataStore) return;

  auto channels = m_dataStore->GetAvailableChannels();

  // Create cards for new channels
  for (const auto& channelId : channels) {
    if (m_channelCards.find(channelId) == m_channelCards.end()) {
      createChannelCard(channelId);
    }
  }

  // Update summary
  m_summaryLabel->setText(QString("Channels: %1").arg(channels.size()));
}