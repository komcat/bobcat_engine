// PIControllerPage.h - PI-specific controller page
#pragma once

#include "MotionControllerPageBase.h"
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QTimer>
#include <QPushButton>
#include <QProgressBar>
#include <map>

// Forward declaration
class PIAlignExecutor;
struct FSMParameters;

/**
 * @brief Page for PI (Physik Instrumente) controllers
 *
 * Displays PI hexapods with 6 axes: X, Y, Z, U, V, W
 * Includes system-wide velocity control (not per-axis)
 * Includes analog input channel reading
 */
class PIControllerPage : public MotionControllerPageBase
{
  Q_OBJECT

public:
  explicit PIControllerPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~PIControllerPage();

  // Override base class virtual methods
  std::string getControllerType() const override;
  std::vector<std::string> getAxisNames() const override;
  QString getPageTitle() const override;

protected:
  // Override to add velocity controls and tabs
  void setupUI() override;
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void updatePositions() override;  // Override base class - we don't use m_deviceTable

private slots:
  void onTabChanged(int index);
  void onPresetTurtleClicked();
  void onPresetRabbitClicked();
  void onDeviceSelected(const QString& deviceName);
  void onApplyVelocityClicked();
  void onAnalogRefreshClicked();
  void onAnalogPollTimer();
  void onFSMAlignClicked(int channel);
  void onZScanClicked(int channel);
  void onPositionPollTimer();  // Update positions in device status cards

  // FSM Executor signal handlers
  void onAlignmentStarted(QString deviceName, int channel);
  void onAlignmentCompleted(QString deviceName, int channel);
  void onAlignmentFailed(QString deviceName, int channel, QString errorMessage);
  void onAlignBusyStateChanged(bool busy);
  void onZScanProgress(QString deviceName, int iteration, int total, double currentZ, double peakZ, double peakVoltage);

private:
  void createMainTab();
  void createDeviceStatusTab();  // New: Device status with vertical table
  void createPivotPointTab();
  void createAnalogInputTab();
  void updateVelocityDisplay();
  void updateDeviceStatusDisplay();  // New: Update vertical device table
  void updatePivotPointDisplay();
  void updateAnalogDisplay();

  // Tab widget
  QTabWidget* m_tabWidget;

  // Velocity control widgets (system-wide velocity, not per-axis)
  QComboBox* m_deviceSelector;
  QDoubleSpinBox* m_systemVelocity;
  QPushButton* m_turtleButton;
  QPushButton* m_rabbitButton;
  QPushButton* m_applyButton;

  // Device status display - vertical cards arranged horizontally
  QWidget* m_deviceStatusWidget;
  QHBoxLayout* m_deviceStatusLayout;
  // Map: deviceName -> (axis -> label) for efficient position updates
  std::map<std::string, std::map<std::string, QLabel*>> m_positionLabels;

  // Pivot point display - container for all devices
  QWidget* m_pivotDisplayWidget;
  QVBoxLayout* m_pivotDisplayLayout;

  // Analog input display
  QWidget* m_analogDisplayWidget;
  QVBoxLayout* m_analogDisplayLayout;
  QComboBox* m_analogDeviceSelector;
  QPushButton* m_analogRefreshButton;
  QTimer* m_analogPollTimer;
  std::map<int, QLabel*> m_analogValueLabels;  // Channel -> Label

  // FSM Alignment settings widgets
  QGroupBox* m_fsmSettingsGroup;
  QComboBox* m_fsmAxis1Selector;
  QDoubleSpinBox* m_fsmLength1SpinBox;
  QComboBox* m_fsmAxis2Selector;
  QDoubleSpinBox* m_fsmLength2SpinBox;
  QDoubleSpinBox* m_fsmThresholdSpinBox;
  QDoubleSpinBox* m_fsmDistanceSpinBox;
  std::map<int, QPushButton*> m_fsmAlignButtons;  // Channel -> Align Button

  // Z-iteration FSM settings
  QComboBox* m_zDirectionSelector;    // Z+ or Z-
  QDoubleSpinBox* m_zRangeSpinBox;    // Total Z distance (e.g., 0.02mm)
  QDoubleSpinBox* m_zStepSpinBox;     // Z step size (e.g., 0.002mm)
  std::map<int, QPushButton*> m_zScanButtons;  // Channel -> Z Scan Button

  // FSM Executor (thread-safe alignment execution)
  PIAlignExecutor* m_alignExecutor;
  int m_currentAlignChannel;  // Track which channel is being aligned

  // Z-scan progress display
  QWidget* m_zScanProgressWidget;
  QProgressBar* m_zScanProgressBar;
  QLabel* m_zScanPeakLabel;
};