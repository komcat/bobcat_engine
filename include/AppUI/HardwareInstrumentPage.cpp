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
#include <chrono>

HardwareInstrumentPage::HardwareInstrumentPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_sidebar(nullptr)
  , m_contentStack(nullptr)
  , m_currentButton(nullptr)
{
  // Content panel pointers are default-initialized to nullptr in the header.
  setupUI();
}

void HardwareInstrumentPage::setupUI()
{
  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  createSidebar();
  mainLayout->addWidget(m_sidebar);

  // Empty stack - panels are added by ensurePanelBuilt() as they are selected.
  m_contentStack = new QStackedWidget();
  mainLayout->addWidget(m_contentStack, 9);

  // Builds the camera panel only; the other nine wait for their button.
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

// ---------------------------------------------------------------------------
// Panel construction
//
// All ten panels used to be built together with this page. None of them
// connects hardware - every connect here is behind a Connect button - but they
// are large widget trees (the PI and SMU pages alone run well over a thousand
// lines each) and each starts one or two worker threads that the page's own
// hideEvent then immediately stops. Building all ten cost several seconds for
// panels the operator may never select.
//
// Each panel is now built on first selection. The build time is logged because
// the relative cost of these panels is not obvious from reading them - if one
// of them dominates, that is where to look next.
// ---------------------------------------------------------------------------
QWidget* HardwareInstrumentPage::ensurePanelBuilt(Panel panel)
{
  // Implicit derived-to-base conversion, NOT a reinterpret_cast: QWidget
  // itself inherits from both QObject and QPaintDevice, so these upcasts can
  // involve a pointer adjustment. All panel types are complete in this TU.
  QWidget* existing = nullptr;
  switch (panel) {
  case Panel::Camera:      existing = m_cameraLightingPage; break;
  case Panel::PIHexapod:   existing = m_piControllerPage; break;
  case Panel::ACS:         existing = m_acsControllerPage; break;
  case Panel::IO:          existing = m_ioControllerPage; break;
  case Panel::Pneumatic:   existing = m_pneumaticControlPage; break;
  case Panel::DataView:    existing = m_dataStoreWidget; break;
  case Panel::TCPClient:   existing = m_tcpClientWidget; break;
  case Panel::LaserTEC:    existing = m_tecControlPage; break;
  case Panel::PowerSupply: existing = m_powerSupplyPage; break;
  case Panel::SMU:         existing = m_smuControlPage; break;
  }
  if (existing) {
    return existing;
  }

  const auto buildStart = std::chrono::steady_clock::now();

  QWidget* built = nullptr;
  const char* name = "";

  switch (panel) {
  case Panel::Camera:
    m_cameraLightingPage = new CameraLightingPage(m_context);
    built = m_cameraLightingPage;
    name = "Camera+Lighting";
    break;
  case Panel::PIHexapod:
    m_piControllerPage = new PIControllerPage(m_context);
    built = m_piControllerPage;
    name = "PI Hexapod";
    break;
  case Panel::ACS:
    m_acsControllerPage = new ACSControllerPage(m_context);
    built = m_acsControllerPage;
    name = "ACS";
    break;
  case Panel::IO:
    m_ioControllerPage = new IOControllerPage(m_context);
    built = m_ioControllerPage;
    name = "IO";
    break;
  case Panel::Pneumatic:
    m_pneumaticControlPage = new PneumaticControlPage(m_context);
    built = m_pneumaticControlPage;
    name = "Pneumatic";
    break;
  case Panel::DataView:
    m_dataStoreWidget = new DataStoreWidget(m_context);
    built = m_dataStoreWidget;
    name = "Data View";
    break;
  case Panel::TCPClient:
    m_tcpClientWidget = new TcpClientWidget(m_context);
    built = m_tcpClientWidget;
    name = "TCP Client";
    break;
  case Panel::LaserTEC:
    m_tecControlPage = new TECControlPage(m_context);
    built = m_tecControlPage;
    name = "Laser/TEC";
    break;
  case Panel::PowerSupply:
    m_powerSupplyPage = new PowerSupplyControlPage(m_context);
    built = m_powerSupplyPage;
    name = "Power Supply";
    break;
  case Panel::SMU:
    m_smuControlPage = new SMUControlPage(m_context);
    built = m_smuControlPage;
    name = "SMU";
    break;
  }

  if (!built) {
    return nullptr;
  }

  m_contentStack->addWidget(built);

  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - buildStart).count();
  std::cout << "[HardwareInstrumentPage] Built panel '" << name << "' in "
            << elapsedMs << " ms" << std::endl;

  return built;
}

void HardwareInstrumentPage::showPanel(Panel panel, QPushButton* button, const char* logName)
{
  QWidget* page = ensurePanelBuilt(panel);
  if (!page) {
    std::cerr << "[HardwareInstrumentPage] Failed to build panel: " << logName << std::endl;
    return;
  }

  m_contentStack->setCurrentWidget(page);
  if (m_currentButton) m_currentButton->setChecked(false);
  button->setChecked(true);
  m_currentButton = button;
  std::cout << "[HardwareInstrumentPage] Showing " << logName << std::endl;
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
  showPanel(Panel::Camera, m_cameraButton, "camera view");
}

void HardwareInstrumentPage::showPIHexapod()
{
  showPanel(Panel::PIHexapod, m_piButton, "PI Hexapod page");
}

void HardwareInstrumentPage::showACS()
{
  showPanel(Panel::ACS, m_acsButton, "ACS page");
}

void HardwareInstrumentPage::showIO()
{
  showPanel(Panel::IO, m_ioButton, "IO page");
}

void HardwareInstrumentPage::showPneumatic()
{
  showPanel(Panel::Pneumatic, m_pneumaticButton, "Pneumatic page");
}

void HardwareInstrumentPage::showDataView()
{
  showPanel(Panel::DataView, m_dataViewButton, "Data View");
}

void HardwareInstrumentPage::showTCPClient()
{
  showPanel(Panel::TCPClient, m_tcpClientButton, "TCP Client");
}

void HardwareInstrumentPage::showLaserTEC()
{
  showPanel(Panel::LaserTEC, m_laserTECButton, "Laser/TEC");
}

void HardwareInstrumentPage::showPowerSupply()
{
  showPanel(Panel::PowerSupply, m_powerSupplyButton, "Power Supply page");
}

void HardwareInstrumentPage::showSMU()
{
  showPanel(Panel::SMU, m_smuButton, "SMU page");
}
