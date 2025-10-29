// ProductSelectorDialog.cpp
#include "ProductSelectorDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>

ProductSelectorDialog::ProductSelectorDialog(const QStringList& products, QWidget* parent)
  : QDialog(parent)
  , m_products(products)
  , m_selectedProduct("")
{
  setupUI();
}

QString ProductSelectorDialog::selectProduct(const QStringList& products, QWidget* parent)
{
  ProductSelectorDialog dialog(products, parent);

  if (dialog.exec() == QDialog::Accepted) {
    return dialog.getSelectedProduct();
  }

  return QString();
}

void ProductSelectorDialog::setupUI()
{
  setWindowTitle("Load Product");
  setMinimumSize(500, 400);
  resize(550, 450);

  // ✅ No dialog stylesheet - inherits dark theme from MainWindow

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(15);

  // Header
  QLabel* titleLabel = new QLabel("Select a Product to Load");
  titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; padding: 10px;");  // ✅ Only custom sizing
  mainLayout->addWidget(titleLabel);

  // Product list
  QGroupBox* listGroup = new QGroupBox(QString("Available Products (%1)").arg(m_products.size()));
  QVBoxLayout* listLayout = new QVBoxLayout(listGroup);

  m_productList = new QListWidget();
  m_productList->setStyleSheet("font-size: 14px; padding: 5px;");  // ✅ Only custom sizing

  // Add products to list
  for (const QString& product : m_products) {
    QListWidgetItem* item = new QListWidgetItem(product);
    item->setData(Qt::UserRole, product);
    m_productList->addItem(item);
  }

  // Select first item by default
  if (m_productList->count() > 0) {
    m_productList->setCurrentRow(0);
    m_selectedProduct = m_products.first();
  }

  connect(m_productList, &QListWidget::itemClicked, this, &ProductSelectorDialog::onProductSelected);
  connect(m_productList, &QListWidget::itemDoubleClicked, this, &ProductSelectorDialog::onProductDoubleClicked);

  listLayout->addWidget(m_productList);
  mainLayout->addWidget(listGroup);

  // Buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_okButton = new QPushButton("OK");
  m_okButton->setObjectName("okButton");  // ✅ Green success button
  m_okButton->setMinimumHeight(35);
  m_okButton->setMinimumWidth(100);
  m_okButton->setStyleSheet(
    "QPushButton#okButton { background-color: #27ae60; font-size: 13px; }"
    "QPushButton#okButton:hover { background-color: #2ecc71; }"
  );
  connect(m_okButton, &QPushButton::clicked, this, &ProductSelectorDialog::onOkClicked);
  buttonLayout->addWidget(m_okButton);

  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setObjectName("cancelButton");  // ✅ Gray neutral button
  m_cancelButton->setMinimumHeight(35);
  m_cancelButton->setMinimumWidth(100);
  m_cancelButton->setStyleSheet(
    "QPushButton#cancelButton { background-color: #555555; font-size: 13px; }"
    "QPushButton#cancelButton:hover { background-color: #666666; }"
  );
  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  buttonLayout->addWidget(m_cancelButton);

  mainLayout->addLayout(buttonLayout);
}

void ProductSelectorDialog::onProductSelected(QListWidgetItem* item)
{
  if (item) {
    m_selectedProduct = item->data(Qt::UserRole).toString();
  }
}

void ProductSelectorDialog::onProductDoubleClicked(QListWidgetItem* item)
{
  if (item) {
    m_selectedProduct = item->data(Qt::UserRole).toString();
    accept();
  }
}

void ProductSelectorDialog::onOkClicked()
{
  if (!m_selectedProduct.isEmpty()) {
    accept();
  }
}
