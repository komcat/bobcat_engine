// LiveChartWidget.cpp
#include "LiveChartWidget.h"
#include "DataStore.h"
#include "TcpClientManager.h"
#include <QTimer>
#include <QDateTime>
#include <iostream>
#include <algorithm>
#include <cmath>

LiveChartWidget::LiveChartWidget(DataStore* dataStore, TcpClientManager* tcpManager, QWidget* parent)
  : QWidget(parent)
  , m_dataStore(dataStore)
  , m_tcpManager(tcpManager)
  , m_isDirty(false)
  , m_latestValue(0.0f)
  , m_hasNewValue(false)
{
  setupUI();

  // Connect to TcpClientManager for real-time data (TCP clients only)
  connect(m_tcpManager, &TcpClientManager::dataReceived,
    this, &LiveChartWidget::onDataReceived);

  // ✅ NEW: Poll DataStore directly for all channels (SMU, TCP, etc.)
  // This ensures we get data from ALL sources, not just TCP clients
  QTimer* pollTimer = new QTimer(this);
  connect(pollTimer, &QTimer::timeout, this, [this]() {
    if (m_currentChannelId.empty() || !m_dataStore) return;

    float value = 0.0f;
    if (m_dataStore->TryGetValue(m_currentChannelId, value)) {
      qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
      onDataReceived(m_currentChannelId, value, timestamp);
    }
  });
  pollTimer->start(100);  // Poll at 10 Hz

  // Chart update timer - 10 Hz
  m_chartUpdateTimer = new QTimer(this);
  connect(m_chartUpdateTimer, &QTimer::timeout, this, &LiveChartWidget::onUpdateChart);
  m_chartUpdateTimer->start(CHART_UPDATE_INTERVAL_MS);

  // Value label update timer - 10 Hz for maximum visibility
  m_valueLabelUpdateTimer = new QTimer(this);
  connect(m_valueLabelUpdateTimer, &QTimer::timeout, this, &LiveChartWidget::onUpdateValueLabel);
  m_valueLabelUpdateTimer->start(VALUE_LABEL_UPDATE_INTERVAL_MS);

  // ✅ REMOVED: Auto-refresh timer (user will refresh manually with button)
  // m_channelRefreshTimer = new QTimer(this);
  // connect(m_channelRefreshTimer, &QTimer::timeout, this, &LiveChartWidget::onRefreshChannels);
  // m_channelRefreshTimer->start(5000);

  // Initial channel list
  refreshChannelList();

  std::cout << "[LiveChartWidget] Created" << std::endl;
}

LiveChartWidget::~LiveChartWidget()
{
  if (m_chartUpdateTimer) {
    m_chartUpdateTimer->stop();
  }
  if (m_valueLabelUpdateTimer) {
    m_valueLabelUpdateTimer->stop();
  }
  // ✅ REMOVED: Auto-refresh timer cleanup
  // if (m_channelRefreshTimer) {
  //   m_channelRefreshTimer->stop();
  // }
  std::cout << "[LiveChartWidget] Destroyed" << std::endl;
}

void LiveChartWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(10, 10, 10, 10);
  mainLayout->setSpacing(10);

  // Header with channel selector
  QWidget* headerWidget = new QWidget();
  headerWidget->setStyleSheet("QWidget { background-color: #2d2d2d; border-radius: 6px; }");
  QVBoxLayout* headerLayout = new QVBoxLayout(headerWidget);
  headerLayout->setContentsMargins(15, 10, 15, 10);
  headerLayout->setSpacing(8);

  // Title
  QLabel* titleLabel = new QLabel("Live Chart View");
  titleLabel->setStyleSheet("QLabel { font-size: 18px; font-weight: bold; color: #ffffff; background: transparent; }");
  headerLayout->addWidget(titleLabel);

  // Channel selector row
  QHBoxLayout* selectorRow = new QHBoxLayout();

  QLabel* selectorLabel = new QLabel("Channel:");
  selectorLabel->setStyleSheet("QLabel { color: #aaaaaa; font-size: 14px; background: transparent; }");
  selectorRow->addWidget(selectorLabel);

  m_channelSelector = new QComboBox();
  m_channelSelector->setStyleSheet(
    "QComboBox { "
    "  background-color: #3d3d3d; "
    "  color: white; "
    "  padding: 5px 10px; "
    "  border: 1px solid #505050; "
    "  border-radius: 4px; "
    "  font-size: 14px; "
    "} "
    "QComboBox::drop-down { border: none; } "
    "QComboBox QAbstractItemView { "
    "  background-color: #3d3d3d; "
    "  color: white; "
    "  selection-background-color: #0078d4; "
    "}");
  connect(m_channelSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &LiveChartWidget::onChannelSelected);
  selectorRow->addWidget(m_channelSelector);

  // ✅ NEW: Manual refresh button for channel list
  m_refreshButton = new QPushButton("↻");
  m_refreshButton->setToolTip("Refresh channel list from DataStore");
  m_refreshButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #0e639c; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 5px 12px; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #1177bb; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #0d5a8f; "
    "}");
  m_refreshButton->setFixedWidth(40);
  connect(m_refreshButton, &QPushButton::clicked, this, &LiveChartWidget::onRefreshChannels);
  selectorRow->addWidget(m_refreshButton);

  selectorRow->addStretch();

  headerLayout->addLayout(selectorRow);

  // Current value display - VERY LARGE and centered below channel selector
  m_valueLabel = new QLabel("--");
  m_valueLabel->setAlignment(Qt::AlignCenter);

  // Set initial font size explicitly
  QFont initialFont = m_valueLabel->font();
  initialFont.setPointSize(72);
  initialFont.setBold(true);
  m_valueLabel->setFont(initialFont);

  m_valueLabel->setStyleSheet(
    "QLabel { "
    "  color: #00d9ff; "
    "  font-weight: bold; "
    "  font-size: 72pt; "
    "  background: transparent; "
    "  padding: 15px; "
    "}");
  m_valueLabel->setMinimumHeight(120);  // Much larger minimum height (was 60)
  m_valueLabel->setMaximumHeight(180);  // ✅ Limit maximum height to prevent excessive growth
  m_valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);  // ✅ Fixed height policy
  headerLayout->addWidget(m_valueLabel);

  // Channel info label
  m_channelInfoLabel = new QLabel("No channel selected");
  m_channelInfoLabel->setStyleSheet("QLabel { color: #888888; font-size: 12px; background: transparent; }");
  headerLayout->addWidget(m_channelInfoLabel);

  // ✅ Add header with stretch factor 0 (don't expand)
  mainLayout->addWidget(headerWidget, 0);

  // Chart
  m_series = new QLineSeries();
  m_series->setColor(QColor(0, 217, 255));  // Cyan

  m_chart = new QChart();
  m_chart->addSeries(m_series);
  m_chart->legend()->hide();
  m_chart->setBackgroundBrush(QBrush(QColor(30, 30, 30)));
  m_chart->setMargins(QMargins(0, 0, 0, 0));

  // X-axis (time)
  m_axisX = new QValueAxis();
  m_axisX->setLabelFormat("%.1f");
  m_axisX->setTitleText("Seconds Ago");
  m_axisX->setTitleBrush(QBrush(Qt::white));
  m_axisX->setLabelsBrush(QBrush(QColor(170, 170, 170)));
  m_axisX->setGridLineColor(QColor(70, 70, 70));
  m_axisX->setTickCount(11);
  m_axisX->setRange(-10, 0);
  m_chart->addAxis(m_axisX, Qt::AlignBottom);
  m_series->attachAxis(m_axisX);

  // Y-axis (value)
  m_axisY = new QValueAxis();
  m_axisY->setLabelFormat("%.3f");
  m_axisY->setTitleText("Value");
  m_axisY->setTitleBrush(QBrush(Qt::white));
  m_axisY->setLabelsBrush(QBrush(QColor(170, 170, 170)));
  m_axisY->setGridLineColor(QColor(70, 70, 70));
  m_chart->addAxis(m_axisY, Qt::AlignLeft);
  m_series->attachAxis(m_axisY);

  m_chartView = new QChartView(m_chart);
  m_chartView->setRenderHint(QPainter::Antialiasing);

  // ✅ Add chart with stretch factor 1 (takes remaining space, ~50% or more)
  mainLayout->addWidget(m_chartView, 1);

  // Set initial font size for value label
  updateValueFontSize();
}

