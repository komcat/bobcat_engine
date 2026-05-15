// JogPanelWindow.cpp - Tabbed interface with Jog, Quick Output, and Lighting
#include "JogPanelWindow.h"
#include "JogWorker.h"
#include "QuickOutputWidget.h"
#include "QuickOutputManager.h"
#include "QuickOutput_JsonLoader.h"
#include "LightingControlWidget.h"
#include "TransformationMatrixLoader.h"
#include "CoordinateTransformer.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "EziIOManager.h"
#include "LogManager.h"

#include "VisionCommandHandler.h"
#include "SequenceContext.h"
#include "CameraManager.h"
#include "safety/SafetyWatchManager.h"

#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QCloseEvent>
#include <QTimer>
#include <QTabWidget>
#include <algorithm>
#include <map>
#include <QStyle>
#include <QFile>
#include <QSpinBox>
#include <QScrollArea>
#include <QMenu>

JogPanelWindow::JogPanelWindow(
  ApplicationContext* context,
  TransformationMatrixLoader* matrixLoader,
  QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_matrixLoader(matrixLoader)
  , m_tabWidget(nullptr)
  , m_jogWidget(nullptr)
  , m_keyboardIndicator(nullptr)
  , m_deviceButtonGroup(nullptr)
  , m_stepSizeButtonGroup(nullptr)
  , m_currentStepLabel(nullptr)
  , m_deviceGroupBox(nullptr)  // ✅ NEW
  , m_devicePanelParent(nullptr)  // ✅ NEW
  , m_posXLabel(nullptr)  // ✅ NEW: Position display
  , m_posYLabel(nullptr)
  , m_posZLabel(nullptr)
  , m_posULabel(nullptr)
  , m_posVLabel(nullptr)
  , m_posWLabel(nullptr)
  , m_positionUpdateTimer(nullptr)
  , m_quickOutputManager(nullptr)
  , m_quickOutputWidget(nullptr)
  , m_lightingWidget(nullptr)
  , m_jogWorker(nullptr)
  , m_velocityPresetsWidget(nullptr)
  , m_currentStepSize(10.0)  // Default 10um
  , m_keyboardActive(false)
{
  setWindowTitle("Jog Panel");
  // Resizable window with title + close + maximize, but no minimize button.
  setWindowFlags(Qt::Window
                 | Qt::WindowStaysOnTopHint
                 | Qt::CustomizeWindowHint
                 | Qt::WindowTitleHint
                 | Qt::WindowMaximizeButtonHint
                 | Qt::WindowCloseButtonHint);

  // Initialize Quick Output Manager
  if (m_context && m_context->ioManager) {
    m_quickOutputManager = new QuickOutputManager(m_context->ioManager);

    // Load quick outputs from JSON configuration
    // Try local file first, then fall back to default
    std::string localConfigPath = "config/quick_outputs.local.json";
    std::string defaultConfigPath = "config/quick_outputs.json";
    std::string configPath;
    bool loaded = false;

    // Try local configuration first
    if (QFile::exists(QString::fromStdString(localConfigPath))) {
      configPath = localConfigPath;
      loaded = QuickOutput_JsonLoader::LoadFromFile(configPath, m_quickOutputManager);
      if (loaded && m_context->logManager) {
        m_context->logManager->Info("Loaded quick output configuration from " + configPath + " (local)", "JogPanel");
      }
    }

    // Fall back to default configuration if local not found or failed to load
    if (!loaded && QFile::exists(QString::fromStdString(defaultConfigPath))) {
      configPath = defaultConfigPath;
      loaded = QuickOutput_JsonLoader::LoadFromFile(configPath, m_quickOutputManager);
      if (loaded && m_context->logManager) {
        m_context->logManager->Info("Loaded quick output configuration from " + configPath + " (default)", "JogPanel");
      }
    }

    // Log warnings if loading failed
    if (!loaded && m_context->logManager) {
      if (QFile::exists(QString::fromStdString(localConfigPath)) || QFile::exists(QString::fromStdString(defaultConfigPath))) {
        m_context->logManager->Warning("Failed to load quick output configuration", "JogPanel");
      }
      else {
        m_context->logManager->Warning("No quick output config files found (checked: " + localConfigPath + ", " + defaultConfigPath + ")", "JogPanel");
      }
    }
  }

  loadStepSizes();  // Load step sizes from JSON config (before UI creation)
  setupUI();
  restoreWindowSettings();  // Restore saved window position and size

  // Create and start jog worker for async motion commands
  if (m_context && m_context->motionManager && m_matrixLoader) {
    m_jogWorker = new JogWorker(m_context->motionManager, m_matrixLoader, this);

    // Connect worker signals
    connect(m_jogWorker, &JogWorker::jogStarted,
            this, &JogPanelWindow::onJogStarted);
    connect(m_jogWorker, &JogWorker::jogCompleted,
            this, &JogPanelWindow::onJogCompleted);
    connect(m_jogWorker, &JogWorker::logMessage,
            this, &JogPanelWindow::onJogWorkerLog);

    m_jogWorker->start();

    if (m_context->logManager) {
      m_context->logManager->Info("JogWorker started", "JogPanel");
    }
  }

  // Setup position update timer
  m_positionUpdateTimer = new QTimer(this);
  m_positionUpdateTimer->setObjectName("jogPositionTimer");
  connect(m_positionUpdateTimer, &QTimer::timeout, this, &JogPanelWindow::updatePositionDisplay);
  // 500ms is plenty for a human-readable label and reads from the
  // motion manager's cached-position thread (non-blocking).
  m_positionUpdateTimer->setInterval(500);

  // Start hidden
  hide();
}

JogPanelWindow::~JogPanelWindow()
{
  // Stop and clean up jog worker
  if (m_jogWorker) {
    m_jogWorker->requestStop();
    m_jogWorker->wait(3000);  // Wait up to 3 seconds
    delete m_jogWorker;
    m_jogWorker = nullptr;
  }

  if (m_quickOutputManager) {
    delete m_quickOutputManager;
    m_quickOutputManager = nullptr;
  }
}

void JogPanelWindow::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(4, 4, 4, 4);
  mainLayout->setSpacing(4);

  // Create tab widget
  m_tabWidget = new QTabWidget(this);
  m_tabWidget->setStyleSheet(
    "QTabWidget::pane { "
    "  border: 1px solid #555; "
    "  background-color: #2b2b2b; "
    "} "
    "QTabBar::tab { "
    "  background-color: #3a3a3a; "
    "  color: white; "
    "  border: 1px solid #555; "
    "  padding: 8px 16px; "
    "  margin-right: 2px; "
    "  font-weight: bold; "
    "  font-size: 11px; "
    "} "
    "QTabBar::tab:selected { "
    "  background-color: #0d7377; "
    "  border-bottom-color: #0d7377; "
    "} "
    "QTabBar::tab:hover { "
    "  background-color: #4a4a4a; "
    "}"
  );

  // Create tabs
  createJogTab();
  createMovPosTab();
  createQuickOutputTab();
  createLightingTab();

  mainLayout->addWidget(m_tabWidget);

  // Global styling
  setStyleSheet(R"(
    QWidget {
      background-color: #2b2b2b;
      color: white;
    }
    QPushButton {
      background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4a4a4a, stop:1 #3a3a3a);
      color: white;
      padding: 4px;
      font-size: 10px;
      min-width: 40px;
      min-height: 24px;
      border-top: 2px solid rgba(255, 255, 255, 0.4);
      border-left: 2px solid rgba(255, 255, 255, 0.4);
      border-bottom: 2px solid rgba(0, 0, 0, 0.4);
      border-right: 2px solid rgba(0, 0, 0, 0.4);
    }
    QPushButton:hover {
      background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a5a5a, stop:1 #4a4a4a);
      border-top: 2px solid rgba(255, 255, 255, 0.5);
      border-left: 2px solid rgba(255, 255, 255, 0.5);
    }
    QPushButton:pressed {
      background-color: #2a2a2a;
      border-top: 2px solid rgba(0, 0, 0, 0.5);
      border-left: 2px solid rgba(0, 0, 0, 0.5);
      border-bottom: 2px solid rgba(255, 255, 255, 0.3);
      border-right: 2px solid rgba(255, 255, 255, 0.3);
      padding-top: 6px;
      padding-left: 6px;
    }
    QPushButton:checked {
      background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1d8387, stop:1 #0d7377);
      border: 2px solid #14ffec;
      border-top: 2px solid rgba(20, 255, 236, 0.6);
      border-left: 2px solid rgba(20, 255, 236, 0.6);
      border-bottom: 2px solid rgba(0, 0, 0, 0.5);
      border-right: 2px solid rgba(0, 0, 0, 0.5);
    }
    QGroupBox {
      border: 1px solid #555;
      margin-top: 8px;
      padding-top: 6px;
      font-weight: bold;
      font-size: 11px;
    }
    QGroupBox::title {
      color: white;
      subcontrol-origin: margin;
      left: 8px;
      padding: 0 4px;
    }
  )");

  // Resizable: enforce a minimum that fits the controls; default to the previous fixed size.
  // restoreWindowSettings() (called after setupUI) will override this when a saved size exists.
  setMinimumSize(400, 600);
  resize(400, 600);
}

