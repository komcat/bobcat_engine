// QuickOutputWidget.cpp
#include "QuickOutputWidget.h"
#include "QuickOutputManager.h"
#include "safety/SafetyWatchManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QCheckBox>
#include <QMenu>
#include <QInputDialog>
#include <QSettings>

// ============================================================================
// OutputButton Implementation
// ============================================================================

OutputButton::OutputButton(const QString& displayName, size_t index, QWidget* parent)
  : QWidget(parent)
  , m_index(index)
  , m_isOn(false)
  , m_isValid(false)
{
  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(2, 2, 2, 2);
  layout->setSpacing(2);

  // Top mini-row: right-aligned momentary checkbox.
  QHBoxLayout* topRow = new QHBoxLayout();
  topRow->setContentsMargins(0, 0, 0, 0);
  topRow->setSpacing(0);
  topRow->addStretch();
  m_momentaryCheck = new QCheckBox("Mom");
  m_momentaryCheck->setToolTip(
    "Momentary mode — when checked, clicking pulses ON for the dwell time then OFF.\n"
    "Right-click the button below to set the dwell time (ms).");
  m_momentaryCheck->setStyleSheet(
    "QCheckBox { color: #cccccc; font-size: 10px; spacing: 3px; }"
    "QCheckBox::indicator { width: 12px; height: 12px; }");
  connect(m_momentaryCheck, &QCheckBox::toggled, this, [this](bool on) {
    m_momentaryEnabled = on;
    emit momentaryToggled(m_index, on);
  });
  topRow->addWidget(m_momentaryCheck);
  layout->addLayout(topRow);

  // Main toggle button
  m_button = new QPushButton(displayName);
  m_button->setMinimumHeight(40);
  m_button->setCheckable(false);
  connect(m_button, &QPushButton::clicked, this, [this]() {
    emit clicked(m_index);
  });
  m_button->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_button, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    emit contextMenuRequested(m_index, m_button->mapToGlobal(pos));
  });
  layout->addWidget(m_button);

  // Status label
  m_statusLabel = new QLabel("OFF");
  m_statusLabel->setAlignment(Qt::AlignCenter);
  m_statusLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 10px; "
    "  font-weight: bold; "
    "  padding: 2px; "
    "  border-radius: 2px; "
    "}"
  );
  layout->addWidget(m_statusLabel);

  updateStyle();
}

void OutputButton::setMomentaryEnabled(bool on)
{
  m_momentaryEnabled = on;
  if (m_momentaryCheck && m_momentaryCheck->isChecked() != on) {
    QSignalBlocker blocker(m_momentaryCheck);  // avoid re-emitting momentaryToggled
    m_momentaryCheck->setChecked(on);
  }
}

void OutputButton::setDwellMs(int ms)
{
  if (ms < 0) ms = 0;
  m_dwellMs = ms;
  if (m_momentaryCheck) {
    m_momentaryCheck->setToolTip(
      QString("Momentary mode — when checked, clicking pulses ON for %1 ms then OFF.\n"
              "Right-click the button below to change the dwell time.").arg(m_dwellMs));
  }
}

void OutputButton::setOutputState(bool isOn, bool isValid)
{
  // 5 Hz poll churns updateStyle() — which sets two stylesheets per call —
  // even when nothing changed. Diff against the cached state and skip the
  // QSS recompute on no-change ticks. First call always paints so the
  // constructor's preliminary updateStyle() can't leave a stale visual.
  if (m_styleInitialized && m_isOn == isOn && m_isValid == isValid) return;
  m_styleInitialized = true;
  m_isOn = isOn;
  m_isValid = isValid;
  updateStyle();
}

