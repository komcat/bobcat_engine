// AuxViewWindow.cpp
#include "AuxViewWindow.h"
#include "LiveViewCameraWidget.h"
#include "LiveChartWidget.h"
#include "CameraManager.h"
#include "DataStore.h"
#include "TcpClientManager.h"
#include <QCloseEvent>
#include <QLabel>
#include <QVBoxLayout>
#include <iostream>

AuxViewWindow::AuxViewWindow(CameraManager* cameraManager, DataStore* dataStore, TcpClientManager* tcpManager, QWidget* parent)
  : QMainWindow(parent)
  , m_cameraManager(cameraManager)
  , m_dataStore(dataStore)
  , m_tcpManager(tcpManager)
{
  setWindowTitle("Auxiliary Display - Bobcat Engine");
  setMinimumSize(1600, 900);
  resize(1920, 1080);  // Default to Full HD

  setupUI();

  std::cout << "[AuxViewWindow] Created" << std::endl;
}

AuxViewWindow::~AuxViewWindow()
{
  std::cout << "[AuxViewWindow] Destroyed" << std::endl;
}

void AuxViewWindow::setupUI()
{
  // Central widget
  m_centralWidget = new QWidget();
  m_centralWidget->setStyleSheet("QWidget { background-color: #1e1e1e; }");
  setCentralWidget(m_centralWidget);

  // Main horizontal layout (50/50 split)
  m_mainLayout = new QHBoxLayout(m_centralWidget);
  m_mainLayout->setContentsMargins(0, 0, 0, 0);
  m_mainLayout->setSpacing(2);  // Small gap between panels

  // Setup panels
  setupLeftPanel();
  setupRightPanel();
}

void AuxViewWindow::setupLeftPanel()
{
  m_leftPanel = new QWidget();
  m_leftPanel->setStyleSheet("QWidget { background-color: #252525; }");

  QVBoxLayout* leftLayout = new QVBoxLayout(m_leftPanel);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(0);

  // Live camera view widget
  m_liveViewWidget = new LiveViewCameraWidget(m_cameraManager);
  leftLayout->addWidget(m_liveViewWidget);

  // Add left panel to main layout (50% width)
  m_mainLayout->addWidget(m_leftPanel, 1);  // stretch factor = 1
}

void AuxViewWindow::setupRightPanel()
{
  m_rightPanel = new QWidget();
  m_rightPanel->setStyleSheet("QWidget { background-color: #252525; }");

  QVBoxLayout* rightLayout = new QVBoxLayout(m_rightPanel);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(0);

  // Live chart widget
  if (m_dataStore && m_tcpManager) {
    m_liveChartWidget = new LiveChartWidget(m_dataStore, m_tcpManager);
    rightLayout->addWidget(m_liveChartWidget);
  }
  else {
    QLabel* errorLabel = new QLabel("Live Chart\n(DataStore or TcpClientManager not available)");
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->setStyleSheet("QLabel { color: #ff4444; font-size: 18px; }");
    rightLayout->addWidget(errorLabel);
    std::cout << "[AuxViewWindow] ERROR: Cannot create LiveChartWidget - missing dependencies" << std::endl;
  }

  // Add right panel to main layout (50% width)
  m_mainLayout->addWidget(m_rightPanel, 1);  // stretch factor = 1
}

void AuxViewWindow::closeEvent(QCloseEvent* event)
{
  // Prevent closing - hide instead
  event->ignore();
  hide();
  std::cout << "[AuxViewWindow] Close prevented - window hidden instead" << std::endl;
}