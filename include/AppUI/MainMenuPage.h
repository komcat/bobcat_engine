// MainMenuPage.h - Main menu with 6 big buttons
#pragma once

#include <QWidget>
#include <QToolButton>

/**
 * @brief Main menu page - 6 category buttons in 2x3 grid
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
  void editConfigClicked();
  void futureUpdateClicked();

private:
  void setupUI();
  QToolButton* createMenuButton(const QString& text, const QString& icon = "");

  QToolButton* m_processButton;
  QToolButton* m_recipeButton;
  QToolButton* m_motionButton;
  QToolButton* m_hardwareButton;
  QToolButton* m_editConfigButton;
  QToolButton* m_futureUpdateButton;
};