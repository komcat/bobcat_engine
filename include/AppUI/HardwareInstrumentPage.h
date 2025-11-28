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
  void setupUI();
  void createSidebar();
  void createContentArea();
  QPushButton* createSidebarButton(const QString& text);

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

  // Content pages
  CameraLightingPage* m_cameraLightingPage;  // Contains Camera + Lighting tabs
  TcpClientWidget* m_tcpClientWidget;
  DataStoreWidget* m_dataStoreWidget;
  PIControllerPage* m_piControllerPage;
  ACSControllerPage* m_acsControllerPage;
  IOControllerPage* m_ioControllerPage;
  PneumaticControlPage* m_pneumaticControlPage;  // ✅ ADD
  TECControlPage* m_tecControlPage;  // Add member variable
  SMUControlPage* m_smuControlPage;  // Add SMU page

  // Track current selection
  QPushButton* m_currentButton;
};