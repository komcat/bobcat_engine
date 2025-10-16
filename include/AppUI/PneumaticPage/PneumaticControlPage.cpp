// PneumaticControlPage.cpp
#include "PneumaticControlPage.h"
#include "ApplicationContext.h"
#include "PneumaticManager.h"
#include "Pneumatic_Types.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <iostream>

PneumaticControlPage::PneumaticControlPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_slideListPanel(nullptr)
  , m_slideList(nullptr)
  , m_refreshButton(nullptr)
  , m_controlPanel(nullptr)
  , m_slideNameLabel(nullptr)
  , m_connectionStatusLabel(nullptr)
  , m_positionLabel(nullptr)
  , m_positionValueLabel(nullptr)
  , m_positionBar(nullptr)
  , m_outputStatusLabel(nullptr)
  , m_extendedSensorLabel(nullptr)
  , m_retractedSensorLabel(nullptr)
  , m_timeoutLabel(nullptr)
  , m_extendButton(nullptr)
  , m_retractButton(nullptr)
  , m_stopButton(nullptr)
  , m_updateTimer(nullptr)
{
  setupUI();

  m_updateTimer = new QTimer(this);
  connect(m_updateTimer, &QTimer::timeout, this, &PneumaticControlPage::updateStatus);
}

PneumaticControlPage::~PneumaticControlPage()
{
  if (m_updateTimer) {
    m_updateTimer->stop();
  }
}

void PneumaticControlPage::setupUI()
{
  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  createSlideListPanel();
  mainLayout->addWidget(m_slideListPanel, 3);

  createControlPanel();
  mainLayout->addWidget(m_controlPanel, 7);

  setStyleSheet("QWidget { background-color: #f5f6fa; }");
}

