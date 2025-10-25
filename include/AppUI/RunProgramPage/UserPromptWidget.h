// UserPromptWidget.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QString>

/**
 * @brief Inline user prompt widget for execution panel
 *
 * Replaces blocking modal dialogs with a seamless inline prompt
 * that appears in the execution panel without blocking the UI.
 */
class UserPromptWidget : public QWidget
{
  Q_OBJECT

public:
  explicit UserPromptWidget(QWidget* parent = nullptr);
  ~UserPromptWidget() = default;

  // Show prompt with title and message
  void showPrompt(const QString& title, const QString& message);

  // Hide prompt
  void hidePrompt();

signals:
  // Emitted when user makes a choice
  void responseReceived(bool accepted);  // true = Yes, false = No

private slots:
  void onYesClicked();
  void onNoClicked();

private:
  void setupUI();

  QLabel* m_titleLabel;
  QLabel* m_messageLabel;
  QPushButton* m_yesButton;
  QPushButton* m_noButton;
};