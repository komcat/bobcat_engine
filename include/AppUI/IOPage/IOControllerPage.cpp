// IOControllerPage.cpp
#include "IOControllerPage.h"
#include "ApplicationContext.h"
#include "EziIOManager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QCheckBox>
#include <QFrame>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

IOControllerPage::IOControllerPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_deviceListPanel(nullptr)
  , m_deviceButtonLayout(nullptr)
  , m_deviceButtonGroup(nullptr)
  , m_refreshButton(nullptr)
  , m_ioPinPanel(nullptr)
  , m_deviceNameLabel(nullptr)
  , m_connectionStatusLabel(nullptr)
  , m_inputPinsTable(nullptr)
  , m_outputPinsTable(nullptr)
  , m_restartWarningLabel(nullptr)
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
  mainLayout->addWidget(m_deviceListPanel, 1);  // 5% width

  createIOPinPanel();
  mainLayout->addWidget(m_ioPinPanel, 19);  // 95% width

  setStyleSheet("QWidget { background-color: #1e1e1e; }");  // Dark theme
}

void IOControllerPage::createDeviceListPanel()
{
  m_deviceListPanel = new QWidget();
  m_deviceListPanel->setStyleSheet("QWidget { background-color: #34495e; }");
  m_deviceListPanel->setMaximumWidth(80);  // Constrain width for narrow column

  QVBoxLayout* layout = new QVBoxLayout(m_deviceListPanel);
  layout->setContentsMargins(5, 10, 5, 10);
  layout->setSpacing(8);

  // Refresh button (icon only)
  m_refreshButton = new QPushButton("🔄");
  m_refreshButton->setToolTip("Refresh device list");
  m_refreshButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5dade2, stop:1 #3498db); "
    "  color: white; "
    "  padding: 8px; "
    "  font-size: 16px; "
    "  min-height: 40px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6dbdf2, stop:1 #5dade2); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2874a6; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 10px; "
    "  padding-left: 10px; "
    "}");
  connect(m_refreshButton, &QPushButton::clicked, this, &IOControllerPage::onRefreshClicked);
  layout->addWidget(m_refreshButton);

  // Separator
  QFrame* separator = new QFrame();
  separator->setFrameShape(QFrame::HLine);
  separator->setStyleSheet("QFrame { background-color: #7f8c8d; }");
  layout->addWidget(separator);

  // Device button container
  m_deviceButtonLayout = new QVBoxLayout();
  m_deviceButtonLayout->setContentsMargins(0, 0, 0, 0);
  m_deviceButtonLayout->setSpacing(5);
  layout->addLayout(m_deviceButtonLayout);

  // Button group for exclusive selection
  m_deviceButtonGroup = new QButtonGroup(this);
  m_deviceButtonGroup->setExclusive(true);
  connect(m_deviceButtonGroup, QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked),
    this, [this](QAbstractButton* /*button*/) { onDeviceSelected(); });

  layout->addStretch();
}

