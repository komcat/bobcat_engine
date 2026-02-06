// MainMenuPage.h - Main menu with 6 big buttons
#pragma once

#include <QWidget>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>

struct ApplicationContext;

/**
 * @brief Main menu page - 6 category buttons in 2x3 grid
 */
class MainMenuPage : public QWidget
{
  Q_OBJECT

public:
  explicit MainMenuPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~MainMenuPage() = default;

signals:
  void processClicked();
  void recipeClicked();
  void motionClicked();
  void hardwareClicked();
  void editConfigClicked();
  void futureUpdateClicked();
  void productChanged();  // Emitted when product is loaded or created

private slots:
  void onNewProduct();
  void onLoadProduct();
  void onSaveProduct();
  void onSaveProductAs();
  void onDeleteProduct();

private:
  void setupUI();
  QToolButton* createMenuButton(const QString& text, const QString& icon = "");
  void updateProductDisplay();

  ApplicationContext* m_context;

  // Product controls
  QLabel* m_productLabel;
  QPushButton* m_newProductBtn;
  QPushButton* m_loadProductBtn;
  QPushButton* m_saveProductBtn;
  QPushButton* m_saveProductAsBtn;
  QPushButton* m_deleteProductBtn;

  // Menu buttons
  QToolButton* m_processButton;
  QToolButton* m_recipeButton;
  QToolButton* m_motionButton;
  QToolButton* m_hardwareButton;
  QToolButton* m_editConfigButton;
  QToolButton* m_futureUpdateButton;
};