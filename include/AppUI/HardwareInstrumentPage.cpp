// HardwareInstrumentPage.cpp
#include "HardwareInstrumentPage.h"
#include "ApplicationContext.h"
#include "MultiCameraView.h"
#include "TcpClientWidget.h"
#include "DataStoreWidget.h"
#include "PIControllerPage.h"
#include "ACSControllerPage.h"
#include "IOControllerPage.h"  // ADD
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <iostream>

HardwareInstrumentPage::HardwareInstrumentPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_sidebar(nullptr)
  , m_contentStack(nullptr)
  , m_cameraView(nullptr)
  , m_tcpClientWidget(nullptr)
  , m_dataStoreWidget(nullptr)
  , m_piControllerPage(nullptr)
  , m_acsControllerPage(nullptr)
  , m_ioControllerPage(nullptr)  // ADD
  , m_currentButton(nullptr)
{
  setupUI();
}

void HardwareInstrumentPage::setupUI()
{
  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  createSidebar();
  mainLayout->addWidget(m_sidebar);

  createContentArea();
  mainLayout->addWidget(m_contentStack, 9);

  showCamera();
}

void HardwareInstrumentPage::createSidebar()
{
  m_sidebar = new QWidget();
  m_sidebar->setStyleSheet("QWidget { background-color: #333333; }");
  m_sidebar->setMinimumWidth(100);
  m_sidebar->setMaximumWidth(250);

  QVBoxLayout* sidebarLayout = new QVBoxLayout(m_sidebar);
  sidebarLayout->setContentsMargins(5, 10, 5, 10);
  sidebarLayout->setSpacing(10);

  m_backButton = new QPushButton("← Back");
  m_backButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #555555; "
    "  color: white; "
    "  border: none; "
    "  padding: 10px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #666666; }");
  connect(m_backButton, &QPushButton::clicked, this, &HardwareInstrumentPage::backToMainMenu);
  sidebarLayout->addWidget(m_backButton);

  QLabel* separator = new QLabel();
  separator->setFixedHeight(2);
  separator->setStyleSheet("QLabel { background-color: #555555; }");
  sidebarLayout->addWidget(separator);

  m_cameraButton = createSidebarButton("Camera");
  connect(m_cameraButton, &QPushButton::clicked, this, &HardwareInstrumentPage::showCamera);
  sidebarLayout->addWidget(m_cameraButton);

  m_piButton = createSidebarButton("PI Hexapod");
  connect(m_piButton, &QPushButton::clicked, this, &HardwareInstrumentPage::showPIHexapod);
  sidebarLayout->addWidget(m_piButton);

  m_acsButton = createSidebarButton("ACS");
  connect(m_acsButton, &QPushButton::clicked, this, &HardwareInstrumentPage::showACS);
  sidebarLayout->addWidget(m_acsButton);

  // ADD IO BUTTON
  m_ioButton = createSidebarButton("IO");
  connect(m_ioButton, &QPushButton::clicked, this, &HardwareInstrumentPage::showIO);
  sidebarLayout->addWidget(m_ioButton);

  m_dataViewButton = createSidebarButton("Data View");
  connect(m_dataViewButton, &QPushButton::clicked, this, &HardwareInstrumentPage::showDataView);
  sidebarLayout->addWidget(m_dataViewButton);

  m_tcpClientButton = createSidebarButton("TCP Client");
  connect(m_tcpClientButton, &QPushButton::clicked, this, &HardwareInstrumentPage::showTCPClient);
  sidebarLayout->addWidget(m_tcpClientButton);

  m_laserTECButton = createSidebarButton("Laser/TEC");
  connect(m_laserTECButton, &QPushButton::clicked, this, &HardwareInstrumentPage::showLaserTEC);
  sidebarLayout->addWidget(m_laserTECButton);

  m_powerSupplyButton = createSidebarButton("Power Supply");
  connect(m_powerSupplyButton, &QPushButton::clicked, this, &HardwareInstrumentPage::showPowerSupply);
  sidebarLayout->addWidget(m_powerSupplyButton);

  m_smuButton = createSidebarButton("SMU");
  connect(m_smuButton, &QPushButton::clicked, this, &HardwareInstrumentPage::showSMU);
  sidebarLayout->addWidget(m_smuButton);

  sidebarLayout->addStretch();
}

