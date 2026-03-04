// MotionControllerPageBase.cpp
#include "MotionControllerPageBase.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "safety/SafetyWatchManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <iostream>

MotionControllerPageBase::MotionControllerPageBase(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_deviceTable(nullptr)
  , m_refreshButton(nullptr)
  , m_pollTimer(nullptr)
{
  // Don't call setupUI here - virtual functions not ready
  // Call initialize() after derived class is constructed
}

void MotionControllerPageBase::initialize()
{
  setupUI();
  setupPolling();
}

MotionControllerPageBase::~MotionControllerPageBase()
{
  if (m_pollTimer) {
    m_pollTimer->stop();
  }
}

std::vector<std::string> MotionControllerPageBase::getAxisNames() const
{
  // Default: X, Y, Z
  return { "X", "Y", "Z" };
}

QString MotionControllerPageBase::getPageTitle() const
{
  return QString::fromStdString(getControllerType() + " Controllers");
}

void MotionControllerPageBase::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(15);

  // Title bar with refresh button
  QHBoxLayout* titleLayout = new QHBoxLayout();

  QLabel* titleLabel = new QLabel(getPageTitle());
  titleLabel->setStyleSheet("QLabel { font-size: 20px; font-weight: bold; color: #ffffff; }");
  titleLayout->addWidget(titleLabel);

  titleLayout->addStretch();

  m_refreshButton = new QPushButton("🔄 Refresh");
  m_refreshButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #0e639c; "
    "  color: white; "
    "  border: 1px solid #0c5280; "
    "  border-radius: 5px; "
    "  padding: 8px 15px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #1177bb; }");
  connect(m_refreshButton, &QPushButton::clicked, this, &MotionControllerPageBase::onRefreshClicked);
  titleLayout->addWidget(m_refreshButton);

  mainLayout->addLayout(titleLayout);

  // Device table
  createDeviceTable();
  mainLayout->addWidget(m_deviceTable);

  // Dark background
  setStyleSheet("QWidget { background-color: #2d2d30; color: #d4d4d4; }");
}

void MotionControllerPageBase::createDeviceTable()
{
  m_deviceTable = new QTableWidget();

  std::vector<std::string> axisNames = getAxisNames();

  // Setup columns: Name | Enabled | Connected | Axis1 | Axis2 | Axis3 | Action
  QStringList headers;
  headers << "Device Name" << "Enabled" << "Connected";
  for (const auto& axis : axisNames) {
    headers << QString::fromStdString(axis + " Position");
  }
  headers << "Action";

  m_deviceTable->setColumnCount(headers.size());
  m_deviceTable->setHorizontalHeaderLabels(headers);

  // Dark theme style
  m_deviceTable->setStyleSheet(
    "QTableWidget { "
    "  background-color: #1e1e1e; "
    "  color: #d4d4d4; "
    "  gridline-color: #3f3f46; "
    "  border: 1px solid #3f3f46; "
    "} "
    "QTableWidget::item { "
    "  padding: 8px; "
    "  border-bottom: 1px solid #3f3f46; "
    "} "
    "QTableWidget::item:selected { "
    "  background-color: #0e639c; "
    "  color: #ffffff; "
    "} "
    "QHeaderView::section { "
    "  background-color: #2d2d30; "
    "  color: #ffffff; "
    "  padding: 10px; "
    "  border: 1px solid #3f3f46; "
    "  font-weight: bold; "
    "}");

  m_deviceTable->horizontalHeader()->setStretchLastSection(false);
  m_deviceTable->horizontalHeader()->setSectionResizeMode(COL_NAME, QHeaderView::Stretch);

  // Set minimum width for Action column to prevent button text cutoff
  int actionColIdx = COL_AXIS_START + static_cast<int>(axisNames.size());
  m_deviceTable->setColumnWidth(actionColIdx, 120);
  m_deviceTable->horizontalHeader()->setSectionResizeMode(actionColIdx, QHeaderView::Fixed);

  m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_deviceTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

  // Set minimum row height for buttons
  m_deviceTable->verticalHeader()->setDefaultSectionSize(45);
  m_deviceTable->verticalHeader()->setMinimumSectionSize(45);
}

