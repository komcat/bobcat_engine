// MotionPage.h
#pragma once
#include <QWidget>
#include <QHBoxLayout>
#include <QTimer>
#include <QPushButton>  // NEW
#include "MotionControlPanel.h"
#include "MotionGraphicsView.h"
#include "unified_motion_manager.h"
#include "TransformationMatrixLoader.h"
#include <QMap>
#include <QVector>
#include <array>

class MotionPage : public QWidget
{
  Q_OBJECT

public:
  explicit MotionPage(UnifiedMotionManager* motionManager,
    TransformationMatrixLoader* matrixLoader,
    QWidget* parent = nullptr);
  ~MotionPage() = default;

signals:
  void backToMainMenu();  // NEW

private slots:
  void onDeviceChanged(const QString& deviceName);
  void onMoveToPositionClicked();
  void onUpdatePositionClicked();
  void onCreatePositionClicked();
  void onHomeClicked();
  void onStopClicked();
  void onPositionSelected(const QString& name, double x, double y, double z);
  void onPositionDeselected();
  void onUpdateLivePosition();

private:
  void setupUI();
  void loadDevices();
  void loadPositionsForDevice(const QString& deviceName);

  void transformLocalToGlobal(double localX, double localY, double localZ,
    double& globalX, double& globalY, double& globalZ);

  bool getCurrentDevicePosition(double& x, double& y, double& z,
    double& u, double& v, double& w);

  bool savePositionToFile(const QString& deviceName, const QString& posName,
    double x, double y, double z,
    double u, double v, double w);

  bool updatePositionInFile(const QString& deviceName, const QString& posName,
    double x, double y, double z,
    double u, double v, double w);

  UnifiedMotionManager* m_motionManager;
  TransformationMatrixLoader* m_matrixLoader;

  MotionControlPanel* m_controlPanel;
  MotionGraphicsView* m_graphicsView;
  QPushButton* m_backButton;  // NEW

  QTimer* m_livePositionTimer;

  QString m_currentDevice;
  QString m_selectedPositionName;

  QMap<QString, QVector<double>> m_currentPositions;
};