// MotionControlPanel.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QGroupBox>
#include <QVBoxLayout>

/**
 * @brief Left control panel for motion operations
 * - Device selection
 * - Position info display
 * - Move to saved position button
 * - Update saved position button
 * - Delete position button
 * - Create new position button
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
    double u, double v, double w);
  void clearSelectedPosition();

  // Get current selected device
  QString getCurrentDevice() const;

signals:
  void deviceChanged(const QString& deviceName);
  void moveToPositionClicked();
  void updatePositionClicked();
  void deletePositionClicked();  // ✅ NEW
  void createPositionClicked();
  void homeClicked();
  void stopClicked();

private:
  void setupUI();

  // Widgets
  QComboBox* m_deviceCombo;

  QLabel* m_posNameLabel;
  QLabel* m_posXLabel;
  QLabel* m_posYLabel;
  QLabel* m_posZLabel;
  QLabel* m_posULabel;
  QLabel* m_posVLabel;
  QLabel* m_posWLabel;

  QPushButton* m_moveToButton;
  QPushButton* m_updatePosButton;
  QPushButton* m_deletePosButton;  // ✅ NEW
  QPushButton* m_createPosButton;
  QPushButton* m_homeButton;
  QPushButton* m_stopButton;

  QString m_selectedPositionName;
};