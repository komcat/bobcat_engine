// CreateProductDialog.cpp
#include "CreateProductDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

CreateProductDialog::CreateProductDialog(QWidget* parent)
  : QDialog(parent)
  , m_productPath("")
{
  setupUI();
}

QString CreateProductDialog::createNewProduct(QWidget* parent)
{
  CreateProductDialog dialog(parent);

  if (dialog.exec() == QDialog::Accepted) {
    return dialog.getProductPath();
  }

  return QString();
}

void CreateProductDialog::setupUI()
{
  setWindowTitle("Create New Product");
  setMinimumSize(600, 500);
  resize(650, 550);

  // Set comprehensive light theme for the entire dialog
  setStyleSheet(
    "QDialog { "
    "  background-color: white; "
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
    "} "
    "QLabel { "
    "  background-color: transparent; "
    "  color: #2c3e50; "
    "} "
    "QLineEdit { "
    "  background-color: white; "
    "  color: #2c3e50; "
    "  border: 1px solid #bdc3c7; "
    "} "
    "QTextEdit { "
    "  background-color: #f8f9fa; "
    "  color: #2c3e50; "
    "  border: 1px solid #ddd; "
    "}"
  );

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(15);

  // ============================================================================
  // Header section
  // ============================================================================
  QLabel* titleLabel = new QLabel("Create New Product");
  titleLabel->setStyleSheet(
    "font-size: 18px; "
    "font-weight: bold; "
    "color: #2c3e50; "
    "padding: 10px;"
  );
  mainLayout->addWidget(titleLabel);

  QLabel* descLabel = new QLabel(
    "This will create a new product folder with the following structure:\n"
    "• Product folder in config/\n"
    "• Recipes subfolder\n"
    "• Programs subfolder\n"
    "• Calibration subfolder\n"
    "• Empty positions file"
  );
  descLabel->setStyleSheet(
    "font-size: 11px; "
    "color: #555; "
    "padding: 5px 10px; "
    "background-color: #f0f0f0; "
    "border-radius: 4px;"
  );
  descLabel->setWordWrap(true);
  mainLayout->addWidget(descLabel);

  // ============================================================================
  // Product name input section
  // ============================================================================
  QGroupBox* nameGroup = new QGroupBox("Product Name");
  nameGroup->setStyleSheet("QGroupBox { font-weight: bold; }");
  QVBoxLayout* nameLayout = new QVBoxLayout(nameGroup);

  QHBoxLayout* inputLayout = new QHBoxLayout();
  QLabel* nameLabel = new QLabel("Name:");
  nameLabel->setFixedWidth(60);
  nameLabel->setStyleSheet("font-size: 12px;");
  inputLayout->addWidget(nameLabel);

  m_nameEdit = new QLineEdit();
  m_nameEdit->setPlaceholderText("Enter product name (e.g., SIPHOG101, BOBCAT1)...");
  m_nameEdit->setStyleSheet("padding: 8px; font-size: 12px;");
  connect(m_nameEdit, &QLineEdit::textChanged, this, &CreateProductDialog::onNameTextChanged);
  inputLayout->addWidget(m_nameEdit);

  nameLayout->addLayout(inputLayout);

  m_previewLabel = new QLabel("Location: <i>not set</i>");
  m_previewLabel->setStyleSheet("font-size: 10px; color: #666; padding: 3px;");
  nameLayout->addWidget(m_previewLabel);

  mainLayout->addWidget(nameGroup);

  // ============================================================================
  // Structure preview
  // ============================================================================
  QLabel* structureLabel = new QLabel("Folder Structure Preview:");
  structureLabel->setStyleSheet("font-size: 11px; font-weight: bold; color: #555;");
  mainLayout->addWidget(structureLabel);

  m_structurePreview = new QTextEdit();
  m_structurePreview->setReadOnly(true);
  m_structurePreview->setMaximumHeight(180);
  m_structurePreview->setStyleSheet(
    "QTextEdit { "
    "  background-color: #2c3e50; "
    "  color: #ecf0f1; "
    "  font-family: 'Consolas', 'Courier New', monospace; "
    "  font-size: 11px; "
    "  border: 2px solid #34495e; "
    "  border-radius: 4px; "
    "  padding: 10px; "
    "}"
  );
  m_structurePreview->setText(
    "config/\n"
    "└── [product_name]/\n"
    "    ├── recipes/\n"
    "    ├── programs/\n"
    "    ├── calibration/\n"
    "    └── positions.[product_name].json"
  );

  mainLayout->addWidget(m_structurePreview);

  // ============================================================================
  // Warning note
  // ============================================================================
  QLabel* warningLabel = new QLabel(
    "⚠️ <b>Note:</b> Product name should not contain special characters like < > : \" / \\ | ? *"
  );
  warningLabel->setStyleSheet(
    "font-size: 10px; "
    "color: #e67e22; "
    "padding: 8px; "
    "background-color: #fff3cd; "
    "border: 1px solid #e67e22; "
    "border-radius: 3px;"
  );
  warningLabel->setWordWrap(true);
  mainLayout->addWidget(warningLabel);

  mainLayout->addStretch();

  // ============================================================================
  // Bottom buttons
  // ============================================================================
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_createButton = new QPushButton("✓ Create Product");
  m_createButton->setMinimumHeight(40);
  m_createButton->setMinimumWidth(150);
  m_createButton->setStyleSheet(
    "QPushButton { background-color: #27ae60; color: white; font-weight: bold; font-size: 13px; border-radius: 3px; }"
    "QPushButton:hover { background-color: #229954; }"
    "QPushButton:disabled { background-color: #bdc3c7; }"
  );
  m_createButton->setEnabled(false);
  connect(m_createButton, &QPushButton::clicked, this, &CreateProductDialog::onCreateClicked);
  buttonLayout->addWidget(m_createButton);

  m_cancelButton = new QPushButton("✗ Cancel");
  m_cancelButton->setMinimumHeight(40);
  m_cancelButton->setMinimumWidth(120);
  m_cancelButton->setStyleSheet(
    "QPushButton { background-color: #95a5a6; color: white; font-weight: bold; font-size: 13px; border-radius: 3px; }"
    "QPushButton:hover { background-color: #7f8c8d; }"
  );
  connect(m_cancelButton, &QPushButton::clicked, this, &CreateProductDialog::onCancel);
  buttonLayout->addWidget(m_cancelButton);

  mainLayout->addLayout(buttonLayout);
}

