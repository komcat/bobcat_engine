// QtCameraChartOverlay.cpp
#include "QtCameraChartOverlay.h"

#include "QtCameraViewport.h"
#include "data/DataStore.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMetaObject>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <iostream>
#include <vector>

namespace {

const char* kGroupStyle =
    "QGroupBox { color: #cccccc; border: 1px solid #3e3e42; border-radius: 4px; "
    "            margin-top: 6px; padding-top: 4px; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 8px; }";

const char* kButtonStyle =
    "QPushButton { background-color: #3e3e42; border: 1px solid #555; "
    "              color: #cccccc; padding: 2px 6px; border-radius: 2px; }"
    "QPushButton:hover { background-color: #505050; }"
    "QPushButton:pressed { background-color: #2d2d30; }";

}  // namespace

QtCameraChartOverlay::QtCameraChartOverlay(QtCameraViewport* viewport,
                                           std::string instanceId,
                                           QWidget* parent)
  : QGroupBox("Chart Overlay", parent)
  , m_viewport(viewport)
  , m_instanceId(std::move(instanceId))
{
  setStyleSheet(kGroupStyle);
  setupUI();
}

QtCameraChartOverlay::~QtCameraChartOverlay() = default;

void QtCameraChartOverlay::setupUI()
{
  auto* layout = new QVBoxLayout(this);
  layout->setSpacing(4);
  layout->setContentsMargins(6, 8, 6, 6);

  // Enable checkbox
  m_enableCheck = new QCheckBox("Enable Chart");
  layout->addWidget(m_enableCheck);

  // Channel list with checkboxes
  m_channelList = new QListWidget();
  m_channelList->setMaximumHeight(120);
  m_channelList->setSelectionMode(QAbstractItemView::NoSelection);
  m_channelList->setStyleSheet(
      "QListWidget { background-color: #1e1e1e; border: 1px solid #3e3e42; "
      "              color: #cccccc; font-size: 11px; }"
      "QListWidget::item { padding: 2px 4px; }"
      "QListWidget::item:hover { background-color: #2a2d2e; }"
      "QListWidget::indicator { width: 14px; height: 14px; }"
      "QListWidget::indicator:unchecked { border: 1px solid #666; background: #1e1e1e; }"
      "QListWidget::indicator:checked { border: 1px solid #0078d4; background: #0078d4; }"
  );
  layout->addWidget(m_channelList);

  // Refresh channels button
  m_refreshBtn = new QPushButton("Refresh Channels");
  m_refreshBtn->setStyleSheet(kButtonStyle);
  layout->addWidget(m_refreshBtn);

  // Scale row: label + "+" / "-" buttons
  auto* scaleRow = new QHBoxLayout();
  auto* scaleLabel = new QLabel("Scale:");
  scaleLabel->setStyleSheet("color: #cccccc;");
  m_scaleUpBtn = new QPushButton("+");
  m_scaleUpBtn->setStyleSheet(kButtonStyle);
  m_scaleUpBtn->setFixedWidth(28);
  m_scaleDnBtn = new QPushButton("-");
  m_scaleDnBtn->setStyleSheet(kButtonStyle);
  m_scaleDnBtn->setFixedWidth(28);

  scaleRow->addWidget(scaleLabel);
  scaleRow->addStretch();
  scaleRow->addWidget(m_scaleUpBtn);
  scaleRow->addWidget(m_scaleDnBtn);
  layout->addLayout(scaleRow);

  // Connections
  connect(m_enableCheck, &QCheckBox::toggled,
          this, &QtCameraChartOverlay::onChartEnableToggled);
  connect(m_refreshBtn, &QPushButton::clicked,
          this, &QtCameraChartOverlay::onChartRefreshChannels);
  connect(m_scaleUpBtn, &QPushButton::clicked,
          this, &QtCameraChartOverlay::onChartScaleIncrease);
  connect(m_scaleDnBtn, &QPushButton::clicked,
          this, &QtCameraChartOverlay::onChartScaleDecrease);
  connect(m_channelList, &QListWidget::itemChanged,
          this, [this](QListWidgetItem* item) {
    std::string channelId = item->text().toStdString();
    std::lock_guard<std::mutex> lock(m_chartMutex);
    if (item->checkState() == Qt::Checked) {
      m_chartEnabledChannels.insert(channelId);
    } else {
      m_chartEnabledChannels.erase(channelId);
    }
    if (m_viewport) m_viewport->update();
  });
}

void QtCameraChartOverlay::setDataStore(DataStore* dataStore)
{
  m_dataStore = dataStore;
  if (m_dataStore) {
    m_dataStore->SubscribeToAll([this](const std::string& channelId, float value) {
      onDataStoreValueChanged(channelId, value);
    });
  }
}

