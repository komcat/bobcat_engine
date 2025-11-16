// ConnectionSplashScreen.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QTimer>
#include <QString>
#include <QMap>

/**
 * @brief Multi-phase splash screen for application startup
 *
 * Supports multiple phases: Loading (managers), UI Creation, Hardware Connection, etc.
 * Architecture is flexible to add new phases easily.
 */
class ConnectionSplashScreen : public QWidget
{
  Q_OBJECT

public:
  // Phase types - extensible for future phases
  enum class Phase {
    Loading,      // Manager/system initialization
    UICreation,   // Creating UI pages (future)
    Connecting,   // Hardware connection
    Complete
  };

  explicit ConnectionSplashScreen(QWidget* parent = nullptr);
  ~ConnectionSplashScreen() = default;

  // ========== NEW FLEXIBLE PHASE API ==========

  /**
   * @brief Start a new phase with specific number of steps
   * @param phase The phase type
   * @param title Display title for this phase
   * @param totalSteps Total number of steps in this phase
   */
  void startPhase(Phase phase, const QString& title, int totalSteps);

  /**
   * @brief Update current phase progress
   * @param stepName Name of the step being completed
   * @param success True if step succeeded, false if failed
   */
  void completeStep(const QString& stepName, bool success = true);

  /**
   * @brief Mark step as in-progress (shows spinner/waiting icon)
   * @param stepName Name of the step being started
   */
  void startStep(const QString& stepName);

  /**
   * @brief Mark all phases complete and show success
   */
  void onAllPhasesComplete();

  // ========== BACKWARD COMPATIBLE API ==========
  // (These wrap the new API for existing code)

public slots:
  void updateStatus(QString component, QString status);
  void setProgress(int percent);
  void onConnectionsComplete();

private:
  void setupUI();
  void updatePhaseUI();
  QString getPhaseIcon(Phase phase) const;
  QString getPhaseColor(Phase phase) const;

  // UI Elements
  QLabel* m_titleLabel;
  QLabel* m_statusLabel;
  QProgressBar* m_progressBar;
  QLabel* m_componentListLabel;

  // Phase tracking
  Phase m_currentPhase;
  QString m_phaseTitle;
  int m_totalSteps;
  int m_completedSteps;
  QString m_stepLog;

  // Legacy compatibility
  int m_totalComponents;
  int m_connectedComponents;
  QString m_componentStatuses;
};