void PneumaticControlPage::createSlideListPanel()
{
  m_slideListPanel = new QWidget();
  m_slideListPanel->setStyleSheet("QWidget { background-color: #ecf0f1; }");

  QVBoxLayout* layout = new QVBoxLayout(m_slideListPanel);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(10);

  // Title
  QLabel* titleLabel = new QLabel("Pneumatic Slides");
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
  connect(m_refreshButton, &QPushButton::clicked, this, &PneumaticControlPage::onRefreshClicked);
  layout->addWidget(m_refreshButton);

  // Slide list
  m_slideList = new QListWidget();
  m_slideList->setStyleSheet(
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
  connect(m_slideList, &QListWidget::itemSelectionChanged, this, &PneumaticControlPage::onSlideSelected);
  layout->addWidget(m_slideList);
}

void PneumaticControlPage::createControlPanel()
{
  m_controlPanel = new QWidget();

  QVBoxLayout* layout = new QVBoxLayout(m_controlPanel);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(20);

  // ============================================================================
  // HEADER
  // ============================================================================
  QHBoxLayout* headerLayout = new QHBoxLayout();

  m_slideNameLabel = new QLabel("No slide selected");
  m_slideNameLabel->setStyleSheet("QLabel { font-size: 18px; font-weight: bold; color: #2c3e50; }");
  headerLayout->addWidget(m_slideNameLabel);

  headerLayout->addStretch();

  m_connectionStatusLabel = new QLabel("⚪ Not Connected");
  m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 14px; color: #7f8c8d; }");
  headerLayout->addWidget(m_connectionStatusLabel);

  layout->addLayout(headerLayout);

  // ============================================================================
  // STATUS GROUP
  // ============================================================================
  QGroupBox* statusGroup = new QGroupBox("Status");
  statusGroup->setStyleSheet(
    "QGroupBox { "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border: 2px solid #bdc3c7; "
    "  border-radius: 5px; "
    "  margin-top: 10px; "
    "  padding-top: 10px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "}");

  QGridLayout* statusLayout = new QGridLayout(statusGroup);
  statusLayout->setSpacing(15);

  // Position
  m_positionLabel = new QLabel("Position:");
  m_positionLabel->setStyleSheet("QLabel { font-weight: bold; }");
  m_positionValueLabel = new QLabel("Unknown");
  m_positionValueLabel->setStyleSheet("QLabel { font-size: 16px; color: #7f8c8d; }");

  statusLayout->addWidget(m_positionLabel, 0, 0);
  statusLayout->addWidget(m_positionValueLabel, 0, 1);

  // Position bar - Visual indicator
  m_positionBar = new QProgressBar();
  m_positionBar->setRange(0, 100);
  m_positionBar->setValue(0);
  m_positionBar->setFormat("");  // No text inside bar
  m_positionBar->setTextVisible(false);
  m_positionBar->setStyleSheet(
    "QProgressBar { "
    "  border: 2px solid #bdc3c7; "
    "  border-radius: 5px; "
    "  background-color: #ecf0f1; "
    "  height: 30px; "
    "  text-align: center; "
    "} "
    "QProgressBar::chunk { "
    "  background-color: #3498db; "
    "  border-radius: 3px; "
    "}");
  statusLayout->addWidget(m_positionBar, 1, 0, 1, 2);

  // Elapsed Time - NEW
  m_elapsedTimeLabel = new QLabel("Elapsed Time:");
  m_elapsedTimeLabel->setStyleSheet("QLabel { font-weight: bold; }");
  m_elapsedTimeValueLabel = new QLabel("-- ms");
  m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 14px; color: #95a5a6; }");

  statusLayout->addWidget(m_elapsedTimeLabel, 2, 0);
  statusLayout->addWidget(m_elapsedTimeValueLabel, 2, 1);

  // Output status
  QLabel* outputLabel = new QLabel("Output:");
  outputLabel->setStyleSheet("QLabel { font-weight: bold; }");
  m_outputStatusLabel = new QLabel("⚪ OFF");

  statusLayout->addWidget(outputLabel, 3, 0);
  statusLayout->addWidget(m_outputStatusLabel, 3, 1);

  // Extended sensor
  QLabel* extendedLabel = new QLabel("Extended Sensor:");
  extendedLabel->setStyleSheet("QLabel { font-weight: bold; }");
  m_extendedSensorLabel = new QLabel("⚪ OFF");

  statusLayout->addWidget(extendedLabel, 4, 0);
  statusLayout->addWidget(m_extendedSensorLabel, 4, 1);

  // Retracted sensor
  QLabel* retractedLabel = new QLabel("Retracted Sensor:");
  retractedLabel->setStyleSheet("QLabel { font-weight: bold; }");
  m_retractedSensorLabel = new QLabel("⚪ OFF");

  statusLayout->addWidget(retractedLabel, 5, 0);
  statusLayout->addWidget(m_retractedSensorLabel, 5, 1);

  // Timeout
  QLabel* timeoutLabelText = new QLabel("Timeout:");
  timeoutLabelText->setStyleSheet("QLabel { font-weight: bold; }");
  m_timeoutLabel = new QLabel("5000 ms");

  statusLayout->addWidget(timeoutLabelText, 6, 0);
  statusLayout->addWidget(m_timeoutLabel, 6, 1);

  layout->addWidget(statusGroup);

  // ============================================================================
  // CONTROL GROUP
  // ============================================================================
  QGroupBox* controlGroup = new QGroupBox("Control");
  controlGroup->setStyleSheet(
    "QGroupBox { "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border: 2px solid #bdc3c7; "
    "  border-radius: 5px; "
    "  margin-top: 10px; "
    "  padding-top: 10px; "
    "}");

  QHBoxLayout* controlLayout = new QHBoxLayout(controlGroup);
  controlLayout->setSpacing(10);

  // Extend button
  m_extendButton = new QPushButton("⬆ Extend");
  m_extendButton->setEnabled(false);
  m_extendButton->setMinimumHeight(50);
  m_extendButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #27ae60; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 5px; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #229954; } "
    "QPushButton:disabled { background-color: #95a5a6; }");
  connect(m_extendButton, &QPushButton::clicked, this, &PneumaticControlPage::onExtendClicked);
  controlLayout->addWidget(m_extendButton);

  // Retract button
  m_retractButton = new QPushButton("⬇ Retract");
  m_retractButton->setEnabled(false);
  m_retractButton->setMinimumHeight(50);
  m_retractButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #e67e22; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 5px; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #d35400; } "
    "QPushButton:disabled { background-color: #95a5a6; }");
  connect(m_retractButton, &QPushButton::clicked, this, &PneumaticControlPage::onRetractClicked);
  controlLayout->addWidget(m_retractButton);

  // Stop button
  m_stopButton = new QPushButton("⏹ Stop");
  m_stopButton->setEnabled(false);
  m_stopButton->setMinimumHeight(50);
  m_stopButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #e74c3c; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 5px; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #c0392b; } "
    "QPushButton:disabled { background-color: #95a5a6; }");
  connect(m_stopButton, &QPushButton::clicked, this, &PneumaticControlPage::onStopClicked);
  controlLayout->addWidget(m_stopButton);

  layout->addWidget(controlGroup);
  layout->addStretch();
}


