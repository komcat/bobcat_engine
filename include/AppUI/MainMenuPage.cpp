// MainMenuPage.cpp
#include "MainMenuPage.h"
#include "ApplicationContext.h"
#include "product_position_manager.h"
#include "FileSelectorDialog.h"
#include "CreateProductDialog.h"
#include "ProductSelectorDialog.h"
#include "version.h"
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
  , m_deleteProductBtn(nullptr)
{
  setupUI();
  updateProductDisplay();
}

void MainMenuPage::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(50, 50, 50, 50);
  mainLayout->setSpacing(30);

  // Title with version
  QString titleText = QString("Bobcat Engine v%1").arg(BobcatVersion::VERSION_STRING);
  QLabel* titleLabel = new QLabel(titleText);
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
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ab69c6, stop:1 #9b59b6);"
    "  color: white;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "  padding: 8px 20px;"
    "  border-top: 2px solid rgba(255, 255, 255, 0.5);"
    "  border-left: 2px solid rgba(255, 255, 255, 0.5);"
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-right: 2px solid rgba(0, 0, 0, 0.5);"
    "}"
    "QPushButton:hover {"
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #bb79d6, stop:1 #ab69c6);"
    "  border-top: 2px solid rgba(255, 255, 255, 0.6);"
    "  border-left: 2px solid rgba(255, 255, 255, 0.6);"
    "}"
    "QPushButton:pressed {"
    "  background-color: #8e44ad;"
    "  border-top: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-left: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3);"
    "  border-right: 2px solid rgba(255, 255, 255, 0.3);"
    "  padding-top: 10px;"
    "  padding-left: 22px;"
    "}"
  );

  m_loadProductBtn = new QPushButton("Load Product");
  m_loadProductBtn->setMinimumHeight(40);
  m_loadProductBtn->setStyleSheet(
    "QPushButton {"
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5dade2, stop:1 #3498db);"
    "  color: white;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "  padding: 8px 20px;"
    "  border-top: 2px solid rgba(255, 255, 255, 0.5);"
    "  border-left: 2px solid rgba(255, 255, 255, 0.5);"
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-right: 2px solid rgba(0, 0, 0, 0.5);"
    "}"
    "QPushButton:hover {"
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6dbdf2, stop:1 #5dade2);"
    "  border-top: 2px solid rgba(255, 255, 255, 0.6);"
    "  border-left: 2px solid rgba(255, 255, 255, 0.6);"
    "}"
    "QPushButton:pressed {"
    "  background-color: #2874a6;"
    "  border-top: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-left: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3);"
    "  border-right: 2px solid rgba(255, 255, 255, 0.3);"
    "  padding-top: 10px;"
    "  padding-left: 22px;"
    "}"
  );

  m_saveProductBtn = new QPushButton("Save");
  m_saveProductBtn->setMinimumHeight(40);
  m_saveProductBtn->setEnabled(false);
  m_saveProductBtn->setStyleSheet(
    "QPushButton {"
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2ecc71, stop:1 #27ae60);"
    "  color: white;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "  padding: 8px 20px;"
    "  border-top: 2px solid rgba(255, 255, 255, 0.5);"
    "  border-left: 2px solid rgba(255, 255, 255, 0.5);"
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-right: 2px solid rgba(0, 0, 0, 0.5);"
    "}"
    "QPushButton:hover {"
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3edc81, stop:1 #2ecc71);"
    "  border-top: 2px solid rgba(255, 255, 255, 0.6);"
    "  border-left: 2px solid rgba(255, 255, 255, 0.6);"
    "}"
    "QPushButton:pressed {"
    "  background-color: #1e8449;"
    "  border-top: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-left: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3);"
    "  border-right: 2px solid rgba(255, 255, 255, 0.3);"
    "  padding-top: 10px;"
    "  padding-left: 22px;"
    "}"
    "QPushButton:disabled {"
    "  background-color: #7f8c8d;"
    "  color: #bdc3c7;"
    "  border-top: 1px solid rgba(255, 255, 255, 0.2);"
    "  border-left: 1px solid rgba(255, 255, 255, 0.2);"
    "  border-bottom: 1px solid rgba(0, 0, 0, 0.3);"
    "  border-right: 1px solid rgba(0, 0, 0, 0.3);"
    "}"
  );

  m_saveProductAsBtn = new QPushButton("Save As...");
  m_saveProductAsBtn->setMinimumHeight(40);
  m_saveProductAsBtn->setStyleSheet(
    "QPushButton {"
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f1c40f, stop:1 #f39c12);"
    "  color: white;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "  padding: 8px 20px;"
    "  border-top: 2px solid rgba(255, 255, 255, 0.5);"
    "  border-left: 2px solid rgba(255, 255, 255, 0.5);"
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-right: 2px solid rgba(0, 0, 0, 0.5);"
    "}"
    "QPushButton:hover {"
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffd41f, stop:1 #f1c40f);"
    "  border-top: 2px solid rgba(255, 255, 255, 0.6);"
    "  border-left: 2px solid rgba(255, 255, 255, 0.6);"
    "}"
    "QPushButton:pressed {"
    "  background-color: #d68910;"
    "  border-top: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-left: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3);"
    "  border-right: 2px solid rgba(255, 255, 255, 0.3);"
    "  padding-top: 10px;"
    "  padding-left: 22px;"
    "}"
  );

  m_deleteProductBtn = new QPushButton("Delete Product");
  m_deleteProductBtn->setMinimumHeight(40);
  m_deleteProductBtn->setEnabled(false);
  m_deleteProductBtn->setStyleSheet(
    "QPushButton {"
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ec7063, stop:1 #e74c3c);"
    "  color: white;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "  padding: 8px 20px;"
    "  border-top: 2px solid rgba(255, 255, 255, 0.5);"
    "  border-left: 2px solid rgba(255, 255, 255, 0.5);"
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-right: 2px solid rgba(0, 0, 0, 0.5);"
    "}"
    "QPushButton:hover {"
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fc8073, stop:1 #ec7063);"
    "  border-top: 2px solid rgba(255, 255, 255, 0.6);"
    "  border-left: 2px solid rgba(255, 255, 255, 0.6);"
    "}"
    "QPushButton:pressed {"
    "  background-color: #c0392b;"
    "  border-top: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-left: 2px solid rgba(0, 0, 0, 0.5);"
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3);"
    "  border-right: 2px solid rgba(255, 255, 255, 0.3);"
    "  padding-top: 10px;"
    "  padding-left: 22px;"
    "}"
    "QPushButton:disabled {"
    "  background-color: #7f8c8d;"
    "  color: #bdc3c7;"
    "  border-top: 1px solid rgba(255, 255, 255, 0.2);"
    "  border-left: 1px solid rgba(255, 255, 255, 0.2);"
    "  border-bottom: 1px solid rgba(0, 0, 0, 0.3);"
    "  border-right: 1px solid rgba(0, 0, 0, 0.3);"
    "}"
  );

  buttonLayout->addStretch();
  buttonLayout->addWidget(m_newProductBtn);
  buttonLayout->addWidget(m_loadProductBtn);
  buttonLayout->addWidget(m_saveProductBtn);
  buttonLayout->addWidget(m_saveProductAsBtn);
  buttonLayout->addWidget(m_deleteProductBtn);
  buttonLayout->addStretch();

  productLayout->addLayout(buttonLayout);
  mainLayout->addWidget(productFrame);

  // Connect product button signals
  connect(m_newProductBtn, &QPushButton::clicked, this, &MainMenuPage::onNewProduct);
  connect(m_loadProductBtn, &QPushButton::clicked, this, &MainMenuPage::onLoadProduct);
  connect(m_saveProductBtn, &QPushButton::clicked, this, &MainMenuPage::onSaveProduct);
  connect(m_saveProductAsBtn, &QPushButton::clicked, this, &MainMenuPage::onSaveProductAs);
  connect(m_deleteProductBtn, &QPushButton::clicked, this, &MainMenuPage::onDeleteProduct);

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
  m_editConfigButton = createMenuButton("Settings", "assets/icons/config.png");
  m_futureUpdateButton = createMenuButton("Vision Manager", "assets/icons/future.png");

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

  // Button styling with 3D Windows appearance
  button->setStyleSheet(
    "QToolButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4d6e8f, stop:1 #2d3e5f); "
    "  color: white; "
    "  font-size: 18px; "
    "  font-weight: bold; "
    "  padding: 20px; "
    "  border-top: 3px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 3px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 3px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 3px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QToolButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5d7e9f, stop:1 #3d5a8c); "
    "  border-top: 3px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 3px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QToolButton:pressed { "
    "  background-color: #1d2e4f; "
    "  border-top: 3px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 3px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 3px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 3px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 23px; "
    "  padding-left: 23px; "
    "}");

  return button;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