void LiveChartWidget::onDataReceived(const std::string& channelOrClientId, float value, qint64 timestamp)
{
  // Only process data for current channel
  if (m_currentChannelId.empty()) return;

  // ✅ FIXED: Accept data if it matches either the channel ID directly OR the client ID
  // This supports both SMU channels (passed as channel ID) and TCP clients (passed as client ID)
  if (channelOrClientId != m_currentChannelId && channelOrClientId != m_currentClientId) return;

  // Debug output (disabled - too verbose)
  // static int debugCounter = 0;
  // if (debugCounter++ % 10 == 0) {  // Log every 10th data point
  //   std::cout << "[LiveChartWidget] Data received for " << m_currentChannelId
  //             << ": " << value << " " << m_currentUnit << std::endl;
  // }

  // Add data point to buffer
  DataPoint point;
  point.timestamp = timestamp;
  point.value = value;
  m_dataBuffer.push_back(point);

  // Keep only last MAX_POINTS
  if (m_dataBuffer.size() > MAX_POINTS) {
    m_dataBuffer.pop_front();
  }

  // Store latest value for throttled label update
  m_latestValue = value;
  m_hasNewValue = true;

  // Mark as dirty for next chart update
  m_isDirty = true;
}

void LiveChartWidget::onChannelSelected(int index)
{
  if (index < 0) return;

  std::string channelId = m_channelSelector->itemData(index).toString().toStdString();
  setChannel(channelId);
}

void LiveChartWidget::setChannel(const std::string& channelId)
{
  if (channelId == m_currentChannelId) return;

  std::cout << "[LiveChartWidget] Switching to channel: " << channelId << std::endl;

  m_currentChannelId = channelId;
  m_dataBuffer.clear();
  m_series->clear();
  m_valueLabel->setText("--");
  m_latestValue = 0.0f;
  m_hasNewValue = false;

  // ✅ FIXED: Try to find TCP client that owns this channel, but handle non-TCP channels too
  bool foundClient = false;
  auto clientIds = m_tcpManager->GetClientIds();
  for (const auto& clientId : clientIds) {
    auto config = m_tcpManager->GetClientConfig(clientId);
    if (config.dataStoreChannel == channelId) {
      m_currentClientId = config.id;
      foundClient = true;

      // Update info label with TCP client name
      QString infoText = QString("%1 (%2)")
        .arg(QString::fromStdString(config.name))
        .arg(QString::fromStdString(clientId));
      m_channelInfoLabel->setText(infoText);
      break;
    }
  }

  // ✅ NEW: If no TCP client found (e.g., SMU channel), use channel ID directly
  if (!foundClient) {
    m_currentClientId = "";  // No client ID for non-TCP channels
    m_channelInfoLabel->setText(QString("DataStore: %1").arg(QString::fromStdString(channelId)));
  }

  // ✅ IMPROVED: Infer unit from channel ID (works for both TCP and non-TCP channels)
  if (channelId.find("Amp") != std::string::npos ||
      channelId.find("Current") != std::string::npos ||
      channelId.find("_Ch") != std::string::npos) {  // SMU channels often have "_Ch" in name
    m_currentUnit = "A";
  }
  else if (channelId.find("Volt") != std::string::npos) {
    m_currentUnit = "V";
  }
  else if (channelId.find("Watt") != std::string::npos || channelId.find("Power") != std::string::npos) {
    m_currentUnit = "W";
  }
  else if (channelId.find("Ohm") != std::string::npos || channelId.find("Resistance") != std::string::npos) {
    m_currentUnit = "Ω";
  }
  else {
    m_currentUnit = "";
  }

  // Update Y-axis title
  QString yAxisTitle = m_currentUnit.empty() ? "Value" : QString::fromStdString(m_currentUnit);
  m_axisY->setTitleText(yAxisTitle);

  emit channelChanged(channelId);

  std::cout << "[LiveChartWidget] Channel set to: " << channelId
            << " (Unit: " << m_currentUnit << ", Has TCP client: " << (foundClient ? "yes" : "no") << ")" << std::endl;
}

void LiveChartWidget::onUpdateChart()
{
  if (m_isDirty) {
    updateChart();
    m_isDirty = false;
  }
}