void OutputButton::updateStyle()
{
  if (!m_isValid) {
    // Invalid/disconnected state
    m_button->setStyleSheet(
      "QPushButton { "
      "  background-color: #555555; "
      "  color: #999999; "
      "  border: 1px solid #666666; "
      "  font-weight: bold; "
      "  border-radius: 3px; "
      "}"
    );
    m_statusLabel->setText("ERR");
    m_statusLabel->setStyleSheet(
      "QLabel { "
      "  background-color: #888888; "
      "  color: white; "
      "  font-size: 10px; "
      "  font-weight: bold; "
      "  padding: 2px; "
      "  border-radius: 2px; "
      "}"
    );
  }
  else if (m_isOn) {
    // ON state - green
    m_button->setStyleSheet(
      "QPushButton { "
      "  background-color: #27ae60; "
      "  color: white; "
      "  border: 2px solid #2ecc71; "
      "  font-weight: bold; "
      "  border-radius: 3px; "
      "} "
      "QPushButton:hover { "
      "  background-color: #229954; "
      "}"
    );
    m_statusLabel->setText("ON");
    m_statusLabel->setStyleSheet(
      "QLabel { "
      "  background-color: #2ecc71; "
      "  color: white; "
      "  font-size: 10px; "
      "  font-weight: bold; "
      "  padding: 2px; "
      "  border-radius: 2px; "
      "}"
    );
  }
  else {
    // OFF state - gray
    m_button->setStyleSheet(
      "QPushButton { "
      "  background-color: #3a3a3a; "
      "  color: white; "
      "  border: 1px solid #555555; "
      "  font-weight: bold; "
      "  border-radius: 3px; "
      "} "
      "QPushButton:hover { "
      "  background-color: #4a4a4a; "
      "}"
    );
    m_statusLabel->setText("OFF");
    m_statusLabel->setStyleSheet(
      "QLabel { "
      "  background-color: #555555; "
      "  color: #cccccc; "
      "  font-size: 10px; "
      "  font-weight: bold; "
      "  padding: 2px; "
      "  border-radius: 2px; "
      "}"
    );
  }
}

// ============================================================================
// QuickOutputWidget Implementation
// ============================================================================

namespace {
// Stable per-pin settings key. Uses pinNumber when available (the configured
// case for everything in quick_outputs.json today), otherwise falls back to
// pinName. Trailing slash so callers append "momentary" / "dwellMs".
QString settingsKey(const OutputPinDefinition& def) {
  QString id = (def.pinNumber >= 0)
    ? QString::number(def.pinNumber)
    : QString::fromStdString(def.pinName);
  return QString("QuickOutput/%1:%2/")
           .arg(QString::fromStdString(def.deviceName))
           .arg(id);
}
}  // namespace

QuickOutputWidget::QuickOutputWidget(QuickOutputManager* manager, QWidget* parent)
  : QWidget(parent)
  , m_manager(manager)
  , m_pollInterval(200)
{
  setupUI();

  m_pollTimer = new QTimer(this);
  connect(m_pollTimer, &QTimer::timeout, this, &QuickOutputWidget::updateOutputStatuses);

  refreshOutputList();
}

QuickOutputWidget::~QuickOutputWidget()
{
  stopPolling();
}

void QuickOutputWidget::setSafetyWatchManager(SafetyWatchManager* safetyMgr)
{
  m_safetyWatchManager = safetyMgr;

  // Resume polling when E-Stop is cleared (if widget is visible)
  if (m_safetyWatchManager) {
    connect(m_safetyWatchManager, &SafetyWatchManager::safetyCleared,
            this, [this]() {
      if (isVisible() && m_pollTimer && !m_pollTimer->isActive()) {
        m_pollTimer->start(m_pollInterval);
      }
    });
  }
}

void QuickOutputWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(4, 4, 4, 4);
  mainLayout->setSpacing(4);

  // Header section with controls
  QGroupBox* headerGroup = new QGroupBox("Quick Output Control");
  headerGroup->setStyleSheet(
    "QGroupBox { "
    "  font-weight: bold; "
    "  font-size: 11px; "
    "  border: 1px solid #555; "
    "  margin-top: 8px; "
    "  padding-top: 6px; "
    "} "
    "QGroupBox::title { "
    "  color: white; "
    "  subcontrol-origin: margin; "
    "  left: 8px; "
    "  padding: 0 4px; "
    "}"
  );
  QVBoxLayout* headerLayout = new QVBoxLayout(headerGroup);
  headerLayout->setSpacing(4);

  // Control buttons
  QHBoxLayout* controlLayout = new QHBoxLayout();
  controlLayout->setSpacing(4);

  m_refreshButton = new QPushButton("🔄 Refresh");
  m_refreshButton->setMinimumHeight(28);
  m_refreshButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3498db; "
    "  color: white; "
    "  font-weight: bold; "
    "  border-radius: 3px; "
    "  padding: 4px 8px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #2980b9; "
    "}"
  );
  connect(m_refreshButton, &QPushButton::clicked, this, &QuickOutputWidget::onRefreshButtonClicked);
  controlLayout->addWidget(m_refreshButton);

  m_turnOffAllButton = new QPushButton("⏹ Turn All OFF");
  m_turnOffAllButton->setMinimumHeight(28);
  m_turnOffAllButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #e74c3c; "
    "  color: white; "
    "  font-weight: bold; "
    "  border-radius: 3px; "
    "  padding: 4px 8px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #c0392b; "
    "}"
  );
  connect(m_turnOffAllButton, &QPushButton::clicked, this, &QuickOutputWidget::onTurnOffAllClicked);
  controlLayout->addWidget(m_turnOffAllButton);

  headerLayout->addLayout(controlLayout);

  // Status label
  m_statusLabel = new QLabel("Ready");
  m_statusLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 10px; "
    "  color: #aaaaaa; "
    "  padding: 2px; "
    "}"
  );
  headerLayout->addWidget(m_statusLabel);

  mainLayout->addWidget(headerGroup);

  // Scroll area for output grid
  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setStyleSheet(
    "QScrollArea { "
    "  background-color: #2b2b2b; "
    "  border: 1px solid #555; "
    "}"
  );

  QWidget* scrollWidget = new QWidget();
  m_outputGrid = new QGridLayout(scrollWidget);
  m_outputGrid->setSpacing(6);
  m_outputGrid->setContentsMargins(4, 4, 4, 4);

  scrollArea->setWidget(scrollWidget);
  mainLayout->addWidget(scrollArea);

  // Global styling
  setStyleSheet(R"(
    QWidget {
      background-color: #2b2b2b;
      color: white;
    }
  )");
}