void IOControllerPage::createIOPinPanel()
{
  m_ioPinPanel = new QWidget();

  QVBoxLayout* layout = new QVBoxLayout(m_ioPinPanel);
  layout->setContentsMargins(15, 15, 15, 15);
  layout->setSpacing(10);

  // ============================================================================
  // HEADER - Minimal
  // ============================================================================
  QHBoxLayout* headerLayout = new QHBoxLayout();

  m_deviceNameLabel = new QLabel("No device selected");
  m_deviceNameLabel->setStyleSheet("QLabel { font-size: 18px; font-weight: 600; color: #e0e0e0; }");
  headerLayout->addWidget(m_deviceNameLabel);

  headerLayout->addStretch();

  m_connectionStatusLabel = new QLabel("● Disconnected");
  m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 14px; color: #666; }");
  headerLayout->addWidget(m_connectionStatusLabel);

  layout->addLayout(headerLayout);

  // ============================================================================
  // RESTART WARNING LABEL - Initially hidden
  // ============================================================================
  m_restartWarningLabel = new QLabel("⚠ Pin names changed. Restart application for changes to take effect.");
  m_restartWarningLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #FF9800; "
    "  color: #000; "
    "  padding: 8px 15px; "
    "  border-radius: 4px; "
    "  font-weight: bold; "
    "  font-size: 12px; "
    "}"
  );
  m_restartWarningLabel->setVisible(false);
  layout->addWidget(m_restartWarningLabel);

  // ============================================================================
  // PIN TABLES - Clean Minimal Style
  // ============================================================================
  QHBoxLayout* tablesLayout = new QHBoxLayout();
  tablesLayout->setSpacing(15);

  // Minimal group box style
  QString groupBoxStyle =
    "QGroupBox { "
    "  font-size: 13px; "
    "  font-weight: 600; "
    "  color: #aaa; "
    "  background-color: #252525; "
    "  border: 1px solid #333; "
    "  border-radius: 4px; "
    "  margin-top: 8px; "
    "  padding-top: 8px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "}";

  // Clean flat table style
  QString tableStyle =
    "QTableWidget { "
    "  background-color: #1a1a1a; "
    "  alternate-background-color: #202020; "
    "  color: #ccc; "
    "  gridline-color: #2a2a2a; "
    "  border: none; "
    "  font-size: 12px; "
    "  selection-background-color: #333; "
    "} "
    "QTableWidget::item { "
    "  padding: 4px 8px; "
    "  border: none; "
    "} "
    "QHeaderView::section { "
    "  background-color: #1a1a1a; "
    "  color: #888; "
    "  padding: 6px 8px; "
    "  border: none; "
    "  border-bottom: 1px solid #333; "
    "  font-weight: 600; "
    "  font-size: 11px; "
    "  text-transform: uppercase; "
    "}";

  // ============================================================================
  // COLUMN 1: INPUT PINS - Minimal (removed Type column)
  // ============================================================================
  QGroupBox* inputGroup = new QGroupBox("Inputs");
  inputGroup->setStyleSheet(groupBoxStyle);

  QVBoxLayout* inputLayout = new QVBoxLayout(inputGroup);
  inputLayout->setContentsMargins(5, 10, 5, 5);

  m_inputPinsTable = new QTableWidget();
  m_inputPinsTable->setColumnCount(3);  // Removed Type column
  m_inputPinsTable->setHorizontalHeaderLabels({ "#", "Name", "Status" });

  m_inputPinsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  m_inputPinsTable->horizontalHeader()->resizeSection(0, 40);
  m_inputPinsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_inputPinsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
  m_inputPinsTable->horizontalHeader()->resizeSection(2, 60);

  m_inputPinsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_inputPinsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_inputPinsTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_inputPinsTable->setAlternatingRowColors(true);
  m_inputPinsTable->verticalHeader()->setVisible(false);  // Hide row numbers
  m_inputPinsTable->verticalHeader()->setDefaultSectionSize(32);  // Compact rows
  m_inputPinsTable->setShowGrid(false);
  m_inputPinsTable->setStyleSheet(tableStyle);

  // Enable right-click context menu for renaming
  m_inputPinsTable->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_inputPinsTable, &QTableWidget::customContextMenuRequested,
    this, &IOControllerPage::onInputTableContextMenu);

  inputLayout->addWidget(m_inputPinsTable);
  tablesLayout->addWidget(inputGroup, 1);

  // ============================================================================
  // COLUMN 2: OUTPUT PINS - Minimal (removed Type column)
  // ============================================================================
  QGroupBox* outputGroup = new QGroupBox("Outputs");
  outputGroup->setStyleSheet(groupBoxStyle);

  QVBoxLayout* outputLayout = new QVBoxLayout(outputGroup);
  outputLayout->setContentsMargins(5, 10, 5, 5);

  m_outputPinsTable = new QTableWidget();
  m_outputPinsTable->setColumnCount(4);  // Removed Type column
  m_outputPinsTable->setHorizontalHeaderLabels({ "", "#", "Name", "Status" });  // Empty header for toggle

  m_outputPinsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  m_outputPinsTable->horizontalHeader()->resizeSection(0, 50);  // Toggle
  m_outputPinsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
  m_outputPinsTable->horizontalHeader()->resizeSection(1, 40);  // Pin #
  m_outputPinsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);  // Name
  m_outputPinsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
  m_outputPinsTable->horizontalHeader()->resizeSection(3, 60);  // Status

  m_outputPinsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_outputPinsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_outputPinsTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_outputPinsTable->setAlternatingRowColors(true);
  m_outputPinsTable->verticalHeader()->setVisible(false);  // Hide row numbers
  m_outputPinsTable->verticalHeader()->setDefaultSectionSize(32);  // Compact rows
  m_outputPinsTable->setShowGrid(false);
  m_outputPinsTable->setStyleSheet(tableStyle);

  // Enable right-click context menu for renaming
  m_outputPinsTable->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_outputPinsTable, &QTableWidget::customContextMenuRequested,
    this, &IOControllerPage::onOutputTableContextMenu);

  outputLayout->addWidget(m_outputPinsTable);
  tablesLayout->addWidget(outputGroup, 1);

  layout->addLayout(tablesLayout);
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

  // Clear existing buttons
  QList<QAbstractButton*> buttons = m_deviceButtonGroup->buttons();
  for (QAbstractButton* btn : buttons) {
    m_deviceButtonGroup->removeButton(btn);
    m_deviceButtonLayout->removeWidget(btn);
    btn->deleteLater();
  }

  // Create button for each device
  QString buttonStyle =
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3c4e60, stop:1 #2c3e50); "
    "  color: white; "
    "  padding: 10px 5px; "
    "  font-size: 11px; "
    "  font-weight: bold; "
    "  text-align: center; "
    "  min-height: 50px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4c5e70, stop:1 #3c4e60); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-color: #3498db; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #1c2e40; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 12px; "
    "  padding-left: 7px; "
    "} "
    "QPushButton:checked { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5dade2, stop:1 #3498db); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "}";

  for (const auto& name : deviceNames) {
    QPushButton* deviceButton = new QPushButton(QString::fromStdString(name));
    deviceButton->setCheckable(true);
    deviceButton->setStyleSheet(buttonStyle);
    deviceButton->setToolTip(QString::fromStdString(name));

    m_deviceButtonLayout->addWidget(deviceButton);
    m_deviceButtonGroup->addButton(deviceButton);
  }

  // Auto-select first device
  if (!deviceNames.empty()) {
    QAbstractButton* firstButton = m_deviceButtonGroup->buttons().first();
    if (firstButton) {
      firstButton->setChecked(true);
      onDeviceSelected();
    }
  }
}

