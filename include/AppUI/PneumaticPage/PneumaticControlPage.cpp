// PneumaticControlPage.cpp
#include "PneumaticControlPage.h"
#include "ApplicationContext.h"
#include "PneumaticManager.h"
#include "IPneumaticSlide.h"
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

  setStyleSheet("QWidget { background-color: #2d2d30; }");
}

void PneumaticControlPage::createSlideListPanel()
{
  m_slideListPanel = new QWidget();
  m_slideListPanel->setStyleSheet("QWidget { background-color: #1e1e1e; }");

  QVBoxLayout* layout = new QVBoxLayout(m_slideListPanel);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(10);

  // Title
  QLabel* titleLabel = new QLabel("Pneumatic Slides");
  titleLabel->setStyleSheet("QLabel { font-size: 13px; color: #aaa; }");
  layout->addWidget(titleLabel);

  // Refresh button - flat minimalistic style
  m_refreshButton = new QPushButton("Refresh");
  m_refreshButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 3px; "
    "  padding: 8px 16px; "
    "  font-size: 12px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #3e3e42; "
    "  border-color: #0e639c; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #0e639c; "
    "}");
  connect(m_refreshButton, &QPushButton::clicked, this, &PneumaticControlPage::onRefreshClicked);
  layout->addWidget(m_refreshButton);

  // Slide list - clean minimalistic style
  m_slideList = new QListWidget();
  m_slideList->setStyleSheet(
    "QListWidget { "
    "  background-color: #1a1a1a; "
    "  border: 1px solid #333; "
    "  border-radius: 2px; "
    "  font-size: 12px; "
    "  color: #d4d4d4; "
    "} "
    "QListWidget::item { "
    "  padding: 8px 10px; "
    "  border-bottom: 1px solid #252525; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #0e639c; "
    "  color: white; "
    "} "
    "QListWidget::item:hover:!selected { "
    "  background-color: #252525; "
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
  m_slideNameLabel->setStyleSheet("QLabel { font-size: 14px; color: #d4d4d4; }");
  headerLayout->addWidget(m_slideNameLabel);

  headerLayout->addStretch();

  m_connectionStatusLabel = new QLabel("Not Connected");
  m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");
  headerLayout->addWidget(m_connectionStatusLabel);

  layout->addLayout(headerLayout);

  // ============================================================================
  // STATUS GROUP
  // ============================================================================
  QGroupBox* statusGroup = new QGroupBox("Status");
  statusGroup->setStyleSheet(
    "QGroupBox { "
    "  font-size: 12px; "
    "  color: #aaa; "
    "  background-color: #1a1a1a; "
    "  border: 1px solid #333; "
    "  border-radius: 3px; "
    "  margin-top: 12px; "
    "  padding-top: 8px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 8px; "
    "  padding: 0 4px; "
    "  color: #888; "
    "}");

  QGridLayout* statusLayout = new QGridLayout(statusGroup);
  statusLayout->setSpacing(8);
  statusLayout->setContentsMargins(12, 16, 12, 12);

  // Position
  m_positionLabel = new QLabel("Position:");
  m_positionLabel->setStyleSheet("QLabel { color: #888; font-size: 12px; }");
  m_positionValueLabel = new QLabel("Unknown");
  m_positionValueLabel->setStyleSheet("QLabel { font-size: 13px; color: #666; }");

  statusLayout->addWidget(m_positionLabel, 0, 0);
  statusLayout->addWidget(m_positionValueLabel, 0, 1);

  // Position bar - Clean flat indicator
  m_positionBar = new QProgressBar();
  m_positionBar->setRange(0, 100);
  m_positionBar->setValue(0);
  m_positionBar->setFormat("");
  m_positionBar->setTextVisible(false);
  m_positionBar->setStyleSheet(
    "QProgressBar { "
    "  border: 1px solid #333; "
    "  border-radius: 2px; "
    "  background-color: #252525; "
    "  height: 16px; "
    "} "
    "QProgressBar::chunk { "
    "  background-color: #4CAF50; "
    "  border-radius: 1px; "
    "}");
  statusLayout->addWidget(m_positionBar, 1, 0, 1, 2);

  // Elapsed Time
  m_elapsedTimeLabel = new QLabel("Elapsed:");
  m_elapsedTimeLabel->setStyleSheet("QLabel { color: #888; font-size: 12px; }");
  m_elapsedTimeValueLabel = new QLabel("--");
  m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");

  statusLayout->addWidget(m_elapsedTimeLabel, 2, 0);
  statusLayout->addWidget(m_elapsedTimeValueLabel, 2, 1);

  // Output status
  QLabel* outputLabel = new QLabel("Output:");
  outputLabel->setStyleSheet("QLabel { color: #888; font-size: 12px; }");
  m_outputStatusLabel = new QLabel("OFF");
  m_outputStatusLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");

  statusLayout->addWidget(outputLabel, 3, 0);
  statusLayout->addWidget(m_outputStatusLabel, 3, 1);

  // Extended sensor
  QLabel* extendedLabel = new QLabel("Extended:");
  extendedLabel->setStyleSheet("QLabel { color: #888; font-size: 12px; }");
  m_extendedSensorLabel = new QLabel("OFF");
  m_extendedSensorLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");

  statusLayout->addWidget(extendedLabel, 4, 0);
  statusLayout->addWidget(m_extendedSensorLabel, 4, 1);

  // Retracted sensor
  QLabel* retractedLabel = new QLabel("Retracted:");
  retractedLabel->setStyleSheet("QLabel { color: #888; font-size: 12px; }");
  m_retractedSensorLabel = new QLabel("OFF");
  m_retractedSensorLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");

  statusLayout->addWidget(retractedLabel, 5, 0);
  statusLayout->addWidget(m_retractedSensorLabel, 5, 1);

  // Timeout
  QLabel* timeoutLabelText = new QLabel("Timeout:");
  timeoutLabelText->setStyleSheet("QLabel { color: #888; font-size: 12px; }");
  m_timeoutLabel = new QLabel("5000 ms");
  m_timeoutLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");

  statusLayout->addWidget(timeoutLabelText, 6, 0);
  statusLayout->addWidget(m_timeoutLabel, 6, 1);

  layout->addWidget(statusGroup);

  // ============================================================================
  // CONTROL GROUP
  // ============================================================================
  QGroupBox* controlGroup = new QGroupBox("Control");
  controlGroup->setStyleSheet(
    "QGroupBox { "
    "  font-size: 12px; "
    "  color: #aaa; "
    "  background-color: #1a1a1a; "
    "  border: 1px solid #333; "
    "  border-radius: 3px; "
    "  margin-top: 12px; "
    "  padding-top: 8px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 8px; "
    "  padding: 0 4px; "
    "  color: #888; "
    "}");

  QHBoxLayout* controlLayout = new QHBoxLayout(controlGroup);
  controlLayout->setSpacing(10);

  // Extend button - flat minimalistic
  m_extendButton = new QPushButton("Extend");
  m_extendButton->setEnabled(false);
  m_extendButton->setMinimumHeight(36);
  m_extendButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #2d5a3d; "
    "  color: #d4d4d4; "
    "  border: 1px solid #3d7a4d; "
    "  border-radius: 3px; "
    "  font-size: 12px; "
    "  padding: 8px 20px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #3d7a4d; "
    "  border-color: #4CAF50; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #4CAF50; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #2a2a2a; "
    "  color: #555; "
    "  border-color: #333; "
    "}");
  connect(m_extendButton, &QPushButton::clicked, this, &PneumaticControlPage::onExtendClicked);
  controlLayout->addWidget(m_extendButton);

  // Retract button - flat minimalistic
  m_retractButton = new QPushButton("Retract");
  m_retractButton->setEnabled(false);
  m_retractButton->setMinimumHeight(36);
  m_retractButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #5a4a2d; "
    "  color: #d4d4d4; "
    "  border: 1px solid #7a6a3d; "
    "  border-radius: 3px; "
    "  font-size: 12px; "
    "  padding: 8px 20px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #7a6a3d; "
    "  border-color: #e67e22; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #e67e22; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #2a2a2a; "
    "  color: #555; "
    "  border-color: #333; "
    "}");
  connect(m_retractButton, &QPushButton::clicked, this, &PneumaticControlPage::onRetractClicked);
  controlLayout->addWidget(m_retractButton);

  // Stop button - flat minimalistic
  m_stopButton = new QPushButton("Stop");
  m_stopButton->setEnabled(false);
  m_stopButton->setMinimumHeight(36);
  m_stopButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #5a2d2d; "
    "  color: #d4d4d4; "
    "  border: 1px solid #7a3d3d; "
    "  border-radius: 3px; "
    "  font-size: 12px; "
    "  padding: 8px 20px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #7a3d3d; "
    "  border-color: #e74c3c; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #e74c3c; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #2a2a2a; "
    "  color: #555; "
    "  border-color: #333; "
    "}");
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
    m_connectionStatusLabel->setText("Not Connected");
    m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");
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
    IPneumaticSlide* slide = pneumaticMgr->GetSlide(m_selectedSlide);
    if (slide) {
      // Check if IO device is connected
      if (!slide->IsIOConnected()) {
        m_connectionStatusLabel->setText("IO Disconnected");
        m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 12px; color: #e74c3c; }");
        m_extendButton->setEnabled(false);
        m_retractButton->setEnabled(false);
        m_stopButton->setEnabled(false);
        m_timeoutLabel->setText(QString::number(slide->GetTimeout()) + " ms");

        // Reset status displays to show disconnected state
        m_positionValueLabel->setText("Unknown");
        m_positionValueLabel->setStyleSheet("QLabel { font-size: 13px; color: #666; }");
        m_positionBar->setValue(0);
        m_elapsedTimeValueLabel->setText("--");
        m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");
        m_outputStatusLabel->setText("--");
        m_outputStatusLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");
        m_extendedSensorLabel->setText("--");
        m_extendedSensorLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");
        m_retractedSensorLabel->setText("--");
        m_retractedSensorLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");
        return;
      }

      m_connectionStatusLabel->setText("Ready");
      m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 12px; color: #4CAF50; }");
      m_extendButton->setEnabled(true);
      m_retractButton->setEnabled(true);
      m_stopButton->setEnabled(true);
      m_timeoutLabel->setText(QString::number(slide->GetTimeout()) + " ms");
    }
  }
  else {
    m_connectionStatusLabel->setText("Not Available");
    m_connectionStatusLabel->setStyleSheet("QLabel { font-size: 12px; color: #e74c3c; }");

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

  // Check if IO is connected before polling
  IPneumaticSlide* slide = pneumaticMgr->GetSlide(m_selectedSlide);
  if (!slide || !slide->IsIOConnected()) {
    // IO not connected - don't poll (would generate errors)
    return;
  }

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
    // Currently moving - show live elapsed time in blue
    if (status.LastMoveTimeMs > 0) {
      m_elapsedTimeValueLabel->setText(QString::number(status.LastMoveTimeMs) + " ms");
      m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 12px; color: #3498db; }");
    }
    else {
      m_elapsedTimeValueLabel->setText("0 ms");
      m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 12px; color: #3498db; }");
    }
  }
  else if (status.Position == PneumaticPosition::EXTENDED ||
    status.Position == PneumaticPosition::RETRACTED) {
    // Finished moving - show final time in green
    if (status.LastMoveTimeMs > 0) {
      m_elapsedTimeValueLabel->setText(QString::number(status.LastMoveTimeMs) + " ms");
      m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 12px; color: #4CAF50; }");
    }
    else {
      m_elapsedTimeValueLabel->setText("--");
      m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");
    }
  }
  else if (status.Position == PneumaticPosition::TIMEOUT) {
    // Timeout - show in red
    m_elapsedTimeValueLabel->setText(QString::number(status.LastMoveTimeMs) + " ms TIMEOUT");
    m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 12px; color: #e74c3c; }");
  }
  else {
    // Unknown/Error - show placeholder
    m_elapsedTimeValueLabel->setText("--");
    m_elapsedTimeValueLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");
  }

  // Update output status
  if (status.OutputActive) {
    m_outputStatusLabel->setText("ON");
    m_outputStatusLabel->setStyleSheet("QLabel { font-size: 12px; color: #4CAF50; }");
  }
  else {
    m_outputStatusLabel->setText("OFF");
    m_outputStatusLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");
  }

  // Update extended sensor
  if (status.ExtendedSensorActive) {
    m_extendedSensorLabel->setText("ON");
    m_extendedSensorLabel->setStyleSheet("QLabel { font-size: 12px; color: #4CAF50; }");
  }
  else {
    m_extendedSensorLabel->setText("OFF");
    m_extendedSensorLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");
  }

  // Update retracted sensor
  if (status.RetractedSensorActive) {
    m_retractedSensorLabel->setText("ON");
    m_retractedSensorLabel->setStyleSheet("QLabel { font-size: 12px; color: #4CAF50; }");
  }
  else {
    m_retractedSensorLabel->setText("OFF");
    m_retractedSensorLabel->setStyleSheet("QLabel { font-size: 12px; color: #666; }");
  }
}


