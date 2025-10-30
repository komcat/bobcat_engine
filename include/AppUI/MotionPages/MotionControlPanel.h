// MotionControlPanel.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QTabWidget>
#include <vector>
#include <string>

/**
 * @brief Left control panel for motion operations with tabbed interface
 * - Actions tab: Motion control buttons
 * - Graph tab: Nodes and Edges visualization (placeholder)
 */
class MotionControlPanel : public QWidget
{
  Q_OBJECT

public:
  explicit MotionControlPanel(QWidget* parent = nullptr);
  ~MotionControlPanel() = default;

  // Update UI with device list
  void setDevices(const QStringList& devices);

  // Update position info display
  void setSelectedPosition(const QString& name, double x, double y, double z,
    double u, double v, double w,
    const std::vector<std::string>& waypoints = {});
  void clearSelectedPosition();

  // Get current selected device
  QString getCurrentDevice() const;

signals:
  void deviceChanged(const QString& deviceName);
  void moveToPositionClicked();
  void moveWithOffsetClicked();
  void editPositionClicked();  // NEW
  void updatePositionClicked();
  void deletePositionClicked();
  void createPositionClicked();
  void editWaypointsClicked();
  void autoFitClicked();  // ✅ NEW: Auto-fit view to positions
  void homeClicked();
  void stopClicked();

private:
  void setupUI();
  void setupActionsTab();
  void setupGraphTab();

  // Widgets
  QTabWidget* m_tabWidget;
  QComboBox* m_deviceCombo;

  QLabel* m_posNameLabel;
  QLabel* m_posXLabel;
  QLabel* m_posYLabel;
  QLabel* m_posZLabel;
  QLabel* m_posULabel;
  QLabel* m_posVLabel;
  QLabel* m_posWLabel;
  QLabel* m_waypointsLabel;

  QPushButton* m_moveToButton;
  QPushButton* m_moveOffsetButton;
  QPushButton* m_editPosButton;  // NEW
  QPushButton* m_updatePosButton;
  QPushButton* m_deletePosButton;
  QPushButton* m_editWaypointsBtn;
  QPushButton* m_createPosButton;
  QPushButton* m_autoFitButton;  // ✅ NEW: Auto-fit view button
  QPushButton* m_homeButton;
  QPushButton* m_stopButton;

  QString m_selectedPositionName;
};