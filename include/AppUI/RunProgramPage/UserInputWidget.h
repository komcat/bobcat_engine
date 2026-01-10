// UserInputWidget.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>

/**
 * @brief Inline user input widget for execution panel
 *
 * Replaces blocking modal dialogs with a seamless inline input
 * that appears in the execution panel without blocking the UI.
 */
class UserInputWidget : public QWidget
{
  Q_OBJECT

public:
  explicit UserInputWidget(QWidget* parent = nullptr);
  ~UserInputWidget() = default;

  // Show input prompt with title, message, and default value
  void showInput(const QString& title, const QString& prompt, const QString& defaultValue);

  // Hide input prompt
  void hideInput();

signals:
  // Emitted when user makes a choice
  // first = accepted (true = OK, false = Cancel)
  // second = input text
  void responseReceived(bool accepted, QString inputText);

private slots:
  void onOkClicked();
  void onCancelClicked();
  void onUseDummyClicked();

private:
  void setupUI();

  QLabel* m_titleLabel;
  QLabel* m_promptLabel;
  QLineEdit* m_inputField;
  QPushButton* m_okButton;
  QPushButton* m_cancelButton;
  QPushButton* m_useDummyButton;  // Generate TEST_timestamp serial
};
