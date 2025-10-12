// MainMenuPage.h - Main menu with 4 big buttons
#pragma once

#include <QWidget>
#include <QPushButton>

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
  QPushButton* createMenuButton(const QString& text, const QString& icon = "");

  QPushButton* m_processButton;
  QPushButton* m_recipeButton;
  QPushButton* m_motionButton;
  QPushButton* m_hardwareButton;
};