// PlaceholderPage.h - Reusable placeholder page with back button
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>

/**
 * @brief Simple placeholder page with title and back button
 */
class PlaceholderPage : public QWidget
{
  Q_OBJECT

public:
  explicit PlaceholderPage(const QString& title, QWidget* parent = nullptr);
  ~PlaceholderPage() = default;

signals:
  void backClicked();

private:
  void setupUI();

  QString m_title;
  QPushButton* m_backButton;
  QLabel* m_titleLabel;
};