// AuxViewWindow.cpp
#include "AuxViewWindow.h"
#include "CameraClickToMoveWidget.h"
#include "LiveChartWidget.h"
#include "WorldOverlayWidget.h"
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

  // Set smaller minimum size
  setMinimumSize(400, 300);

  // Default size
  resize(400, 300);

  setupUI();

  std::cout << "[AuxViewWindow] Created with tabbed interface" << std::endl;
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

  QVBoxLayout* mainLayout = new QVBoxLayout(m_centralWidget);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Create tab widget
  setupTabs();
  mainLayout->addWidget(m_tabWidget);
}

void AuxViewWindow::setupTabs()
{
  m_tabWidget = new QTabWidget();
  m_tabWidget->setStyleSheet(R"(
    QTabWidget::pane {
      border: none;
      background-color: #1e1e1e;
    }
    QTabBar::tab {
      background-color: #2d2d2d;
      color: #a0a0a0;
      padding: 8px 20px;
      margin-right: 2px;
      border-top-left-radius: 4px;
      border-top-right-radius: 4px;
      font-size: 12px;
    }
    QTabBar::tab:selected {
      background-color: #0078d4;
      color: white;
    }
    QTabBar::tab:hover:!selected {
      background-color: #3d3d3d;
    }
  )");

  // Tab 1: Live View (Camera + Chart)
  QWidget* liveViewTab = createLiveViewTab();
  m_tabWidget->addTab(liveViewTab, "📹 Live View");

  // Tab 2: World Overlay (created later when position manager is set)
  QWidget* overlayTab = createOverlayTab();
  m_tabWidget->addTab(overlayTab, "🌐 World Overlay");
}

QWidget* AuxViewWindow::createLiveViewTab()
{
  QWidget* tab = new QWidget();
  tab->setStyleSheet("QWidget { background-color: #1e1e1e; }");

  QHBoxLayout* layout = new QHBoxLayout(tab);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);

  // Left panel - Camera view
  QWidget* leftPanel = new QWidget();
  leftPanel->setStyleSheet("QWidget { background-color: #252525; }");
  leftPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  leftPanel->setMinimumWidth(200);

  QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(0);

  m_cameraClickToMoveWidget = new CameraClickToMoveWidget(m_cameraManager, m_motionManager);
  m_cameraClickToMoveWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  leftLayout->addWidget(m_cameraClickToMoveWidget, 1);

  layout->addWidget(leftPanel, 1);

  // Right panel - Chart view
  QWidget* rightPanel = new QWidget();
  rightPanel->setStyleSheet("QWidget { background-color: #252525; }");
  rightPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  rightPanel->setMinimumWidth(200);

  QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(0);

  if (m_dataStore && m_tcpManager) {
    m_liveChartWidget = new LiveChartWidget(m_dataStore, m_tcpManager);
    m_liveChartWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightLayout->addWidget(m_liveChartWidget, 1);
  } else {
    QLabel* errorLabel = new QLabel("Live Chart\n(DataStore or TcpClientManager not available)");
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->setStyleSheet("QLabel { color: #ff4444; font-size: 18px; }");
    rightLayout->addWidget(errorLabel, 1);
    std::cout << "[AuxViewWindow] ERROR: Cannot create LiveChartWidget - missing dependencies" << std::endl;
  }

  layout->addWidget(rightPanel, 1);

  return tab;
}

QWidget* AuxViewWindow::createOverlayTab()
{
  // Create placeholder - actual WorldOverlayWidget will be created when position manager is set
  QWidget* tab = new QWidget();
  tab->setStyleSheet("QWidget { background-color: #1e1e1e; }");

  QVBoxLayout* layout = new QVBoxLayout(tab);
  layout->setContentsMargins(0, 0, 0, 0);

  // Create the world overlay widget
  m_worldOverlayWidget = new WorldOverlayWidget(m_cameraManager, m_motionManager, tab);
  m_worldOverlayWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  layout->addWidget(m_worldOverlayWidget);

  return tab;
}

void AuxViewWindow::setProductPositionManager(ProductPositionManager* positionManager)
{
  m_positionManager = positionManager;

  // Pass to overlay widget if it exists
  if (m_worldOverlayWidget) {
    m_worldOverlayWidget->setProductPositionManager(positionManager);
  }
}

void AuxViewWindow::switchToLiveViewTab()
{
  if (m_tabWidget) {
    m_tabWidget->setCurrentIndex(0);
  }
}

void AuxViewWindow::switchToOverlayTab()
{
  if (m_tabWidget) {
    m_tabWidget->setCurrentIndex(1);
  }
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
  // Trigger delayed camera selection after window is fully shown and initialized
  if (m_cameraClickToMoveWidget) {
    std::cout << "[AuxViewWindow] Triggering delayed camera selection..." << std::endl;
    m_cameraClickToMoveWidget->selectFirstCameraDelayed();
  } else {
    std::cout << "[AuxViewWindow] ERROR: CameraClickToMoveWidget is null!" << std::endl;
  }
}