void JogPanelWindow::createJogTab()
{
  m_jogWidget = new QWidget();
  QVBoxLayout* jogLayout = new QVBoxLayout(m_jogWidget);
  jogLayout->setContentsMargins(4, 8, 4, 8);
  jogLayout->setSpacing(6);

  // Keyboard indicator banner at top
  m_keyboardIndicator = new QLabel("⌨ Keyboard Control: INACTIVE");
  m_keyboardIndicator->setAlignment(Qt::AlignCenter);
  m_keyboardIndicator->setStyleSheet(
    "QLabel { "
    "  background-color: #555555; "
    "  color: #888888; "
    "  font-weight: bold; "
    "  font-size: 11px; "
    "  padding: 2px; "
    "  border-radius: 2px; "
    "}"
  );
  jogLayout->addWidget(m_keyboardIndicator);

  // Two-column layout
  QHBoxLayout* columnsLayout = new QHBoxLayout();
  columnsLayout->setSpacing(8);

  // LEFT COLUMN: Device + Step Size
  QWidget* leftColumn = new QWidget();
  QVBoxLayout* leftLayout = new QVBoxLayout(leftColumn);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(6);
  leftColumn->setMaximumWidth(220);

  createDevicePanel(leftColumn);
  createStepSizePanel(leftColumn);

  columnsLayout->addWidget(leftColumn);

  // RIGHT COLUMN: Movement Controls
  QWidget* rightColumn = new QWidget();
  createMovementPanel(rightColumn);

  columnsLayout->addWidget(rightColumn);

  jogLayout->addLayout(columnsLayout);

  // Exposure controls (below the two-column layout)
  createExposurePanel(m_jogWidget);

  // Auto Focus buttons
  createAutoFocusPanel(m_jogWidget);

  // Emergency Stop button - bottom-right of the Jog tab
  QHBoxLayout* estopRow = new QHBoxLayout();
  estopRow->setContentsMargins(4, 4, 4, 4);
  estopRow->addStretch(1);
  m_btnEStop = new QPushButton("E-STOP");
  m_btnEStop->setMinimumSize(100, 44);
  m_btnEStop->setToolTip("Emergency Stop - halts motion and disables all outputs");
  m_btnEStop->setStyleSheet(
    "QPushButton { background-color: #c00000; color: white; font-weight: bold; "
    "font-size: 14px; border: 2px solid #800000; border-radius: 6px; padding: 4px 10px; }"
    "QPushButton:hover { background-color: #e00000; }"
    "QPushButton:pressed { background-color: #800000; }");
  connect(m_btnEStop, &QPushButton::clicked, this, [this]() {
    if (m_context && m_context->safetyWatchManager) {
      m_context->safetyWatchManager->TriggerManualEStop(
        QStringLiteral("Jog Panel E-Stop button"));
    } else if (m_context && m_context->motionManager) {
      (void)m_context->motionManager->EmergencyStopAll();
    }
  });
  estopRow->addWidget(m_btnEStop);
  jogLayout->addLayout(estopRow);

  // Add to tab widget
  m_tabWidget->addTab(m_jogWidget, "Jog");
}

void JogPanelWindow::createQuickOutputTab()
{
  if (m_quickOutputManager) {
    m_quickOutputWidget = new QuickOutputWidget(m_quickOutputManager, this);
    if (m_context && m_context->safetyWatchManager) {
      m_quickOutputWidget->setSafetyWatchManager(m_context->safetyWatchManager);
    }
    m_tabWidget->addTab(m_quickOutputWidget, "Quick Output");
  }
  else {
    // Create placeholder if IO manager not available
    QWidget* placeholder = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(placeholder);
    QLabel* label = new QLabel("IO Manager not available.\n\nQuick Output requires EziIOManager.");
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("color: #888888; font-size: 11px;");
    layout->addWidget(label);
    m_tabWidget->addTab(placeholder, "Quick Output");
  }
}

void JogPanelWindow::createLightingTab()
{
  if (m_context && m_context->lightingManager) {
    m_lightingWidget = new LightingControlWidget(m_context, this);
    m_tabWidget->addTab(m_lightingWidget, "Lighting");
  }
  else {
    // Create placeholder if Lighting manager not available
    QWidget* placeholder = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(placeholder);
    QLabel* label = new QLabel("Lighting Manager not available.\n\nLighting control requires LightingManager.");
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("color: #888888; font-size: 11px;");
    layout->addWidget(label);
    m_tabWidget->addTab(placeholder, "Lighting");
  }
}

// ===========================================================================
// MovPos tab — 2x5 named-position slot grid with right-click teach.
// Slot→position-name mapping is persisted in QSettings, keyed per device.
// ===========================================================================

QString JogPanelWindow::movPosSlotSettingsKey(const std::string& deviceName,
                                              int slotIndex) const
{
  return QString("MovPosSlots/%1/%2").arg(QString::fromStdString(deviceName)).arg(slotIndex);
}

double JogPanelWindow::defaultVelocityForDevice(const std::string& deviceName) const
{
  // Hex devices (PI) move at 2 mm/s by default; gantry/stage (ACS) at 10 mm/s.
  if (!m_context || !m_context->motionManager || deviceName.empty()) return 10.0;
  std::string type = m_context->motionManager->GetDeviceType(deviceName);
  for (auto& c : type) c = static_cast<char>(toupper(c));
  return (type == "PI") ? 2.0 : 10.0;
}

void JogPanelWindow::applyDefaultVelocityFor(const std::string& deviceName)
{
  if (!m_context || !m_context->motionManager || deviceName.empty()) return;

  const double vel = defaultVelocityForDevice(deviceName);
  std::string type = m_context->motionManager->GetDeviceType(deviceName);
  std::string upperType = type;
  for (auto& c : upperType) c = static_cast<char>(toupper(c));

  // Same dispatch shape as JogPanelWindow::applyVelocity (PI = system, ACS = per-axis).
  if (upperType == "PI") {
    m_context->motionManager->SetSystemVelocity(deviceName, vel);
  } else {
    std::string axes = m_context->motionManager->GetInstalledAxes(deviceName);
    for (char c : axes) {
      m_context->motionManager->SetVelocity(deviceName, std::string(1, c), vel);
    }
  }

  if (m_context->logManager) {
    m_context->logManager->Info(
      "MovPos: pre-move velocity " + std::to_string(vel) + " mm/s on " + deviceName,
      "JogPanel");
  }
}

void JogPanelWindow::createMovPosTab()
{
  m_movPosWidget = new QWidget();
  QHBoxLayout* root = new QHBoxLayout(m_movPosWidget);
  root->setContentsMargins(4, 8, 4, 8);
  root->setSpacing(8);

  // -------- Left column: device selector (mirrors Jog tab pattern) --------
  QWidget* leftColumn = new QWidget();
  leftColumn->setMaximumWidth(180);
  QVBoxLayout* leftLayout = new QVBoxLayout(leftColumn);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(4);

  QGroupBox* deviceBox = new QGroupBox("Device");
  QVBoxLayout* deviceBoxLayout = new QVBoxLayout(deviceBox);
  deviceBoxLayout->setSpacing(3);
  deviceBoxLayout->setContentsMargins(4, 8, 4, 4);

  QScrollArea* scroll = new QScrollArea(deviceBox);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll->setMaximumHeight(4 * 34 + 3 * 3 + 4);  // ~4 buttons visible

  QWidget* listContainer = new QWidget();
  m_movPosDeviceListLayout = new QVBoxLayout(listContainer);
  m_movPosDeviceListLayout->setSpacing(3);
  m_movPosDeviceListLayout->setContentsMargins(0, 0, 0, 0);
  scroll->setWidget(listContainer);
  deviceBoxLayout->addWidget(scroll);

  m_movPosDeviceGroup = new QButtonGroup(this);
  m_movPosDeviceGroup->setExclusive(true);

  rebuildMovPosDeviceList();

  leftLayout->addWidget(deviceBox);
  leftLayout->addStretch();

  root->addWidget(leftColumn);

  // -------- Right column: 2 x 5 slot grid --------
  QWidget* rightColumn = new QWidget();
  QVBoxLayout* rightLayout = new QVBoxLayout(rightColumn);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(4);

  QLabel* hint = new QLabel("Click: move • Right-click: assign from product positions");
  hint->setStyleSheet("QLabel { color: #888888; font-size: 10px; }");
  rightLayout->addWidget(hint);

  QGridLayout* slotGrid = new QGridLayout();
  slotGrid->setSpacing(6);
  slotGrid->setContentsMargins(0, 0, 0, 0);

  m_movPosSlotButtons.assign(kMovPosSlotCount, nullptr);
  for (int i = 0; i < kMovPosSlotCount; ++i) {
    QPushButton* btn = new QPushButton();
    btn->setMinimumHeight(48);
    btn->setStyleSheet(
      "QPushButton { "
      "  background-color: #3c3c3c; "
      "  color: #d4d4d4; "
      "  border: 1px solid #555555; "
      "  border-radius: 4px; "
      "  font-size: 11px; "
      "  font-weight: bold; "
      "  padding: 4px; "
      "} "
      "QPushButton:hover { background-color: #0e639c; color: white; border-color: #1177bb; } "
      "QPushButton:pressed { background-color: #0c5280; }"
    );
    btn->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(btn, &QPushButton::clicked, this, [this, i]() {
      onMovPosSlotClicked(i);
    });
    connect(btn, &QWidget::customContextMenuRequested, this, [this, i, btn](const QPoint& pos) {
      onMovPosSlotContextMenu(i, btn->mapToGlobal(pos));
    });

    int row = i / 2;  // 5 rows
    int col = i % 2;  // 2 cols
    slotGrid->addWidget(btn, row, col);
    m_movPosSlotButtons[i] = btn;
  }

  rightLayout->addLayout(slotGrid);
  rightLayout->addStretch();
  root->addWidget(rightColumn, 1);

  refreshMovPosSlotLabels();

  m_tabWidget->addTab(m_movPosWidget, "MovPos");
}

