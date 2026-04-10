// MotionControlPanel.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QListWidget>
#include <QLineEdit>
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

  // Position list management
  void updatePositionList(const QStringList& positions);
  void selectPositionInList(const QString& positionName);

  // Graph selector
  void updateGraphList(const QStringList& graphNames);
  QString getCurrentGraph() const;

  // Get current selected device
  QString getCurrentDevice() const;

  // ✅ NEW: Update breadcrumb display
  void updateBreadcrumb(const QString& deviceName, const QString& positionName);

signals:
  void deviceChanged(const QString& deviceName);
  void moveToPositionClicked();
  void moveWithOffsetClicked();
  void editPositionClicked();  // NEW
  void updatePositionClicked();
  void deletePositionClicked();
  void createPositionClicked();
  // void editWaypointsClicked();  // ❌ REMOVED: Waypoints not used
  void autoFitClicked();  // ✅ NEW: Auto-fit view to positions
  void homeClicked();
  void stopClicked();
  void velocityPresetClicked(double velocity);  // Velocity preset button clicked
  void positionSelectedFromList(const QString& positionName);
  void graphChanged(const QString& graphName);

private slots:
  void onPositionListSelectionChanged();
  void onFilterTextChanged(const QString& text);

private:
  void setupUI();
  void setupActionsTab();
  void setupGraphTab();

  // Widgets
  QTabWidget* m_tabWidget;
  QComboBox* m_deviceCombo;
  QListWidget* m_positionList;
  QLineEdit* m_positionFilter;
  QComboBox* m_graphSelector;
  QLabel* m_breadcrumbLabel;  // ✅ NEW: Breadcrumb display for device >> position

  QLabel* m_posNameLabel;
  QLabel* m_posXLabel;
  QLabel* m_posYLabel;
  QLabel* m_posZLabel;
  QLabel* m_posULabel;
  QLabel* m_posVLabel;
  QLabel* m_posWLabel;
  // QLabel* m_waypointsLabel;  // ❌ REMOVED: Waypoints not used

  QPushButton* m_moveToButton;
  QPushButton* m_moveOffsetButton;
  QPushButton* m_editPosButton;  // NEW
  QPushButton* m_updatePosButton;
  QPushButton* m_deletePosButton;
  // QPushButton* m_editWaypointsBtn;  // ❌ REMOVED: Waypoints not used
  QPushButton* m_createPosButton;
  QPushButton* m_autoFitButton;  // ✅ NEW: Auto-fit view button
  QPushButton* m_homeButton;
  QPushButton* m_stopButton;

  // Velocity presets
  QWidget* m_velocityPresetsWidget;
  QList<QPushButton*> m_velocityButtons;

  QString m_selectedPositionName;

public:
  // Update velocity preset buttons based on device type
  void updateVelocityPresets(const QString& deviceType);
};