// PneumaticControlPage.h - Pneumatic slide control page
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <cstdint>
#include <string>

#include "Pneumatic_Types.h"

struct ApplicationContext;
class PneumaticManager;

/**
 * @brief Pneumatic Control page with slide list and control panel
 *
 * Layout:
 * - Left panel (30%): List of pneumatic slides
 * - Right panel (70%): Selected slide's control panel with status and buttons
 */
class PneumaticControlPage : public QWidget
{
  Q_OBJECT

public:
  explicit PneumaticControlPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~PneumaticControlPage();

protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private slots:
  void onSlideSelected();
  void onRefreshClicked();
  void onExtendClicked();
  void onRetractClicked();
  void onStopClicked();
  void updateStatus();
  void onPositionChanged(const QString& slideName);  // ✅ ADD THIS

private:
  void setupUI();
  void createSlideListPanel();
  void createControlPanel();
  void refreshSlideList();
  void updateSlideStatus();
  void updatePositionIndicator(const std::string& position);

  PneumaticManager* getPneumaticManager();

  ApplicationContext* m_context;

  // Left panel - slide list
  QWidget* m_slideListPanel;
  QListWidget* m_slideList;
  QPushButton* m_refreshButton;

  // Right panel - control panel
  QWidget* m_controlPanel;
  QLabel* m_slideNameLabel;
  QLabel* m_connectionStatusLabel;

  // Status display
  QLabel* m_positionLabel;
  QLabel* m_positionValueLabel;
  QProgressBar* m_positionBar;
  QLabel* m_outputStatusLabel;
  QLabel* m_extendedSensorLabel;
  QLabel* m_retractedSensorLabel;
  QLabel* m_timeoutLabel;

  // Control buttons
  QPushButton* m_extendButton;
  QPushButton* m_retractButton;
  QPushButton* m_stopButton;
  QLabel* m_elapsedTimeLabel;      // ✅ ADD THIS - shows elapsed time
  QLabel* m_elapsedTimeValueLabel; // ✅ ADD THIS - time value

  // Status update
  QTimer* m_updateTimer;

  // Current selection
  std::string m_selectedSlide;

  // Change detection so the poll only touches widgets whose value flipped.
  // Reset m_haveLastStatus = false to force a full repaint on the next tick
  // (used when the selected slide changes).
  PneumaticPosition m_lastPosition = PneumaticPosition::UNKNOWN;
  bool     m_lastOutputActive = false;
  bool     m_lastExtendedSensor = false;
  bool     m_lastRetractedSensor = false;
  uint32_t m_lastMoveTimeMs = 0;
  bool     m_haveLastStatus = false;

  static constexpr int UPDATE_INTERVAL_MS = 100;


};