void JogPanelWindow::rebuildMovPosDeviceList()
{
  if (!m_movPosDeviceListLayout || !m_movPosDeviceGroup) return;

  // Clear existing buttons
  QList<QAbstractButton*> buttons = m_movPosDeviceGroup->buttons();
  for (QAbstractButton* btn : buttons) {
    m_movPosDeviceGroup->removeButton(btn);
    m_movPosDeviceListLayout->removeWidget(btn);
    delete btn;
  }
  // Drop trailing stretch if present
  if (m_movPosDeviceListLayout->count() > 0) {
    QLayoutItem* tail = m_movPosDeviceListLayout->takeAt(m_movPosDeviceListLayout->count() - 1);
    delete tail;
  }

  if (!m_context || !m_context->motionManager || !m_matrixLoader) {
    m_movPosDeviceListLayout->addStretch();
    return;
  }

  std::vector<std::string> piDevices = m_context->motionManager->GetDeviceNamesByType("PI");
  std::vector<std::string> acsDevices = m_context->motionManager->GetDeviceNamesByType("ACS");
  std::vector<std::string> all;
  all.insert(all.end(), piDevices.begin(), piDevices.end());
  all.insert(all.end(), acsDevices.begin(), acsDevices.end());

  std::map<std::string, bool> connectionStatus = m_context->motionManager->GetConnectionStatus();

  std::vector<std::string> validDevices;
  for (const auto& deviceId : all) {
    if (!m_matrixLoader->HasDevice(deviceId)) continue;
    auto it = connectionStatus.find(deviceId);
    if (it == connectionStatus.end() || !it->second) continue;
    validDevices.push_back(deviceId);
  }
  std::sort(validDevices.begin(), validDevices.end());

  for (const auto& deviceId : validDevices) {
    QPushButton* btn = new QPushButton(QString::fromStdString(deviceId));
    btn->setCheckable(true);
    btn->setMinimumHeight(26);
    btn->setMaximumHeight(26);
    btn->setStyleSheet(getDeviceButtonStyleSheet(deviceId));
    m_movPosDeviceGroup->addButton(btn);
    m_movPosDeviceListLayout->addWidget(btn);
    connect(btn, &QPushButton::clicked, this, &JogPanelWindow::onMovPosDeviceSelected);
  }

  if (!validDevices.empty()) {
    auto firstButton = qobject_cast<QPushButton*>(m_movPosDeviceGroup->buttons().first());
    if (firstButton) {
      firstButton->setChecked(true);
      m_movPosSelectedDevice = validDevices[0];
    }
  } else {
    m_movPosSelectedDevice.clear();
  }

  m_movPosDeviceListLayout->addStretch();
}

void JogPanelWindow::onMovPosDeviceSelected()
{
  QPushButton* btn = qobject_cast<QPushButton*>(sender());
  if (!btn || !btn->isChecked()) return;
  m_movPosSelectedDevice = btn->text().toStdString();
  refreshMovPosSlotLabels();
}

void JogPanelWindow::refreshMovPosSlotLabels()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  for (int i = 0; i < kMovPosSlotCount; ++i) {
    if (!m_movPosSlotButtons[i]) continue;
    QString name;
    if (!m_movPosSelectedDevice.empty()) {
      name = settings.value(movPosSlotSettingsKey(m_movPosSelectedDevice, i)).toString();
    }
    if (name.isEmpty()) {
      m_movPosSlotButtons[i]->setText(QString("Slot %1\n(empty)").arg(i + 1));
      m_movPosSlotButtons[i]->setToolTip("Right-click to teach this slot.");
    } else {
      m_movPosSlotButtons[i]->setText(QString("Slot %1\n%2").arg(i + 1).arg(name));
      m_movPosSlotButtons[i]->setToolTip(QString("Move to '%1'\nRight-click to retach or clear.").arg(name));
    }
  }
}

void JogPanelWindow::onMovPosSlotClicked(int slotIndex)
{
  if (m_movPosSelectedDevice.empty()) {
    if (m_context && m_context->logManager) {
      m_context->logManager->Warning("MovPos: no device selected", "JogPanel");
    }
    return;
  }
  QSettings settings("Fabrinet", "BobcatEngine");
  QString name = settings.value(movPosSlotSettingsKey(m_movPosSelectedDevice, slotIndex)).toString();
  if (name.isEmpty()) {
    if (m_context && m_context->logManager) {
      m_context->logManager->Warning(
        "MovPos: slot " + std::to_string(slotIndex + 1) + " is empty — right-click to teach",
        "JogPanel");
    }
    return;
  }
  if (!m_context || !m_context->motionManager) return;

  // Per request: set default velocity (gantry/stage = 10 mm/s, hex/PI = 2 mm/s) before moving.
  applyDefaultVelocityFor(m_movPosSelectedDevice);

  bool ok = m_context->motionManager->MoveToNamedPosition(
    m_movPosSelectedDevice, name.toStdString(), /*blocking=*/false);

  if (m_context->logManager) {
    if (ok) {
      m_context->logManager->Info(
        "MovPos: moving " + m_movPosSelectedDevice + " → '" + name.toStdString() + "'",
        "JogPanel");
    } else {
      m_context->logManager->Warning(
        "MovPos: MoveToNamedPosition failed for " + m_movPosSelectedDevice + " '" + name.toStdString() + "'",
        "JogPanel");
    }
  }
}

void JogPanelWindow::onMovPosSlotContextMenu(int slotIndex, const QPoint& globalPos)
{
  QMenu menu;

  if (m_movPosSelectedDevice.empty() ||
      !m_context || !m_context->motionManager) {
    QAction* warn = menu.addAction("(select a device first)");
    warn->setEnabled(false);
    menu.exec(globalPos);
    return;
  }

  // Pull the named-position list for the currently selected device (loaded
  // from the active product's motor_devices file).
  std::vector<std::string> positions =
    m_context->motionManager->GetNamedPositionList(m_movPosSelectedDevice);
  std::sort(positions.begin(), positions.end());

  QMenu* assignMenu = menu.addMenu(
    QString("Assign position for %1…").arg(QString::fromStdString(m_movPosSelectedDevice)));

  if (positions.empty()) {
    QAction* none = assignMenu->addAction("(no named positions for this device)");
    none->setEnabled(false);
  } else {
    for (const auto& name : positions) {
      QAction* act = assignMenu->addAction(QString::fromStdString(name));
      const std::string captured = name;
      connect(act, &QAction::triggered, this, [this, slotIndex, captured]() {
        QSettings settings("Fabrinet", "BobcatEngine");
        settings.setValue(movPosSlotSettingsKey(m_movPosSelectedDevice, slotIndex),
                          QString::fromStdString(captured));
        refreshMovPosSlotLabels();
        if (m_context && m_context->logManager) {
          m_context->logManager->Info(
            "MovPos: slot " + std::to_string(slotIndex + 1) +
            " on " + m_movPosSelectedDevice + " → '" + captured + "'",
            "JogPanel");
        }
      });
    }
  }

  menu.addSeparator();
  QAction* clearAct = menu.addAction("Clear slot");
  clearAct->setEnabled(!m_movPosSelectedDevice.empty());

  QAction* chosen = menu.exec(globalPos);
  if (chosen == clearAct) {
    clearMovPosSlot(slotIndex);
  }
}

void JogPanelWindow::clearMovPosSlot(int slotIndex)
{
  if (m_movPosSelectedDevice.empty()) return;
  QSettings settings("Fabrinet", "BobcatEngine");
  settings.remove(movPosSlotSettingsKey(m_movPosSelectedDevice, slotIndex));
  refreshMovPosSlotLabels();
  if (m_context && m_context->logManager) {
    m_context->logManager->Info(
      "MovPos: cleared slot " + std::to_string(slotIndex + 1) + " on " + m_movPosSelectedDevice,
      "JogPanel");
  }
}

void JogPanelWindow::loadStepSizes()
{
  // Default step sizes (fallback if no config file found)
  const std::vector<double> defaultSteps = {
    0.1, 0.2, 0.3, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0,
    100.0, 200.0, 250.0, 500.0, 1000.0, 2000.0, 5000.0
  };
  double defaultStepSize = 10.0;

  std::string localConfigPath = "config/jog_stepsize.local.json";
  std::string defaultConfigPath = "config/jog_stepsize.json";
  std::string configPath;
  bool loaded = false;

  // Try local configuration first
  if (QFile::exists(QString::fromStdString(localConfigPath))) {
    configPath = localConfigPath;
  }
  else if (QFile::exists(QString::fromStdString(defaultConfigPath))) {
    configPath = defaultConfigPath;
  }

  if (!configPath.empty()) {
    std::ifstream file(configPath);
    if (file.is_open()) {
      try {
        nlohmann::json j;
        file >> j;
        file.close();

        if (j.contains("step_sizes_um") && j["step_sizes_um"].is_array()) {
          m_stepSizes.clear();
          for (const auto& val : j["step_sizes_um"]) {
            if (val.is_number()) {
              m_stepSizes.push_back(val.get<double>());
            }
          }
          std::sort(m_stepSizes.begin(), m_stepSizes.end());

          // Enforce maximum of 30 step sizes
          const size_t MAX_STEP_SIZES = 30;
          if (m_stepSizes.size() > MAX_STEP_SIZES) {
            m_stepSizes.resize(MAX_STEP_SIZES);
            if (m_context && m_context->logManager) {
              m_context->logManager->Warning(
                "Jog step sizes truncated to " + std::to_string(MAX_STEP_SIZES) + " entries",
                "JogPanel"
              );
            }
          }
        }

        m_currentStepSize = j.value("default_step_um", defaultStepSize);
        loaded = true;

        if (m_context && m_context->logManager) {
          m_context->logManager->Info(
            "Loaded " + std::to_string(m_stepSizes.size()) + " jog step sizes from " + configPath,
            "JogPanel"
          );
        }
      }
      catch (const std::exception& e) {
        if (m_context && m_context->logManager) {
          m_context->logManager->Warning(
            "Failed to parse jog step size config: " + std::string(e.what()),
            "JogPanel"
          );
        }
      }
    }
  }

  // Fall back to hardcoded defaults
  if (!loaded || m_stepSizes.empty()) {
    m_stepSizes = defaultSteps;
    m_currentStepSize = defaultStepSize;

    if (m_context && m_context->logManager) {
      m_context->logManager->Info("Using default jog step sizes (no config file found)", "JogPanel");
    }
  }
}

