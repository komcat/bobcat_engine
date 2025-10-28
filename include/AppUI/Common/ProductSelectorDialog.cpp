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

  // Set light theme
  setStyleSheet(
    "QDialog { "
    "  background-color: white; "
    "} "
    "QLabel { "
    "  background-color: transparent; "
    "  color: #2c3e50; "
    "} "
    "QListWidget { "
    "  background-color: white; "
    "  color: #2c3e50; "
    "  border: 1px solid #bdc3c7; "
    "  border-radius: 4px; "
    "} "
    "QListWidget::item { "
    "  padding: 8px; "
    "  border-bottom: 1px solid #ecf0f1; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #3498db; "
    "  color: white; "
    "} "
    "QListWidget::item:hover { "
    "  background-color: #e8f4f8; "
    "} "
    "QGroupBox { "
    "  background-color: white; "
    "  color: #2c3e50; "
    "  border: 1px solid #ddd; "
    "  border-radius: 4px; "
    "  margin-top: 10px; "
    "  padding-top: 10px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  subcontrol-position: top left; "
    "  padding: 2px 5px; "
    "  background-color: white; "
    "  color: #2c3e50; "
    "}"
  );

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(15);

  // Header
  QLabel* titleLabel = new QLabel("Select a Product to Load");
  titleLabel->setStyleSheet(
    "font-size: 16px; "
    "font-weight: bold; "
    "color: #2c3e50; "
    "padding: 10px;"
  );
  mainLayout->addWidget(titleLabel);

  // Product list
  QGroupBox* listGroup = new QGroupBox(QString("Available Products (%1)").arg(m_products.size()));
  QVBoxLayout* listLayout = new QVBoxLayout(listGroup);

  m_productList = new QListWidget();
  m_productList->setStyleSheet(
    "QListWidget { "
    "  font-size: 14px; "
    "  padding: 5px; "
    "}"
  );

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
  m_okButton->setMinimumHeight(35);
  m_okButton->setMinimumWidth(100);
  m_okButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3498db; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  font-size: 13px; "
    "  font-weight: bold; "
    "  padding: 8px 20px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #2980b9; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #21618c; "
    "}"
  );
  connect(m_okButton, &QPushButton::clicked, this, &ProductSelectorDialog::onOkClicked);
  buttonLayout->addWidget(m_okButton);

  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setMinimumHeight(35);
  m_cancelButton->setMinimumWidth(100);
  m_cancelButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #95a5a6; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  font-size: 13px; "
    "  font-weight: bold; "
    "  padding: 8px 20px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #7f8c8d; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #6c7a7b; "
    "}"
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