bool CreateProductDialog::validateProductName(const QString& name)
{
  if (name.isEmpty()) {
    return false;
  }

  // Check for invalid characters
  QString invalidChars = "<>:\"/\\|?*";
  for (QChar c : invalidChars) {
    if (name.contains(c)) {
      return false;
    }
  }

  // Check for spaces (optional - you might want to allow spaces)
  if (name.contains(' ')) {
    return false;
  }

  return true;
}

bool CreateProductDialog::createProductStructure(const QString& productName)
{
  QString basePath = QString("config/%1").arg(productName);

  // Check if product folder already exists
  QDir baseDir(basePath);
  if (baseDir.exists()) {
    QMessageBox::StandardButton reply = QMessageBox::question(
      this,
      "Product Already Exists",
      QString("A product named '%1' already exists.\n\nDo you want to use the existing product?")
        .arg(productName),
      QMessageBox::Yes | QMessageBox::No,
      QMessageBox::No
    );

    if (reply != QMessageBox::Yes) {
      return false;
    }

    // Use existing product
    m_productPath = QString("%1/positions.%2.json").arg(basePath).arg(productName);
    return true;
  }

  // Create main product folder
  if (!QDir().mkpath(basePath)) {
    QMessageBox::critical(this, "Error",
      QString("Failed to create product folder:\n%1").arg(basePath));
    return false;
  }

  // Create recipes subfolder
  QString recipesPath = QString("%1/recipes").arg(basePath);
  if (!QDir().mkpath(recipesPath)) {
    QMessageBox::critical(this, "Error",
      QString("Failed to create recipes folder:\n%1").arg(recipesPath));
    return false;
  }

  // Create programs subfolder
  QString programsPath = QString("%1/programs").arg(basePath);
  if (!QDir().mkpath(programsPath)) {
    QMessageBox::critical(this, "Error",
      QString("Failed to create programs folder:\n%1").arg(programsPath));
    return false;
  }

  // Create calibration subfolder
  QString calibrationPath = QString("%1/calibration").arg(basePath);
  if (!QDir().mkpath(calibrationPath)) {
    QMessageBox::critical(this, "Error",
      QString("Failed to create calibration folder:\n%1").arg(calibrationPath));
    return false;
  }

  // Create empty positions file
  QString positionsFile = QString("%1/positions.%2.json").arg(basePath).arg(productName);
  QFile file(positionsFile);
  if (!file.open(QIODevice::WriteOnly)) {
    QMessageBox::critical(this, "Error",
      QString("Failed to create positions file:\n%1").arg(positionsFile));
    return false;
  }

  // Write empty positions template
  QJsonObject root;
  root["version"] = "1.0";
  root["product_name"] = productName;
  root["devices"] = QJsonObject();

  QJsonDocument doc(root);
  file.write(doc.toJson(QJsonDocument::Indented));
  file.close();

  m_productPath = positionsFile;
  return true;
}