QString JogPanelWindow::formatStepLabel(double stepUm)
{
  if (stepUm >= 1000.0) {
    double mm = stepUm / 1000.0;
    if (mm == static_cast<int>(mm)) {
      return QString::number(static_cast<int>(mm)) + "mm";
    }
    return QString::number(mm, 'f', 1) + "mm";
  }
  if (stepUm < 1.0) {
    // Sub-micron: show decimal (e.g., "0.1µ", "0.3µ")
    QString num = QString::number(stepUm, 'f', 1);
    // Trim trailing zero after decimal if not needed (0.10 → 0.1)
    if (num.endsWith('0') && num.contains('.')) {
      num.chop(1);
    }
    return num + QStringLiteral("\u00B5");
  }
  // Whole micron values
  if (stepUm == static_cast<int>(stepUm)) {
    return QString::number(static_cast<int>(stepUm)) + QStringLiteral("\u00B5");
  }
  return QString::number(stepUm, 'f', 1) + QStringLiteral("\u00B5");
}

QString JogPanelWindow::getDeviceButtonStyleSheet(const std::string& deviceId)
{
  // Assign distinct colors per device type
  struct DeviceColor {
    const char* bgChecked;
    const char* textChecked;
    const char* borderChecked;
  };

  DeviceColor color = { "#f0ad4e", "#000000", "#ffcc00" }; // default orange

  if (deviceId.find("gantry") != std::string::npos) {
    color = { "#f0ad4e", "#000000", "#ffcc00" }; // orange
  }
  else if (deviceId.find("hex") != std::string::npos) {
    color = { "#00bcd4", "#000000", "#14ffec" }; // cyan/teal
  }
  else if (deviceId.find("stage") != std::string::npos) {
    color = { "#4caf50", "#000000", "#66ff66" }; // green
  }
  else if (deviceId.find("rotary") != std::string::npos) {
    color = { "#bb86fc", "#000000", "#d4aaff" }; // purple
  }

  return QString(
    "QPushButton { "
    "  background-color: #3a3a3a; "
    "  color: white; "
    "  border: 1px solid #555; "
    "  border-radius: 3px; "
    "  padding: 2px 6px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  text-align: left; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4a4a4a; "
    "  border-color: #777; "
    "} "
    "QPushButton:checked { "
    "  background-color: %1; "
    "  color: %2; "
    "  border: 2px solid %3; "
    "}"
  ).arg(color.bgChecked, color.textChecked, color.borderChecked);
}

void JogPanelWindow::createDevicePanel(QWidget* parent)
{
  // ✅ Store parent for refresh
  m_devicePanelParent = parent;

  QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(parent->layout());
  if (!layout) {
    layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
  }

  // ✅ Store groupBox for refresh
  m_deviceGroupBox = new QGroupBox("Device");
  QVBoxLayout* groupLayout = new QVBoxLayout(m_deviceGroupBox);
  groupLayout->setSpacing(3);
  groupLayout->setContentsMargins(4, 8, 4, 4);

  // Scroll area: caps the visible device list to ~4 buttons; scrolls vertically beyond that.
  // Button height (26) + groupLayout spacing (3) -> ~29px per row; 4 rows ≈ 116px + small frame.
  QScrollArea* deviceScroll = new QScrollArea(m_deviceGroupBox);
  deviceScroll->setWidgetResizable(true);
  deviceScroll->setFrameShape(QFrame::NoFrame);
  deviceScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  deviceScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  deviceScroll->setMaximumHeight(4 * 34 + 3 * 3 + 4);  // 4 buttons + 3 gaps + slight padding

  QWidget* deviceListContainer = new QWidget();
  m_deviceListLayout = new QVBoxLayout(deviceListContainer);
  m_deviceListLayout->setSpacing(3);
  m_deviceListLayout->setContentsMargins(0, 0, 0, 0);

  deviceScroll->setWidget(deviceListContainer);
  groupLayout->addWidget(deviceScroll);

  m_deviceButtonGroup = new QButtonGroup(this);
  m_deviceButtonGroup->setExclusive(true);

  // Get device list from UnifiedMotionManager
  if (m_context && m_context->motionManager && m_matrixLoader) {
    std::vector<std::string> piDevices = m_context->motionManager->GetDeviceNamesByType("PI");
    std::vector<std::string> acsDevices = m_context->motionManager->GetDeviceNamesByType("ACS");

    std::vector<std::string> allDevices;
    allDevices.insert(allDevices.end(), piDevices.begin(), piDevices.end());
    allDevices.insert(allDevices.end(), acsDevices.begin(), acsDevices.end());

    // ✅ Get connection status for all devices
    std::map<std::string, bool> connectionStatus = m_context->motionManager->GetConnectionStatus();

    std::vector<std::string> validDevices;
    for (const auto& deviceId : allDevices) {
      // ✅ Check if device has transformation matrix
      if (!m_matrixLoader->HasDevice(deviceId)) {
        if (m_context->logManager) {
          m_context->logManager->Warning(
            "Device '" + deviceId + "' has no transformation matrix - skipping",
            "JogPanel"
          );
        }
        continue;
      }

      // ✅ Check if device is connected/enabled
      auto it = connectionStatus.find(deviceId);
      if (it == connectionStatus.end() || !it->second) {
        if (m_context->logManager) {
          m_context->logManager->Debug(
            "Device '" + deviceId + "' is not connected - skipping",
            "JogPanel"
          );
        }
        continue;
      }

      // Device is valid and connected
      validDevices.push_back(deviceId);
    }

    // ✅ Sort devices alphabetically (ascending)
    std::sort(validDevices.begin(), validDevices.end());

    // Create compact push buttons with per-device colors
    for (const auto& deviceId : validDevices) {
      QPushButton* btn = new QPushButton(QString::fromStdString(deviceId));
      btn->setCheckable(true);
      btn->setMinimumHeight(34);
      btn->setMaximumHeight(34);
      btn->setStyleSheet(getDeviceButtonStyleSheet(deviceId));

      m_deviceButtonGroup->addButton(btn);
      m_deviceListLayout->addWidget(btn);

      connect(btn, &QPushButton::clicked, this, &JogPanelWindow::onDeviceSelected);
    }

    // Select first device by default
    if (!validDevices.empty()) {
      auto firstButton = qobject_cast<QPushButton*>(m_deviceButtonGroup->buttons().first());
      if (firstButton) {
        firstButton->setChecked(true);
        m_selectedDevice = validDevices[0];
      }
    }

    if (m_context->logManager) {
      m_context->logManager->Info(
        "Jog panel loaded " + std::to_string(validDevices.size()) + " devices",
        "JogPanel"
      );
    }
  }

  m_deviceListLayout->addStretch();
  layout->addWidget(m_deviceGroupBox);
}

// ✅ NEW: Refresh device list when panel is shown
void JogPanelWindow::refreshDeviceList()
{
  if (!m_deviceGroupBox || !m_devicePanelParent || !m_deviceListLayout) {
    return;  // Panel not initialized yet
  }

  // Operate on the inner scroll-area layout that holds the device buttons.
  QVBoxLayout* groupLayout = m_deviceListLayout;

  // Clear all existing device buttons
  if (m_deviceButtonGroup) {
    // Remove and delete all buttons from the button group
    QList<QAbstractButton*> buttons = m_deviceButtonGroup->buttons();
    for (QAbstractButton* btn : buttons) {
      m_deviceButtonGroup->removeButton(btn);
      groupLayout->removeWidget(btn);
      delete btn;
    }
  }

  // Remove the trailing stretch if it exists
  if (groupLayout->count() > 0) {
    QLayoutItem* stretch = groupLayout->takeAt(groupLayout->count() - 1);
    if (stretch) {
      delete stretch;
    }
  }

  // Rebuild device list (same logic as createDevicePanel)
  if (m_context && m_context->motionManager && m_matrixLoader) {
    std::vector<std::string> piDevices = m_context->motionManager->GetDeviceNamesByType("PI");
    std::vector<std::string> acsDevices = m_context->motionManager->GetDeviceNamesByType("ACS");

    std::vector<std::string> allDevices;
    allDevices.insert(allDevices.end(), piDevices.begin(), piDevices.end());
    allDevices.insert(allDevices.end(), acsDevices.begin(), acsDevices.end());

    // ✅ Get connection status for all devices
    std::map<std::string, bool> connectionStatus = m_context->motionManager->GetConnectionStatus();

    std::vector<std::string> validDevices;
    for (const auto& deviceId : allDevices) {
      // ✅ Check if device has transformation matrix
      if (!m_matrixLoader->HasDevice(deviceId)) {
        if (m_context->logManager) {
          m_context->logManager->Warning(
            "Device '" + deviceId + "' has no transformation matrix - skipping",
            "JogPanel"
          );
        }
        continue;
      }

      // ✅ Check if device is connected/enabled
      auto it = connectionStatus.find(deviceId);
      if (it == connectionStatus.end() || !it->second) {
        if (m_context->logManager) {
          m_context->logManager->Debug(
            "Device '" + deviceId + "' is not connected - skipping",
            "JogPanel"
          );
        }
        continue;
      }

      // Device is valid and connected
      validDevices.push_back(deviceId);
    }

    // ✅ Sort devices alphabetically (ascending)
    std::sort(validDevices.begin(), validDevices.end());

    // Create compact push buttons with per-device colors
    for (const auto& deviceId : validDevices) {
      QPushButton* btn = new QPushButton(QString::fromStdString(deviceId));
      btn->setCheckable(true);
      btn->setMinimumHeight(34);
      btn->setMaximumHeight(34);
      btn->setStyleSheet(getDeviceButtonStyleSheet(deviceId));

      m_deviceButtonGroup->addButton(btn);
      groupLayout->addWidget(btn);

      connect(btn, &QPushButton::clicked, this, &JogPanelWindow::onDeviceSelected);
    }

    // Select first device by default if available
    if (!validDevices.empty()) {
      auto firstButton = qobject_cast<QPushButton*>(m_deviceButtonGroup->buttons().first());
      if (firstButton) {
        firstButton->setChecked(true);
        m_selectedDevice = validDevices[0];
      }
    }
    else {
      m_selectedDevice.clear();
    }

    if (m_context->logManager) {
      m_context->logManager->Debug(
        "Jog panel refreshed with " + std::to_string(validDevices.size()) + " devices",
        "JogPanel"
      );
    }
  }

  // Add stretch back
  groupLayout->addStretch();

  // Update velocity presets for the selected device
  updateVelocityPresets();

  // Keep the MovPos tab's device list in sync as well.
  rebuildMovPosDeviceList();
  refreshMovPosSlotLabels();
}

