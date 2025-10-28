// CreateProductDialog.h
#pragma once

#include <QDialog>
#include <QString>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>

/**
 * @brief Dialog for creating a new product with proper folder structure
 *
 * Creates:
 * - config/[product_name]/
 * - config/[product_name]/recipes/
 * - config/[product_name]/programs/
 * - config/[product_name]/positions.[product_name].json (empty template)
 */
class CreateProductDialog : public QDialog
{
  Q_OBJECT

public:
  explicit CreateProductDialog(QWidget* parent = nullptr);

  /**
   * @brief Get the created product path
   * @return Path to positions file, or empty string if cancelled
   */
  QString getProductPath() const { return m_productPath; }

  /**
   * @brief Static convenience method - show dialog and return product path
   * @param parent Parent widget
   * @return Path to created positions file, or empty string if cancelled
   */
  static QString createNewProduct(QWidget* parent);

private slots:
  void onCreateClicked();
  void onCancel();
  void onNameTextChanged(const QString& text);

private:
  void setupUI();
  bool validateProductName(const QString& name);
  bool createProductStructure(const QString& productName);

  QString m_productPath;

  QLineEdit* m_nameEdit;
  QLabel* m_previewLabel;
  QTextEdit* m_structurePreview;
  QPushButton* m_createButton;
  QPushButton* m_cancelButton;
};
