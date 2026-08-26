// MotionPage.h
#pragma once
#include <QWidget>
#include <QHBoxLayout>
#include <QTimer>
#include <QPushButton>
#include <QTabWidget>
#include "MotionControlPanel.h"
#include "MotionGraphicsView.h"
#include "ScanControlPanel.h"
#include "DeviceButtonStack.h"
#include "GraphWidget.h"
#include "AdaptiveScanController.h"
#include "InterlockWidget.h"
#include "DataStore.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include "TransformationMatrixLoader.h"
#include "CoordinateSystemManager.h"
#include <QMap>
#include <QVector>
#include <array>
#include <memory>

class SequenceEngine;
class InterlockManager;

class MotionPage : public QWidget
{
  Q_OBJECT

public:
  explicit MotionPage(UnifiedMotionManager* motionManager,
    ProductPositionManager* productManager,
    TransformationMatrixLoader* matrixLoader,
    DataStore* dataStore,
    CoordinateSystemManager* coordinateSystemManager,
    SequenceEngine* sequenceEngine,
    InterlockManager* interlockManager = nullptr,
    QWidget* parent = nullptr);
  ~MotionPage() override;

  // ✅ NEW: Refresh device list to show only connected devices
  void refreshDeviceList();

public slots:
  void onProductChanged();  // Called when product is loaded/created in MainMenu

signals:
  void backToMainMenu();

private slots:
  // Motion control slots
  void onDeviceChanged(const QString& deviceName);
  void onMoveToPositionClicked();
  void onMoveWithOffsetClicked();
  void onEditPositionClicked();  // ✅ ADD THIS LINE

  void onUpdatePositionClicked();
  void onCreatePositionClicked();
  void onDeletePositionClicked();
  // void onEditWaypointsClicked();  // ❌ REMOVED: Waypoints not used
  void onHomeClicked();
  void onStopClicked();
  void onPositionSelected(const QString& name, double x, double y, double z);
  void onPositionDeselected();
  void onPositionSelectedFromList(const QString& name);  // ✅ NEW: Position selected from list
  void onUpdateLivePosition();

  // Product management slots
  void onLoadProductClicked();
  void onSaveProductClicked();

  // Scan control slots
  void onScanDeviceChanged(const QString& deviceName);
  void onScanPresetChanged(const QString& preset);
  void onScanDataChannelChanged(const QString& channel);
  void onStartScanClicked();
  void onStopScanClicked();

protected:
  // ✅ NEW: Override showEvent to refresh device list when page is shown
  void showEvent(QShowEvent* event) override;

private slots:
  // Builds the contents of whichever tab was just revealed, once.
  void onTabChanged(int index);

private:
  void setupUI();

  // Lazy tab contents - only the Motion Control tab is built with the page.
  void buildCoordinateSystemTab();
  void buildInterlockTab();
  void ensureGraphWidget();
  void ensureScanPanel();

  void loadDevices();
  void loadPositionsForDevice(const QString& deviceName);
  void refreshPositionDisplay();

  void transformLocalToGlobal(double localX, double localY, double localZ,
    double& globalX, double& globalY, double& globalZ);

  bool getCurrentDevicePosition(double& x, double& y, double& z,
    double& u, double& v, double& w);

  void loadGraphEdges(const QString& graphName);

  // Scan helper methods
  void createScanController();
  ScanParameters createScanParameters(const QString& preset);
  void onScanProgress(const ScanProgressEvent& event);
  void onScanDataPoint(const ScanDataPoint& dataPoint);
  void onScanPeakFound(const PeakInfo& peak);
  void onScanCompleted(const ScanResults& results);
  void onScanError(const ScanError& error);

  UnifiedMotionManager* m_motionManager;
  ProductPositionManager* m_productManager;
  TransformationMatrixLoader* m_matrixLoader;
  DataStore* m_dataStore;
  CoordinateSystemManager* m_coordinateSystemManager;
  SequenceEngine* m_sequenceEngine;
  InterlockManager* m_interlockManager;

  // Tab widget
  QTabWidget* m_tabWidget;

  // Motion control tab widgets
  DeviceButtonStack* m_deviceButtonStack;
  MotionControlPanel* m_controlPanel;
  MotionGraphicsView* m_graphicsView;
  ScanControlPanel* m_scanPanel;
  GraphWidget* m_graphWidget;
  QPushButton* m_backButton;

  // Lazily populated tab pages (the QWidget shells exist from setupUI, their
  // contents do not until the tab is first shown)
  QWidget* m_coordinateSystemTab;
  bool m_coordinateSystemBuilt;
  QWidget* m_interlockTab;
  bool m_interlockBuilt;
  QWidget* m_graphTab;
  bool m_graphWidgetStale;

  // Product control buttons
  QPushButton* m_loadProductBtn;
  QPushButton* m_saveProductBtn;
  QLabel* m_currentProductLabel;

  QTimer* m_livePositionTimer;
  QTimer* m_motionUpdateTimer;

  QString m_currentDevice;
  QString m_selectedPositionName;

  // Scan state
  QString m_scanDevice;
  QString m_scanPreset;
  QString m_scanDataChannel;  // ✅ ADD THIS - was missing!
  std::unique_ptr<AdaptiveScanController> m_scanController;

  QMap<QString, QVector<double>> m_currentPositions;
  QMap<QString, QVector<double>> m_rawPositions;  // Raw (local) axis values before transformation
};