// RecipeProgressWidget.h
#pragma once

#include <QWidget>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QTime>
#include <QTimer>

/**
 * @brief Progress panel shown during recipe execution
 *
 * Features:
 * - Progress bar with step count (e.g., "Step 2/5 - 40%")
 * - Current step name display
 * - Elapsed time counter (HH:MM:SS)
 * - Stop execution button
 * - Simple status text
 *
 * This is a pure UI widget with no business logic
 */
class RecipeProgressWidget : public QWidget
{
  Q_OBJECT

public:
  explicit RecipeProgressWidget(QWidget* parent = nullptr);
  ~RecipeProgressWidget();

  // Progress updates
  void setTotalSteps(int total);
  void setCurrentStep(int current, const QString& stepName);
  void setStatusText(const QString& status);

  // Time tracking
  void startTimer();
  void stopTimer();
  void resetTimer();

  void showCompletionHint();  // ✅ ADD THIS

signals:
  void stopRequested();

private slots:
  void updateElapsedTime();

private:
  void setupUI();
  QString formatElapsedTime(int totalSeconds) const;

  // UI Components
  QProgressBar* m_progressBar;
  QLabel* m_stepLabel;
  QLabel* m_stepNameLabel;
  QLabel* m_statusLabel;
  QLabel* m_elapsedTimeLabel;
  QPushButton* m_stopButton;

  // Progress tracking
  int m_totalSteps;
  int m_currentStep;

  // Time tracking
  QTime m_startTime;
  QTimer* m_elapsedTimer;
  int m_elapsedSeconds;
};