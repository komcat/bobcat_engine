// MotionPage.h
#pragma once
#include <QWidget>
#include <QHBoxLayout>
#include <QTimer>
#include <QPushButton>
#include "MotionControlPanel.h"
#include "MotionGraphicsView.h"
#include "ScanControlPanel.h"
#include "AdaptiveScanController.h"
#include "DataStore.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include "TransformationMatrixLoader.h"
#include <QMap>
#include <QVector>
#include <array>
#include <memory>

class MotionPage : public QWidget
{
  Q_OBJECT

public:
  explicit MotionPage(UnifiedMotionManager* motionManager,
    ProductPositionManager* productManager,
    TransformationMatrixLoader* matrixLoader,
    DataStore* dataStore,
    QWidget* parent = nullptr);
  ~MotionPage() = default;

signals:
  void backToMainMenu();

private slots:
  // Motion control slots
  void onDeviceChanged(const QString& deviceName);
  void onMoveToPositionClicked();
  void onMoveWithOffsetClicked();
  void onUpdatePositionClicked();
  void onCreatePositionClicked();
  void onDeletePositionClicked();
  void onEditWaypointsClicked();
  void onHomeClicked();
  void onStopClicked();
  void onPositionSelected(const QString& name, double x, double y, double z);
  void onPositionDeselected();
  void onUpdateLivePosition();

  // Product management slots
  void onLoadProductClicked();
  void onSaveProductClicked();
  void onSaveProductAsClicked();

  // Scan control slots
  void onScanDeviceChanged(const QString& deviceName);
  void onScanPresetChanged(const QString& preset);
  void onScanDataChannelChanged(const QString& channel);
  void onStartScanClicked();
  void onStopScanClicked();

private:
  void setupUI();
  void loadDevices();
  void loadPositionsForDevice(const QString& deviceName);
  void refreshPositionDisplay();

  void transformLocalToGlobal(double localX, double localY, double localZ,
    double& globalX, double& globalY, double& globalZ);

  bool getCurrentDevicePosition(double& x, double& y, double& z,
    double& u, double& v, double& w);

  // Label offset methods
  void loadLabelOffsets();
  void saveLabelOffsets();
  void onLabelOffsetChanged(const QString& name, double offsetX, double offsetY);

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

  MotionControlPanel* m_controlPanel;
  MotionGraphicsView* m_graphicsView;
  ScanControlPanel* m_scanPanel;
  QPushButton* m_backButton;

  // Product control buttons
  QPushButton* m_loadProductBtn;
  QPushButton* m_saveProductBtn;
  QPushButton* m_saveProductAsBtn;
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
  QMap<QString, QPointF> m_labelOffsets;
  QString m_labelOffsetsFilePath;
};