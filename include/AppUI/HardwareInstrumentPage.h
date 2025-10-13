// HardwareInstrumentPage.h - Hardware page with sidebar + content
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

struct ApplicationContext;  // Forward declaration
class MultiCameraView;
class TcpClientWidget;  // Forward declaration

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
  void showDataView();        // NEW
  void showTCPClient();       // NEW
  void showLaserTEC();        // NEW
  void showPowerSupply();     // NEW
  void showSMU();             // NEW

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
  QPushButton* m_dataViewButton;      // NEW
  QPushButton* m_tcpClientButton;     // NEW
  QPushButton* m_laserTECButton;      // NEW
  QPushButton* m_powerSupplyButton;   // NEW
  QPushButton* m_smuButton;           // NEW

  // Content pages
  MultiCameraView* m_cameraView;
  TcpClientWidget* m_tcpClientWidget;  // ADD THIS

  // Track current selection
  QPushButton* m_currentButton;
};