void QuickOutputWidget::refreshOutputList()
{
  clearOutputButtons();
  createOutputButtons();
  updateOutputStatuses();
}

void QuickOutputWidget::createOutputButtons()
{
  if (!m_manager) {
    return;
  }

  size_t pinCount = m_manager->GetPinCount();

  if (pinCount == 0) {
    QLabel* emptyLabel = new QLabel("No outputs configured.\n\nAdd outputs to QuickOutputManager to get started.");
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setStyleSheet(
      "QLabel { "
      "  color: #888888; "
      "  font-size: 11px; "
      "  padding: 20px; "
      "}"
    );
    m_outputGrid->addWidget(emptyLabel, 0, 0);
    return;
  }

  // Create buttons in grid (3 columns)
  const int columns = 3;

  QSettings settings("Fabrinet", "BobcatEngine");

  for (size_t i = 0; i < pinCount; ++i) {
    const OutputPinDefinition* pinDef = m_manager->GetPinDefinition(i);
    if (!pinDef) {
      continue;
    }

    QString displayName = QString::fromStdString(pinDef->displayName);
    OutputButton* button = new OutputButton(displayName, i, this);

    // Restore persisted momentary settings for this pin.
    const QString keyBase = settingsKey(*pinDef);
    button->setMomentaryEnabled(settings.value(keyBase + "momentary", false).toBool());
    button->setDwellMs(settings.value(keyBase + "dwellMs", 0).toInt());

    connect(button, &OutputButton::clicked, this, &QuickOutputWidget::onOutputButtonClicked);
    connect(button, &OutputButton::momentaryToggled, this, &QuickOutputWidget::onMomentaryToggled);
    connect(button, &OutputButton::contextMenuRequested, this, &QuickOutputWidget::onOutputContextMenu);

    int row = static_cast<int>(i) / columns;
    int col = static_cast<int>(i) % columns;
    m_outputGrid->addWidget(button, row, col);

    m_outputButtons.push_back(button);
  }

  m_statusLabel->setText(QString("Loaded %1 output(s)").arg(pinCount));
}

void QuickOutputWidget::clearOutputButtons()
{
  // Clear all widgets from grid
  while (m_outputGrid->count() > 0) {
    QLayoutItem* item = m_outputGrid->takeAt(0);
    if (item->widget()) {
      delete item->widget();
    }
    delete item;
  }

  m_outputButtons.clear();
}

void QuickOutputWidget::onOutputButtonClicked(size_t index)
{
  if (!m_manager) {
    return;
  }

  // Momentary path: ON now, OFF after dwell. Reuses the same QTimer::singleShot
  // pattern already used by this widget for delayed status updates.
  OutputButton* btn = (index < m_outputButtons.size()) ? m_outputButtons[index] : nullptr;
  if (btn && btn->isMomentaryEnabled()) {
    EziIOError onErr = m_manager->TurnOn(index);
    if (onErr != EziIOError::SUCCESS) {
      m_statusLabel->setText(QString("Error: %1").arg(QString::fromStdString(GetErrorString(onErr))));
      return;
    }
    updateOutputStatuses();
    OutputPinStatus status = m_manager->GetPinStatus(index);
    emit outputToggled(index, true);
    m_statusLabel->setText(QString("Output %1: pulse %2 ms")
      .arg(QString::fromStdString(status.displayName))
      .arg(btn->dwellMs()));

    int dwell = btn->dwellMs();
    if (dwell < 0) dwell = 0;
    QTimer::singleShot(dwell, this, [this, index]() {
      if (m_manager) {
        EziIOError offErr = m_manager->TurnOff(index);
        if (offErr != EziIOError::SUCCESS) {
          m_statusLabel->setText(QString("Error: %1").arg(QString::fromStdString(GetErrorString(offErr))));
        }
      }
      updateOutputStatuses();
      emit outputToggled(index, false);
    });
    return;
  }

  // Latching (existing) path: toggle on click.
  EziIOError error = m_manager->ToggleOutput(index);

  if (error == EziIOError::SUCCESS) {
    // Immediate status update
    updateOutputStatuses();

    // Get new state
    OutputPinStatus status = m_manager->GetPinStatus(index);
    emit outputToggled(index, status.isOn);

    m_statusLabel->setText(QString("Output %1: %2")
      .arg(QString::fromStdString(status.displayName))
      .arg(status.isOn ? "ON" : "OFF"));

    // Force another update after a short delay to ensure UI reflects hardware state
    QTimer::singleShot(50, this, [this]() {
      updateOutputStatuses();
    });
  }
  else {
    m_statusLabel->setText(QString("Error: %1").arg(QString::fromStdString(GetErrorString(error))));
  }
}

