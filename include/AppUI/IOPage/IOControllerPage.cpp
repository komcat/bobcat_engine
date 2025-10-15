// IOControllerPage.cpp
#include "IOControllerPage.h"
#include "ApplicationContext.h"
#include "EziIOManager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QCheckBox>
#include <iostream>

IOControllerPage::IOControllerPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_deviceListPanel(nullptr)
  , m_deviceList(nullptr)
  , m_refreshButton(nullptr)
  , m_ioPinPanel(nullptr)
  , m_deviceNameLabel(nullptr)
  , m_connectionStatusLabel(nullptr)
  , m_ioPinTable(nullptr)
  , m_pollTimer(nullptr)
{
  setupUI();

  m_pollTimer = new QTimer(this);
  connect(m_pollTimer, &QTimer::timeout, this, &IOControllerPage::updateIOStatus);
}

IOControllerPage::~IOControllerPage()
{
  if (m_pollTimer) {
    m_pollTimer->stop();
  }
}

void IOControllerPage::setupUI()
{
  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  createDeviceListPanel();
  mainLayout->addWidget(m_deviceListPanel, 3);

  createIOPinPanel();
  mainLayout->addWidget(m_ioPinPanel, 7);

  setStyleSheet("QWidget { background-color: #f5f6fa; }");
}

void IOControllerPage::createDeviceListPanel()
{
  m_deviceListPanel = new QWidget();
  m_deviceListPanel->setStyleSheet("QWidget { background-color: #ecf0f1; }");

  QVBoxLayout* layout = new QVBoxLayout(m_deviceListPanel);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(10);

  // Title
  QLabel* titleLabel = new QLabel("IO Devices");
  titleLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: #2c3e50; }");
  layout->addWidget(titleLabel);

  // Refresh button
  m_refreshButton = new QPushButton("🔄 Refresh");
  m_refreshButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3498db; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 8px; "
    "  font-size: 13px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #2980b9; }");
  connect(m_refreshButton, &QPushButton::clicked, this, &IOControllerPage::onRefreshClicked);
  layout->addWidget(m_refreshButton);

  // Device list
  m_deviceList = new QListWidget();
  m_deviceList->setStyleSheet(
    "QListWidget { "
    "  background-color: white; "
    "  border: 1px solid #bdc3c7; "
    "  border-radius: 4px; "
    "  padding: 5px; "
    "  font-size: 14px; "
    "  color: #2c3e50; "
    "} "
    "QListWidget::item { "
    "  padding: 10px; "
    "  border-bottom: 1px solid #ecf0f1; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #3498db; "
    "  color: white; "
    "} "
    "QListWidget::item:hover { "
    "  background-color: #dfe6e9; "
    "}");
  connect(m_deviceList, &QListWidget::itemSelectionChanged, this, &IOControllerPage::onDeviceSelected);
  layout->addWidget(m_deviceList);
}

void IOControllerPage::createIOPinPanel()
{
  m_ioPinPanel = new QWidget();

  QVBoxLayout* layout = new QVBoxLayout(m_ioPinPanel);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(15);

  // Device info header
  QHBoxLayout* headerLayout = new QHBoxLayout();

  m_deviceNameLabel = new QLabel("No device selected");
  m_deviceNameLabel->setStyleSheet("QLabel { font-size: 18px; font-weight: bold; color: #2c3e50; }");
  headerLayout->addWidget(m_deviceNameLabel);

  headerLayout->addStretch();

  m_connectionStatusLabel = new QLabel("⚪ Disconnected");
  m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 14px; color: #7f8c8d; }");
  headerLayout->addWidget(m_connectionStatusLabel);

  layout->addLayout(headerLayout);

  // IO pin table
  m_ioPinTable = new QTableWidget();
  m_ioPinTable->setColumnCount(5);
  m_ioPinTable->setHorizontalHeaderLabels({ "Pin #", "Name", "Type", "Status", "Control" });

  m_ioPinTable->setStyleSheet(
    "QTableWidget { "
    "  background-color: white; "
    "  color: #2c3e50; "
    "  gridline-color: #dcdde1; "
    "  border: 1px solid #bdc3c7; "
    "} "
    "QTableWidget::item { "
    "  padding: 8px; "
    "  border-bottom: 1px solid #ecf0f1; "
    "} "
    "QTableWidget::item:selected { "
    "  background-color: #dfe6e9; "
    "} "
    "QHeaderView::section { "
    "  background-color: #ecf0f1; "
    "  color: #2c3e50; "
    "  padding: 10px; "
    "  border: 1px solid #bdc3c7; "
    "  font-weight: bold; "
    "}");

  m_ioPinTable->horizontalHeader()->setSectionResizeMode(COL_PIN_NUMBER, QHeaderView::ResizeToContents);
  m_ioPinTable->horizontalHeader()->setSectionResizeMode(COL_PIN_NAME, QHeaderView::Stretch);
  m_ioPinTable->horizontalHeader()->setSectionResizeMode(COL_TYPE, QHeaderView::ResizeToContents);
  m_ioPinTable->horizontalHeader()->setSectionResizeMode(COL_STATUS, QHeaderView::ResizeToContents);
  m_ioPinTable->horizontalHeader()->setSectionResizeMode(COL_TOGGLE, QHeaderView::ResizeToContents);

  m_ioPinTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_ioPinTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_ioPinTable->setSelectionMode(QAbstractItemView::SingleSelection);

  m_ioPinTable->verticalHeader()->setDefaultSectionSize(40);
  m_ioPinTable->verticalHeader()->setMinimumSectionSize(40);

  layout->addWidget(m_ioPinTable);
}