void JogPanelWindow::createVelocityPanel(QWidget* /*parent*/)
{
  // Velocity presets are now created inside createMovementPanel
}

void JogPanelWindow::updateVelocityPresets()
{
  if (!m_velocityPresetsWidget) return;

  // Clear existing buttons
  for (auto* btn : m_velocityButtons) {
    btn->deleteLater();
  }
  m_velocityButtons.clear();

  QGridLayout* velGrid = qobject_cast<QGridLayout*>(m_velocityPresetsWidget->layout());
  if (!velGrid) return;

  // Remove any remaining widgets (unit label from previous update)
  while (velGrid->count() > 0) {
    QLayoutItem* item = velGrid->takeAt(0);
    if (item->widget()) {
      item->widget()->deleteLater();
    }
    delete item;
  }

  // Determine device type
  QString deviceType = "ACS";
  if (m_context && m_context->motionManager && !m_selectedDevice.empty()) {
    std::string type = m_context->motionManager->GetDeviceType(m_selectedDevice);
    std::string upperType = type;
    for (auto& c : upperType) c = static_cast<char>(toupper(c));
    deviceType = QString::fromStdString(upperType);
  }

  // Choose presets based on device type
  QList<double> presets;
  if (deviceType == "PI") {
    presets = { 0.05, 0.1, 0.5, 1, 2, 5, 10, 12.5 };
  } else {
    presets = { 1, 2, 5, 10, 20, 30, 40, 50 };
  }

  int col = 0;
  int row = 0;
  int maxCols = 4;

  for (double vel : presets) {
    QString label;
    if (vel == static_cast<int>(vel)) {
      label = QString::number(static_cast<int>(vel));
    } else if (vel < 1.0) {
      label = QString::number(vel, 'f', 2);
    } else {
      label = QString::number(vel, 'f', 1);
    }

    QPushButton* btn = new QPushButton(label);
    btn->setMinimumHeight(26);
    btn->setStyleSheet(
      "QPushButton {"
      "  background-color: #3c3c3c;"
      "  color: #d4d4d4;"
      "  border: 1px solid #555555;"
      "  border-radius: 3px;"
      "  font-size: 11px;"
      "  font-weight: bold;"
      "  padding: 4px 2px;"
      "}"
      "QPushButton:hover {"
      "  background-color: #0e639c;"
      "  color: white;"
      "  border-color: #1177bb;"
      "}"
      "QPushButton:pressed {"
      "  background-color: #0c5280;"
      "}"
    );

    connect(btn, &QPushButton::clicked, this, [this, vel]() {
      applyVelocity(vel);
    });

    velGrid->addWidget(btn, row, col);
    m_velocityButtons.append(btn);

    col++;
    if (col >= maxCols) {
      col = 0;
      row++;
    }
  }

  // Add "mm/s" label at the end
  QLabel* unitLabel = new QLabel("mm/s");
  unitLabel->setStyleSheet("QLabel { color: #888888; font-size: 10px; }");
  unitLabel->setAlignment(Qt::AlignCenter);
  velGrid->addWidget(unitLabel, row, col);
}

void JogPanelWindow::applyVelocity(double velocity)
{
  if (!m_context || !m_context->motionManager || m_selectedDevice.empty()) return;

  std::string type = m_context->motionManager->GetDeviceType(m_selectedDevice);
  std::string upperType = type;
  for (auto& c : upperType) c = static_cast<char>(toupper(c));

  if (upperType == "PI") {
    m_context->motionManager->SetSystemVelocity(m_selectedDevice, velocity);
  } else {
    // ACS: set all installed axes
    std::string axes = m_context->motionManager->GetInstalledAxes(m_selectedDevice);
    for (char c : axes) {
      m_context->motionManager->SetVelocity(m_selectedDevice, std::string(1, c), velocity);
    }
  }

  m_currentVelocity = velocity;
  if (m_currentVelocityLabel) {
    QString label;
    if (velocity == static_cast<int>(velocity)) {
      label = QString::number(static_cast<int>(velocity));
    } else if (velocity < 1.0) {
      label = QString::number(velocity, 'f', 2);
    } else {
      label = QString::number(velocity, 'f', 1);
    }
    m_currentVelocityLabel->setText(label + " mm/s");
  }

  if (m_context->logManager) {
    m_context->logManager->Info(
      "Velocity set to " + std::to_string(velocity) + " mm/s for " + m_selectedDevice,
      "JogPanel"
    );
  }
}

void JogPanelWindow::createStepSizePanel(QWidget* parent)
{
  QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(parent->layout());
  if (!layout) {
    layout = new QVBoxLayout(parent);
  }

  QGroupBox* groupBox = new QGroupBox("Step Size");
  QVBoxLayout* groupLayout = new QVBoxLayout(groupBox);
  groupLayout->setSpacing(3);
  groupLayout->setContentsMargins(4, 8, 4, 4);

  // Current step display
  m_currentStepLabel = new QLabel();
  m_currentStepLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 36px; "
    "  font-weight: bold; "
    "  color: #ffff00; "
    "  background-color: #1565c0; "
    "  padding: 6px; "
    "  border-radius: 4px; "
    "}"
  );
  m_currentStepLabel->setAlignment(Qt::AlignCenter);
  updateStepSizeDisplay();
  groupLayout->addWidget(m_currentStepLabel);

  // Step size buttons in compact grid
  m_stepSizeButtonGroup = new QButtonGroup(this);
  m_stepSizeButtonGroup->setExclusive(true);

  QGridLayout* gridLayout = new QGridLayout();
  gridLayout->setSpacing(2);
  gridLayout->setContentsMargins(0, 0, 0, 0);

  int row = 0, col = 0;
  for (size_t i = 0; i < m_stepSizes.size(); ++i) {
    QPushButton* btn = new QPushButton(formatStepLabel(m_stepSizes[i]));
    btn->setCheckable(true);
    btn->setProperty("stepSize", m_stepSizes[i]);
    btn->setMinimumHeight(22);
    btn->setMaximumHeight(22);
    btn->setStyleSheet(
      "QPushButton { "
      "  font-size: 9px; "
      "  padding: 2px; "
      "  min-width: 25px; "
      "}"
    );

    m_stepSizeButtonGroup->addButton(btn, static_cast<int>(i));
    gridLayout->addWidget(btn, row, col);

    connect(btn, &QPushButton::clicked, this, &JogPanelWindow::onStepSizeSelected);

    // Default to 10um
    if (m_stepSizes[i] == 10.0) {
      btn->setChecked(true);
    }

    col++;
    if (col >= 3) {  // 3 columns
      col = 0;
      row++;
    }
  }

  groupLayout->addLayout(gridLayout);
  groupLayout->addStretch();
  layout->addWidget(groupBox);
}

