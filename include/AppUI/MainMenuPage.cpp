// MainMenuPage.cpp
#include "MainMenuPage.h"
#include "ApplicationContext.h"
#include "product_position_manager.h"
#include "FileSelectorDialog.h"
#include "CreateProductDialog.h"
#include "ProductSelectorDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QIcon>
#include <QMessageBox>
#include <QInputDialog>
#include <QSettings>
#include <QFrame>
#include <QDir>
#include <QFile>
#include <QFileInfo>

MainMenuPage::MainMenuPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_productLabel(nullptr)
  , m_newProductBtn(nullptr)
  , m_loadProductBtn(nullptr)
  , m_saveProductBtn(nullptr)
  , m_saveProductAsBtn(nullptr)
{
  setupUI();
  updateProductDisplay();
}

void MainMenuPage::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(50, 50, 50, 50);
  mainLayout->setSpacing(30);

  // Title
  QLabel* titleLabel = new QLabel("Bobcat Engine");
  titleLabel->setAlignment(Qt::AlignCenter);
  titleLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 24px; "
    "  font-weight: bold; "
    "  color: #ffffff; "
    "  padding: 10px; "
    "}");
  mainLayout->addWidget(titleLabel);

  // ========================================
  // Product Controls Section
  // ========================================
  QFrame* productFrame = new QFrame();
  productFrame->setStyleSheet(
    "QFrame {"
    "  background-color: #2d3e5f;"
    "  border: 2px solid #3d5a8c;"
    "  border-radius: 8px;"
    "  padding: 15px;"
    "}"
  );

  QVBoxLayout* productLayout = new QVBoxLayout(productFrame);
  productLayout->setSpacing(10);

  // Product label
  m_productLabel = new QLabel("Product: None");
  m_productLabel->setAlignment(Qt::AlignCenter);
  m_productLabel->setStyleSheet(
    "QLabel {"
    "  font-size: 16px;"
    "  font-weight: bold;"
    "  color: #ffffff;"
    "  background-color: transparent;"
    "  border: none;"
    "  padding: 5px;"
    "}"
  );
  productLayout->addWidget(m_productLabel);

  // Buttons row
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(15);

  m_newProductBtn = new QPushButton("New Product");
  m_newProductBtn->setMinimumHeight(40);
  m_newProductBtn->setStyleSheet(
    "QPushButton {"
    "  background-color: #9b59b6;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 5px;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "  padding: 8px 20px;"
    "}"
    "QPushButton:hover {"
    "  background-color: #8e44ad;"
    "}"
    "QPushButton:pressed {"
    "  background-color: #7d3c98;"
    "}"
  );

  m_loadProductBtn = new QPushButton("Load Product");
  m_loadProductBtn->setMinimumHeight(40);
  m_loadProductBtn->setStyleSheet(
    "QPushButton {"
    "  background-color: #3498db;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 5px;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "  padding: 8px 20px;"
    "}"
    "QPushButton:hover {"
    "  background-color: #5dade2;"
    "}"
    "QPushButton:pressed {"
    "  background-color: #2874a6;"
    "}"
  );

  m_saveProductBtn = new QPushButton("Save");
  m_saveProductBtn->setMinimumHeight(40);
  m_saveProductBtn->setEnabled(false);
  m_saveProductBtn->setStyleSheet(
    "QPushButton {"
    "  background-color: #27ae60;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 5px;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "  padding: 8px 20px;"
    "}"
    "QPushButton:hover {"
    "  background-color: #2ecc71;"
    "}"
    "QPushButton:pressed {"
    "  background-color: #1e8449;"
    "}"
    "QPushButton:disabled {"
    "  background-color: #7f8c8d;"
    "  color: #bdc3c7;"
    "}"
  );

  m_saveProductAsBtn = new QPushButton("Save As...");
  m_saveProductAsBtn->setMinimumHeight(40);
  m_saveProductAsBtn->setStyleSheet(
    "QPushButton {"
    "  background-color: #f39c12;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 5px;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "  padding: 8px 20px;"
    "}"
    "QPushButton:hover {"
    "  background-color: #f1c40f;"
    "}"
    "QPushButton:pressed {"
    "  background-color: #d68910;"
    "}"
  );

  buttonLayout->addStretch();
  buttonLayout->addWidget(m_newProductBtn);
  buttonLayout->addWidget(m_loadProductBtn);
  buttonLayout->addWidget(m_saveProductBtn);
  buttonLayout->addWidget(m_saveProductAsBtn);
  buttonLayout->addStretch();

  productLayout->addLayout(buttonLayout);
  mainLayout->addWidget(productFrame);

  // Connect product button signals
  connect(m_newProductBtn, &QPushButton::clicked, this, &MainMenuPage::onNewProduct);
  connect(m_loadProductBtn, &QPushButton::clicked, this, &MainMenuPage::onLoadProduct);
  connect(m_saveProductBtn, &QPushButton::clicked, this, &MainMenuPage::onSaveProduct);
  connect(m_saveProductAsBtn, &QPushButton::clicked, this, &MainMenuPage::onSaveProductAs);

  // Add stretch to center buttons vertically
  mainLayout->addStretch();

  // Button grid (3x2 - 3 columns, 2 rows)
  QGridLayout* buttonGrid = new QGridLayout();
  buttonGrid->setSpacing(30);

  // Create buttons WITH ICONS
  m_processButton = createMenuButton("Process", "assets/icons/process.png");
  m_recipeButton = createMenuButton("Recipe", "assets/icons/recipe.png");
  m_motionButton = createMenuButton("Motion", "assets/icons/motion.png");
  m_hardwareButton = createMenuButton("Hardware &\nInstrument", "assets/icons/hardware.png");
  m_editConfigButton = createMenuButton("Edit Config", "assets/icons/config.png");
  m_futureUpdateButton = createMenuButton("(future update)", "assets/icons/future.png");

  // Add to grid (3 columns, 2 rows)
  buttonGrid->addWidget(m_processButton, 0, 0);
  buttonGrid->addWidget(m_recipeButton, 0, 1);
  buttonGrid->addWidget(m_motionButton, 0, 2);
  buttonGrid->addWidget(m_hardwareButton, 1, 0);
  buttonGrid->addWidget(m_editConfigButton, 1, 1);
  buttonGrid->addWidget(m_futureUpdateButton, 1, 2);

  mainLayout->addLayout(buttonGrid);
  mainLayout->addStretch();

  // Connect signals
  connect(m_processButton, &QToolButton::clicked, this, &MainMenuPage::processClicked);
  connect(m_recipeButton, &QToolButton::clicked, this, &MainMenuPage::recipeClicked);
  connect(m_motionButton, &QToolButton::clicked, this, &MainMenuPage::motionClicked);
  connect(m_hardwareButton, &QToolButton::clicked, this, &MainMenuPage::hardwareClicked);
  connect(m_editConfigButton, &QToolButton::clicked, this, &MainMenuPage::editConfigClicked);
  connect(m_futureUpdateButton, &QToolButton::clicked, this, &MainMenuPage::futureUpdateClicked);

  // Set dark blue background
  setStyleSheet("QWidget { background-color: #1a1f2e; }");
}