void IOControllerPage::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);

  std::cout << "[IOControllerPage] Page shown - starting polling" << std::endl;

  refreshDeviceList();
  m_pollTimer->start(POLL_INTERVAL_MS);
}

void IOControllerPage::hideEvent(QHideEvent* event)
{
  QWidget::hideEvent(event);

  std::cout << "[IOControllerPage] Page hidden - stopping polling" << std::endl;

  m_pollTimer->stop();
}

void IOControllerPage::refreshDeviceList()
{
  EziIOManager* ioMgr = getIOManager();
  if (!ioMgr) {
    std::cout << "[IOControllerPage] ERROR: No IO manager" << std::endl;
    return;
  }

  std::vector<std::string> deviceNames = ioMgr->GetAllDeviceNames();

  std::cout << "[IOControllerPage] Found " << deviceNames.size() << " IO devices" << std::endl;

  m_deviceList->clear();

  for (const auto& name : deviceNames) {
    QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(name));
    m_deviceList->addItem(item);
  }

  // Auto-select first device
  if (!deviceNames.empty()) {
    m_deviceList->setCurrentRow(0);
  }
}

void IOControllerPage::onDeviceSelected()
{
  if (m_deviceList->selectedItems().isEmpty()) {
    m_selectedDevice = "";
    m_deviceNameLabel->setText("No device selected");
    m_connectionStatusLabel->setText("⚪ Disconnected");
    m_ioPinTable->setRowCount(0);
    return;
  }

  m_selectedDevice = m_deviceList->currentItem()->text().toStdString();

  std::cout << "[IOControllerPage] Device selected: " << m_selectedDevice << std::endl;

  // Update device info
  m_deviceNameLabel->setText(QString::fromStdString(m_selectedDevice));

  // Check connection status
  EziIOManager* ioMgr = getIOManager();
  if (ioMgr) {
    IEziIODevice* device = ioMgr->GetDevice(m_selectedDevice);
    if (device && device->IsConnected()) {
      m_connectionStatusLabel->setText("🟢 Connected");
      m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 14px; color: #27ae60; }");
    }
    else {
      m_connectionStatusLabel->setText("🔴 Disconnected");
      m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 14px; color: #e74c3c; }");
    }
  }

  // Refresh pin table
  refreshIOPinTable();
}

// IOControllerPage.cpp
// Find refreshIOPinTable() method (around line 235) and update it:

void IOControllerPage::refreshIOPinTable()
{
  if (m_selectedDevice.empty()) {
    return;
  }

  EziIOManager* ioMgr = getIOManager();
  if (!ioMgr) return;

  IEziIODevice* device = ioMgr->GetDevice(m_selectedDevice);
  if (!device) return;

  int inputCount = device->GetInputCount();
  int outputCount = device->GetOutputCount();

  std::cout << "[IOControllerPage] Refreshing IO table: "
    << inputCount << " inputs, " << outputCount << " outputs" << std::endl;

  m_ioPinTable->setRowCount(0);

  // Add input pins
  for (int i = 0; i < inputCount; ++i) {
    std::string pinName = device->GetInputPinName(i);  // ✅ FIXED: Get actual name
    int row = m_ioPinTable->rowCount();
    m_ioPinTable->insertRow(row);
    updatePinRow(row, i, pinName, false, false);
  }

  // Add output pins  
  for (int i = 0; i < outputCount; ++i) {
    std::string pinName = device->GetOutputPinName(i);  // ✅ FIXED: Get actual name
    int row = m_ioPinTable->rowCount();
    m_ioPinTable->insertRow(row);
    updatePinRow(row, i, pinName, true, false);
  }
}