void JogPanelWindow::createMovementPanel(QWidget* parent)
{
  QVBoxLayout* layout = new QVBoxLayout(parent);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  QGroupBox* groupBox = new QGroupBox("Movement Controls");
  QVBoxLayout* groupLayout = new QVBoxLayout(groupBox);
  groupLayout->setSpacing(6);
  groupLayout->setContentsMargins(4, 8, 4, 4);

  // XYZ Controls - Compact grid
  QLabel* xyzLabel = new QLabel("XYZ Axes");
  xyzLabel->setStyleSheet("QLabel { font-weight: bold; color: #14ffec; font-size: 10px; }");
  groupLayout->addWidget(xyzLabel);

  QGridLayout* xyzGrid = new QGridLayout();
  xyzGrid->setSpacing(4);

  // Y- (top)
  m_btnYMinus = new QPushButton("Y-");
  m_btnYMinus->setMinimumSize(30, 28);
  xyzGrid->addWidget(m_btnYMinus, 0, 1);
  connect(m_btnYMinus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnYMinus);
    sendJogCommand("Y", -1.0);
  });

  // X-, X+
  m_btnXMinus = new QPushButton("X-");
  m_btnXMinus->setMinimumSize(30, 28);
  xyzGrid->addWidget(m_btnXMinus, 1, 0);
  connect(m_btnXMinus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnXMinus);
    sendJogCommand("X", -1.0);
  });

  m_btnXPlus = new QPushButton("X+");
  m_btnXPlus->setMinimumSize(30, 28);
  xyzGrid->addWidget(m_btnXPlus, 1, 2);
  connect(m_btnXPlus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnXPlus);
    sendJogCommand("X", 1.0);
  });

  // Y+ (bottom)
  m_btnYPlus = new QPushButton("Y+");
  m_btnYPlus->setMinimumSize(30, 28);
  xyzGrid->addWidget(m_btnYPlus, 2, 1);
  connect(m_btnYPlus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnYPlus);
    sendJogCommand("Y", 1.0);
  });

  // Z controls
  QHBoxLayout* zLayout = new QHBoxLayout();
  zLayout->setSpacing(4);

  m_btnZPlus = new QPushButton("Z+");
  m_btnZPlus->setMinimumSize(30, 28);
  connect(m_btnZPlus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnZPlus);
    sendJogCommand("Z", 1.0);
  });

  m_btnZMinus = new QPushButton("Z-");
  m_btnZMinus->setMinimumSize(30, 28);
  connect(m_btnZMinus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnZMinus);
    sendJogCommand("Z", -1.0);
  });

  zLayout->addWidget(m_btnZMinus);
  zLayout->addWidget(m_btnZPlus);
  xyzGrid->addLayout(zLayout, 1, 1);

  groupLayout->addLayout(xyzGrid);

  // ✅ NEW: XYZ Position Display
  QWidget* xyzPosWidget = new QWidget();
  QGridLayout* xyzPosLayout = new QGridLayout(xyzPosWidget);
  xyzPosLayout->setContentsMargins(4, 2, 4, 2);
  xyzPosLayout->setSpacing(2);

  m_posXLabel = new QLabel("X: ---");
  m_posYLabel = new QLabel("Y: ---");
  m_posZLabel = new QLabel("Z: ---");

  QString posLabelStyle = "QLabel { color: #14ffec; font-size: 10px; font-family: monospace; background-color: #1e1e1e; padding: 2px; }";
  m_posXLabel->setStyleSheet(posLabelStyle);
  m_posYLabel->setStyleSheet(posLabelStyle);
  m_posZLabel->setStyleSheet(posLabelStyle);

  xyzPosLayout->addWidget(m_posXLabel, 0, 0);
  xyzPosLayout->addWidget(m_posYLabel, 0, 1);
  xyzPosLayout->addWidget(m_posZLabel, 0, 2);

  groupLayout->addWidget(xyzPosWidget);

  // UVW Controls - Compact grid
  QLabel* uvwLabel = new QLabel("UVW Axes");
  uvwLabel->setStyleSheet("QLabel { font-weight: bold; color: #14ffec; font-size: 10px; }");
  groupLayout->addWidget(uvwLabel);

  QGridLayout* uvwGrid = new QGridLayout();
  uvwGrid->setSpacing(4);

  // U controls
  m_btnUMinus = new QPushButton("U-");
  m_btnUMinus->setMinimumSize(30, 28);
  connect(m_btnUMinus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnUMinus);
    sendJogCommand("U", -1.0);
  });

  m_btnUPlus = new QPushButton("U+");
  m_btnUPlus->setMinimumSize(30, 28);
  connect(m_btnUPlus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnUPlus);
    sendJogCommand("U", 1.0);
  });

  uvwGrid->addWidget(m_btnUMinus, 0, 0);
  uvwGrid->addWidget(m_btnUPlus, 0, 1);

  // V controls
  m_btnVMinus = new QPushButton("V-");
  m_btnVMinus->setMinimumSize(30, 28);
  connect(m_btnVMinus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnVMinus);
    sendJogCommand("V", -1.0);
  });

  m_btnVPlus = new QPushButton("V+");
  m_btnVPlus->setMinimumSize(30, 28);
  connect(m_btnVPlus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnVPlus);
    sendJogCommand("V", 1.0);
  });

  uvwGrid->addWidget(m_btnVMinus, 1, 0);
  uvwGrid->addWidget(m_btnVPlus, 1, 1);

  // W controls
  m_btnWMinus = new QPushButton("W-");
  m_btnWMinus->setMinimumSize(30, 28);
  connect(m_btnWMinus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnWMinus);
    sendJogCommand("W", -1.0);
  });

  m_btnWPlus = new QPushButton("W+");
  m_btnWPlus->setMinimumSize(30, 28);
  connect(m_btnWPlus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnWPlus);
    sendJogCommand("W", 1.0);
  });

  uvwGrid->addWidget(m_btnWMinus, 2, 0);
  uvwGrid->addWidget(m_btnWPlus, 2, 1);

  groupLayout->addLayout(uvwGrid);

  // ✅ NEW: UVW Position Display
  QWidget* uvwPosWidget = new QWidget();
  QGridLayout* uvwPosLayout = new QGridLayout(uvwPosWidget);
  uvwPosLayout->setContentsMargins(4, 2, 4, 2);
  uvwPosLayout->setSpacing(2);

  m_posULabel = new QLabel("U: ---");
  m_posVLabel = new QLabel("V: ---");
  m_posWLabel = new QLabel("W: ---");

  m_posULabel->setStyleSheet(posLabelStyle);
  m_posVLabel->setStyleSheet(posLabelStyle);
  m_posWLabel->setStyleSheet(posLabelStyle);

  uvwPosLayout->addWidget(m_posULabel, 0, 0);
  uvwPosLayout->addWidget(m_posVLabel, 0, 1);
  uvwPosLayout->addWidget(m_posWLabel, 0, 2);

  groupLayout->addWidget(uvwPosWidget);

  // Velocity presets
  QLabel* velLabel = new QLabel("Velocity");
  velLabel->setStyleSheet("QLabel { font-weight: bold; color: #14ffec; font-size: 10px; }");
  groupLayout->addWidget(velLabel);

  // Current velocity readout — large blue/yellow display, mirrors the step-size style.
  m_currentVelocityLabel = new QLabel("— mm/s");
  m_currentVelocityLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 36px; "
    "  font-weight: bold; "
    "  color: #ffff00; "
    "  background-color: #1565c0; "
    "  padding: 6px; "
    "  border-radius: 4px; "
    "}"
  );
  m_currentVelocityLabel->setAlignment(Qt::AlignCenter);
  groupLayout->addWidget(m_currentVelocityLabel);

  m_velocityPresetsWidget = new QWidget();
  QGridLayout* velGrid = new QGridLayout(m_velocityPresetsWidget);
  velGrid->setSpacing(2);
  velGrid->setContentsMargins(0, 0, 0, 0);
  groupLayout->addWidget(m_velocityPresetsWidget);

  // Populate initial presets
  updateVelocityPresets();

  groupLayout->addStretch();

  layout->addWidget(groupBox);
}


void JogPanelWindow::createExposurePanel(QWidget* parent)
{
  QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(parent->layout());
  if (!layout) return;

  if (!m_context || !m_context->cameraManager) return;

  std::vector<std::string> cameraIds = m_context->cameraManager->GetCameraIds();
  if (cameraIds.empty()) return;

  // Define which cameras get exposure controls
  struct ExposureCameraConfig {
    std::string cameraId;
    std::string label;
  };

  std::vector<ExposureCameraConfig> configs = {
    {"main_camera", "Main"},
    {"swir_camera", "SWIR"}
  };

  // Filter to only installed cameras
  std::vector<ExposureCameraConfig> activeConfigs;
  for (const auto& cfg : configs) {
    for (const auto& id : cameraIds) {
      if (id == cfg.cameraId) {
        activeConfigs.push_back(cfg);
        break;
      }
    }
  }

  if (activeConfigs.empty()) return;

  for (const auto& cfg : activeConfigs) {
    QHBoxLayout* row = new QHBoxLayout();
    row->setSpacing(4);
    row->setContentsMargins(4, 2, 4, 2);

    QLabel* label = new QLabel(QString::fromStdString(cfg.label + " Exp:"));
    label->setStyleSheet("QLabel { font-weight: bold; color: #14ffec; font-size: 10px; }");
    label->setFixedWidth(65);
    row->addWidget(label);

    QSpinBox* spinBox = new QSpinBox();
    spinBox->setMinimum(1);
    spinBox->setMaximum(1000000);
    spinBox->setSingleStep(100);
    spinBox->setSuffix(QString(" %1s").arg(QChar(0x00B5)));
    spinBox->setAlignment(Qt::AlignLeft);
    spinBox->setFixedWidth(90);  // Narrow, left-aligned — leaves room for the step grid.
    spinBox->setStyleSheet(
      "QSpinBox { "
      "  background-color: #2a2a2a; "
      "  color: white; "
      "  border: 1px solid #555; "
      "  border-radius: 3px; "
      "  padding: 2px 4px; "
      "  font-size: 10px; "
      "}"
    );

    // Read current exposure from camera
    auto settings = m_context->cameraManager->GetExposureSettings(cfg.cameraId);
    spinBox->setValue(static_cast<int>(settings.exposure_time));

    row->addWidget(spinBox, 0, Qt::AlignLeft);

    QPushButton* applyBtn = new QPushButton("Apply");
    applyBtn->setMinimumHeight(24);
    applyBtn->setMaximumHeight(24);
    applyBtn->setStyleSheet(
      "QPushButton { "
      "  background-color: #2d5a2d; "
      "  color: white; "
      "  border: 1px solid #3d8b3d; "
      "  border-radius: 3px; "
      "  padding: 2px 8px; "
      "  font-size: 10px; "
      "  font-weight: bold; "
      "} "
      "QPushButton:hover { "
      "  background-color: #3d8b3d; "
      "  border-color: #5cb85c; "
      "}"
    );

    connect(applyBtn, &QPushButton::clicked, this,
      [this, cameraId = cfg.cameraId, spinBox]() {
        applyExposure(cameraId, spinBox);
      });

    row->addWidget(applyBtn);

    // 2x2 step grid: -1k / +1k on top, -100 / +100 on bottom.
    // Each click adjusts the spinbox value (clamped to its min/max) and applies immediately.
    QGridLayout* stepGrid = new QGridLayout();
    stepGrid->setSpacing(2);
    stepGrid->setContentsMargins(0, 0, 0, 0);

    auto makeStepButton = [this, cameraId = cfg.cameraId, spinBox](const QString& text, int delta) {
      QPushButton* b = new QPushButton(text);
      b->setFixedSize(40, 20);
      b->setStyleSheet(
        "QPushButton { "
        "  background-color: #3c3c3c; "
        "  color: #d4d4d4; "
        "  border: 1px solid #555555; "
        "  border-radius: 3px; "
        "  font-size: 10px; "
        "  font-weight: bold; "
        "} "
        "QPushButton:hover { "
        "  background-color: #0e639c; "
        "  color: white; "
        "  border-color: #1177bb; "
        "} "
        "QPushButton:pressed { background-color: #0c5280; }"
      );
      connect(b, &QPushButton::clicked, this, [this, spinBox, cameraId, delta]() {
        int newVal = std::clamp(spinBox->value() + delta, spinBox->minimum(), spinBox->maximum());
        spinBox->setValue(newVal);
        applyExposure(cameraId, spinBox);
      });
      return b;
    };

    stepGrid->addWidget(makeStepButton("-1k", -1000), 0, 0);
    stepGrid->addWidget(makeStepButton("+1k", +1000), 0, 1);
    stepGrid->addWidget(makeStepButton("-100", -100), 1, 0);
    stepGrid->addWidget(makeStepButton("+100", +100), 1, 1);

    row->addLayout(stepGrid);
    row->addStretch();  // Anything past the step grid is empty space on the right.

    layout->addLayout(row);
  }
}