QToolButton* MainMenuPage::createMenuButton(const QString& text, const QString& iconPath)
{
  QToolButton* button = new QToolButton();
  button->setMinimumSize(200, 180);
  button->setMaximumSize(400, 250);
  button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  // Load icon if path provided
  if (!iconPath.isEmpty()) {
    QIcon icon(iconPath);

    // Check if icon loaded successfully
    if (!icon.isNull()) {
      button->setIcon(icon);
      button->setIconSize(QSize(48, 48));  // Icon display size
    }
    else {
      // Icon failed to load - print warning
      qWarning() << "Failed to load icon:" << iconPath;
    }
  }

  // Set text
  button->setText(text);

  // Style for icon-above-text layout
  button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

  // Button styling
  button->setStyleSheet(
    "QToolButton { "
    "  background-color: #2d3e5f; "
    "  color: white; "
    "  border: 2px solid #3d5a8c; "
    "  border-radius: 10px; "
    "  font-size: 18px; "
    "  font-weight: bold; "
    "  padding: 20px; "
    "} "
    "QToolButton:hover { "
    "  background-color: #3d5a8c; "
    "  border-color: #5a8fd6; "
    "} "
    "QToolButton:pressed { "
    "  background-color: #1d2e4f; "
    "}");

  return button;
}

// ============================================================================
// PRODUCT MANAGEMENT
// ============================================================================

void MainMenuPage::onNewProduct()
{
  if (!m_context || !m_context->productPositionManager) {
    QMessageBox::critical(this, "Error", "Product manager not initialized");
    return;
  }

  // Show create product dialog
  QString productPath = CreateProductDialog::createNewProduct(this);

  if (productPath.isEmpty()) {
    // User cancelled
    return;
  }

  // Load the newly created product
  if (m_context->productPositionManager->LoadProduct(productPath.toStdString())) {
    // Export positions to motion manager if available
    if (m_context->motionManager) {
      m_context->productPositionManager->ExportToMotionManager(*m_context->motionManager);
    }

    // Save to application-wide setting (shared across all pages)
    QSettings settings("Fabrinet", "BobcatEngine");
    settings.setValue("Application/lastProductFile", productPath);

    updateProductDisplay();

    // Notify other pages that product has changed
    emit productChanged();

    QMessageBox::information(this, "Product Ready",
      QString("New product created and loaded successfully!\n\n"
              "Product: %1\n\n"
              "You can now:\n"
              "• Add positions in Motion page\n"
              "• Create recipes in Recipe page\n"
              "• Create programs in Process page")
        .arg(QString::fromStdString(m_context->productPositionManager->GetCurrentProductName())));
  }
  else {
    QMessageBox::critical(this, "Error",
      "Product folder was created, but failed to load the positions file.");
  }
}

