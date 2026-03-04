// QuickOutputWidget.cpp
#include "QuickOutputWidget.h"
#include "QuickOutputManager.h"
#include "safety/SafetyWatchManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>

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

  // Main toggle button
  m_button = new QPushButton(displayName);
  m_button->setMinimumHeight(40);
  m_button->setCheckable(false);
  connect(m_button, &QPushButton::clicked, this, [this]() {
    emit clicked(m_index);
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

void OutputButton::setOutputState(bool isOn, bool isValid)
{
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

  for (size_t i = 0; i < pinCount; ++i) {
    const OutputPinDefinition* pinDef = m_manager->GetPinDefinition(i);
    if (!pinDef) {
      continue;
    }

    QString displayName = QString::fromStdString(pinDef->displayName);
    OutputButton* button = new OutputButton(displayName, i, this);

    connect(button, &OutputButton::clicked, this, &QuickOutputWidget::onOutputButtonClicked);

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

  // Toggle the output
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