void JogPanelWindow::applyExposure(const std::string& cameraId, QSpinBox* spinBox)
{
  if (!m_context || !m_context->cameraManager || !spinBox) return;

  auto settings = m_context->cameraManager->GetExposureSettings(cameraId);
  settings.exposure_time = static_cast<double>(spinBox->value());
  settings.auto_exposure = false;
  settings.auto_gain = false;

  bool ok = m_context->cameraManager->ApplyExposureSettings(cameraId, settings);

  if (m_context->logManager) {
    if (ok) {
      m_context->logManager->Debug(
        "Applied exposure " + std::to_string(spinBox->value()) + " us to " + cameraId,
        "JogPanel");
    } else {
      m_context->logManager->Warning(
        "Failed to apply exposure to " + cameraId, "JogPanel");
    }
  }
}

void JogPanelWindow::createAutoFocusPanel(QWidget* parent)
{
  QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(parent->layout());
  if (!layout) return;

  // Check if we have camera manager and any cameras
  if (!m_context || !m_context->cameraManager) return;

  std::vector<std::string> cameraIds = m_context->cameraManager->GetCameraIds();
  if (cameraIds.empty()) return;

  // Define autofocus configurations per camera
  // {camera_id, motion_device, z_range_mm, z_step_mm, display_label}
  struct AutoFocusConfig {
    std::string cameraId;
    std::string motionDevice;
    double zRangeMm;
    double zStepMm;
    std::string label;
  };

  std::vector<AutoFocusConfig> configs = {
    {"main_camera",   "gantry-main", 0.1, 0.02, "AF Main"},
    {"swir_camera",   "gantry-main", 0.1, 0.01, "AF SWIR"},
    {"gantry_camera", "gantry-main", 0.1, 0.02, "AF Gantry"}
  };

  // Filter to only installed cameras
  std::vector<AutoFocusConfig> activeConfigs;
  for (const auto& cfg : configs) {
    for (const auto& id : cameraIds) {
      if (id == cfg.cameraId) {
        activeConfigs.push_back(cfg);
        break;
      }
    }
  }

  if (activeConfigs.empty()) return;

  // Create horizontal layout for autofocus buttons
  QHBoxLayout* afLayout = new QHBoxLayout();
  afLayout->setSpacing(4);
  afLayout->setContentsMargins(4, 0, 4, 0);

  QLabel* afLabel = new QLabel("Auto Focus:");
  afLabel->setStyleSheet("QLabel { font-weight: bold; color: #14ffec; font-size: 10px; }");
  afLayout->addWidget(afLabel);

  for (const auto& cfg : activeConfigs) {
    QPushButton* btn = new QPushButton(QString::fromStdString(cfg.label));
    btn->setMinimumHeight(26);
    btn->setMaximumHeight(26);
    btn->setStyleSheet(
      "QPushButton { "
      "  background-color: #2d5a2d; "
      "  color: white; "
      "  border: 1px solid #3d8b3d; "
      "  border-radius: 3px; "
      "  padding: 2px 8px; "
      "  font-size: 10px; "
      "  font-weight: bold; "
      "} "
      "QPushButton:hover { "
      "  background-color: #3d8b3d; "
      "  border-color: #5cb85c; "
      "} "
      "QPushButton:disabled { "
      "  background-color: #555; "
      "  color: #888; "
      "  border-color: #666; "
      "}"
    );

    connect(btn, &QPushButton::clicked, this,
      [this, camera = cfg.cameraId, device = cfg.motionDevice,
       range = cfg.zRangeMm, step = cfg.zStepMm, btn]() {
        runAutoFocus(camera, device, range, step, btn);
      });

    afLayout->addWidget(btn);
  }

  afLayout->addStretch();
  layout->addLayout(afLayout);
}

void JogPanelWindow::runAutoFocus(const std::string& camera, const std::string& motionDevice,
                                   double zRangeMm, double zStepMm, QPushButton* button)
{
  // Check if autofocus is already running
  if (m_autoFocusFuture.valid() &&
      m_autoFocusFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    if (m_context && m_context->logManager) {
      m_context->logManager->Warning("Auto focus already running", "JogPanel");
    }
    return;
  }

  if (!m_context) return;

  // Disable button while running
  if (button) button->setEnabled(false);

  if (m_context->logManager) {
    m_context->logManager->Info(
      "Starting auto focus: camera=" + camera + " device=" + motionDevice +
      " range=" + std::to_string(zRangeMm) + "mm step=" + std::to_string(zStepMm) + "mm",
      "JogPanel");
  }

  // Run autofocus on background thread
  ApplicationContext* ctx = m_context;
  m_autoFocusFuture = std::async(std::launch::async,
    [ctx, camera, motionDevice, zRangeMm, zStepMm, button]() {
      VisionCommandHandler visionHandler(ctx);
      SequenceContext seqCtx;
      std::string error;

      std::map<std::string, std::string> params = {
        {"motion_device", motionDevice},
        {"camera", camera},
        {"z_range_mm", std::to_string(zRangeMm)},
        {"z_step_mm", std::to_string(zStepMm)},
        {"settle_time_ms", "100"},
        {"use_center_roi", "true"},
        {"roi_width", "300"},
        {"roi_height", "300"},
        {"context_prefix", "autofocus"}
      };

      bool ok = visionHandler.ExecuteCommand(
        nullptr, "vision", "auto_focus", params, error, &seqCtx);

      if (ok) {
        std::cout << "[JogPanel] Auto focus completed successfully" << std::endl;
      }
      else {
        std::cout << "[JogPanel] Auto focus failed: " << error << std::endl;
      }

      // Re-enable button on UI thread
      if (button) {
        QMetaObject::invokeMethod(button, [button]() {
          button->setEnabled(true);
        }, Qt::QueuedConnection);
      }
    });
}

void JogPanelWindow::flashButton(QPushButton* button)
{
  if (!button) return;

  // Flash green to indicate movement
  button->setStyleSheet(
    "QPushButton { "
    "  background-color: #28a745; "
    "  color: white; "
    "  border: 2px solid #5cb85c; "
    "}"
  );

  // Reset after 150ms
  QTimer::singleShot(150, this, [button]() {
    button->setStyleSheet("");  // Clear inline style
    button->style()->unpolish(button);  // Remove old style
    button->style()->polish(button);    // Reapply global style
    button->update();  // Force repaint
  });
}

void JogPanelWindow::setKeyboardActive(bool active)
{
  m_keyboardActive = active;

  if (active) {
    m_keyboardIndicator->setText("⌨ Keyboard Control: ACTIVE");
    m_keyboardIndicator->setStyleSheet(
      "QLabel { "
      "  background-color: #28a745; "
      "  color: white; "
      "  font-weight: bold; "
      "  font-size: 11px; "
      "  padding: 4px; "
      "  border-radius: 3px; "
      "}"
    );
  }
  else {
    m_keyboardIndicator->setText("⌨ Keyboard Control: INACTIVE");
    m_keyboardIndicator->setStyleSheet(
      "QLabel { "
      "  background-color: #555555; "
      "  color: #888888; "
      "  font-weight: bold; "
      "  font-size: 11px; "
      "  padding: 4px; "
      "  border-radius: 3px; "
      "}"
    );
  }
}

void JogPanelWindow::onDeviceSelected()
{
  QPushButton* btn = qobject_cast<QPushButton*>(sender());
  if (btn && btn->isChecked()) {
    m_selectedDevice = btn->text().toStdString();

    if (m_context && m_context->logManager) {
      m_context->logManager->Info(
        "Device selected: " + m_selectedDevice,
        "JogPanel"
      );
    }

    // Reset current-velocity readout — last-applied value is per-device and now unknown.
    m_currentVelocity = -1.0;
    if (m_currentVelocityLabel) {
      m_currentVelocityLabel->setText("— mm/s");
    }

    // Update velocity presets based on device type
    updateVelocityPresets();

    // Update UVW button enabled state based on device capabilities
    updateUVWButtonState();

    // Update position display immediately when device changes
    updatePositionDisplay();
  }
}

void JogPanelWindow::updateUVWButtonState()
{
  if (!m_context || !m_context->motionManager || m_selectedDevice.empty()) {
    return;
  }

  // Check if device supports U/V/W axes
  std::string installedAxes = m_context->motionManager->GetInstalledAxes(m_selectedDevice);
  bool hasU = installedAxes.find('U') != std::string::npos;
  bool hasV = installedAxes.find('V') != std::string::npos;
  bool hasW = installedAxes.find('W') != std::string::npos;

  // Enable/disable U buttons
  if (m_btnUMinus) m_btnUMinus->setEnabled(hasU);
  if (m_btnUPlus) m_btnUPlus->setEnabled(hasU);

  // Enable/disable V buttons
  if (m_btnVMinus) m_btnVMinus->setEnabled(hasV);
  if (m_btnVPlus) m_btnVPlus->setEnabled(hasV);

  // Enable/disable W buttons
  if (m_btnWMinus) m_btnWMinus->setEnabled(hasW);
  if (m_btnWPlus) m_btnWPlus->setEnabled(hasW);
}