void HardwareInstrumentPage::createContentArea()
{
  m_contentStack = new QStackedWidget();

  // Index 0: Camera view
  m_cameraView = new MultiCameraView(m_context);
  m_contentStack->addWidget(m_cameraView);

  // Index 1: PI Hexapod
  m_piControllerPage = new PIControllerPage(m_context);
  m_contentStack->addWidget(m_piControllerPage);

  // Index 2: ACS
  m_acsControllerPage = new ACSControllerPage(m_context);
  m_contentStack->addWidget(m_acsControllerPage);

  // Index 3: IO - NEW PAGE
  m_ioControllerPage = new IOControllerPage(m_context);
  m_contentStack->addWidget(m_ioControllerPage);

  // Index 4: Data View
  m_dataStoreWidget = new DataStoreWidget(m_context);
  m_contentStack->addWidget(m_dataStoreWidget);

  // Index 5: TCP Client
  m_tcpClientWidget = new TcpClientWidget(m_context);
  m_contentStack->addWidget(m_tcpClientWidget);

  // Index 6: Laser/TEC placeholder
  QLabel* laserTECPlaceholder = new QLabel("Laser/TEC\n(Coming Soon)");
  laserTECPlaceholder->setAlignment(Qt::AlignCenter);
  laserTECPlaceholder->setStyleSheet("QLabel { font-size: 24px; color: #888888; background-color: #2b2b2b; }");
  m_contentStack->addWidget(laserTECPlaceholder);

  // Index 7: Power Supply placeholder
  QLabel* powerSupplyPlaceholder = new QLabel("Power Supply\n(Coming Soon)");
  powerSupplyPlaceholder->setAlignment(Qt::AlignCenter);
  powerSupplyPlaceholder->setStyleSheet("QLabel { font-size: 24px; color: #888888; background-color: #2b2b2b; }");
  m_contentStack->addWidget(powerSupplyPlaceholder);

  // Index 8: SMU placeholder
  QLabel* smuPlaceholder = new QLabel("SMU\n(Coming Soon)");
  smuPlaceholder->setAlignment(Qt::AlignCenter);
  smuPlaceholder->setStyleSheet("QLabel { font-size: 24px; color: #888888; background-color: #2b2b2b; }");
  m_contentStack->addWidget(smuPlaceholder);
}

QPushButton* HardwareInstrumentPage::createSidebarButton(const QString& text)
{
  QPushButton* button = new QPushButton(text);
  button->setCheckable(true);
  button->setStyleSheet(
    "QPushButton { "
    "  background-color: #3a3a3a; "
    "  color: white; "
    "  border: none; "
    "  border-left: 3px solid transparent; "
    "  padding: 15px 10px; "
    "  font-size: 14px; "
    "  text-align: left; "
    "} "
    "QPushButton:hover { background-color: #4a4a4a; } "
    "QPushButton:checked { "
    "  background-color: #2b2b2b; "
    "  border-left-color: #00aaff; "
    "  font-weight: bold; "
    "}");

  return button;
}

void HardwareInstrumentPage::showCamera()
{
  m_contentStack->setCurrentIndex(0);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_cameraButton->setChecked(true);
  m_currentButton = m_cameraButton;
  std::cout << "[HardwareInstrumentPage] Showing camera view" << std::endl;
}

void HardwareInstrumentPage::showPIHexapod()
{
  m_contentStack->setCurrentIndex(1);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_piButton->setChecked(true);
  m_currentButton = m_piButton;
  std::cout << "[HardwareInstrumentPage] Showing PI Hexapod page" << std::endl;
}

void HardwareInstrumentPage::showACS()
{
  m_contentStack->setCurrentIndex(2);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_acsButton->setChecked(true);
  m_currentButton = m_acsButton;
  std::cout << "[HardwareInstrumentPage] Showing ACS page" << std::endl;
}

// ADD NEW SLOT
void HardwareInstrumentPage::showIO()
{
  m_contentStack->setCurrentIndex(3);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_ioButton->setChecked(true);
  m_currentButton = m_ioButton;
  std::cout << "[HardwareInstrumentPage] Showing IO page" << std::endl;
}

void HardwareInstrumentPage::showDataView()
{
  m_contentStack->setCurrentIndex(4);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_dataViewButton->setChecked(true);
  m_currentButton = m_dataViewButton;
  std::cout << "[HardwareInstrumentPage] Showing Data View" << std::endl;
}

void HardwareInstrumentPage::showTCPClient()
{
  m_contentStack->setCurrentIndex(5);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_tcpClientButton->setChecked(true);
  m_currentButton = m_tcpClientButton;
  std::cout << "[HardwareInstrumentPage] Showing TCP Client" << std::endl;
}

void HardwareInstrumentPage::showLaserTEC()
{
  m_contentStack->setCurrentIndex(6);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_laserTECButton->setChecked(true);
  m_currentButton = m_laserTECButton;
  std::cout << "[HardwareInstrumentPage] Showing Laser/TEC (placeholder)" << std::endl;
}

void HardwareInstrumentPage::showPowerSupply()
{
  m_contentStack->setCurrentIndex(7);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_powerSupplyButton->setChecked(true);
  m_currentButton = m_powerSupplyButton;
  std::cout << "[HardwareInstrumentPage] Showing Power Supply (placeholder)" << std::endl;
}

void HardwareInstrumentPage::showSMU()
{
  m_contentStack->setCurrentIndex(8);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_smuButton->setChecked(true);
  m_currentButton = m_smuButton;
  std::cout << "[HardwareInstrumentPage] Showing SMU (placeholder)" << std::endl;
}