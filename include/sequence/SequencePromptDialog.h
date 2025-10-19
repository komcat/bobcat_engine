// SequencePromptDialog.h
#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QString>

class SequencePromptDialog : public QDialog
{
  Q_OBJECT

public:
  explicit SequencePromptDialog(const QString& title,
    const QString& message,
    QWidget* parent = nullptr);
  ~SequencePromptDialog() = default;

  // Static helper - returns true if user clicked Confirm, false if Cancel
  static bool ShowPrompt(const QString& title, const QString& message, QWidget* parent = nullptr);

private:
  QLabel* m_messageLabel;
  QPushButton* m_confirmButton;
  QPushButton* m_cancelButton;
};