void IOControllerPage::updatePinRow(int row, int pinNumber, const std::string& pinName,
  bool isOutput, bool currentState)
{
  // Pin number
  QTableWidgetItem* pinNumItem = new QTableWidgetItem(QString::number(pinNumber));
  pinNumItem->setTextAlignment(Qt::AlignCenter);
  m_ioPinTable->setItem(row, COL_PIN_NUMBER, pinNumItem);

  // Pin name
  QTableWidgetItem* nameItem = new QTableWidgetItem(QString::fromStdString(pinName));
  nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_ioPinTable->setItem(row, COL_PIN_NAME, nameItem);

  // Type
  QTableWidgetItem* typeItem = new QTableWidgetItem(isOutput ? "Output" : "Input");
  typeItem->setTextAlignment(Qt::AlignCenter);
  typeItem->setForeground(isOutput ? QColor("#e67e22") : QColor("#3498db"));
  m_ioPinTable->setItem(row, COL_TYPE, typeItem);

  // Status (will be updated by polling)
  QTableWidgetItem* statusItem = new QTableWidgetItem("--");
  statusItem->setTextAlignment(Qt::AlignCenter);
  m_ioPinTable->setItem(row, COL_STATUS, statusItem);

  // Toggle button (only for outputs)
  if (isOutput) {
    QCheckBox* toggleButton = new QCheckBox();
    toggleButton->setChecked(currentState);
    toggleButton->setStyleSheet(
      "QCheckBox::indicator { "
      "  width: 30px; "
      "  height: 30px; "
      "  border: 2px solid #bdc3c7; "
      "  border-radius: 4px; "
      "  background-color: white; "
      "} "
      "QCheckBox::indicator:checked { "
      "  background-color: #27ae60; "
      "  border-color: #229954; "
      "} "
      "QCheckBox::indicator:hover { "
      "  border-color: #7f8c8d; "
      "}");

    connect(toggleButton, &QCheckBox::toggled, this, [this, pinName, isOutput](bool checked) {
      EziIOManager* ioMgr = getIOManager();
      if (ioMgr && !m_selectedDevice.empty()) {
        std::cout << "[IOControllerPage] Toggle " << pinName << " -> "
          << (checked ? "ON" : "OFF") << std::endl;

        // Try to set output by name first, fall back to pin number
        EziIOError result = ioMgr->SetOutputByName(m_selectedDevice, pinName, checked);

        if (result != EziIOError::SUCCESS) {
          std::cerr << "[IOControllerPage] Failed to set output: "
            << GetErrorString(result) << std::endl;
        }
      }
    });

    QWidget* toggleWidget = new QWidget();
    QHBoxLayout* toggleLayout = new QHBoxLayout(toggleWidget);
    toggleLayout->addWidget(toggleButton);
    toggleLayout->setAlignment(Qt::AlignCenter);
    toggleLayout->setContentsMargins(0, 0, 0, 0);

    m_ioPinTable->setCellWidget(row, COL_TOGGLE, toggleWidget);
  }
  else {
    // Input - no toggle
    QTableWidgetItem* noToggle = new QTableWidgetItem("--");
    noToggle->setTextAlignment(Qt::AlignCenter);
    noToggle->setForeground(QColor("#95a5a6"));
    m_ioPinTable->setItem(row, COL_TOGGLE, noToggle);
  }
}

void IOControllerPage::updateIOStatus()
{
  if (m_selectedDevice.empty()) {
    return;
  }

  EziIOManager* ioMgr = getIOManager();
  if (!ioMgr) return;

  IEziIODevice* device = ioMgr->GetDevice(m_selectedDevice);
  if (!device || !device->IsConnected()) {
    return;
  }

  // Read inputs
  uint32_t inputs = 0, latch = 0;
  if (device->ReadInputs(inputs, latch) == EziIOError::SUCCESS) {
    int inputCount = device->GetInputCount();
    for (int i = 0; i < inputCount && i < m_ioPinTable->rowCount(); ++i) {
      bool state = (inputs & (1U << i)) != 0;

      QTableWidgetItem* statusItem = m_ioPinTable->item(i, COL_STATUS);
      if (statusItem) {
        statusItem->setText(state ? "🟢 ON" : "⚪ OFF");
        statusItem->setForeground(state ? QColor("#27ae60") : QColor("#95a5a6"));
      }
    }
  }

  // Read outputs
  uint32_t outputs = 0, status = 0;
  if (device->ReadOutputs(outputs, status) == EziIOError::SUCCESS) {
    int inputCount = device->GetInputCount();
    int outputCount = device->GetOutputCount();

    for (int i = 0; i < outputCount; ++i) {
      int row = inputCount + i;
      if (row >= m_ioPinTable->rowCount()) break;

      bool state = (outputs & (1U << i)) != 0;

      QTableWidgetItem* statusItem = m_ioPinTable->item(row, COL_STATUS);
      if (statusItem) {
        statusItem->setText(state ? "🟢 ON" : "⚪ OFF");
        statusItem->setForeground(state ? QColor("#27ae60") : QColor("#95a5a6"));
      }

      // Update toggle button to match actual state
      QWidget* toggleWidget = m_ioPinTable->cellWidget(row, COL_TOGGLE);
      if (toggleWidget) {
        QCheckBox* toggleButton = toggleWidget->findChild<QCheckBox*>();
        if (toggleButton && toggleButton->isChecked() != state) {
          toggleButton->blockSignals(true);
          toggleButton->setChecked(state);
          toggleButton->blockSignals(false);
        }
      }
    }
  }
}

void IOControllerPage::onRefreshClicked()
{
  std::cout << "[IOControllerPage] Manual refresh requested" << std::endl;
  refreshDeviceList();
}

EziIOManager* IOControllerPage::getIOManager()
{
  if (!m_context || !m_context->ioManager) {
    return nullptr;
  }
  return m_context->ioManager;
}