void PneumaticControlPage::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);

  std::cout << "[PneumaticControlPage] Page shown - starting updates" << std::endl;

  refreshSlideList();

  // ✅ Connect to position changes for instant updates
  PneumaticManager* pneumaticMgr = getPneumaticManager();
  if (pneumaticMgr) {
    // Note: This requires adding a Qt signal to PneumaticManager
    // For now, just use the faster timer approach
  }

  m_updateTimer->start(50);  // ✅ 50ms for faster updates
}



void PneumaticControlPage::hideEvent(QHideEvent* event)
{
  QWidget::hideEvent(event);

  std::cout << "[PneumaticControlPage] Page hidden - stopping updates" << std::endl;

  m_updateTimer->stop();
}

void PneumaticControlPage::refreshSlideList()
{
  PneumaticManager* pneumaticMgr = getPneumaticManager();
  if (!pneumaticMgr) {
    std::cout << "[PneumaticControlPage] ERROR: No pneumatic manager" << std::endl;
    return;
  }

  std::vector<std::string> slideNames = pneumaticMgr->GetAllSlideNames();

  std::cout << "[PneumaticControlPage] Found " << slideNames.size() << " pneumatic slides" << std::endl;

  m_slideList->clear();

  for (const auto& name : slideNames) {
    QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(name));
    m_slideList->addItem(item);
  }

  // Auto-select first slide
  if (!slideNames.empty()) {
    m_slideList->setCurrentRow(0);
  }
}

void PneumaticControlPage::onSlideSelected()
{
  if (m_slideList->selectedItems().isEmpty()) {
    m_selectedSlide = "";
    m_slideNameLabel->setText("No slide selected");
    m_connectionStatusLabel->setText("⚪ Not Connected");
    m_extendButton->setEnabled(false);
    m_retractButton->setEnabled(false);
    m_stopButton->setEnabled(false);
    return;
  }

  m_selectedSlide = m_slideList->currentItem()->text().toStdString();

  std::cout << "[PneumaticControlPage] Slide selected: " << m_selectedSlide << std::endl;

  // Update slide info
  m_slideNameLabel->setText(QString::fromStdString(m_selectedSlide));

  // Check if pneumatic manager is available
  PneumaticManager* pneumaticMgr = getPneumaticManager();
  if (pneumaticMgr && pneumaticMgr->HasSlide(m_selectedSlide)) {
    m_connectionStatusLabel->setText("🟢 Ready");
    m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 14px; color: #27ae60; }");

    m_extendButton->setEnabled(true);
    m_retractButton->setEnabled(true);
    m_stopButton->setEnabled(true);

    // Update timeout display
    IPneumaticSlide* slide = pneumaticMgr->GetSlide(m_selectedSlide);
    if (slide) {
      m_timeoutLabel->setText(QString::number(slide->GetTimeout()) + " ms");
    }
  }
  else {
    m_connectionStatusLabel->setText("🔴 Not Available");
    m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 14px; color: #e74c3c; }");

    m_extendButton->setEnabled(false);
    m_retractButton->setEnabled(false);
    m_stopButton->setEnabled(false);
  }

  // Immediate status update
  updateSlideStatus();
}