void MotionControllerPageBase::setupPolling()
{
  m_pollTimer = new QTimer(this);
  connect(m_pollTimer, &QTimer::timeout, this, &MotionControllerPageBase::updatePositions);
  // Timer will start in showEvent()

  // Resume polling when E-Stop is cleared (if page is visible)
  if (m_context && m_context->safetyWatchManager) {
    connect(m_context->safetyWatchManager, &SafetyWatchManager::safetyCleared,
            this, [this]() {
      if (isVisible() && m_pollTimer && !m_pollTimer->isActive()) {
        m_pollTimer->start(POLL_INTERVAL_MS);
      }
    });
  }
}

void MotionControllerPageBase::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);

  std::cout << "[MotionControllerPageBase] Page shown - starting polling" << std::endl;

  // Don't start polling during E-Stop - hardware may be unresponsive
  if (m_context && m_context->safetyWatchManager && m_context->safetyWatchManager->IsCriticalFlagRaised()) {
    std::cout << "[MotionControllerPageBase] E-Stop active - skipping polling" << std::endl;
    return;
  }

  // Refresh device list
  refreshDeviceList();

  // Start position polling
  m_pollTimer->start(POLL_INTERVAL_MS);
}

void MotionControllerPageBase::hideEvent(QHideEvent* event)
{
  QWidget::hideEvent(event);

  std::cout << "[MotionControllerPageBase] Page hidden - stopping polling" << std::endl;

  // Stop polling to save CPU
  m_pollTimer->stop();
}

void MotionControllerPageBase::refreshDeviceList()
{
  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) {
    std::cout << "[MotionControllerPageBase] ERROR: No motion manager" << std::endl;
    return;
  }

  std::string controllerType = getControllerType();
  std::vector<std::string> deviceNames = motionMgr->GetDeviceNamesByType(controllerType);

  std::cout << "[MotionControllerPageBase] Refreshing device list for type: "
    << controllerType << " (" << deviceNames.size() << " devices)" << std::endl;

  // Clear and rebuild table (m_deviceTable may be null if subclass overrides setupUI)
  if (!m_deviceTable) return;

  m_deviceTable->setRowCount(0);

  for (const auto& deviceName : deviceNames) {
    int row = m_deviceTable->rowCount();
    m_deviceTable->insertRow(row);
    updateDeviceRow(row, deviceName);
  }
}

void MotionControllerPageBase::updateDeviceRow(int row, const std::string& deviceName)
{
  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) return;

  std::vector<std::string> axisNames = getAxisNames();
  int actionCol = getActionColumnIndex();

  // Device name
  QTableWidgetItem* nameItem = new QTableWidgetItem(QString::fromStdString(deviceName));
  nameItem->setTextAlignment(Qt::AlignCenter);
  nameItem->setForeground(QColor("#d4d4d4"));
  m_deviceTable->setItem(row, COL_NAME, nameItem);

  // Enabled status (TODO: get from config)
  QTableWidgetItem* enabledItem = new QTableWidgetItem("✓");
  enabledItem->setTextAlignment(Qt::AlignCenter);
  enabledItem->setForeground(QColor("#27ae60"));
  m_deviceTable->setItem(row, COL_ENABLED, enabledItem);

  // Connection status
  std::map<std::string, bool> status = motionMgr->GetConnectionStatus();
  bool isConnected = status[deviceName];

  QTableWidgetItem* connectedItem = new QTableWidgetItem(isConnected ? "🟢 Connected" : "🔴 Disconnected");
  connectedItem->setTextAlignment(Qt::AlignCenter);
  connectedItem->setForeground(isConnected ? QColor("#27ae60") : QColor("#e74c3c"));
  m_deviceTable->setItem(row, COL_CONNECTED, connectedItem);

  // Position columns (will be updated by polling)
  for (size_t i = 0; i < axisNames.size(); ++i) {
    QTableWidgetItem* posItem = new QTableWidgetItem("--");
    posItem->setTextAlignment(Qt::AlignCenter);
    posItem->setForeground(QColor("#888888"));
    m_deviceTable->setItem(row, COL_AXIS_START + static_cast<int>(i), posItem);
  }

  // Connect/Disconnect button
  QPushButton* actionButton = new QPushButton(isConnected ? "Disconnect" : "Connect");
  actionButton->setStyleSheet(
    "QPushButton { "
    "  background-color: " + QString(isConnected ? "#e74c3c" : "#27ae60") + "; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 8px 16px; "
    "  font-weight: bold; "
    "  font-size: 13px; "
    "} "
    "QPushButton:hover { "
    "  background-color: " + QString(isConnected ? "#c0392b" : "#229954") + "; "
    "}");

  if (isConnected) {
    connect(actionButton, &QPushButton::clicked, this, [this, deviceName]() {
      onDisconnectClicked(deviceName);
    });
  }
  else {
    connect(actionButton, &QPushButton::clicked, this, [this, deviceName]() {
      onConnectClicked(deviceName);
    });
  }

  m_deviceTable->setCellWidget(row, actionCol, actionButton);
}