void LiveChartWidget::onUpdateValueLabel()
{
  // Only update label if there's a new value
  if (m_hasNewValue) {
    // Update text
    m_valueLabel->setText(formatValueWithUnit(m_latestValue, m_currentUnit));

    // Update color based on value magnitude (but preserve font size!)
    QColor valueColor = getValueColor(m_latestValue);

    // Get current font size to preserve it in stylesheet
    int currentFontSize = m_valueLabel->font().pointSize();

    // Ensure valid font size (fallback to 72pt if not set yet)
    if (currentFontSize <= 0) {
      currentFontSize = 72;
    }

    m_valueLabel->setStyleSheet(
      QString("QLabel { "
              "  color: %1; "
              "  font-weight: bold; "
              "  font-size: %2pt; "
              "  background: transparent; "
              "  padding: 15px; "
              "}").arg(valueColor.name()).arg(currentFontSize));

    m_hasNewValue = false;
  }
}

void LiveChartWidget::updateChart()
{
  if (m_dataBuffer.empty()) {
    m_series->clear();
    return;
  }

  qint64 latestTime = m_dataBuffer.back().timestamp;

  QVector<QPointF> points;
  points.reserve(m_dataBuffer.size());

  float minValue = (std::numeric_limits<float>::max)();
  float maxValue = std::numeric_limits<float>::lowest();

  for (const auto& point : m_dataBuffer) {
    // Relative time in seconds (negative = past)
    double relativeTime = (point.timestamp - latestTime) / 1000.0;
    points.append(QPointF(relativeTime, point.value));

    minValue = (std::min)(minValue, point.value);
    maxValue = (std::max)(maxValue, point.value);
  }

  m_series->replace(points);

  // X-axis: fixed 10-second window
  double timeRange = TIME_WINDOW_MS / 1000.0;
  m_axisX->setRange(-timeRange, 0);

  // Y-axis with hard-coded limits to prevent unrealistic spike rescaling
  // Clamp calculated values to prevent spike-induced rescaling (max: 20mA, min: -20mA)
  float clampedMin = (std::max)(minValue, Y_AXIS_MIN_LIMIT);
  float clampedMax = (std::min)(maxValue, Y_AXIS_MAX_LIMIT);

  // Y-axis with 10% margin (within clamped range)
  if (clampedMin < clampedMax) {
    float margin = (clampedMax - clampedMin) * 0.1f;
    float rangeMin = clampedMin - margin;
    float rangeMax = clampedMax + margin;

    // Ensure range stays within hard limits
    rangeMin = (std::max)(rangeMin, Y_AXIS_MIN_LIMIT);
    rangeMax = (std::min)(rangeMax, Y_AXIS_MAX_LIMIT);

    m_axisY->setRange(rangeMin, rangeMax);
  }
  else {
    // Single value case - use symmetric range around value
    float center = clampedMin;
    float halfRange = std::abs(center) * 0.1f;
    if (halfRange < 1e-9f) halfRange = 1e-9f; // Minimum range

    float rangeMin = (std::max)(center - halfRange, Y_AXIS_MIN_LIMIT);
    float rangeMax = (std::min)(center + halfRange, Y_AXIS_MAX_LIMIT);

    m_axisY->setRange(rangeMin, rangeMax);
  }
}

void LiveChartWidget::onRefreshChannels()
{
  refreshChannelList();
}

void LiveChartWidget::refreshChannelList()
{
  if (!m_dataStore) {
    std::cout << "[LiveChartWidget] ERROR: DataStore is null!" << std::endl;
    return;
  }

  // Store current selection
  std::string currentChannel = getCurrentChannel();

  // Block signals to prevent unwanted channel switches
  m_channelSelector->blockSignals(true);
  m_channelSelector->clear();

  auto channels = m_dataStore->GetAvailableChannels();

  std::cout << "[LiveChartWidget] Refreshing channel list - found " << channels.size() << " channels from DataStore" << std::endl;
  for (const auto& ch : channels) {
    std::cout << "[LiveChartWidget]   - Channel: " << ch << std::endl;
  }

  if (channels.empty()) {
    m_channelSelector->addItem("No channels available", QVariant(QString("")));
    m_channelSelector->blockSignals(false);
    std::cout << "[LiveChartWidget] No channels available in DataStore" << std::endl;
    return;
  }

  int currentIndex = -1;
  int idx = 0;

  for (const auto& channelId : channels) {
    // ✅ FIXED: Try to find client info for display name, but add channel even if not found
    QString displayName;
    bool foundClient = false;

    auto clientIds = m_tcpManager->GetClientIds();
    for (const auto& clientId : clientIds) {
      auto config = m_tcpManager->GetClientConfig(clientId);
      if (config.dataStoreChannel == channelId) {
        displayName = QString("%1 (%2)")
          .arg(QString::fromStdString(config.name))
          .arg(QString::fromStdString(channelId));
        foundClient = true;
        break;
      }
    }

    // If no TCP client found, use channel ID as display name (e.g., SMU channels)
    if (!foundClient) {
      displayName = QString::fromStdString(channelId);
    }

    m_channelSelector->addItem(displayName, QVariant(QString::fromStdString(channelId)));

    if (channelId == currentChannel) {
      currentIndex = idx;
    }
    idx++;
  }

  // Restore selection or select first
  if (currentIndex >= 0) {
    m_channelSelector->setCurrentIndex(currentIndex);
  }
  else if (m_channelSelector->count() > 0) {
    m_channelSelector->setCurrentIndex(0);
  }

  m_channelSelector->blockSignals(false);

  // If selection changed, update channel
  std::string newChannel = getCurrentChannel();
  if (newChannel != currentChannel && !newChannel.empty()) {
    setChannel(newChannel);
  }
}