namespace {
  const QString DIALOG_STYLESHEET =
    "QMessageBox { background-color: #f0f0f0; color: #000000; } "
    "QInputDialog { background-color: #f0f0f0; } "
    "QLabel { color: #000000; font-size: 12px; background-color: transparent; } "
    "QLineEdit { background-color: white; color: #000000; border: 1px solid #999999; "
    "border-radius: 3px; padding: 5px; font-size: 12px; } "
    "QPushButton { background-color: #e0e0e0; color: #000000; border: 1px solid #999999; "
    "border-radius: 4px; padding: 6px 20px; font-size: 12px; min-width: 80px; } "
    "QPushButton:hover { background-color: #d0d0d0; border-color: #666666; } "
    "QPushButton:pressed { background-color: #c0c0c0; } "
    "QPushButton:default { background-color: #5a8fd6; color: white; border-color: #4a7fc6; } "
    "QPushButton:default:hover { background-color: #4a7fc6; }";
}

// ============================================================================
// PRODUCT MANAGEMENT
// ============================================================================

void MainMenuPage::onNewProduct()
{
  if (!m_context || !m_context->productPositionManager) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setWindowTitle("Error");
    msgBox.setText("Product manager not initialized");
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
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

    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setWindowTitle("Product Ready");
    msgBox.setText(QString("New product created and loaded successfully!\n\n"
              "Product: %1\n\n"
              "You can now:\n"
              "• Add positions in Motion page\n"
              "• Create recipes in Recipe page\n"
              "• Create programs in Process page")
        .arg(QString::fromStdString(m_context->productPositionManager->GetCurrentProductName())));
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
  }
  else {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setWindowTitle("Error");
    msgBox.setText("Product folder was created, but failed to load the positions file.");
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
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
    m_deleteProductBtn->setEnabled(true);
  }
  else {
    m_productLabel->setText("Product: None");
    m_saveProductBtn->setEnabled(false);
    m_deleteProductBtn->setEnabled(false);
  }
}