void MotionControllerPageBase::updatePositions()
{
  // Skip hardware access during E-Stop
  if (m_context && m_context->safetyWatchManager && m_context->safetyWatchManager->IsCriticalFlagRaised()) {
    m_pollTimer->stop();
    return;
  }

  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) return;

  std::vector<std::string> axisNames = getAxisNames();

  if (!m_deviceTable) return;

  for (int row = 0; row < m_deviceTable->rowCount(); ++row) {
    QTableWidgetItem* nameItem = m_deviceTable->item(row, COL_NAME);
    if (!nameItem) continue;

    std::string deviceName = nameItem->text().toStdString();

    // Check if connected
    std::map<std::string, bool> status = motionMgr->GetConnectionStatus();
    if (!status[deviceName]) {
      // Not connected - show "--"
      for (size_t i = 0; i < axisNames.size(); ++i) {
        QTableWidgetItem* posItem = m_deviceTable->item(row, COL_AXIS_START + static_cast<int>(i));
        if (posItem) {
          posItem->setText("--");
          posItem->setForeground(QColor("#666666"));
        }
      }
      continue;
    }

    // Update positions for each axis
    for (size_t i = 0; i < axisNames.size(); ++i) {
      double position = 0.0;
      bool success = motionMgr->GetPosition(deviceName, axisNames[i], position);

      QTableWidgetItem* posItem = m_deviceTable->item(row, COL_AXIS_START + static_cast<int>(i));
      if (posItem) {
        if (success) {
          posItem->setText(QString::number(position, 'f', 3));
          posItem->setForeground(QColor("#d4d4d4"));
        }
        else {
          posItem->setText("--");
          posItem->setForeground(QColor("#666666"));
        }
      }
    }
  }
}

void MotionControllerPageBase::onConnectClicked(const std::string& deviceName)
{
  std::cout << "[MotionControllerPageBase] Connecting device: " << deviceName << std::endl;

  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) return;

  bool success = motionMgr->ConnectDevice(deviceName);

  if (success) {
    std::cout << "[MotionControllerPageBase] Connected: " << deviceName << std::endl;
  }
  else {
    std::cout << "[MotionControllerPageBase] Failed to connect: " << deviceName << std::endl;
  }

  // Refresh the device list to update UI
  refreshDeviceList();
}

void MotionControllerPageBase::onDisconnectClicked(const std::string& deviceName)
{
  std::cout << "[MotionControllerPageBase] Disconnecting device: " << deviceName << std::endl;

  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) return;

  bool success = motionMgr->DisconnectDevice(deviceName);

  if (success) {
    std::cout << "[MotionControllerPageBase] Disconnected: " << deviceName << std::endl;
  }
  else {
    std::cout << "[MotionControllerPageBase] Failed to disconnect: " << deviceName << std::endl;
  }

  // Refresh the device list to update UI
  refreshDeviceList();
}

void MotionControllerPageBase::onRefreshClicked()
{
  std::cout << "[MotionControllerPageBase] Manual refresh requested" << std::endl;
  refreshDeviceList();
}

UnifiedMotionManager* MotionControllerPageBase::getMotionManager()
{
  if (!m_context || !m_context->motionManager) {
    return nullptr;
  }
  return m_context->motionManager;  // Already a raw pointer, no .get() needed
}

int MotionControllerPageBase::findDeviceRow(const std::string& deviceName) const
{
  if (!m_deviceTable) return -1;
  for (int row = 0; row < m_deviceTable->rowCount(); ++row) {
    QTableWidgetItem* nameItem = m_deviceTable->item(row, COL_NAME);
    if (nameItem && nameItem->text().toStdString() == deviceName) {
      return row;
    }
  }
  return -1;
}