void IOControllerPage::onDeviceSelected()
{
  QAbstractButton* selectedButton = m_deviceButtonGroup->checkedButton();
  if (!selectedButton) {
    m_selectedDevice = "";
    m_deviceNameLabel->setText("No device selected");
    m_connectionStatusLabel->setText("⚪ Disconnected");
    m_inputPinsTable->setRowCount(0);
    m_outputPinsTable->setRowCount(0);
    return;
  }

  m_selectedDevice = selectedButton->text().toStdString();

  std::cout << "[IOControllerPage] Device selected: " << m_selectedDevice << std::endl;

  // Update device info
  m_deviceNameLabel->setText(QString::fromStdString(m_selectedDevice));

  // Check connection status - minimal style
  EziIOManager* ioMgr = getIOManager();
  if (ioMgr) {
    IEziIODevice* device = ioMgr->GetDevice(m_selectedDevice);
    if (device && device->IsConnected()) {
      m_connectionStatusLabel->setText("● Connected");
      m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 14px; color: #4CAF50; }");
    }
    else {
      m_connectionStatusLabel->setText("● Disconnected");
      m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 14px; color: #666; }");
    }
  }

  // Refresh pin table
  refreshIOPinTable();
}

void IOControllerPage::refreshIOPinTable()
{
  if (m_selectedDevice.empty()) {
    return;
  }

  EziIOManager* ioMgr = getIOManager();
  if (!ioMgr) return;

  IEziIODevice* device = ioMgr->GetDevice(m_selectedDevice);
  if (!device) return;

  // Don't show IO list if device is not connected
  if (!device->IsConnected()) {
    m_inputPinsTable->setRowCount(0);
    m_outputPinsTable->setRowCount(0);
    std::cout << "[IOControllerPage] Device not connected, hiding IO tables" << std::endl;
    return;
  }

  int inputCount = device->GetInputCount();
  int outputCount = device->GetOutputCount();

  std::cout << "[IOControllerPage] Refreshing IO table: "
    << inputCount << " inputs, " << outputCount << " outputs" << std::endl;

  // Clear tables
  m_inputPinsTable->setRowCount(0);
  m_outputPinsTable->setRowCount(0);

  // Add input pins
  for (int i = 0; i < inputCount; ++i) {
    std::string pinName = device->GetInputPinName(i);
    int row = m_inputPinsTable->rowCount();
    m_inputPinsTable->insertRow(row);
    updatePinRow(m_inputPinsTable, row, i, pinName, false, false);
  }

  // Add output pins  
  for (int i = 0; i < outputCount; ++i) {
    std::string pinName = device->GetOutputPinName(i);
    int row = m_outputPinsTable->rowCount();
    m_outputPinsTable->insertRow(row);
    updatePinRow(m_outputPinsTable, row, i, pinName, true, false);
  }
}