void MainMenuPage::onLoadProduct()
{
  if (!m_context || !m_context->productPositionManager) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setWindowTitle("Error");
    msgBox.setText("Product manager not initialized");
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
    return;
  }

  // Get list of product folders in config directory
  QDir configDir("config");
  if (!configDir.exists()) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setWindowTitle("Error");
    msgBox.setText("Config directory not found");
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
    return;
  }

  QStringList productFolders = configDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

  if (productFolders.isEmpty()) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle("No Products");
    msgBox.setText("No product folders found in config directory.\n\n"
      "Create a new product using 'New Product' button.");
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
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
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setWindowTitle("Error");
    msgBox.setText(QString("Positions file not found:\n%1\n\n"
              "Expected format: positions.%2.json")
        .arg(filePath)
        .arg(selectedProduct));
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
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

    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setWindowTitle("Success");
    msgBox.setText(QString("Product loaded successfully!\n\n"
              "Product: %1\nFile: %2")
        .arg(selectedProduct)
        .arg(QFileInfo(filePath).fileName()));
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
  }
  else {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setWindowTitle("Error");
    msgBox.setText("Failed to load product from:\n" + filePath);
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
  }
}

void MainMenuPage::onSaveProduct()
{
  if (!m_context || !m_context->productPositionManager) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setWindowTitle("Error");
    msgBox.setText("Product manager not initialized");
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
    return;
  }

  if (!m_context->productPositionManager->HasLoadedProduct()) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle("No Product");
    msgBox.setText("No product loaded. Use 'Save As...' to create a new product file.");
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
    return;
  }

  // Import current positions from motion manager if available
  if (m_context->motionManager) {
    m_context->productPositionManager->ImportFromMotionManager(*m_context->motionManager);
  }

  if (m_context->productPositionManager->SaveProduct()) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setWindowTitle("Success");
    msgBox.setText("Product saved successfully");
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
  }
  else {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setWindowTitle("Error");
    msgBox.setText("Failed to save product file");
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
  }
}

void MainMenuPage::onSaveProductAs()
{
  if (!m_context || !m_context->productPositionManager) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setWindowTitle("Error");
    msgBox.setText("Product manager not initialized");
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
    return;
  }

  bool ok;
  QInputDialog inputDialog(this);
  inputDialog.setWindowTitle("Save Product As");
  inputDialog.setLabelText("Enter product name (without .json):");
  inputDialog.setTextValue(m_context->productPositionManager->HasLoadedProduct()
      ? QString::fromStdString(m_context->productPositionManager->GetCurrentProductName())
      : "");
  inputDialog.setStyleSheet(DIALOG_STYLESHEET);

  ok = inputDialog.exec() == QDialog::Accepted;
  QString productName = inputDialog.textValue();

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

    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setWindowTitle("Success");
    msgBox.setText("Product saved as: " + productName);
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
  }
  else {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setWindowTitle("Error");
    msgBox.setText("Failed to save product file");
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
  }
}