void JogPanelWindow::onStepSizeSelected()
{
  QPushButton* btn = qobject_cast<QPushButton*>(sender());
  if (btn) {
    m_currentStepSize = btn->property("stepSize").toDouble();
    updateStepSizeDisplay();

    if (m_context && m_context->logManager) {
      m_context->logManager->Info(
        "Step size: " + QString::number(m_currentStepSize).toStdString() + "um",
        "JogPanel"
      );
    }
  }
}

void JogPanelWindow::updateStepSizeDisplay()
{
  if (!m_currentStepLabel) return;

  QString text;
  if (m_currentStepSize >= 1000.0) {
    text = QString::number(m_currentStepSize / 1000.0, 'f', 0) + " mm";
  }
  else if (m_currentStepSize < 1.0) {
    // Show decimal for sub-micron steps (0.1, 0.5)
    text = QString::number(m_currentStepSize, 'f', 1) + " µm";
  }
  else {
    text = QString::number(m_currentStepSize, 'f', 0) + " µm";
  }

  m_currentStepLabel->setText("⚙ " + text);
}

void JogPanelWindow::sendJogCommand(const std::string& axis, double direction)
{
  if (m_selectedDevice.empty()) {
    if (m_context && m_context->logManager) {
      m_context->logManager->Warning("No device selected", "JogPanel");
    }
    return;
  }

  if (!m_jogWorker) {
    // Fallback: Worker not available, log error
    if (m_context && m_context->logManager) {
      m_context->logManager->Error("JogWorker not initialized", "JogPanel");
    }
    return;
  }

  // Queue the jog command to the worker thread
  // This returns immediately, motion happens asynchronously
  m_jogWorker->queueJogCommand(m_selectedDevice, axis, direction, m_currentStepSize);
}

void JogPanelWindow::onJogStarted(QString deviceName, QString axis, double direction)
{
  // Optional: Could update UI to show jog in progress
  Q_UNUSED(deviceName);
  Q_UNUSED(axis);
  Q_UNUSED(direction);
}

void JogPanelWindow::onJogCompleted(QString deviceName, QString axis, bool success, QString message)
{
  Q_UNUSED(deviceName);
  Q_UNUSED(axis);

  if (m_context && m_context->logManager) {
    if (success) {
      m_context->logManager->Info(message.toStdString(), "JogPanel");
    }
    else {
      m_context->logManager->Error(message.toStdString(), "JogPanel");
    }
  }
}

void JogPanelWindow::onJogWorkerLog(QString message)
{
  if (m_context && m_context->logManager) {
    m_context->logManager->Info(message.toStdString(), "JogPanel");
  }
}

void JogPanelWindow::showPanel()
{
  // Refresh device list to show only connected devices
  refreshDeviceList();

  // Update U/V/W button state for currently selected device
  updateUVWButtonState();

  // Start position update timer
  if (m_positionUpdateTimer) {
    m_positionUpdateTimer->start();
    updatePositionDisplay();  // Initial update
  }

  show();
  raise();
  activateWindow();
  emit visibilityChanged(true);
}

void JogPanelWindow::hidePanel()
{
  // Stop position update timer
  if (m_positionUpdateTimer) {
    m_positionUpdateTimer->stop();
  }

  // Save window position before hiding
  saveWindowSettings();

  hide();
  emit visibilityChanged(false);
}

bool JogPanelWindow::isPanelVisible() const
{
  return isVisible();
}

std::string JogPanelWindow::getSelectedDevice() const
{
  return m_selectedDevice;
}

double JogPanelWindow::getCurrentStepSize() const
{
  return m_currentStepSize;
}

void JogPanelWindow::closeEvent(QCloseEvent* event)
{
  // Save window position before closing
  saveWindowSettings();
  hidePanel();
  event->accept();
}

void JogPanelWindow::jogX(double direction)
{
  if (direction > 0) flashButton(m_btnXPlus);
  else flashButton(m_btnXMinus);
  sendJogCommand("X", direction);
}

void JogPanelWindow::jogY(double direction)
{
  if (direction > 0) flashButton(m_btnYPlus);
  else flashButton(m_btnYMinus);
  sendJogCommand("Y", direction);
}

void JogPanelWindow::jogZ(double direction)
{
  if (direction > 0) flashButton(m_btnZPlus);
  else flashButton(m_btnZMinus);
  sendJogCommand("Z", direction);
}

void JogPanelWindow::jogU(double direction)
{
  if (direction > 0) flashButton(m_btnUPlus);
  else flashButton(m_btnUMinus);
  sendJogCommand("U", direction);
}

void JogPanelWindow::jogV(double direction)
{
  if (direction > 0) flashButton(m_btnVPlus);
  else flashButton(m_btnVMinus);
  sendJogCommand("V", direction);
}

void JogPanelWindow::jogW(double direction)
{
  if (direction > 0) flashButton(m_btnWPlus);
  else flashButton(m_btnWMinus);
  sendJogCommand("W", direction);
}

// ✅ NEW: Update position display labels
void JogPanelWindow::updatePositionDisplay()
{
  if (m_selectedDevice.empty() || !m_context || !m_context->motionManager) {
    // No device selected or no motion manager available
    if (m_posXLabel) m_posXLabel->setText("X: ---");
    if (m_posYLabel) m_posYLabel->setText("Y: ---");
    if (m_posZLabel) m_posZLabel->setText("Z: ---");
    if (m_posULabel) m_posULabel->setText("U: ---");
    if (m_posVLabel) m_posVLabel->setText("V: ---");
    if (m_posWLabel) m_posWLabel->setText("W: ---");
    return;
  }

  // Get installed axes for this device to avoid querying unsupported axes
  std::string installedAxes = m_context->motionManager->GetInstalledAxes(m_selectedDevice);
  bool hasU = installedAxes.find('U') != std::string::npos;
  bool hasV = installedAxes.find('V') != std::string::npos;
  bool hasW = installedAxes.find('W') != std::string::npos;

  // Read from the motion manager's background-polled cache — non-blocking.
  // The cache is refreshed every kPositionPollIntervalMs (~200ms) by a
  // dedicated worker thread, so this never touches hardware on the UI thread.
  PositionStruct pos;
  const bool cacheValid = m_context->motionManager->GetCachedPosition(
      m_selectedDevice, pos);

  const double x = pos.x, y = pos.y, z = pos.z;
  const double u = pos.u, v = pos.v, w = pos.w;
  const bool xValid = cacheValid;
  const bool yValid = cacheValid;
  const bool zValid = cacheValid;
  const bool uValid = cacheValid && hasU;
  const bool vValid = cacheValid && hasV;
  const bool wValid = cacheValid && hasW;

  // Update labels with formatted values (4 decimal places — 0.1 µm visible)
  if (m_posXLabel) {
    m_posXLabel->setText(xValid ? QString("X: %1").arg(x, 0, 'f', 4) : "X: ---");
  }
  if (m_posYLabel) {
    m_posYLabel->setText(yValid ? QString("Y: %1").arg(y, 0, 'f', 4) : "Y: ---");
  }
  if (m_posZLabel) {
    m_posZLabel->setText(zValid ? QString("Z: %1").arg(z, 0, 'f', 4) : "Z: ---");
  }
  if (m_posULabel) {
    m_posULabel->setText(uValid ? QString("U: %1").arg(u, 0, 'f', 4) : "U: ---");
  }
  if (m_posVLabel) {
    m_posVLabel->setText(vValid ? QString("V: %1").arg(v, 0, 'f', 4) : "V: ---");
  }
  if (m_posWLabel) {
    m_posWLabel->setText(wValid ? QString("W: %1").arg(w, 0, 'f', 4) : "W: ---");
  }
}

void JogPanelWindow::cycleStepSizeUp()
{
  auto it = std::find(m_stepSizes.begin(), m_stepSizes.end(), m_currentStepSize);
  if (it != m_stepSizes.end()) {
    ++it;
    if (it == m_stepSizes.end()) {
      it = m_stepSizes.begin();
    }

    size_t index = std::distance(m_stepSizes.begin(), it);
    m_currentStepSize = *it;

    auto button = m_stepSizeButtonGroup->button(static_cast<int>(index));
    if (button) {
      button->setChecked(true);
    }

    updateStepSizeDisplay();

    if (m_context && m_context->logManager) {
      m_context->logManager->Info(
        "Step size: " + QString::number(m_currentStepSize).toStdString() + "µm",
        "JogPanel"
      );
    }
  }
}

void JogPanelWindow::cycleStepSizeDown()
{
  auto it = std::find(m_stepSizes.begin(), m_stepSizes.end(), m_currentStepSize);
  if (it != m_stepSizes.end()) {
    if (it == m_stepSizes.begin()) {
      it = m_stepSizes.end();
    }
    --it;

    size_t index = std::distance(m_stepSizes.begin(), it);
    m_currentStepSize = *it;

    auto button = m_stepSizeButtonGroup->button(static_cast<int>(index));
    if (button) {
      button->setChecked(true);
    }

    updateStepSizeDisplay();

    if (m_context && m_context->logManager) {
      m_context->logManager->Info(
        "Step size: " + QString::number(m_currentStepSize).toStdString() + "µm",
        "JogPanel"
      );
    }
  }
}

void JogPanelWindow::saveWindowSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  settings.setValue("JogPanelWindow/geometry", saveGeometry());
  std::cout << "[JogPanelWindow] Window settings saved" << std::endl;
}

void JogPanelWindow::restoreWindowSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  QByteArray geometry = settings.value("JogPanelWindow/geometry").toByteArray();

  if (!geometry.isEmpty()) {
    restoreGeometry(geometry);
    std::cout << "[JogPanelWindow] Window geometry restored" << std::endl;
  }
}