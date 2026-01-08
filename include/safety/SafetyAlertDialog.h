// SafetyAlertDialog.h
#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QString>

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

  void setupUI(const QString& title, const QString& message);
  void applyStyle();
};
