// AuxViewWindow.cpp
#include "AuxViewWindow.h"
#include "CameraClickToMoveWidget.h"
#include "LiveChartWidget.h"
#include "WorldOverlayWidget.h"
#include "WorldOverlayManager.h"
#include "CameraManager.h"
#include "CameraSettingsWidget.h"
#include "unified_motion_manager.h"
#include "DataStore.h"
#include "TcpClientManager.h"
#include <QCloseEvent>
#include <QHideEvent>
#include <QLabel>
#include <QVBoxLayout>
#include <QSettings>
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
  restoreWindowSettings();  // Restore saved window position and size

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

  // Tab 3: Exposure Settings
  QWidget* exposureTab = createExposureTab();
  m_tabWidget->addTab(exposureTab, "☀ Exposure");
}

QWidget* AuxViewWindow::createLiveViewTab()
{
  QWidget* tab = new QWidget();
  tab->setStyleSheet("QWidget { background-color: #1e1e1e; }");

  QVBoxLayout* mainLayout = new QVBoxLayout(tab);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // --- Toolbar with layout toggle button ---
  QWidget* toolbar = new QWidget();
  toolbar->setFixedHeight(28);
  toolbar->setStyleSheet("QWidget { background-color: #2d2d2d; }");

  QHBoxLayout* toolbarLayout = new QHBoxLayout(toolbar);
  toolbarLayout->setContentsMargins(4, 2, 4, 2);
  toolbarLayout->addStretch();

  m_layoutToggleButton = new QPushButton();
  m_layoutToggleButton->setText(QString::fromUtf8("\xe2\xac\x8c"));  // Horizontal arrows
  m_layoutToggleButton->setToolTip("Layout: Horizontal (click to switch to Vertical)");
  m_layoutToggleButton->setFixedSize(24, 24);
  m_layoutToggleButton->setStyleSheet(R"(
    QPushButton {
      background-color: #3d3d3d;
      color: white;
      border: none;
      border-radius: 4px;
      font-size: 14px;
    }
    QPushButton:hover { background-color: #4d4d4d; }
    QPushButton:pressed { background-color: #0078d4; }
  )");
  connect(m_layoutToggleButton, &QPushButton::clicked,
          this, &AuxViewWindow::toggleLiveViewLayout);
  toolbarLayout->addWidget(m_layoutToggleButton);

  mainLayout->addWidget(toolbar);

  // --- Splitter for Camera and Chart ---
  m_liveViewSplitter = new QSplitter(Qt::Horizontal);  // Default: horizontal
  m_liveViewSplitter->setStyleSheet(R"(
    QSplitter::handle {
      background-color: #3d3d3d;
    }
    QSplitter::handle:horizontal { width: 4px; }
    QSplitter::handle:vertical { height: 4px; }
  )");

  // Camera widget
  m_cameraClickToMoveWidget = new CameraClickToMoveWidget(m_cameraManager, m_motionManager);
  m_cameraClickToMoveWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_cameraClickToMoveWidget->setMinimumSize(200, 150);
  m_liveViewSplitter->addWidget(m_cameraClickToMoveWidget);

  // Chart widget
  if (m_dataStore && m_tcpManager) {
    m_liveChartWidget = new LiveChartWidget(m_dataStore, m_tcpManager);
    m_liveChartWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_liveChartWidget->setMinimumSize(200, 150);
    m_liveViewSplitter->addWidget(m_liveChartWidget);
  } else {
    QLabel* errorLabel = new QLabel("Live Chart\n(DataStore or TcpClientManager not available)");
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->setStyleSheet("QLabel { color: #ff4444; font-size: 18px; }");
    m_liveViewSplitter->addWidget(errorLabel);
    std::cout << "[AuxViewWindow] ERROR: Cannot create LiveChartWidget - missing dependencies" << std::endl;
  }

  // Set initial splitter sizes (50/50)
  m_liveViewSplitter->setSizes({500, 500});

  mainLayout->addWidget(m_liveViewSplitter, 1);

  // Restore saved layout preference
  restoreLiveViewLayoutSettings();

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

  // Share the overlay manager with CameraClickToMoveWidget for "Show Overlay" feature
  if (m_cameraClickToMoveWidget && m_worldOverlayWidget->overlayManager()) {
    m_cameraClickToMoveWidget->setOverlayManager(m_worldOverlayWidget->overlayManager());
    std::cout << "[AuxViewWindow] Shared overlay manager with CameraClickToMoveWidget" << std::endl;
  }

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
  // Save window geometry before hiding
  saveWindowSettings();

  // Prevent closing - hide instead
  event->ignore();
  hide();
  std::cout << "[AuxViewWindow] Close prevented - window hidden instead" << std::endl;
}

void AuxViewWindow::hideEvent(QHideEvent* event)
{
  // Save window geometry when hiding
  saveWindowSettings();
  QMainWindow::hideEvent(event);
}

void AuxViewWindow::saveWindowSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  settings.setValue("AuxViewWindow/geometry", saveGeometry());
  settings.setValue("AuxViewWindow/windowState", saveState());
  saveLiveViewLayoutSettings();  // Save layout and splitter state
  std::cout << "[AuxViewWindow] Window settings saved" << std::endl;
}

void AuxViewWindow::restoreWindowSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  QByteArray geometry = settings.value("AuxViewWindow/geometry").toByteArray();
  QByteArray state = settings.value("AuxViewWindow/windowState").toByteArray();

  if (!geometry.isEmpty()) {
    restoreGeometry(geometry);
    std::cout << "[AuxViewWindow] Window geometry restored" << std::endl;
  }

  if (!state.isEmpty()) {
    restoreState(state);
    std::cout << "[AuxViewWindow] Window state restored" << std::endl;
  }
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

// =============================================================================
// LIVE VIEW LAYOUT SWITCHING
// =============================================================================

void AuxViewWindow::toggleLiveViewLayout()
{
  if (m_liveViewLayout == LiveViewLayout::Horizontal) {
    setLiveViewLayout(LiveViewLayout::Vertical);
  } else {
    setLiveViewLayout(LiveViewLayout::Horizontal);
  }
}

void AuxViewWindow::setLiveViewLayout(LiveViewLayout layout, bool saveSettings)
{
  if (!m_liveViewSplitter || !m_layoutToggleButton) return;

  m_liveViewLayout = layout;

  if (layout == LiveViewLayout::Horizontal) {
    m_liveViewSplitter->setOrientation(Qt::Horizontal);
    m_layoutToggleButton->setText(QString::fromUtf8("\xe2\xac\x8c"));  // Horizontal arrows
    m_layoutToggleButton->setToolTip("Layout: Horizontal (click to switch to Vertical)");
  } else {
    m_liveViewSplitter->setOrientation(Qt::Vertical);
    m_layoutToggleButton->setText(QString::fromUtf8("\xe2\xac\x8d"));  // Vertical arrows
    m_layoutToggleButton->setToolTip("Layout: Vertical (click to switch to Horizontal)");
  }

  // Save preference (skip during restore to avoid redundant writes)
  if (saveSettings) {
    saveLiveViewLayoutSettings();
  }

  std::cout << "[AuxViewWindow] Layout changed to "
            << (layout == LiveViewLayout::Horizontal ? "Horizontal" : "Vertical")
            << std::endl;
}

void AuxViewWindow::saveLiveViewLayoutSettings()
{
  if (!m_liveViewSplitter) return;  // Guard against null pointer

  QSettings settings("Fabrinet", "BobcatEngine");
  settings.setValue("AuxViewWindow/liveViewLayout",
                    static_cast<int>(m_liveViewLayout));

  // Also save splitter sizes for each orientation
  QString key = (m_liveViewLayout == LiveViewLayout::Horizontal)
                ? "AuxViewWindow/splitterHorizontal"
                : "AuxViewWindow/splitterVertical";
  settings.setValue(key, m_liveViewSplitter->saveState());
}

void AuxViewWindow::restoreLiveViewLayoutSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");

  // Restore layout orientation (don't save during restore)
  int savedLayout = settings.value("AuxViewWindow/liveViewLayout", 0).toInt();
  LiveViewLayout layout = static_cast<LiveViewLayout>(savedLayout);
  setLiveViewLayout(layout, false);  // false = don't save during restore

  // Restore splitter sizes for current orientation
  QString key = (layout == LiveViewLayout::Horizontal)
                ? "AuxViewWindow/splitterHorizontal"
                : "AuxViewWindow/splitterVertical";
  QByteArray splitterState = settings.value(key).toByteArray();
  if (!splitterState.isEmpty()) {
    m_liveViewSplitter->restoreState(splitterState);
  }
}

// =============================================================================
// TAB 3: EXPOSURE SETTINGS
// =============================================================================

QWidget* AuxViewWindow::createExposureTab()
{
  QWidget* tab = new QWidget();
  tab->setStyleSheet("QWidget { background-color: #1e1e1e; }");

  QVBoxLayout* layout = new QVBoxLayout(tab);
  layout->setContentsMargins(10, 10, 10, 10);

  // Use the reusable CameraSettingsWidget
  m_cameraSettingsWidget = new CameraSettingsWidget(m_cameraManager, tab);
  layout->addWidget(m_cameraSettingsWidget);

  std::cout << "[AuxViewWindow] Created Exposure tab with CameraSettingsWidget" << std::endl;

  return tab;
}