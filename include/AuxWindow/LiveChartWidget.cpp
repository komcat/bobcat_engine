// LiveChartWidget.cpp
#include "LiveChartWidget.h"
#include "DataStore.h"
#include "TcpClientManager.h"
#include <QTimer>
#include <QDateTime>
#include <QFontMetrics>
#include <QFile>
#include <QInputDialog>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

LiveChartWidget::LiveChartWidget(DataStore* dataStore, TcpClientManager* tcpManager, QWidget* parent)
  : QWidget(parent)
  , m_dataStore(dataStore)
  , m_tcpManager(tcpManager)
  , m_setTargetButton(nullptr)
  , m_clearTargetButton(nullptr)
  , m_percentageLabel(nullptr)
  , m_isDirty(false)
  , m_latestValue(0.0f)
  , m_hasNewValue(false)
{
  // Load saved targets from config file
  loadTargets();

  setupUI();

  // Connect to TcpClientManager for real-time data with original timestamps
  connect(m_tcpManager, &TcpClientManager::dataReceived,
    this, &LiveChartWidget::onDataReceived);

  // ✅ Poll DataStore for NON-TCP channels only (e.g., SMU channels)
  // TCP channels receive data directly via the signal above with proper timestamps
  QTimer* pollTimer = new QTimer(this);
  connect(pollTimer, &QTimer::timeout, this, [this]() {
    if (m_currentChannelId.empty() || !m_dataStore) return;

    // Skip polling if this is a TCP client channel (they use real-time signals)
    if (!m_currentClientId.empty()) return;

    // Poll only for non-TCP channels (SMU, etc.)
    float value = 0.0f;
    if (m_dataStore->TryGetValue(m_currentChannelId, value)) {
      qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
      onDataReceived(m_currentChannelId, value, timestamp);
    }
  });
  pollTimer->start(100);  // Poll at 10 Hz for non-TCP channels

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

  // Set Target button (before channel selector)
  m_setTargetButton = new QPushButton("🎯");
  m_setTargetButton->setToolTip("Set current value as target for this channel");
  m_setTargetButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #28a745; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 5px 8px; "
    "  font-size: 14px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #34c759; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #1e7e34; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #555555; "
    "  color: #888888; "
    "}");
  m_setTargetButton->setFixedWidth(36);
  connect(m_setTargetButton, &QPushButton::clicked, this, &LiveChartWidget::onSetTarget);
  selectorRow->addWidget(m_setTargetButton);

  // Clear Target button
  m_clearTargetButton = new QPushButton("✕");
  m_clearTargetButton->setToolTip("Clear target for this channel");
  m_clearTargetButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #dc3545; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 5px 8px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #e04555; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #c82333; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #555555; "
    "  color: #888888; "
    "}");
  m_clearTargetButton->setFixedWidth(36);
  connect(m_clearTargetButton, &QPushButton::clicked, this, &LiveChartWidget::onClearTarget);
  selectorRow->addWidget(m_clearTargetButton);

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

  // ✅ Current value display - TWO ROWS (value on top, unit on bottom)
  QVBoxLayout* valueLayout = new QVBoxLayout();
  valueLayout->setSpacing(0);  // No spacing between value and unit
  valueLayout->setAlignment(Qt::AlignCenter);

  // Value label (numeric part only) - TOP ROW
  m_valueLabel = new QLabel("--");
  m_valueLabel->setAlignment(Qt::AlignCenter);
  m_valueLabel->setWordWrap(false);

  QFont valueFont("Consolas", 72);
  if (!valueFont.exactMatch()) {
    valueFont.setFamily("Courier New");
  }
  valueFont.setBold(true);
  m_valueLabel->setFont(valueFont);

  m_valueLabel->setStyleSheet(
    "QLabel { "
    "  color: #00d9ff; "
    "  font-weight: bold; "
    "  font-size: 72pt; "
    "  font-family: 'Consolas', 'Courier New', monospace; "
    "  background: transparent; "
    "  padding: 0px; "
    "  white-space: nowrap; "
    "}");

  m_valueLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_valueLabel->setMaximumHeight(90);  // Limit height for value

  // Unit label - BOTTOM ROW
  m_unitLabel = new QLabel("");
  m_unitLabel->setAlignment(Qt::AlignCenter);
  m_unitLabel->setWordWrap(false);

  QFont unitFont("Consolas", 36);  // Smaller font for unit
  if (!unitFont.exactMatch()) {
    unitFont.setFamily("Courier New");
  }
  unitFont.setBold(true);
  m_unitLabel->setFont(unitFont);

  m_unitLabel->setStyleSheet(
    "QLabel { "
    "  color: #888888; "  // Dimmer color for unit
    "  font-weight: bold; "
    "  font-size: 36pt; "
    "  font-family: 'Consolas', 'Courier New', monospace; "
    "  background: transparent; "
    "  padding: 0px; "
    "  white-space: nowrap; "
    "}");

  m_unitLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_unitLabel->setMaximumHeight(50);  // Limit height for unit

  // Percentage label - shows when target is set
  m_percentageLabel = new QLabel("");
  m_percentageLabel->setAlignment(Qt::AlignCenter);
  m_percentageLabel->setWordWrap(false);

  QFont percentFont("Consolas", 48);
  if (!percentFont.exactMatch()) {
    percentFont.setFamily("Courier New");
  }
  percentFont.setBold(true);
  m_percentageLabel->setFont(percentFont);

  m_percentageLabel->setStyleSheet(
    "QLabel { "
    "  color: #00ff00; "
    "  font-weight: bold; "
    "  font-size: 48pt; "
    "  font-family: 'Consolas', 'Courier New', monospace; "
    "  background: transparent; "
    "  padding: 0px; "
    "  white-space: nowrap; "
    "}");

  m_percentageLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_percentageLabel->setMaximumHeight(60);
  m_percentageLabel->hide();  // Hidden by default

  // Add to vertical layout (value on top, unit in middle, percentage on bottom)
  valueLayout->addWidget(m_valueLabel);
  valueLayout->addWidget(m_unitLabel);
  valueLayout->addWidget(m_percentageLabel);

  headerLayout->addLayout(valueLayout);

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

    // Check both single-channel and multi-channel configs
    bool isThisChannel = false;
    if (config.protocolType == "single" && config.dataStoreChannel == channelId) {
      isThisChannel = true;
    }
    else if (config.protocolType == "multi") {
      // Check if channel is in the multi-channel array
      for (const auto& ch : config.dataChannels) {
        if (ch == channelId) {
          isThisChannel = true;
          break;
        }
      }
    }

    if (isThisChannel) {
      m_currentClientId = config.id;
      foundClient = true;

      // Update info label with TCP client name
      QString infoText = QString("%1 (%2)")
        .arg(QString::fromStdString(config.name))
        .arg(QString::fromStdString(channelId));
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

  // Update target button states for this channel
  updateTargetButtonStates();

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
    // Split value and unit into separate labels
    QString fullText = formatValueWithUnit(m_latestValue, m_currentUnit);

    // Parse the formatted string to split value and unit
    // Format is like "-633.299 nA" or "1.278 µA"
    QStringList parts = fullText.split(' ', Qt::SkipEmptyParts);
    if (parts.size() == 2) {
      m_valueLabel->setText(parts[0]);  // Numeric value
      m_unitLabel->setText(parts[1]);   // Unit with prefix
    } else {
      m_valueLabel->setText(fullText);
      m_unitLabel->setText("");
    }

    // Check if target is set for current channel
    auto targetIt = m_targetValues.find(m_currentChannelId);
    bool hasTarget = (targetIt != m_targetValues.end());

    QColor valueColor;

    if (hasTarget && targetIt->second != 0.0f) {
      // Calculate percentage relative to target
      float percentage = (m_latestValue / targetIt->second) * 100.0f;

      // Show percentage label with color coding
      m_percentageLabel->setText(QString("%1%").arg(percentage, 0, 'f', 1));
      m_percentageLabel->show();

      // Get color based on percentage thresholds
      QColor percentColor = getPercentageColor(percentage);
      valueColor = percentColor;  // Use same color for value

      // Get current font size for percentage label
      int percentFontSize = m_percentageLabel->font().pointSize();
      if (percentFontSize <= 0) percentFontSize = 48;

      m_percentageLabel->setStyleSheet(
        QString("QLabel { "
                "  color: %1; "
                "  font-weight: bold; "
                "  font-size: %2pt; "
                "  background: transparent; "
                "  padding: 0px; "
                "}").arg(percentColor.name()).arg(percentFontSize));
    } else {
      // No target - hide percentage, use value-based color
      m_percentageLabel->hide();
      valueColor = getValueColor(m_latestValue);
    }

    // Update button states (enable Set Target once we have data)
    updateTargetButtonStates();

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

      // Check both single-channel and multi-channel configs
      bool isThisChannel = false;
      if (config.protocolType == "single" && config.dataStoreChannel == channelId) {
        isThisChannel = true;
      }
      else if (config.protocolType == "multi") {
        // Check if channel is in the multi-channel array
        for (const auto& ch : config.dataChannels) {
          if (ch == channelId) {
            isThisChannel = true;
            break;
          }
        }
      }

      if (isThisChannel) {
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
  if (!m_valueLabel || !m_unitLabel) return;

  // Calculate font size based on widget height
  int widgetHeight = height();
  int valueFontSize = static_cast<int>(widgetHeight * 0.20);  // 20% of height for value

  // Set minimum and maximum bounds for readability
  if (valueFontSize < 72) valueFontSize = 72;
  if (valueFontSize > 200) valueFontSize = 200;

  // Update value label font
  QFont valueFont = m_valueLabel->font();
  valueFont.setPointSize(valueFontSize);
  valueFont.setBold(true);
  m_valueLabel->setFont(valueFont);

  // Update max height for value label
  QFontMetrics valueFm(valueFont);
  int valueHeight = valueFm.height();
  m_valueLabel->setMaximumHeight(valueHeight + 10);

  // ✅ Update unit label font (smaller, proportional to value font)
  int unitFontSize = static_cast<int>(valueFontSize * 0.5);  // Half of value font size
  if (unitFontSize < 36) unitFontSize = 36;
  if (unitFontSize > 100) unitFontSize = 100;

  QFont unitFont = m_unitLabel->font();
  unitFont.setPointSize(unitFontSize);
  unitFont.setBold(true);
  m_unitLabel->setFont(unitFont);

  // Update max height for unit label
  QFontMetrics unitFm(unitFont);
  int unitHeight = unitFm.height();
  m_unitLabel->setMaximumHeight(unitHeight + 10);

  std::cout << "[LiveChartWidget] Font sizes updated - Value: " << valueFontSize << "pt (height: " << valueHeight << "px), Unit: " << unitFontSize << "pt (height: " << unitHeight << "px)" << std::endl;

  // Update percentage label font size
  if (m_percentageLabel) {
    int percentFontSize = static_cast<int>(valueFontSize * 0.67);  // 2/3 of value font size
    if (percentFontSize < 48) percentFontSize = 48;
    if (percentFontSize > 130) percentFontSize = 130;

    QFont percentFont = m_percentageLabel->font();
    percentFont.setPointSize(percentFontSize);
    percentFont.setBold(true);
    m_percentageLabel->setFont(percentFont);

    QFontMetrics percentFm(percentFont);
    int percentHeight = percentFm.height();
    m_percentageLabel->setMaximumHeight(percentHeight + 10);
  }
}

QColor LiveChartWidget::getPercentageColor(float percentage) const
{
  // Color coding based on percentage thresholds:
  // <85%: Red (Need work)
  // 85-95%: Yellow/Orange (Marginal)
  // 95-120%: Green (Pass)
  // >120%: Purple (Exceptional)

  if (percentage < 85.0f) {
    return QColor(255, 80, 80);  // Red - Need work
  }
  else if (percentage < 95.0f) {
    return QColor(255, 193, 7);  // Yellow/Orange - Marginal
  }
  else if (percentage <= 120.0f) {
    return QColor(40, 200, 40);  // Green - Pass
  }
  else {
    return QColor(180, 100, 255);  // Purple - Exceptional
  }
}

void LiveChartWidget::onSetTarget()
{
  if (m_currentChannelId.empty()) {
    std::cout << "[LiveChartWidget] Cannot set target: no channel selected" << std::endl;
    return;
  }

  // Determine the unit and appropriate default value
  QString unitStr = QString::fromStdString(m_currentUnit);

  // Get current target or use current live value as default
  float defaultValue = m_latestValue;
  auto it = m_targetValues.find(m_currentChannelId);
  if (it != m_targetValues.end()) {
    defaultValue = it->second;
  }

  // Format default value with SI prefix for display
  QString formattedDefault = formatValueWithUnit(defaultValue, m_currentUnit);

  // Show input dialog
  bool ok;
  QString title = "Set Target";
  QString label = QString("Enter target value for %1:\n\n"
                          "Current value: %2\n"
                          "Enter value in base unit (%3):")
                    .arg(QString::fromStdString(m_currentChannelId))
                    .arg(formatValueWithUnit(m_latestValue, m_currentUnit))
                    .arg(unitStr.isEmpty() ? "value" : unitStr);

  // Use scientific notation for small values
  double inputValue = QInputDialog::getDouble(
    this,
    title,
    label,
    defaultValue,          // default value
    -1e15,                 // min
    1e15,                  // max
    9,                     // decimals (for precision)
    &ok,
    Qt::WindowFlags(),
    0.0                    // step
  );

  if (ok) {
    m_targetValues[m_currentChannelId] = static_cast<float>(inputValue);
    saveTargets();
    updateTargetButtonStates();

    std::cout << "[LiveChartWidget] Target set for " << m_currentChannelId
              << ": " << inputValue << " " << m_currentUnit << std::endl;
  }
}

void LiveChartWidget::onClearTarget()
{
  if (m_currentChannelId.empty()) {
    return;
  }

  auto it = m_targetValues.find(m_currentChannelId);
  if (it != m_targetValues.end()) {
    m_targetValues.erase(it);
    saveTargets();
    updateTargetButtonStates();

    // Hide percentage label
    if (m_percentageLabel) {
      m_percentageLabel->hide();
    }

    std::cout << "[LiveChartWidget] Target cleared for " << m_currentChannelId << std::endl;
  }
}

void LiveChartWidget::updateTargetButtonStates()
{
  if (!m_setTargetButton || !m_clearTargetButton) return;

  bool hasChannel = !m_currentChannelId.empty();
  bool hasTarget = (m_targetValues.find(m_currentChannelId) != m_targetValues.end());
  bool hasData = !m_dataBuffer.empty();  // Check if we have any data for this channel

  // Set target button: enabled when channel selected and we have any data
  m_setTargetButton->setEnabled(hasChannel && hasData);

  // Clear target button: enabled only when target exists
  m_clearTargetButton->setEnabled(hasChannel && hasTarget);

  // Update tooltips with target info
  if (hasTarget) {
    float target = m_targetValues[m_currentChannelId];
    QString targetStr = formatValueWithUnit(target, m_currentUnit);
    m_setTargetButton->setToolTip(QString("Update target (current: %1)").arg(targetStr));
    m_clearTargetButton->setToolTip(QString("Clear target: %1").arg(targetStr));
  } else {
    m_setTargetButton->setToolTip("Set current value as target");
    m_clearTargetButton->setToolTip("No target set");
  }
}

void LiveChartWidget::loadTargets()
{
  try {
    std::ifstream file(TARGET_CONFIG_FILE);
    if (!file.is_open()) {
      std::cout << "[LiveChartWidget] No saved targets file found" << std::endl;
      return;
    }

    nlohmann::json j;
    file >> j;

    m_targetValues.clear();
    if (j.contains("targets") && j["targets"].is_object()) {
      for (auto& [channelId, targetValue] : j["targets"].items()) {
        if (targetValue.is_number()) {
          m_targetValues[channelId] = targetValue.get<float>();
          std::cout << "[LiveChartWidget] Loaded target for " << channelId
                    << ": " << m_targetValues[channelId] << std::endl;
        }
      }
    }

    std::cout << "[LiveChartWidget] Loaded " << m_targetValues.size() << " targets from config" << std::endl;
  }
  catch (const std::exception& e) {
    std::cout << "[LiveChartWidget] Error loading targets: " << e.what() << std::endl;
  }
}

void LiveChartWidget::saveTargets()
{
  try {
    nlohmann::json j;
    j["targets"] = nlohmann::json::object();

    for (const auto& [channelId, targetValue] : m_targetValues) {
      j["targets"][channelId] = targetValue;
    }

    std::ofstream file(TARGET_CONFIG_FILE);
    if (!file.is_open()) {
      std::cout << "[LiveChartWidget] ERROR: Cannot open file for writing: " << TARGET_CONFIG_FILE << std::endl;
      return;
    }

    file << j.dump(2);
    std::cout << "[LiveChartWidget] Saved " << m_targetValues.size() << " targets to config" << std::endl;
  }
  catch (const std::exception& e) {
    std::cout << "[LiveChartWidget] Error saving targets: " << e.what() << std::endl;
  }
}