void QtCameraChartOverlay::onDataStoreValueChanged(const std::string& channelId, float value)
{
  bool isNewChannel = false;
  {
    std::lock_guard<std::mutex> lock(m_chartMutex);

    isNewChannel = (m_chartChannels.find(channelId) == m_chartChannels.end());

    auto& ch = m_chartChannels[channelId];
    ch.values.push_back(static_cast<double>(value));
    if (ch.values.size() > CHART_MAX_POINTS)
      ch.values.pop_front();
    if (value < ch.runningMin) ch.runningMin = value;
    if (value > ch.runningMax) ch.runningMax = value;
  }

  // Auto-add new channels to the list widget (must run on Qt thread)
  if (isNewChannel) {
    QMetaObject::invokeMethod(this, [this]() {
      refreshChannelList();
    }, Qt::QueuedConnection);
  }

  // Throttle repaints to ~20fps — push fresh chart data to viewport
  if (m_chartEnabled && m_chartEnabledChannels.count(channelId)) {
    const auto now     = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - m_lastChartRepaint).count();
    if (elapsed >= 50) {
      m_lastChartRepaint = now;
      QMetaObject::invokeMethod(this, [this]() {
        pushSnapshotsToViewport();
        if (m_viewport) m_viewport->update();
      }, Qt::QueuedConnection);
    }
  }
}

void QtCameraChartOverlay::refreshChannelList()
{
  if (!m_channelList || !m_dataStore) return;

  m_channelList->clear();
  const auto channels = m_dataStore->GetAvailableChannels();

  for (const auto& ch : channels) {
    auto* item = new QListWidgetItem(QString::fromStdString(ch));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    {
      std::lock_guard<std::mutex> lock(m_chartMutex);
      item->setCheckState(m_chartEnabledChannels.count(ch) ? Qt::Checked : Qt::Unchecked);
    }
    m_channelList->addItem(item);
  }
}

void QtCameraChartOverlay::pushSnapshotsToViewport()
{
  if (!m_viewport) return;

  if (m_chartEnabled) {
    std::lock_guard<std::mutex> lock(m_chartMutex);
    std::vector<QtCameraViewport::ChartChannelSnapshot> snapshots;
    for (const auto& chId : m_chartEnabledChannels) {
      auto it = m_chartChannels.find(chId);
      if (it != m_chartChannels.end() && !it->second.values.empty()) {
        snapshots.push_back({chId, it->second.values});
      }
    }
    m_viewport->setChartOverlayData(true, m_chartScale, snapshots);
  } else {
    m_viewport->setChartOverlayData(false, 1.0f, {});
  }
}

QString QtCameraChartOverlay::settingsKey(const QString& key) const
{
  return QString("ChartOverlay%1/%2")
      .arg(QString::fromStdString(m_instanceId), key);
}

void QtCameraChartOverlay::setChartEnabled(bool enabled)
{
  m_chartEnabled = enabled;
  if (m_enableCheck) m_enableCheck->setChecked(enabled);
  if (m_viewport) m_viewport->update();
}

void QtCameraChartOverlay::onChartEnableToggled(bool checked)
{
  m_chartEnabled = checked;
  if (m_viewport) m_viewport->update();
  std::cout << "[ChartOverlay] Chart overlay " << (checked ? "enabled" : "disabled") << std::endl;
}

void QtCameraChartOverlay::onChartScaleIncrease()
{
  m_chartScale = std::min(m_chartScale + 0.25f, 3.0f);
  if (m_viewport) m_viewport->update();
}

void QtCameraChartOverlay::onChartScaleDecrease()
{
  m_chartScale = std::max(m_chartScale - 0.25f, 0.5f);
  if (m_viewport) m_viewport->update();
}

void QtCameraChartOverlay::onChartRefreshChannels()
{
  refreshChannelList();
}

void QtCameraChartOverlay::saveSettings() const
{
  QSettings settings("Fabrinet", "BobcatEngine");
  settings.setValue(settingsKey("Enabled"), m_chartEnabled);
  settings.setValue(settingsKey("Scale"), static_cast<double>(m_chartScale));
  QStringList enabledChannels;
  {
    std::lock_guard<std::mutex> lock(m_chartMutex);
    for (const auto& ch : m_chartEnabledChannels) {
      enabledChannels.append(QString::fromStdString(ch));
    }
  }
  settings.setValue(settingsKey("EnabledChannels"), enabledChannels);
}

void QtCameraChartOverlay::loadSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  m_chartEnabled = settings.value(settingsKey("Enabled"), false).toBool();
  m_chartScale   = settings.value(settingsKey("Scale"), 1.0).toFloat();
  {
    std::lock_guard<std::mutex> lock(m_chartMutex);
    const QStringList savedChannels = settings.value(settingsKey("EnabledChannels")).toStringList();
    for (const auto& ch : savedChannels) {
      m_chartEnabledChannels.insert(ch.toStdString());
    }
  }
  if (m_enableCheck) m_enableCheck->setChecked(m_chartEnabled);
}