void IOControllerPage::updatePinRow(QTableWidget* table, int row, int pinNumber,
  const std::string& pinName, bool isOutput, bool currentState)
{
  // Minimal layout:
  // For outputs: Toggle, #, Name, Status (columns 0-3)
  // For inputs: #, Name, Status (columns 0-2)

  int colOffset = isOutput ? 1 : 0;

  // Toggle button for outputs (column 0)
  if (isOutput) {
    QCheckBox* toggleButton = new QCheckBox();
    toggleButton->setChecked(currentState);
    toggleButton->setStyleSheet(
      "QCheckBox::indicator { "
      "  width: 24px; "
      "  height: 24px; "
      "  border: 1px solid #444; "
      "  border-radius: 4px; "
      "  background-color: #2a2a2a; "
      "} "
      "QCheckBox::indicator:checked { "
      "  background-color: #4CAF50; "
      "  border-color: #4CAF50; "
      "} "
      "QCheckBox::indicator:hover { "
      "  border-color: #666; "
      "}");

    connect(toggleButton, &QCheckBox::toggled, this, [this, pinName](bool checked) {
      EziIOManager* ioMgr = getIOManager();
      if (ioMgr && !m_selectedDevice.empty()) {
        EziIOError result = ioMgr->SetOutputByName(m_selectedDevice, pinName, checked);
        if (result != EziIOError::SUCCESS) {
          std::cerr << "[IO] Failed to set " << pinName << std::endl;
        }
      }
    });

    QWidget* toggleWidget = new QWidget();
    toggleWidget->setStyleSheet("background: transparent;");
    QHBoxLayout* toggleLayout = new QHBoxLayout(toggleWidget);
    toggleLayout->addWidget(toggleButton);
    toggleLayout->setAlignment(Qt::AlignCenter);
    toggleLayout->setContentsMargins(0, 0, 0, 0);

    table->setCellWidget(row, 0, toggleWidget);
  }

  // Pin number
  QTableWidgetItem* pinNumItem = new QTableWidgetItem(QString::number(pinNumber));
  pinNumItem->setTextAlignment(Qt::AlignCenter);
  pinNumItem->setForeground(QColor("#666"));
  table->setItem(row, 0 + colOffset, pinNumItem);

  // Pin name
  QTableWidgetItem* nameItem = new QTableWidgetItem(QString::fromStdString(pinName));
  nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  table->setItem(row, 1 + colOffset, nameItem);

  // Status (will be updated by polling) - column 2 for inputs, column 3 for outputs
  QTableWidgetItem* statusItem = new QTableWidgetItem("--");
  statusItem->setTextAlignment(Qt::AlignCenter);
  statusItem->setForeground(QColor("#666"));
  table->setItem(row, 2 + colOffset, statusItem);
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
  uint64_t inputs = 0, latch = 0;
  if (device->ReadInputs(inputs, latch) == EziIOError::SUCCESS) {
    int inputCount = device->GetInputCount();
    for (int i = 0; i < inputCount && i < m_inputPinsTable->rowCount(); ++i) {
      bool state = (inputs & (1ULL << i)) != 0;

      // Update status text (column 2 for inputs: #, Name, Status)
      QTableWidgetItem* statusItem = m_inputPinsTable->item(i, 2);
      if (statusItem) {
        statusItem->setText(state ? "ON" : "OFF");
        statusItem->setForeground(state ? QColor("#4CAF50") : QColor("#666"));
      }

      // Subtle row highlight for ON state
      for (int col = 0; col < m_inputPinsTable->columnCount(); ++col) {
        QTableWidgetItem* item = m_inputPinsTable->item(i, col);
        if (item) {
          if (state) {
            item->setBackground(QColor("#1a2f1a"));  // Subtle green tint
            item->setForeground(col == 2 ? QColor("#4CAF50") : QColor("#ccc"));
          } else {
            item->setBackground(QColor(i % 2 == 0 ? 0x1a1a1a : 0x202020));  // Alternating
            item->setForeground(col == 2 ? QColor("#666") : QColor("#999"));
          }
        }
      }
    }
  }

  // Read outputs
  uint64_t outputs = 0, status = 0;
  if (device->ReadOutputs(outputs, status) == EziIOError::SUCCESS) {
    int outputCount = device->GetOutputCount();

    // Outputs use bits 8-15 for 8-pin, or bits 16-31 for 16-pin devices
    bool is16Pin = (outputCount > 8);
    int bitOffset = is16Pin ? 16 : 8;

    for (int i = 0; i < outputCount; ++i) {
      int row = i;
      if (row >= m_outputPinsTable->rowCount()) break;

      bool state = (outputs & (1ULL << (bitOffset + i))) != 0;

      // Update status text (column 3 for outputs: Toggle, #, Name, Status)
      QTableWidgetItem* statusItem = m_outputPinsTable->item(row, 3);
      if (statusItem) {
        statusItem->setText(state ? "ON" : "OFF");
        statusItem->setForeground(state ? QColor("#4CAF50") : QColor("#666"));
      }

      // Update toggle button to match actual state (column 0)
      QWidget* toggleWidget = m_outputPinsTable->cellWidget(row, 0);
      if (toggleWidget) {
        QCheckBox* toggleButton = toggleWidget->findChild<QCheckBox*>();
        if (toggleButton && toggleButton->isChecked() != state) {
          toggleButton->blockSignals(true);
          toggleButton->setChecked(state);
          toggleButton->blockSignals(false);
        }
        toggleWidget->setStyleSheet("background: transparent;");
      }

      // Subtle row highlight for ON state (skip column 0 toggle widget)
      for (int col = 1; col < m_outputPinsTable->columnCount(); ++col) {
        QTableWidgetItem* item = m_outputPinsTable->item(row, col);
        if (item) {
          if (state) {
            item->setBackground(QColor("#1a2f1a"));  // Subtle green tint
            item->setForeground(col == 3 ? QColor("#4CAF50") : QColor("#ccc"));
          } else {
            item->setBackground(QColor(row % 2 == 0 ? 0x1a1a1a : 0x202020));  // Alternating
            item->setForeground(col == 3 ? QColor("#666") : QColor("#999"));
          }
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

// ============================================================================
// CONTEXT MENU HANDLERS
// ============================================================================

void IOControllerPage::onInputTableContextMenu(const QPoint& pos)
{
  QTableWidgetItem* item = m_inputPinsTable->itemAt(pos);
  if (!item) return;

  int row = item->row();

  // Get pin number from column 0 (# column for inputs)
  QTableWidgetItem* pinItem = m_inputPinsTable->item(row, 0);
  if (!pinItem) return;

  int pinNumber = pinItem->text().toInt();

  QMenu contextMenu(this);
  contextMenu.setStyleSheet(
    "QMenu { background-color: #2a2a2a; color: #ccc; border: 1px solid #444; }"
    "QMenu::item { padding: 6px 20px; }"
    "QMenu::item:selected { background-color: #3498db; color: white; }"
  );

  QAction* renameAction = contextMenu.addAction("✏️ Rename Pin...");
  connect(renameAction, &QAction::triggered, this, [this, pinNumber]() {
    showRenameDialog(pinNumber, false);
  });

  contextMenu.exec(m_inputPinsTable->viewport()->mapToGlobal(pos));
}

void IOControllerPage::onOutputTableContextMenu(const QPoint& pos)
{
  QTableWidgetItem* item = m_outputPinsTable->itemAt(pos);
  if (!item) return;

  int row = item->row();

  // Get pin number from column 1 (# column for outputs, column 0 is toggle)
  QTableWidgetItem* pinItem = m_outputPinsTable->item(row, 1);
  if (!pinItem) return;

  int pinNumber = pinItem->text().toInt();

  QMenu contextMenu(this);
  contextMenu.setStyleSheet(
    "QMenu { background-color: #2a2a2a; color: #ccc; border: 1px solid #444; }"
    "QMenu::item { padding: 6px 20px; }"
    "QMenu::item:selected { background-color: #3498db; color: white; }"
  );

  QAction* renameAction = contextMenu.addAction("✏️ Rename Pin...");
  connect(renameAction, &QAction::triggered, this, [this, pinNumber]() {
    showRenameDialog(pinNumber, true);
  });

  contextMenu.exec(m_outputPinsTable->viewport()->mapToGlobal(pos));
}

void IOControllerPage::showRenameDialog(int pinNumber, bool isOutput)
{
  if (m_selectedDevice.empty()) return;

  EziIOManager* ioMgr = getIOManager();
  if (!ioMgr) return;

  IEziIODevice* device = ioMgr->GetDevice(m_selectedDevice);
  if (!device) return;

  // Get current name
  std::string currentName = isOutput
    ? device->GetOutputPinName(pinNumber)
    : device->GetInputPinName(pinNumber);

  // Show input dialog
  bool ok;
  QString newName = QInputDialog::getText(
    this,
    isOutput ? "Rename Output Pin" : "Rename Input Pin",
    QString("Enter new name for %1 pin %2:")
      .arg(isOutput ? "output" : "input")
      .arg(pinNumber),
    QLineEdit::Normal,
    QString::fromStdString(currentName),
    &ok
  );

  if (ok && !newName.isEmpty() && newName.toStdString() != currentName) {
    // Save to local config
    if (savePinNameToLocalConfig(m_selectedDevice, pinNumber, newName.toStdString(), isOutput)) {
      // Update the table immediately for visual feedback
      if (isOutput) {
        QTableWidgetItem* nameItem = m_outputPinsTable->item(pinNumber, 2);  // Name is column 2 for outputs
        if (nameItem) {
          nameItem->setText(newName);
          nameItem->setForeground(QColor("#FFEB3B"));  // Yellow to indicate changed
        }
      } else {
        QTableWidgetItem* nameItem = m_inputPinsTable->item(pinNumber, 1);  // Name is column 1 for inputs
        if (nameItem) {
          nameItem->setText(newName);
          nameItem->setForeground(QColor("#FFEB3B"));  // Yellow to indicate changed
        }
      }

      // Show restart warning
      m_restartWarningLabel->setVisible(true);

      std::cout << "[IOControllerPage] Pin renamed: " << m_selectedDevice << " "
        << (isOutput ? "output" : "input") << " " << pinNumber
        << " -> " << newName.toStdString() << std::endl;
    } else {
      QMessageBox::warning(this, "Error", "Failed to save pin name to configuration file.");
    }
  }
}

std::string IOControllerPage::getLocalConfigPath() const
{
  QString appDir = QCoreApplication::applicationDirPath();
  return (appDir + "/config/io_devices.local.json").toStdString();
}

bool IOControllerPage::savePinNameToLocalConfig(const std::string& deviceName, int pinNumber,
  const std::string& newName, bool isOutput)
{
  std::string localConfigPath = getLocalConfigPath();
  nlohmann::json localConfig;

  // Load existing local config
  std::ifstream inFile(localConfigPath);
  if (!inFile.is_open()) {
    std::cerr << "[IOControllerPage] Failed to open local config for reading: " << localConfigPath << std::endl;
    return false;
  }

  try {
    inFile >> localConfig;
  } catch (const std::exception& e) {
    std::cerr << "[IOControllerPage] Error parsing local config: " << e.what() << std::endl;
    inFile.close();
    return false;
  }
  inFile.close();

  // Find the device in eziio array and update the pin name
  bool found = false;
  if (localConfig.contains("eziio") && localConfig["eziio"].is_array()) {
    for (auto& device : localConfig["eziio"]) {
      if (device.contains("name") && device["name"].get<std::string>() == deviceName) {
        std::string pinType = isOutput ? "outputs" : "inputs";

        if (device.contains("ioConfig") && device["ioConfig"].contains(pinType)) {
          auto& pins = device["ioConfig"][pinType];

          // Find the pin entry or create it
          bool pinFound = false;
          for (auto& pin : pins) {
            if (pin.contains("pin") && pin["pin"].get<int>() == pinNumber) {
              pin["name"] = newName;
              pinFound = true;
              found = true;
              break;
            }
          }

          // If pin entry doesn't exist, add it
          if (!pinFound) {
            nlohmann::json newPin;
            newPin["pin"] = pinNumber;
            newPin["name"] = newName;
            pins.push_back(newPin);
            found = true;
          }
        }
        break;
      }
    }
  }

  // Also check wago array if not found in eziio
  if (!found && localConfig.contains("wago") && localConfig["wago"].is_array()) {
    for (auto& device : localConfig["wago"]) {
      if (device.contains("name") && device["name"].get<std::string>() == deviceName) {
        std::string pinType = isOutput ? "outputs" : "inputs";

        if (device.contains("ioConfig") && device["ioConfig"].contains(pinType)) {
          auto& pins = device["ioConfig"][pinType];

          bool pinFound = false;
          for (auto& pin : pins) {
            if (pin.contains("pin") && pin["pin"].get<int>() == pinNumber) {
              pin["name"] = newName;
              pinFound = true;
              found = true;
              break;
            }
          }

          if (!pinFound) {
            nlohmann::json newPin;
            newPin["pin"] = pinNumber;
            newPin["name"] = newName;
            pins.push_back(newPin);
            found = true;
          }
        }
        break;
      }
    }
  }

  if (!found) {
    std::cerr << "[IOControllerPage] Device not found in config: " << deviceName << std::endl;
    return false;
  }

  // Write back to file
  std::ofstream outFile(localConfigPath);
  if (!outFile.is_open()) {
    std::cerr << "[IOControllerPage] Failed to open local config for writing: " << localConfigPath << std::endl;
    return false;
  }

  outFile << localConfig.dump(2);
  outFile.close();

  std::cout << "[IOControllerPage] Saved pin name to: " << localConfigPath << std::endl;
  return true;
}