std::string LiveChartWidget::getCurrentChannel() const
{
  if (m_channelSelector->currentIndex() < 0) {
    return "";
  }
  return m_channelSelector->currentData().toString().toStdString();
}

QString LiveChartWidget::formatValueWithUnit(float value, const std::string& baseUnit) const
{
  if (baseUnit.empty()) {
    return QString::number(value, 'f', 6);
  }

  float absValue = std::abs(value);
  QString prefix;
  float scaledValue = value;

  // Select appropriate SI prefix
  if (absValue >= 1e9) {
    prefix = "G";
    scaledValue = value / 1e9;
  }
  else if (absValue >= 1e6) {
    prefix = "M";
    scaledValue = value / 1e6;
  }
  else if (absValue >= 1e3) {
    prefix = "k";
    scaledValue = value / 1e3;
  }
  else if (absValue >= 1.0) {
    prefix = "";
    scaledValue = value;
  }
  else if (absValue >= 1e-3) {
    prefix = "m";
    scaledValue = value / 1e-3;
  }
  else if (absValue >= 1e-6) {
    prefix = "µ";
    scaledValue = value / 1e-6;
  }
  else if (absValue >= 1e-9) {
    prefix = "n";
    scaledValue = value / 1e-9;
  }
  else if (absValue >= 1e-12) {
    prefix = "p";
    scaledValue = value / 1e-12;
  }
  else {
    prefix = "f";
    scaledValue = value / 1e-15;
  }

  QString unitStr = QString::fromStdString(baseUnit);
  return QString("%1 %2%3")
    .arg(scaledValue, 0, 'f', 3)
    .arg(prefix)
    .arg(unitStr);
}

QColor LiveChartWidget::getValueColor(float value) const
{
  float absValue = std::abs(value);

  // Color ranges based on absolute value magnitude
  if (absValue >= 10e-3) {
    // >= 10m: Purple
    return QColor(200, 100, 255);  // Purple
  }
  else if (absValue >= 1e-3) {
    // >= 1m: Orange
    return QColor(255, 165, 0);  // Orange
  }
  else if (absValue >= 500e-6) {
    // >= 500µ: Light blue
    return QColor(0, 217, 255);  // Cyan/Light blue
  }
  else if (absValue >= 1e-6) {
    // >= 1µ: Dark green
    return QColor(0, 200, 0);  // Dark green
  }
  else {
    // < 1µ: Gray
    return QColor(150, 150, 150);  // Gray
  }
}

void LiveChartWidget::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  updateValueFontSize();
}

void LiveChartWidget::updateValueFontSize()
{
  if (!m_valueLabel) return;

  // Calculate font size - MUCH larger for maximum visibility
  int widgetHeight = height();
  int fontSize = static_cast<int>(widgetHeight * 0.25);  // Increased to 25% of height

  // Set minimum and maximum bounds for readability
  if (fontSize < 72) fontSize = 72;  // Very large minimum for visibility
  if (fontSize > 200) fontSize = 200;  // Higher maximum

  QFont font = m_valueLabel->font();
  font.setPointSize(fontSize);
  font.setBold(true);  // Make it bold for better visibility
  m_valueLabel->setFont(font);

  std::cout << "[LiveChartWidget] Font size updated to: " << fontSize << "pt (widget height: " << widgetHeight << "px)" << std::endl;
}