void PneumaticControlPage::updatePositionIndicator(const std::string& position)
{
  // Clean flat progress bar base style
  QString baseStyle = "QProgressBar { border: 1px solid #333; border-radius: 2px; background-color: #252525; height: 16px; } ";

  if (position == "Extended") {
    m_positionValueLabel->setStyleSheet("QLabel { font-size: 13px; color: #4CAF50; }");
    m_positionBar->setValue(100);
    m_positionBar->setStyleSheet(baseStyle + "QProgressBar::chunk { background-color: #4CAF50; border-radius: 1px; }");
  }
  else if (position == "Retracted") {
    m_positionValueLabel->setStyleSheet("QLabel { font-size: 13px; color: #e67e22; }");
    m_positionBar->setValue(0);
    m_positionBar->setStyleSheet(baseStyle + "QProgressBar::chunk { background-color: #e67e22; border-radius: 1px; }");
  }
  else if (position == "Extending") {
    m_positionValueLabel->setStyleSheet("QLabel { font-size: 13px; color: #3498db; }");
    m_positionBar->setValue(50);
    m_positionBar->setStyleSheet(baseStyle + "QProgressBar::chunk { background-color: #3498db; border-radius: 1px; }");
  }
  else if (position == "Retracting") {
    m_positionValueLabel->setStyleSheet("QLabel { font-size: 13px; color: #9b59b6; }");
    m_positionBar->setValue(50);
    m_positionBar->setStyleSheet(baseStyle + "QProgressBar::chunk { background-color: #9b59b6; border-radius: 1px; }");
  }
  else if (position == "Hardware Error" || position == "Timeout") {
    m_positionValueLabel->setStyleSheet("QLabel { font-size: 13px; color: #e74c3c; }");
    m_positionBar->setValue(0);
    m_positionBar->setStyleSheet(baseStyle + "QProgressBar::chunk { background-color: #e74c3c; border-radius: 1px; }");
  }
  else {
    m_positionValueLabel->setStyleSheet("QLabel { font-size: 13px; color: #666; }");
    m_positionBar->setValue(0);
    m_positionBar->setStyleSheet(baseStyle + "QProgressBar::chunk { background-color: #555; border-radius: 1px; }");
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