void MainMenuPage::onDeleteProduct()
{
  if (!m_context || !m_context->productPositionManager) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setWindowTitle("Error");
    msgBox.setText("Product manager not initialized");
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
    return;
  }

  if (!m_context->productPositionManager->HasLoadedProduct()) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle("No Product");
    msgBox.setText("No product loaded to delete.");
    msgBox.setStyleSheet(DIALOG_STYLESHEET);
    msgBox.exec();
    return;
  }

  QString productName = QString::fromStdString(
    m_context->productPositionManager->GetCurrentProductName()
  );

  // Show warning message first
  QMessageBox warningBox(this);
  warningBox.setIcon(QMessageBox::Warning);
  warningBox.setWindowTitle("Delete Product - WARNING");
  warningBox.setText(QString("You are about to DELETE the entire product folder:\n\n"
                             "Product: %1\n"
                             "Folder: config/%1/\n\n"
                             "This will permanently delete:\n"
                             "• All positions\n"
                             "• All recipes\n"
                             "• All programs\n"
                             "• All configuration files\n\n"
                             "THIS CANNOT BE UNDONE!").arg(productName));
  warningBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
  warningBox.setDefaultButton(QMessageBox::Cancel);
  warningBox.setStyleSheet(DIALOG_STYLESHEET);

  if (warningBox.exec() != QMessageBox::Ok) {
    return;
  }

  // Require user to type exact phrase
  bool ok;
  QInputDialog confirmDialog(this);
  confirmDialog.setWindowTitle("Confirm Deletion");
  confirmDialog.setLabelText(QString("To confirm deletion of product '%1', please type exactly:\n\n"
            "I want to delete everything and everything.\n\n"
            "Type the confirmation phrase below:").arg(productName));
  confirmDialog.setTextValue("");
  confirmDialog.setStyleSheet(DIALOG_STYLESHEET);

  ok = confirmDialog.exec() == QDialog::Accepted;
  QString confirmText = confirmDialog.textValue();

  if (!ok) {
    return;
  }

  // Check if text matches exactly
  if (confirmText != "I want to delete everything and everything.") {
    QMessageBox errorBox(this);
    errorBox.setIcon(QMessageBox::Critical);
    errorBox.setWindowTitle("Deletion Cancelled");
    errorBox.setText("The confirmation text did not match.\n\n"
      "Expected: I want to delete everything and everything.\n"
      "You typed: " + confirmText + "\n\n"
      "Product deletion cancelled.");
    errorBox.setStyleSheet(DIALOG_STYLESHEET);
    errorBox.exec();
    return;
  }

  // Perform deletion
  std::string productDir = "config/" + productName.toStdString();

  try {
    if (std::filesystem::exists(productDir)) {
      std::filesystem::remove_all(productDir);

      // Clear product from manager
      m_context->productPositionManager->ClearAllPositions();

      // Clear application setting
      QSettings settings("Fabrinet", "BobcatEngine");
      settings.remove("Application/lastProductFile");

      updateProductDisplay();

      // Notify other pages that product has been deleted
      emit productChanged();

      QMessageBox successBox(this);
      successBox.setIcon(QMessageBox::Information);
      successBox.setWindowTitle("Product Deleted");
      successBox.setText(QString("Product '%1' has been permanently deleted.").arg(productName));
      successBox.setStyleSheet(DIALOG_STYLESHEET);
      successBox.exec();
    }
    else {
      QMessageBox errorBox(this);
      errorBox.setIcon(QMessageBox::Critical);
      errorBox.setWindowTitle("Error");
      errorBox.setText(QString("Product folder not found:\n%1").arg(QString::fromStdString(productDir)));
      errorBox.setStyleSheet(DIALOG_STYLESHEET);
      errorBox.exec();
    }
  }
  catch (const std::exception& e) {
    QMessageBox errorBox(this);
    errorBox.setIcon(QMessageBox::Critical);
    errorBox.setWindowTitle("Error");
    errorBox.setText(QString("Failed to delete product folder:\n%1\n\nError: %2")
        .arg(QString::fromStdString(productDir))
        .arg(e.what()));
    errorBox.setStyleSheet(DIALOG_STYLESHEET);
    errorBox.exec();
  }
}