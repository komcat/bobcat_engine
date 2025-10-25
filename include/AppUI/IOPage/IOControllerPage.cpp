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
#include <iostream>

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
    "  background-color: #3498db; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 8px; "
    "  font-size: 16px; "
    "  min-height: 40px; "
    "} "
    "QPushButton:hover { background-color: #2980b9; }");
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
    this, [this](QAbstractButton* button) { onDeviceSelected(); });

  layout->addStretch();
}

void IOControllerPage::createIOPinPanel()
{
  m_ioPinPanel = new QWidget();

  QVBoxLayout* layout = new QVBoxLayout(m_ioPinPanel);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(15);

  // ============================================================================
  // HEADER
  // ============================================================================
  QHBoxLayout* headerLayout = new QHBoxLayout();

  m_deviceNameLabel = new QLabel("No device selected");
  m_deviceNameLabel->setStyleSheet("QLabel { font-size: 22px; font-weight: bold; color: #ecf0f1; }");  // Larger, light color
  headerLayout->addWidget(m_deviceNameLabel);

  headerLayout->addStretch();

  m_connectionStatusLabel = new QLabel("⚪ Disconnected");
  m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: #95a5a6; }");  // Larger, bold
  headerLayout->addWidget(m_connectionStatusLabel);

  layout->addLayout(headerLayout);

  // ============================================================================
  // PIN TABLES - TWO COLUMNS
  // ============================================================================
  QHBoxLayout* tablesLayout = new QHBoxLayout();
  tablesLayout->setSpacing(10);

  QString groupBoxStyle =
    "QGroupBox { "
    "  font-size: 16px; "  // Larger font
    "  font-weight: bold; "
    "  color: #ecf0f1; "  // Light color for dark theme
    "  background-color: #2c3e50; "  // Dark background
    "  border: 2px solid #34495e; "
    "  border-radius: 5px; "
    "  margin-top: 10px; "
    "  padding-top: 10px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "}";

  QString tableStyle =
    "QTableWidget { "
    "  background-color: #34495e; "  // Dark background
    "  color: #ecf0f1; "  // Light text color
    "  gridline-color: #4a5f7f; "
    "  border: 1px solid #2c3e50; "
    "  font-size: 14px; "  // Larger font
    "  font-weight: bold; "  // Bold text
    "} "
    "QTableWidget::item { "
    "  padding: 10px; "  // More padding for larger font
    "  border-bottom: 1px solid #2c3e50; "
    "} "
    "QTableWidget::item:selected { "
    "  background-color: #5d6d7e; "  // Darker selection
    "} "
    "QHeaderView::section { "
    "  background-color: #2c3e50; "  // Dark header
    "  color: #ecf0f1; "  // Light text
    "  padding: 12px; "  // More padding
    "  border: 1px solid #34495e; "
    "  font-weight: bold; "
    "  font-size: 14px; "  // Larger font
    "}";

  // ============================================================================
  // COLUMN 1: INPUT PINS
  // ============================================================================
  QGroupBox* inputGroup = new QGroupBox("Input Pins");
  inputGroup->setStyleSheet(groupBoxStyle);

  QVBoxLayout* inputLayout = new QVBoxLayout(inputGroup);

  m_inputPinsTable = new QTableWidget();
  m_inputPinsTable->setColumnCount(4);
  m_inputPinsTable->setHorizontalHeaderLabels({ "Pin #", "Name", "Type", "Status" });

  m_inputPinsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_inputPinsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_inputPinsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_inputPinsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

  m_inputPinsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_inputPinsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_inputPinsTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_inputPinsTable->verticalHeader()->setDefaultSectionSize(50);  // Larger rows for bigger font
  m_inputPinsTable->verticalHeader()->setMinimumSectionSize(50);
  m_inputPinsTable->setStyleSheet(tableStyle);

  inputLayout->addWidget(m_inputPinsTable);
  tablesLayout->addWidget(inputGroup, 1);

  // ============================================================================
  // COLUMN 2: OUTPUT PINS (with control)
  // ============================================================================
  QGroupBox* outputGroup = new QGroupBox("Output Pins");
  outputGroup->setStyleSheet(groupBoxStyle);

  QVBoxLayout* outputLayout = new QVBoxLayout(outputGroup);

  m_outputPinsTable = new QTableWidget();
  m_outputPinsTable->setColumnCount(5);
  m_outputPinsTable->setHorizontalHeaderLabels({ "Control", "Pin #", "Name", "Type", "Status" });  // Control moved to left

  m_outputPinsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);  // Control
  m_outputPinsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);  // Pin #
  m_outputPinsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);           // Name
  m_outputPinsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);  // Type
  m_outputPinsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);  // Status

  m_outputPinsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_outputPinsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_outputPinsTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_outputPinsTable->verticalHeader()->setDefaultSectionSize(50);  // Larger rows for bigger font
  m_outputPinsTable->verticalHeader()->setMinimumSectionSize(50);
  m_outputPinsTable->setStyleSheet(tableStyle);

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
    "  background-color: #2c3e50; "
    "  color: white; "
    "  border: 2px solid #34495e; "
    "  border-radius: 4px; "
    "  padding: 10px 5px; "
    "  font-size: 11px; "
    "  font-weight: bold; "
    "  text-align: center; "
    "  min-height: 50px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #34495e; "
    "  border-color: #3498db; "
    "} "
    "QPushButton:checked { "
    "  background-color: #3498db; "
    "  border-color: #2980b9; "
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

  // Check connection status
  EziIOManager* ioMgr = getIOManager();
  if (ioMgr) {
    IEziIODevice* device = ioMgr->GetDevice(m_selectedDevice);
    if (device && device->IsConnected()) {
      m_connectionStatusLabel->setText("🟢 Connected");
      m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: #2ecc71; }");
    }
    else {
      m_connectionStatusLabel->setText("🔴 Disconnected");
      m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: #e74c3c; }");
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
  // For outputs: Control, Pin #, Name, Type, Status (columns 0-4)
  // For inputs: Pin #, Name, Type, Status (columns 0-3)

  int colOffset = isOutput ? 1 : 0;  // Offset by 1 for outputs (Control is in column 0)

  // Toggle button FIRST for outputs (column 0)
  if (isOutput) {
    QCheckBox* toggleButton = new QCheckBox();
    toggleButton->setChecked(currentState);
    toggleButton->setStyleSheet(
      "QCheckBox::indicator { "
      "  width: 35px; "  // Larger for easier clicking
      "  height: 35px; "
      "  border: 2px solid #7f8c8d; "
      "  border-radius: 6px; "
      "  background-color: #2c3e50; "  // Dark background
      "} "
      "QCheckBox::indicator:checked { "
      "  background-color: #27ae60; "
      "  border-color: #229954; "
      "} "
      "QCheckBox::indicator:hover { "
      "  border-color: #3498db; "
      "}");

    connect(toggleButton, &QCheckBox::toggled, this, [this, pinName](bool checked) {
      EziIOManager* ioMgr = getIOManager();
      if (ioMgr && !m_selectedDevice.empty()) {
        std::cout << "[IOControllerPage] Toggle " << pinName << " -> "
          << (checked ? "ON" : "OFF") << std::endl;

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

    table->setCellWidget(row, 0, toggleWidget);  // Column 0 for outputs
  }

  // Pin number
  QTableWidgetItem* pinNumItem = new QTableWidgetItem(QString::number(pinNumber));
  pinNumItem->setTextAlignment(Qt::AlignCenter);
  table->setItem(row, 0 + colOffset, pinNumItem);

  // Pin name
  QTableWidgetItem* nameItem = new QTableWidgetItem(QString::fromStdString(pinName));
  nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  table->setItem(row, 1 + colOffset, nameItem);

  // Type
  QTableWidgetItem* typeItem = new QTableWidgetItem(isOutput ? "Output" : "Input");
  typeItem->setTextAlignment(Qt::AlignCenter);
  typeItem->setForeground(isOutput ? QColor("#f39c12") : QColor("#3498db"));  // Brighter colors for dark theme
  table->setItem(row, 2 + colOffset, typeItem);

  // Status (will be updated by polling)
  QTableWidgetItem* statusItem = new QTableWidgetItem("--");
  statusItem->setTextAlignment(Qt::AlignCenter);
  table->setItem(row, 3 + colOffset, statusItem);
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
    for (int i = 0; i < inputCount && i < m_inputPinsTable->rowCount(); ++i) {
      bool state = (inputs & (1U << i)) != 0;

      // Update status text (column 3 for inputs)
      QTableWidgetItem* statusItem = m_inputPinsTable->item(i, 3);
      if (statusItem) {
        statusItem->setText(state ? "🟢 ON" : "⚪ OFF");
        statusItem->setForeground(state ? QColor("#2ecc71") : QColor("#95a5a6"));
      }

      // Highlight entire row based on status
      QColor rowColor = state ? QColor("#27ae60") : QColor("#34495e");  // Green for ON, dark gray for OFF
      for (int col = 0; col < m_inputPinsTable->columnCount(); ++col) {
        QTableWidgetItem* item = m_inputPinsTable->item(i, col);
        if (item) {
          item->setBackground(rowColor);
          // Keep Type column color distinct, white for others
          if (col == 2) {  // Type column
            item->setForeground(QColor("#3498db"));  // Keep blue for Input
          } else {
            item->setForeground(QColor("#ecf0f1"));  // White for readable text
          }
        }
      }
    }
  }

  // Read outputs
  uint32_t outputs = 0, status = 0;
  if (device->ReadOutputs(outputs, status) == EziIOError::SUCCESS) {
    int outputCount = device->GetOutputCount();

    // Outputs use bits 8-15 for 8-pin, or bits 16-31 for 16-pin devices
    bool is16Pin = (outputCount > 8);
    int bitOffset = is16Pin ? 16 : 8;

    for (int i = 0; i < outputCount; ++i) {
      int row = i;
      if (row >= m_outputPinsTable->rowCount()) break;

      bool state = (outputs & (1U << (bitOffset + i))) != 0;

      // Update status text (column 4 for outputs - new position)
      QTableWidgetItem* statusItem = m_outputPinsTable->item(row, 4);
      if (statusItem) {
        statusItem->setText(state ? "🟢 ON" : "⚪ OFF");
        statusItem->setForeground(state ? QColor("#2ecc71") : QColor("#95a5a6"));
      }

      // Update toggle button to match actual state (column 0 - new position)
      QWidget* toggleWidget = m_outputPinsTable->cellWidget(row, 0);
      if (toggleWidget) {
        QCheckBox* toggleButton = toggleWidget->findChild<QCheckBox*>();
        if (toggleButton && toggleButton->isChecked() != state) {
          toggleButton->blockSignals(true);
          toggleButton->setChecked(state);
          toggleButton->blockSignals(false);
        }
        // Also set background for toggle widget
        toggleWidget->setStyleSheet(state ? "background-color: #27ae60;" : "background-color: #34495e;");
      }

      // Highlight entire row based on status (skip column 0 which has the toggle widget)
      QColor rowColor = state ? QColor("#27ae60") : QColor("#34495e");  // Green for ON, dark gray for OFF
      for (int col = 1; col < m_outputPinsTable->columnCount(); ++col) {  // Start from col 1 to skip Control column
        QTableWidgetItem* item = m_outputPinsTable->item(row, col);
        if (item) {
          item->setBackground(rowColor);
          // Keep Type column color distinct, white for others
          if (col == 3) {  // Type column (col 3 in output table: Control, Pin#, Name, Type, Status)
            item->setForeground(QColor("#f39c12"));  // Keep orange for Output
          } else {
            item->setForeground(QColor("#ecf0f1"));  // White for readable text
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