// HardwareInstrumentPage.h - Hardware page with sidebar + content
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

struct ApplicationContext;
class CameraLightingPage;  // Replaces MultiCameraView
class TcpClientWidget;
class DataStoreWidget;
class PIControllerPage;
class ACSControllerPage;
class IOControllerPage;
class PneumaticControlPage;  // ✅ ADD
class TECControlPage;  // Add forward declaration
class PowerSupplyControlPage;
class SMUControlPage;  // Add forward declaration for SMU

/**
 * @brief Hardware & Instrument page
 * Left sidebar (10%) + right content area (90%)
 */
class HardwareInstrumentPage : public QWidget
{
  Q_OBJECT

public:
  explicit HardwareInstrumentPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~HardwareInstrumentPage() = default;

signals:
  void backToMainMenu();

private slots:
  void showCamera();
  void showPIHexapod();
  void showACS();
  void showIO();
  void showPneumatic();  // ✅ ADD
  void showDataView();
  void showTCPClient();
  void showLaserTEC();
  void showPowerSupply();
  void showSMU();

private:
  // One entry per sidebar button. Panels are built on first selection - see
  // ensurePanelBuilt().
  enum class Panel {
    Camera,
    PIHexapod,
    ACS,
    IO,
    Pneumatic,
    DataView,
    TCPClient,
    LaserTEC,
    PowerSupply,
    SMU
  };

  void setupUI();
  void createSidebar();
  QPushButton* createSidebarButton(const QString& text);

  // Build `panel` if it does not exist yet and add it to the content stack.
  // Idempotent. Logs how long the build took - these panels are large and
  // their relative cost is not obvious from reading them.
  QWidget* ensurePanelBuilt(Panel panel);

  // ensurePanelBuilt() + show it + update the sidebar button state.
  void showPanel(Panel panel, QPushButton* button, const char* logName);

  ApplicationContext* m_context;

  // UI Components
  QWidget* m_sidebar;
  QStackedWidget* m_contentStack;

  // Sidebar buttons
  QPushButton* m_backButton;
  QPushButton* m_cameraButton;
  QPushButton* m_piButton;
  QPushButton* m_acsButton;
  QPushButton* m_ioButton;
  QPushButton* m_pneumaticButton;  // ✅ ADD
  QPushButton* m_dataViewButton;
  QPushButton* m_tcpClientButton;
  QPushButton* m_laserTECButton;
  QPushButton* m_powerSupplyButton;
  QPushButton* m_smuButton;

  // Content pages - all null until ensurePanelBuilt() creates them.
  CameraLightingPage* m_cameraLightingPage = nullptr;  // Contains Camera + Lighting tabs
  TcpClientWidget* m_tcpClientWidget = nullptr;
  DataStoreWidget* m_dataStoreWidget = nullptr;
  PIControllerPage* m_piControllerPage = nullptr;
  ACSControllerPage* m_acsControllerPage = nullptr;
  IOControllerPage* m_ioControllerPage = nullptr;
  PneumaticControlPage* m_pneumaticControlPage = nullptr;
  TECControlPage* m_tecControlPage = nullptr;
  PowerSupplyControlPage* m_powerSupplyPage = nullptr;  // was a local - now needed for lazy build
  SMUControlPage* m_smuControlPage = nullptr;

  // Track current selection
  QPushButton* m_currentButton;
};