void PneumaticControlPage::updateSlideStatus()
{
  if (m_selectedSlide.empty()) {
    return;
  }

  PneumaticManager* pneumaticMgr = getPneumaticManager();
  if (!pneumaticMgr) return;

  PneumaticStatus status = pneumaticMgr->GetStatus(m_selectedSlide);
  if (!status.Valid) {
    return;
  }

  // Update position
  std::string posStr = GetPositionString(status.Position);
  m_positionValueLabel->setText(QString::fromStdString(posStr));

  // Update position color and bar
  updatePositionIndicator(posStr);

  // Update elapsed time
  if (status.Position == PneumaticPosition::EXTENDING ||
    status.Position == PneumaticPosition::RETRACTING) {
    // Currently moving - show live elapsed time in BLUE
    if (status.LastMoveTimeMs > 0) {
      m_elapsedTimeValueLabel->setText(QString::number(status.LastMoveTimeMs) + " ms");
      m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 14px; color: #3498db; font-weight: bold; }");
    }
    else {
      m_elapsedTimeValueLabel->setText("0 ms");
      m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 14px; color: #3498db; font-weight: bold; }");
    }
  }
  else if (status.Position == PneumaticPosition::EXTENDED ||
    status.Position == PneumaticPosition::RETRACTED) {
    // Finished moving - show final time in GREEN
    if (status.LastMoveTimeMs > 0) {
      m_elapsedTimeValueLabel->setText(QString::number(status.LastMoveTimeMs) + " ms ✓");
      m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 14px; color: #27ae60; font-weight: bold; }");
    }
    else {
      m_elapsedTimeValueLabel->setText("-- ms");
      m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 14px; color: #95a5a6; }");
    }
  }
  else if (status.Position == PneumaticPosition::TIMEOUT) {
    // Timeout - show in RED
    m_elapsedTimeValueLabel->setText(QString::number(status.LastMoveTimeMs) + " ms ⚠");
    m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 14px; color: #e74c3c; font-weight: bold; }");
  }
  else {
    // Unknown/Error - show placeholder
    m_elapsedTimeValueLabel->setText("-- ms");
    m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 14px; color: #95a5a6; }");
  }

  // Update output status
  if (status.OutputActive) {
    m_outputStatusLabel->setText("🟢 ON");
    m_outputStatusLabel->setStyleSheet("QLabel { color: #27ae60; font-weight: bold; }");
  }
  else {
    m_outputStatusLabel->setText("⚪ OFF");
    m_outputStatusLabel->setStyleSheet("QLabel { color: #95a5a6; }");
  }

  // Update extended sensor
  if (status.ExtendedSensorActive) {
    m_extendedSensorLabel->setText("🟢 ON");
    m_extendedSensorLabel->setStyleSheet("QLabel { color: #27ae60; font-weight: bold; }");
  }
  else {
    m_extendedSensorLabel->setText("⚪ OFF");
    m_extendedSensorLabel->setStyleSheet("QLabel { color: #95a5a6; }");
  }

  // Update retracted sensor
  if (status.RetractedSensorActive) {
    m_retractedSensorLabel->setText("🟢 ON");
    m_retractedSensorLabel->setStyleSheet("QLabel { color: #27ae60; font-weight: bold; }");
  }
  else {
    m_retractedSensorLabel->setText("⚪ OFF");
    m_retractedSensorLabel->setStyleSheet("QLabel { color: #95a5a6; }");
  }
}


