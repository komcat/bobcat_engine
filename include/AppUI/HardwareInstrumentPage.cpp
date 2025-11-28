// HardwareInstrumentPage.cpp - UPDATED with CameraLightingPage
#include "HardwareInstrumentPage.h"
#include "ApplicationContext.h"
#include "CameraLightingPage.h"
#include "TcpClientWidget.h"
#include "DataStoreWidget.h"
#include "PIControllerPage.h"
#include "ACSControllerPage.h"
#include "IOControllerPage.h"
#include "PneumaticControlPage.h"
#include "TECControlPage.h"
#include "PowerSupplyControlPage.h"  // ✅ ADD THIS
#include "SMUControlPage.h"  // ✅ ADD SMU PAGE
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <iostream>

HardwareInstrumentPage::HardwareInstrumentPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_sidebar(nullptr)
  , m_contentStack(nullptr)
  , m_cameraLightingPage(nullptr)
  , m_tcpClientWidget(nullptr)
  , m_tecControlPage(nullptr)
  , m_dataStoreWidget(nullptr)
  , m_piControllerPage(nullptr)
  , m_acsControllerPage(nullptr)
  , m_ioControllerPage(nullptr)
  , m_pneumaticControlPage(nullptr)
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
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #656565, stop:1 #555555); "
    "  color: white; "
    "  padding: 10px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #757575, stop:1 #656565); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #454545; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 12px; "
    "  padding-left: 12px; "
    "}");
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

  m_ioButton = createSidebarButton("IO");
  connect(m_ioButton, &QPushButton::clicked, this, &HardwareInstrumentPage::showIO);
  sidebarLayout->addWidget(m_ioButton);

  m_pneumaticButton = createSidebarButton("Pneumatic");
  connect(m_pneumaticButton, &QPushButton::clicked, this, &HardwareInstrumentPage::showPneumatic);
  sidebarLayout->addWidget(m_pneumaticButton);

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

  // Index 0: Camera + Lighting (tabbed page)
  m_cameraLightingPage = new CameraLightingPage(m_context);
  m_contentStack->addWidget(m_cameraLightingPage);

  // Index 1: PI Hexapod
  m_piControllerPage = new PIControllerPage(m_context);
  m_contentStack->addWidget(m_piControllerPage);

  // Index 2: ACS
  m_acsControllerPage = new ACSControllerPage(m_context);
  m_contentStack->addWidget(m_acsControllerPage);

  // Index 3: IO
  m_ioControllerPage = new IOControllerPage(m_context);
  m_contentStack->addWidget(m_ioControllerPage);

  // Index 4: Pneumatic
  m_pneumaticControlPage = new PneumaticControlPage(m_context);
  m_contentStack->addWidget(m_pneumaticControlPage);

  // Index 5: Data View
  m_dataStoreWidget = new DataStoreWidget(m_context);
  m_contentStack->addWidget(m_dataStoreWidget);

  // Index 6: TCP Client
  m_tcpClientWidget = new TcpClientWidget(m_context);
  m_contentStack->addWidget(m_tcpClientWidget);

  // Index 7: Laser/TEC
  m_tecControlPage = new TECControlPage(m_context);
  m_contentStack->addWidget(m_tecControlPage);

  // Index 8: Power Supply - ✅ UPDATED TO USE ACTUAL PAGE
  PowerSupplyControlPage* powerSupplyPage = new PowerSupplyControlPage(m_context);
  m_contentStack->addWidget(powerSupplyPage);

  // Index 9: SMU - ✅ UPDATED TO USE ACTUAL PAGE
  m_smuControlPage = new SMUControlPage(m_context);
  m_contentStack->addWidget(m_smuControlPage);
}

QPushButton* HardwareInstrumentPage::createSidebarButton(const QString& text)
{
  QPushButton* button = new QPushButton(text);
  button->setCheckable(true);
  button->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4a4a4a, stop:1 #3a3a3a); "
    "  color: white; "
    "  border-left: 3px solid transparent; "
    "  padding: 15px 10px; "
    "  font-size: 14px; "
    "  text-align: left; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a5a5a, stop:1 #4a4a4a); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2a2a2a; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 17px; "
    "  padding-left: 12px; "
    "} "
    "QPushButton:checked { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3b3b3b, stop:1 #2b2b2b); "
    "  border-left-color: #00aaff; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.2); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.2); "
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

void HardwareInstrumentPage::showIO()
{
  m_contentStack->setCurrentIndex(3);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_ioButton->setChecked(true);
  m_currentButton = m_ioButton;
  std::cout << "[HardwareInstrumentPage] Showing IO page" << std::endl;
}

void HardwareInstrumentPage::showPneumatic()
{
  m_contentStack->setCurrentIndex(4);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_pneumaticButton->setChecked(true);
  m_currentButton = m_pneumaticButton;
  std::cout << "[HardwareInstrumentPage] Showing Pneumatic page" << std::endl;
}

void HardwareInstrumentPage::showDataView()
{
  m_contentStack->setCurrentIndex(5);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_dataViewButton->setChecked(true);
  m_currentButton = m_dataViewButton;
  std::cout << "[HardwareInstrumentPage] Showing Data View" << std::endl;
}

void HardwareInstrumentPage::showTCPClient()
{
  m_contentStack->setCurrentIndex(6);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_tcpClientButton->setChecked(true);
  m_currentButton = m_tcpClientButton;
  std::cout << "[HardwareInstrumentPage] Showing TCP Client" << std::endl;
}

void HardwareInstrumentPage::showLaserTEC()
{
  m_contentStack->setCurrentIndex(7);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_laserTECButton->setChecked(true);
  m_currentButton = m_laserTECButton;
  std::cout << "[HardwareInstrumentPage] Showing Laser/TEC" << std::endl;
}

void HardwareInstrumentPage::showPowerSupply()
{
  m_contentStack->setCurrentIndex(8);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_powerSupplyButton->setChecked(true);
  m_currentButton = m_powerSupplyButton;
  std::cout << "[HardwareInstrumentPage] Showing Power Supply page" << std::endl;
}

void HardwareInstrumentPage::showSMU()
{
  m_contentStack->setCurrentIndex(9);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_smuButton->setChecked(true);
  m_currentButton = m_smuButton;
  std::cout << "[HardwareInstrumentPage] Showing SMU page" << std::endl;
}