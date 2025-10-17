// WaypointEditorDialog.h
#pragma once

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <vector>
#include <string>

class WaypointEditorDialog : public QDialog
{
  Q_OBJECT

public:
  explicit WaypointEditorDialog(const QString& positionName,
    const std::vector<std::string>& currentWaypoints,
    const std::vector<std::string>& availablePositions,
    QWidget* parent = nullptr);
  ~WaypointEditorDialog() = default;

  // Get the modified waypoint list
  std::vector<std::string> getWaypoints() const;

private slots:
  void onAddWaypoint();
  void onRemoveWaypoint();
  void onMoveUp();
  void onMoveDown();
  void onWaypointSelectionChanged();

private:
  void setupUI();
  void updateButtonStates();
  void populateAvailablePositions();

  QString m_positionName;
  std::vector<std::string> m_availablePositions;

  // UI Components
  QListWidget* m_waypointList;
  QComboBox* m_positionCombo;
  QPushButton* m_addBtn;
  QPushButton* m_removeBtn;
  QPushButton* m_moveUpBtn;
  QPushButton* m_moveDownBtn;
  QPushButton* m_okBtn;
  QPushButton* m_cancelBtn;
};