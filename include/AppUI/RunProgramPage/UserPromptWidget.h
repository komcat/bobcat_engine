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
 * Alignment can be set to Center (default) or TopLeft via setAlignment().
 */
class UserPromptWidget : public QWidget
{
  Q_OBJECT

public:
  enum class Alignment { Center, TopLeft };

  explicit UserPromptWidget(QWidget* parent = nullptr);
  ~UserPromptWidget() = default;

  // Set widget alignment (must be called before first showPrompt, or call rebuildLayout after)
  void setAlignment(Alignment align);
  Alignment alignment() const { return m_alignment; }

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
  void rebuildLayout();

  Alignment m_alignment = Alignment::Center;
  QWidget* m_container = nullptr;
  QLabel* m_titleLabel;
  QLabel* m_messageLabel;
  QPushButton* m_yesButton;
  QPushButton* m_noButton;
};