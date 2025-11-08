// TopBar.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>

struct ApplicationContext;

/**
 * @brief Fixed top bar widget that appears on all pages
 *
 * Features:
 * - Displays product name permanently
 * - Contains JOG button for jog panel access
 * - Height: 50px
 * - Always visible across all pages
 */
class TopBar : public QWidget
{
  Q_OBJECT

public:
  explicit TopBar(ApplicationContext* context, QWidget* parent = nullptr);
  ~TopBar();

  /**
   * @brief Update the displayed product information
   * @param productName Product name to display
   * @param workingPath Working folder path (optional)
   */
  void setProductInfo(const QString& productName, const QString& workingPath = "");

  /**
   * @brief Get the JOG button widget
   * @return Pointer to JOG button
   */
  QPushButton* getJogButton() const { return m_jogButton; }

signals:
  void jogButtonClicked();
  void auxWindowButtonClicked();

private:
  void setupUI();

  ApplicationContext* m_context;
  QLabel* m_productNameLabel;
  QLabel* m_workingPathLabel;
  QPushButton* m_jogButton;
  QPushButton* m_auxWindowButton;
};
