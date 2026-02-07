// SafetyAlertDialog.h
#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTimer>

/**
 * @brief Modal popup dialog for safety alerts
 *
 * Displays critical (red) or warning (yellow) styled alerts.
 * Stays on top of all windows until acknowledged.
 *
 * Usage:
 *   SafetyAlertDialog::ShowAlert("E-Stop!", "Emergency stop pressed.", true, parent);
 */
class SafetyAlertDialog : public QDialog
{
  Q_OBJECT

public:
  /**
   * @brief Constructor
   * @param title Alert title
   * @param message Alert message
   * @param critical True for critical (red), false for warning (yellow)
   * @param parent Parent widget
   */
  explicit SafetyAlertDialog(const QString& title,
                              const QString& message,
                              bool critical,
                              QWidget* parent = nullptr);
  ~SafetyAlertDialog() = default;

  /**
   * @brief Show a safety alert dialog (blocking)
   * @param title Alert title
   * @param message Alert message
   * @param critical True for critical (red), false for warning (yellow)
   * @param parent Parent widget
   * @return true when acknowledged
   */
  static bool ShowAlert(const QString& title,
                        const QString& message,
                        bool critical,
                        QWidget* parent = nullptr);

signals:
  /**
   * @brief Emitted when user acknowledges the alert
   */
  void acknowledged();

private:
  QLabel* m_iconLabel;
  QLabel* m_titleLabel;
  QLabel* m_messageLabel;
  QPushButton* m_acknowledgeButton;
  bool m_critical;

  // Auto-acknowledge countdown
  QTimer* m_countdownTimer = nullptr;
  int m_remainingSeconds = 0;
  static constexpr int CRITICAL_TIMEOUT_S = 30;
  static constexpr int WARNING_TIMEOUT_S = 15;

  // Audio alarm
  QTimer* m_beepTimer = nullptr;
  int m_beepCount = 0;
  static constexpr int BEEP_INTERVAL_MS = 1500;
  static constexpr int WARNING_BEEP_MAX = 3;

  void setupUI(const QString& title, const QString& message);
  void applyStyle();
  void startCountdown();
  void onCountdownTick();
  void startAudioAlarm();
  void stopAudioAlarm();
};