void PneumaticControlPage::updatePositionIndicator(const std::string& position)
{
  if (position == "Extended") {
    m_positionValueLabel->setStyleSheet("QLabel { font-size: 16px; color: #27ae60; font-weight: bold; }");
    m_positionBar->setValue(100);
    m_positionBar->setStyleSheet(
      "QProgressBar { border: 2px solid #bdc3c7; border-radius: 5px; background-color: #ecf0f1; height: 25px; } "
      "QProgressBar::chunk { background-color: #27ae60; border-radius: 3px; }");
  }
  else if (position == "Retracted") {
    m_positionValueLabel->setStyleSheet("QLabel { font-size: 16px; color: #e67e22; font-weight: bold; }");
    m_positionBar->setValue(0);
    m_positionBar->setStyleSheet(
      "QProgressBar { border: 2px solid #bdc3c7; border-radius: 5px; background-color: #ecf0f1; height: 25px; } "
      "QProgressBar::chunk { background-color: #e67e22; border-radius: 3px; }");
  }
  else if (position == "Extending") {
    m_positionValueLabel->setStyleSheet("QLabel { font-size: 16px; color: #3498db; font-weight: bold; }");
    m_positionBar->setValue(50);
    m_positionBar->setStyleSheet(
      "QProgressBar { border: 2px solid #bdc3c7; border-radius: 5px; background-color: #ecf0f1; height: 25px; } "
      "QProgressBar::chunk { background-color: #3498db; border-radius: 3px; }");
  }
  else if (position == "Retracting") {
    m_positionValueLabel->setStyleSheet("QLabel { font-size: 16px; color: #9b59b6; font-weight: bold; }");
    m_positionBar->setValue(50);
    m_positionBar->setStyleSheet(
      "QProgressBar { border: 2px solid #bdc3c7; border-radius: 5px; background-color: #ecf0f1; height: 25px; } "
      "QProgressBar::chunk { background-color: #9b59b6; border-radius: 3px; }");
  }
  else if (position == "Hardware Error" || position == "Timeout") {
    m_positionValueLabel->setStyleSheet("QLabel { font-size: 16px; color: #e74c3c; font-weight: bold; }");
    m_positionBar->setValue(0);
    m_positionBar->setStyleSheet(
      "QProgressBar { border: 2px solid #bdc3c7; border-radius: 5px; background-color: #ecf0f1; height: 25px; } "
      "QProgressBar::chunk { background-color: #e74c3c; border-radius: 3px; }");
  }
  else {
    m_positionValueLabel->setStyleSheet("QLabel { font-size: 16px; color: #7f8c8d; }");
    m_positionBar->setValue(0);
    m_positionBar->setStyleSheet(
      "QProgressBar { border: 2px solid #bdc3c7; border-radius: 5px; background-color: #ecf0f1; height: 25px; } "
      "QProgressBar::chunk { background-color: #95a5a6; border-radius: 3px; }");
  }
}

void PneumaticControlPage::updateStatus()
{
  updateSlideStatus();
}

void PneumaticControlPage::onRefreshClicked()
{
  std::cout << "[PneumaticControlPage] Manual refresh requested" << std::endl;
  refreshSlideList();
}

void PneumaticControlPage::onExtendClicked()
{
  if (m_selectedSlide.empty()) return;

  PneumaticManager* pneumaticMgr = getPneumaticManager();
  if (!pneumaticMgr) return;

  std::cout << "[PneumaticControlPage] Extending " << m_selectedSlide << std::endl;

  PneumaticError result = pneumaticMgr->Extend(m_selectedSlide);
  if (result != PneumaticError::SUCCESS && result != PneumaticError::ALREADY_AT_POSITION) {
    std::cerr << "[PneumaticControlPage] Failed to extend: "
      << GetPneumaticErrorString(result) << std::endl;
  }
}

void PneumaticControlPage::onRetractClicked()
{
  if (m_selectedSlide.empty()) return;

  PneumaticManager* pneumaticMgr = getPneumaticManager();
  if (!pneumaticMgr) return;

  std::cout << "[PneumaticControlPage] Retracting " << m_selectedSlide << std::endl;

  PneumaticError result = pneumaticMgr->Retract(m_selectedSlide);
  if (result != PneumaticError::SUCCESS && result != PneumaticError::ALREADY_AT_POSITION) {
    std::cerr << "[PneumaticControlPage] Failed to retract: "
      << GetPneumaticErrorString(result) << std::endl;
  }
}

void PneumaticControlPage::onStopClicked()
{
  if (m_selectedSlide.empty()) return;

  PneumaticManager* pneumaticMgr = getPneumaticManager();
  if (!pneumaticMgr) return;

  std::cout << "[PneumaticControlPage] Stopping " << m_selectedSlide << std::endl;

  PneumaticError result = pneumaticMgr->Stop(m_selectedSlide);
  if (result != PneumaticError::SUCCESS) {
    std::cerr << "[PneumaticControlPage] Failed to stop: "
      << GetPneumaticErrorString(result) << std::endl;
  }
}

PneumaticManager* PneumaticControlPage::getPneumaticManager()
{
  if (!m_context || !m_context->pneumaticManager) {
    return nullptr;
  }
  return m_context->pneumaticManager;
}

void PneumaticControlPage::onPositionChanged(const QString& slideName)
{
  // Only update if it's the currently selected slide
  if (slideName.toStdString() == m_selectedSlide) {
    std::cout << "[PneumaticControlPage] Position changed for "
      << m_selectedSlide << " - immediate update!" << std::endl;
    updateSlideStatus();
  }
}