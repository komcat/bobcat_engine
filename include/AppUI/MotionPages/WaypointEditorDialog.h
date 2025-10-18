// WaypointEditorDialog.h
#pragma once

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QTabWidget>
#include <vector>
#include <string>

class WaypointEditorDialog : public QDialog
{
  Q_OBJECT

public:
  explicit WaypointEditorDialog(const QString& positionName,
    const std::vector<std::string>& currentWaypoints,
    const std::vector<std::string>& currentExitWaypoints,  // ✅ NEW
    const std::vector<std::string>& availablePositions,
    QWidget* parent = nullptr);
  ~WaypointEditorDialog() = default;

  // Get the modified waypoint lists
  std::vector<std::string> getWaypoints() const;
  std::vector<std::string> getExitWaypoints() const;  // ✅ NEW

private slots:
  // Entry waypoints
  void onAddWaypoint();
  void onRemoveWaypoint();
  void onMoveUpWaypoint();
  void onMoveDownWaypoint();
  void onWaypointSelectionChanged();

  // Exit waypoints  // ✅ NEW
  void onAddExitWaypoint();
  void onRemoveExitWaypoint();
  void onMoveUpExitWaypoint();
  void onMoveDownExitWaypoint();
  void onExitWaypointSelectionChanged();

private:
  void setupUI();
  void updateButtonStates();
  void updateExitButtonStates();  // ✅ NEW
  void populateAvailablePositions();

  QString m_positionName;
  std::vector<std::string> m_availablePositions;

  // Entry waypoint UI
  QListWidget* m_waypointList;
  QComboBox* m_positionCombo;
  QPushButton* m_addBtn;
  QPushButton* m_removeBtn;
  QPushButton* m_moveUpBtn;
  QPushButton* m_moveDownBtn;

  // Exit waypoint UI  // ✅ NEW
  QListWidget* m_exitWaypointList;
  QComboBox* m_exitPositionCombo;
  QPushButton* m_addExitBtn;
  QPushButton* m_removeExitBtn;
  QPushButton* m_moveUpExitBtn;
  QPushButton* m_moveDownExitBtn;

  // Dialog buttons
  QPushButton* m_okBtn;
  QPushButton* m_cancelBtn;
};