// QuickOutputWidget.h - UI for quick output control
#pragma once

#include <QWidget>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <vector>

class QuickOutputManager;
class SafetyWatchManager;

/**
 * @brief Output control button with state display
 */
class OutputButton : public QWidget
{
  Q_OBJECT

public:
  explicit OutputButton(const QString& displayName, size_t index, QWidget* parent = nullptr);

  void setOutputState(bool isOn, bool isValid);
  bool isOn() const { return m_isOn; }
  size_t getIndex() const { return m_index; }

signals:
  void clicked(size_t index);

private:
  void updateStyle();

  QPushButton* m_button;
  QLabel* m_statusLabel;
  size_t m_index;
  bool m_isOn;
  bool m_isValid;
};

/**
 * @brief Quick Output Widget - UI for output pin control
 *
 * Displays a grid of output buttons that users can click to toggle.
 * Shows current state (ON/OFF) with visual indicators.
 * Polls for status updates automatically.
 */
class QuickOutputWidget : public QWidget
{
  Q_OBJECT

public:
  explicit QuickOutputWidget(QuickOutputManager* manager, QWidget* parent = nullptr);
  ~QuickOutputWidget();

  void setSafetyWatchManager(SafetyWatchManager* safetyMgr);

  // Start/stop status polling
  void startPolling(int intervalMs = 200);
  void stopPolling();

  // Refresh the output list (call after adding/removing outputs)
  void refreshOutputList();

signals:
  void outputToggled(size_t index, bool newState);

protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private slots:
  void onOutputButtonClicked(size_t index);
  void onRefreshButtonClicked();
  void onTurnOffAllClicked();
  void updateOutputStatuses();

private:
  void setupUI();
  void createOutputButtons();
  void clearOutputButtons();

  QuickOutputManager* m_manager;
  SafetyWatchManager* m_safetyWatchManager = nullptr;

  // UI Components
  QGridLayout* m_outputGrid;
  QPushButton* m_refreshButton;
  QPushButton* m_turnOffAllButton;
  QLabel* m_statusLabel;

  // Output buttons
  std::vector<OutputButton*> m_outputButtons;

  // Polling
  QTimer* m_pollTimer;
  int m_pollInterval;
};
