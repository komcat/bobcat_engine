// MotionPage.h
#pragma once
#include <QWidget>
#include <QHBoxLayout>
#include <QTimer>
#include <QPushButton>
#include "MotionControlPanel.h"
#include "MotionGraphicsView.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"  // NEW
#include "TransformationMatrixLoader.h"
#include <QMap>
#include <QVector>
#include <array>

class MotionPage : public QWidget
{
  Q_OBJECT

public:
  explicit MotionPage(UnifiedMotionManager* motionManager,
    ProductPositionManager* productManager,  // NEW
    TransformationMatrixLoader* matrixLoader,
    QWidget* parent = nullptr);
  ~MotionPage() = default;

signals:
  void backToMainMenu();

private slots:
  void onDeviceChanged(const QString& deviceName);
  void onMoveToPositionClicked();
  void onUpdatePositionClicked();
  void onCreatePositionClicked();
  void onDeletePositionClicked();  // NEW
  void onHomeClicked();
  void onStopClicked();
  void onPositionSelected(const QString& name, double x, double y, double z);
  void onPositionDeselected();
  void onUpdateLivePosition();

  // NEW - Product management slots
  void onLoadProductClicked();
  void onSaveProductClicked();
  void onSaveProductAsClicked();

private:
  void setupUI();
  void loadDevices();
  void loadPositionsForDevice(const QString& deviceName);
  void refreshPositionDisplay();  // NEW

  void transformLocalToGlobal(double localX, double localY, double localZ,
    double& globalX, double& globalY, double& globalZ);

  bool getCurrentDevicePosition(double& x, double& y, double& z,
    double& u, double& v, double& w);

  // Label offset methods
  void loadLabelOffsets();
  void saveLabelOffsets();
  void onLabelOffsetChanged(const QString& name, double offsetX, double offsetY);

  UnifiedMotionManager* m_motionManager;
  ProductPositionManager* m_productManager;  // NEW
  TransformationMatrixLoader* m_matrixLoader;

  MotionControlPanel* m_controlPanel;
  MotionGraphicsView* m_graphicsView;
  QPushButton* m_backButton;

  // NEW - Product control buttons
  QPushButton* m_loadProductBtn;
  QPushButton* m_saveProductBtn;
  QPushButton* m_saveProductAsBtn;
  QLabel* m_currentProductLabel;

  QTimer* m_livePositionTimer;

  QString m_currentDevice;
  QString m_selectedPositionName;

  QMap<QString, QVector<double>> m_currentPositions;
  QMap<QString, QPointF> m_labelOffsets;
  QString m_labelOffsetsFilePath;
};