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

  // Wait countdown
  void setWaitCountdown(int remainingMs, int totalMs);
  void clearWaitCountdown();

  // User prompts
  void showUserPrompt(const QString& title, const QString& message, bool allowCancel);
  void clearUserPrompt();

  // Time tracking
  void startTimer();
  void stopTimer();
  void resetTimer();

  void showCompletionHint();  // ✅ ADD THIS

signals:
  void stopRequested();
  void userPromptResponse(bool confirmed);

private slots:
  void updateElapsedTime();
  void onPromptOkClicked();
  void onPromptCancelClicked();

private:
  void setupUI();
  QString formatElapsedTime(int totalSeconds) const;

  // UI Components
  QProgressBar* m_progressBar;
  QLabel* m_stepLabel;
  QLabel* m_stepNameLabel;
  QLabel* m_statusLabel;
  QLabel* m_elapsedTimeLabel;
  QLabel* m_waitCountdownLabel;  // Wait/Delay countdown display
  QPushButton* m_stopButton;

  // User prompt components
  QLabel* m_promptTitleLabel;
  QLabel* m_promptMessageLabel;
  QPushButton* m_promptOkButton;
  QPushButton* m_promptCancelButton;
  QWidget* m_promptContainer;  // Container to show/hide prompt area

  // Progress tracking
  int m_totalSteps;
  int m_currentStep;

  // Time tracking
  QTime m_startTime;
  QTimer* m_elapsedTimer;
  int m_elapsedSeconds;
};