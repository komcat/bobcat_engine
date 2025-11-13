// AuxViewWindow.cpp
#include "AuxViewWindow.h"
#include "CameraClickToMoveWidget.h"
#include "LiveChartWidget.h"
#include "CameraManager.h"
#include "unified_motion_manager.h"
#include "DataStore.h"
#include "TcpClientManager.h"
#include <QCloseEvent>
#include <QLabel>
#include <QVBoxLayout>
#include <iostream>

AuxViewWindow::AuxViewWindow(CameraManager* cameraManager,
                               UnifiedMotionManager* motionManager,
                               DataStore* dataStore,
                               TcpClientManager* tcpManager,
                               QWidget* parent)
  : QMainWindow(parent)
  , m_cameraManager(cameraManager)
  , m_motionManager(motionManager)
  , m_dataStore(dataStore)
  , m_tcpManager(tcpManager)
{
  setWindowTitle("Auxiliary Display - Bobcat Engine");

  // Set smaller minimum size (was 1600x900, now 800x600)
  setMinimumSize(400, 300);

  // Default size remains Full HD for second monitor
  resize(400, 300);

  setupUI();

  std::cout << "[AuxViewWindow] Created with click-to-move feature" << std::endl;
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
  m_leftPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_leftPanel->setMinimumWidth(200);  // ✅ Equal minimum width for 50/50 split

  QVBoxLayout* leftLayout = new QVBoxLayout(m_leftPanel);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(0);

  // Camera view with click-to-move widget
  m_cameraClickToMoveWidget = new CameraClickToMoveWidget(m_cameraManager, m_motionManager);
  m_cameraClickToMoveWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  leftLayout->addWidget(m_cameraClickToMoveWidget, 1);  // Stretch factor = 1

  // Add left panel to main layout (50% width)
  m_mainLayout->addWidget(m_leftPanel, 1);  // stretch factor = 1
}

void AuxViewWindow::setupRightPanel()
{
  m_rightPanel = new QWidget();
  m_rightPanel->setStyleSheet("QWidget { background-color: #252525; }");
  m_rightPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_rightPanel->setMinimumWidth(200);  // ✅ Equal minimum width for 50/50 split

  QVBoxLayout* rightLayout = new QVBoxLayout(m_rightPanel);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(0);

  // Live chart widget
  if (m_dataStore && m_tcpManager) {
    m_liveChartWidget = new LiveChartWidget(m_dataStore, m_tcpManager);
    m_liveChartWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightLayout->addWidget(m_liveChartWidget, 1);  // Stretch factor = 1
  }
  else {
    QLabel* errorLabel = new QLabel("Live Chart\n(DataStore or TcpClientManager not available)");
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->setStyleSheet("QLabel { color: #ff4444; font-size: 18px; }");
    rightLayout->addWidget(errorLabel, 1);
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

void AuxViewWindow::initializeCameraSelection()
{
  // ✅ FIX: Trigger delayed camera selection after window is fully shown and initialized
  if (m_cameraClickToMoveWidget) {
    std::cout << "[AuxViewWindow] Triggering delayed camera selection..." << std::endl;
    m_cameraClickToMoveWidget->selectFirstCameraDelayed();
  } else {
    std::cout << "[AuxViewWindow] ERROR: CameraClickToMoveWidget is null!" << std::endl;
  }
}