// MainMenuPage.h - Main menu with 4 big buttons
#pragma once

#include <QWidget>
#include <QToolButton>  // ← CHANGED from QPushButton

/**
 * @brief Main menu page - 4 category buttons
 */
class MainMenuPage : public QWidget
{
  Q_OBJECT

public:
  explicit MainMenuPage(QWidget* parent = nullptr);
  ~MainMenuPage() = default;

signals:
  void processClicked();
  void recipeClicked();
  void motionClicked();
  void hardwareClicked();

private:
  void setupUI();
  QToolButton* createMenuButton(const QString& text, const QString& icon = "");  // ← CHANGED

  QToolButton* m_processButton;      // ← CHANGED
  QToolButton* m_recipeButton;       // ← CHANGED
  QToolButton* m_motionButton;       // ← CHANGED
  QToolButton* m_hardwareButton;     // ← CHANGED
};