void QuickOutputWidget::onMomentaryToggled(size_t index, bool enabled)
{
  if (!m_manager) return;
  const OutputPinDefinition* pinDef = m_manager->GetPinDefinition(index);
  if (!pinDef) return;
  QSettings settings("Fabrinet", "BobcatEngine");
  settings.setValue(settingsKey(*pinDef) + "momentary", enabled);
}

void QuickOutputWidget::onOutputContextMenu(size_t index, QPoint globalPos)
{
  if (!m_manager) return;
  if (index >= m_outputButtons.size()) return;
  const OutputPinDefinition* pinDef = m_manager->GetPinDefinition(index);
  if (!pinDef) return;

  OutputButton* btn = m_outputButtons[index];

  QMenu menu;
  QAction* setDwell = menu.addAction("Set dwell time…");
  QAction* clearDwell = menu.addAction("Reset (momentary off, dwell 0)");
  QAction* chosen = menu.exec(globalPos);
  if (!chosen) return;

  QSettings settings("Fabrinet", "BobcatEngine");
  const QString keyBase = settingsKey(*pinDef);

  if (chosen == setDwell) {
    bool ok = false;
    int newDwell = QInputDialog::getInt(
      this,
      "Momentary Dwell",
      QString("Dwell time for '%1' (ms):")
        .arg(QString::fromStdString(pinDef->displayName)),
      btn->dwellMs(),  // current
      0,               // min
      60000,           // max — 60 s ceiling, plenty for any momentary pulse
      1,               // step
      &ok);
    if (!ok) return;
    btn->setDwellMs(newDwell);
    settings.setValue(keyBase + "dwellMs", newDwell);
  }
  else if (chosen == clearDwell) {
    btn->setMomentaryEnabled(false);
    btn->setDwellMs(0);
    settings.setValue(keyBase + "momentary", false);
    settings.setValue(keyBase + "dwellMs", 0);
  }
}

void QuickOutputWidget::onRefreshButtonClicked()
{
  if (m_manager) {
    m_manager->RefreshAllStatuses();
    updateOutputStatuses();
    m_statusLabel->setText("Refreshed");
  }
}

void QuickOutputWidget::onTurnOffAllClicked()
{
  if (m_manager) {
    m_manager->TurnOffAll();
    updateOutputStatuses();
    m_statusLabel->setText("All outputs turned OFF");
  }
}

void QuickOutputWidget::updateOutputStatuses()
{
  // Skip hardware access during E-Stop
  if (m_safetyWatchManager && m_safetyWatchManager->IsCriticalFlagRaised()) {
    stopPolling();
    return;
  }

  if (!m_manager) {
    return;
  }

  for (OutputButton* button : m_outputButtons) {
    size_t index = button->getIndex();
    OutputPinStatus status = m_manager->GetPinStatus(index);
    button->setOutputState(status.isOn, status.isValid);
  }
}

void QuickOutputWidget::startPolling(int intervalMs)
{
  // Don't start polling during E-Stop
  if (m_safetyWatchManager && m_safetyWatchManager->IsCriticalFlagRaised()) {
    return;
  }

  m_pollInterval = intervalMs;
  if (!m_pollTimer->isActive()) {
    m_pollTimer->start(m_pollInterval);
  }
}

void QuickOutputWidget::stopPolling()
{
  if (m_pollTimer->isActive()) {
    m_pollTimer->stop();
  }
}

void QuickOutputWidget::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  startPolling(m_pollInterval);
  updateOutputStatuses();
}

void QuickOutputWidget::hideEvent(QHideEvent* event)
{
  QWidget::hideEvent(event);
  stopPolling();
}
