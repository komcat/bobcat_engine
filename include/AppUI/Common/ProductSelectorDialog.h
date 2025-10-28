// ProductSelectorDialog.h
#pragma once

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QString>
#include <QStringList>

/**
 * @brief Dialog for selecting a product from a list
 */
class ProductSelectorDialog : public QDialog
{
  Q_OBJECT

public:
  explicit ProductSelectorDialog(const QStringList& products, QWidget* parent = nullptr);
  ~ProductSelectorDialog() = default;

  QString getSelectedProduct() const { return m_selectedProduct; }

  static QString selectProduct(const QStringList& products, QWidget* parent = nullptr);

private slots:
  void onProductSelected(QListWidgetItem* item);
  void onProductDoubleClicked(QListWidgetItem* item);
  void onOkClicked();

private:
  void setupUI();

  QStringList m_products;
  QString m_selectedProduct;

  QListWidget* m_productList;
  QPushButton* m_okButton;
  QPushButton* m_cancelButton;
};