void CreateProductDialog::onCreateClicked()
{
  QString productName = m_nameEdit->text().trimmed();

  if (!validateProductName(productName)) {
    QMessageBox::warning(this, "Invalid Product Name",
      "Please enter a valid product name.\n\n"
      "Invalid characters: < > : \" / \\ | ? *\n"
      "Spaces are not allowed.");
    return;
  }

  if (createProductStructure(productName)) {
    QMessageBox::information(this, "Success",
      QString("Product '%1' created successfully!\n\n"
              "Location: config/%1/\n\n"
              "Created:\n"
              "• Recipes folder\n"
              "• Programs folder\n"
              "• Calibration folder\n"
              "• Empty positions file")
        .arg(productName));
    accept();
  }
}

void CreateProductDialog::onCancel()
{
  m_productPath.clear();
  reject();
}

void CreateProductDialog::onNameTextChanged(const QString& text)
{
  QString trimmed = text.trimmed();

  if (trimmed.isEmpty()) {
    m_previewLabel->setText("Location: <i>not set</i>");
    m_previewLabel->setStyleSheet("font-size: 10px; color: #666; padding: 3px;");
    m_createButton->setEnabled(false);

    m_structurePreview->setText(
      "config/\n"
      "└── [product_name]/\n"
      "    ├── recipes/\n"
      "    ├── programs/\n"
      "    └── positions.[product_name].json"
    );
    return;
  }

  if (!validateProductName(trimmed)) {
    m_previewLabel->setText("⚠️ <b>Invalid product name</b>");
    m_previewLabel->setStyleSheet("font-size: 10px; color: #e74c3c; padding: 3px;");
    m_createButton->setEnabled(false);

    m_structurePreview->setText(
      "config/\n"
      "└── [product_name]/\n"
      "    ├── recipes/\n"
      "    ├── programs/\n"
      "    ├── calibration/\n"
      "    └── positions.[product_name].json"
    );
    return;
  }

  m_previewLabel->setText(QString("Location: <b>config/%1/</b>").arg(trimmed));
  m_previewLabel->setStyleSheet("font-size: 10px; color: #27ae60; padding: 3px;");
  m_createButton->setEnabled(true);

  // Update structure preview with actual product name
  m_structurePreview->setText(
    QString(
      "config/\n"
      "└── %1/\n"
      "    ├── recipes/\n"
      "    ├── programs/\n"
      "    ├── calibration/\n"
      "    └── positions.%1.json"
    ).arg(trimmed)
  );
}