void MainMenuPage::updateProductDisplay()
{
  if (!m_productLabel || !m_context || !m_context->productPositionManager) {
    return;
  }

  if (m_context->productPositionManager->HasLoadedProduct()) {
    QString productName = QString::fromStdString(
      m_context->productPositionManager->GetCurrentProductName()
    );
    m_productLabel->setText("Product: " + productName);
    m_saveProductBtn->setEnabled(true);
  }
  else {
    m_productLabel->setText("Product: None");
    m_saveProductBtn->setEnabled(false);
  }
}

void MainMenuPage::onLoadProduct()
{
  if (!m_context || !m_context->productPositionManager) {
    QMessageBox::critical(this, "Error", "Product manager not initialized");
    return;
  }

  // Get list of product folders in config directory
  QDir configDir("config");
  if (!configDir.exists()) {
    QMessageBox::critical(this, "Error", "Config directory not found");
    return;
  }

  QStringList productFolders = configDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

  if (productFolders.isEmpty()) {
    QMessageBox::warning(this, "No Products",
      "No product folders found in config directory.\n\n"
      "Create a new product using 'New Product' button.");
    return;
  }

  // Show dialog to select product folder
  QString selectedProduct = ProductSelectorDialog::selectProduct(productFolders, this);

  if (selectedProduct.isEmpty()) {
    return;
  }

  // Construct path to positions file: config/[product]/positions.[product].json
  QString filePath = QString("config/%1/positions.%2.json")
    .arg(selectedProduct)
    .arg(selectedProduct);

  // Check if file exists
  if (!QFile::exists(filePath)) {
    QMessageBox::critical(this, "Error",
      QString("Positions file not found:\n%1\n\n"
              "Expected format: positions.%2.json")
        .arg(filePath)
        .arg(selectedProduct));
    return;
  }

  if (m_context->productPositionManager->LoadProduct(filePath.toStdString())) {
    // Export positions to motion manager if available
    if (m_context->motionManager) {
      m_context->productPositionManager->ExportToMotionManager(*m_context->motionManager);
    }

    // Save to application-wide setting (shared across all pages)
    QSettings settings("Fabrinet", "BobcatEngine");
    settings.setValue("Application/lastProductFile", filePath);

    updateProductDisplay();

    // Notify other pages that product has changed
    emit productChanged();

    QMessageBox::information(this, "Success",
      QString("Product loaded successfully!\n\n"
              "Product: %1\nFile: %2")
        .arg(selectedProduct)
        .arg(QFileInfo(filePath).fileName()));
  }
  else {
    QMessageBox::critical(this, "Error",
      "Failed to load product from:\n" + filePath);
  }
}

void MainMenuPage::onSaveProduct()
{
  if (!m_context || !m_context->productPositionManager) {
    QMessageBox::critical(this, "Error", "Product manager not initialized");
    return;
  }

  if (!m_context->productPositionManager->HasLoadedProduct()) {
    QMessageBox::warning(this, "No Product",
      "No product loaded. Use 'Save As...' to create a new product file.");
    return;
  }

  // Import current positions from motion manager if available
  if (m_context->motionManager) {
    m_context->productPositionManager->ImportFromMotionManager(*m_context->motionManager);
  }

  if (m_context->productPositionManager->SaveProduct()) {
    QMessageBox::information(this, "Success", "Product saved successfully");
  }
  else {
    QMessageBox::critical(this, "Error", "Failed to save product file");
  }
}

void MainMenuPage::onSaveProductAs()
{
  if (!m_context || !m_context->productPositionManager) {
    QMessageBox::critical(this, "Error", "Product manager not initialized");
    return;
  }

  bool ok;
  QString productName = QInputDialog::getText(
    this,
    "Save Product As",
    "Enter product name (without .json):",
    QLineEdit::Normal,
    m_context->productPositionManager->HasLoadedProduct()
      ? QString::fromStdString(m_context->productPositionManager->GetCurrentProductName())
      : "",
    &ok
  );

  if (!ok || productName.isEmpty()) {
    return;
  }

  if (productName.endsWith(".json")) {
    productName = productName.left(productName.length() - 5);
  }

  // Import current positions from motion manager if available
  if (m_context->motionManager) {
    m_context->productPositionManager->ImportFromMotionManager(*m_context->motionManager);
  }

  if (m_context->productPositionManager->SaveProductAs(productName.toStdString())) {
    // Save to application-wide setting (shared across all pages)
    QSettings settings("Fabrinet", "BobcatEngine");
    settings.setValue("Application/lastProductFile",
      QString::fromStdString(m_context->productPositionManager->GetCurrentProductPath()));

    updateProductDisplay();

    QMessageBox::information(this, "Success",
      "Product saved as: " + productName);
  }
  else {
    QMessageBox::critical(this, "Error", "Failed to save product file");
  }
}