// RecipeSaveAsDialog.cpp
#include "RecipeSaveAsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

RecipeSaveAsDialog::RecipeSaveAsDialog(const QString& currentRecipeName, QWidget* parent)
  : QDialog(parent)
  , m_currentRecipeName(currentRecipeName)
  , m_saveFilePath("")
  , m_folderPath("recipes")
{
  setupUI();
  loadExistingRecipes();
  populateRecipeTable();

  // Set default name in text field if provided
  if (!currentRecipeName.isEmpty()) {
    m_nameEdit->setText(currentRecipeName);
  }
}

QString RecipeSaveAsDialog::getSaveFileName(QWidget* parent, const QString& currentRecipeName)
{
  RecipeSaveAsDialog dialog(currentRecipeName, parent);

  if (dialog.exec() == QDialog::Accepted) {
    return dialog.getSaveFilePath();
  }

  return QString();
}

void RecipeSaveAsDialog::setupUI()
{
  setWindowTitle("Save Recipe As");
  setMinimumSize(700, 550);
  resize(800, 650);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // ============================================================================
  // Header section
  // ============================================================================
  QGroupBox* headerGroup = new QGroupBox();
  QVBoxLayout* headerLayout = new QVBoxLayout(headerGroup);

  m_folderLabel = new QLabel(QString("📁 Save to: <b>%1</b>").arg(m_folderPath));
  m_folderLabel->setStyleSheet("font-size: 12px; padding: 5px;");
  headerLayout->addWidget(m_folderLabel);

  m_fileCountLabel = new QLabel();
  m_fileCountLabel->setStyleSheet("font-size: 11px; color: #666; padding: 2px 5px;");
  headerLayout->addWidget(m_fileCountLabel);

  mainLayout->addWidget(headerGroup);

  // ============================================================================
  // Recipe name input section
  // ============================================================================
  QGroupBox* nameGroup = new QGroupBox("Recipe Name");
  nameGroup->setStyleSheet("QGroupBox { font-weight: bold; }");
  QVBoxLayout* nameLayout = new QVBoxLayout(nameGroup);

  QHBoxLayout* inputLayout = new QHBoxLayout();
  QLabel* nameLabel = new QLabel("Name:");
  nameLabel->setStyleSheet("font-size: 11px;");
  inputLayout->addWidget(nameLabel);

  m_nameEdit = new QLineEdit();
  m_nameEdit->setPlaceholderText("Enter recipe name (without .json)...");
  m_nameEdit->setStyleSheet("padding: 8px; font-size: 12px;");
  connect(m_nameEdit, &QLineEdit::textChanged, this, &RecipeSaveAsDialog::onNameTextChanged);
  inputLayout->addWidget(m_nameEdit);

  m_refreshButton = new QPushButton("🔄");
  m_refreshButton->setFixedSize(35, 35);
  m_refreshButton->setToolTip("Refresh recipe list");
  m_refreshButton->setStyleSheet(
    "QPushButton { background-color: #95a5a6; color: white; font-weight: bold; border-radius: 3px; }"
    "QPushButton:hover { background-color: #7f8c8d; }"
  );
  connect(m_refreshButton, &QPushButton::clicked, this, &RecipeSaveAsDialog::onRefresh);
  inputLayout->addWidget(m_refreshButton);

  nameLayout->addLayout(inputLayout);

  m_previewLabel = new QLabel("File will be saved as: <i>not set</i>");
  m_previewLabel->setStyleSheet("font-size: 10px; color: #666; padding: 3px;");
  nameLayout->addWidget(m_previewLabel);

  mainLayout->addWidget(nameGroup);

  // ============================================================================
  // Existing recipes table
  // ============================================================================
  QLabel* existingLabel = new QLabel("Existing Recipes (click to use name):");
  existingLabel->setStyleSheet("font-size: 11px; font-weight: bold; color: #555;");
  mainLayout->addWidget(existingLabel);

  m_recipeTable = new QTableWidget();
  m_recipeTable->setColumnCount(3);
  m_recipeTable->setHorizontalHeaderLabels({"Recipe Name", "Date Modified", "Size"});
  m_recipeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_recipeTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_recipeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_recipeTable->horizontalHeader()->setStretchLastSection(false);
  m_recipeTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_recipeTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_recipeTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_recipeTable->setAlternatingRowColors(true);
  m_recipeTable->setMaximumHeight(250);
  m_recipeTable->setStyleSheet(
    "QTableWidget { "
    "  background-color: white; "
    "  alternate-background-color: #f5f5f5; "
    "  gridline-color: #ddd; "
    "  font-size: 11px; "
    "} "
    "QTableWidget::item:selected { "
    "  background-color: #3498db; "
    "  color: white; "
    "}"
  );

  connect(m_recipeTable, &QTableWidget::cellClicked, this, &RecipeSaveAsDialog::onFileClicked);

  mainLayout->addWidget(m_recipeTable);

  // ============================================================================
  // Warning label
  // ============================================================================
  QLabel* warningLabel = new QLabel(
    "⚠️ <b>Note:</b> If a recipe with the same name exists, you will be prompted to confirm overwrite."
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

  // ============================================================================
  // Bottom buttons
  // ============================================================================
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_saveButton = new QPushButton("💾 Save Recipe");
  m_saveButton->setMinimumHeight(40);
  m_saveButton->setMinimumWidth(140);
  m_saveButton->setStyleSheet(
    "QPushButton { background-color: #27ae60; color: white; font-weight: bold; font-size: 13px; border-radius: 3px; }"
    "QPushButton:hover { background-color: #229954; }"
    "QPushButton:disabled { background-color: #bdc3c7; }"
  );
  m_saveButton->setEnabled(false);
  connect(m_saveButton, &QPushButton::clicked, this, &RecipeSaveAsDialog::onSaveClicked);
  buttonLayout->addWidget(m_saveButton);

  m_cancelButton = new QPushButton("✗ Cancel");
  m_cancelButton->setMinimumHeight(40);
  m_cancelButton->setMinimumWidth(120);
  m_cancelButton->setStyleSheet(
    "QPushButton { background-color: #e74c3c; color: white; font-weight: bold; font-size: 13px; border-radius: 3px; }"
    "QPushButton:hover { background-color: #c0392b; }"
  );
  connect(m_cancelButton, &QPushButton::clicked, this, &RecipeSaveAsDialog::onCancel);
  buttonLayout->addWidget(m_cancelButton);

  mainLayout->addLayout(buttonLayout);
}

void RecipeSaveAsDialog::loadExistingRecipes()
{
  m_existingRecipes.clear();

  QDir dir(m_folderPath);
  if (!dir.exists()) {
    dir.mkpath(".");
  }

  QStringList filters;
  filters << "*.json";
  dir.setNameFilters(filters);
  dir.setFilter(QDir::Files);
  dir.setSorting(QDir::Time | QDir::Reversed);

  QFileInfoList fileList = dir.entryInfoList();

  for (const QFileInfo& fileInfo : fileList) {
    RecipeInfo info;
    info.fileName = fileInfo.fileName();
    info.fullPath = fileInfo.absoluteFilePath();
    info.dateModified = fileInfo.lastModified().toString("yyyy-MM-dd hh:mm:ss");
    info.fileSize = fileInfo.size();

    // Try to extract recipe name from JSON
    QFile file(info.fullPath);
    if (file.open(QIODevice::ReadOnly)) {
      QByteArray data = file.readAll();
      QJsonDocument doc = QJsonDocument::fromJson(data);
      if (doc.isObject()) {
        QJsonObject obj = doc.object();
        info.recipeName = obj.value("recipe_name").toString();
      }
      file.close();
    }

    if (info.recipeName.isEmpty()) {
      // Fallback to filename without extension
      info.recipeName = fileInfo.baseName();
    }

    m_existingRecipes.append(info);
  }
}

void RecipeSaveAsDialog::populateRecipeTable()
{
  m_recipeTable->setRowCount(0);

  for (int i = 0; i < m_existingRecipes.size(); ++i) {
    const RecipeInfo& recipe = m_existingRecipes[i];

    m_recipeTable->insertRow(i);

    // Recipe name
    QTableWidgetItem* nameItem = new QTableWidgetItem(recipe.recipeName);
    nameItem->setData(Qt::UserRole, recipe.fileName);
    m_recipeTable->setItem(i, 0, nameItem);

    // Date modified
    QTableWidgetItem* dateItem = new QTableWidgetItem(recipe.dateModified);
    m_recipeTable->setItem(i, 1, dateItem);

    // File size
    QString sizeStr;
    if (recipe.fileSize < 1024) {
      sizeStr = QString("%1 B").arg(recipe.fileSize);
    } else if (recipe.fileSize < 1024 * 1024) {
      sizeStr = QString("%1 KB").arg(recipe.fileSize / 1024.0, 0, 'f', 1);
    } else {
      sizeStr = QString("%1 MB").arg(recipe.fileSize / (1024.0 * 1024.0), 0, 'f', 2);
    }
    QTableWidgetItem* sizeItem = new QTableWidgetItem(sizeStr);
    sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_recipeTable->setItem(i, 2, sizeItem);
  }

  // Update count label
  m_fileCountLabel->setText(QString("Found <b>%1</b> existing recipe(s)")
    .arg(m_existingRecipes.size()));
}

bool RecipeSaveAsDialog::validateFilename(const QString& filename)
{
  if (filename.isEmpty()) {
    return false;
  }

  // Check for invalid characters
  QString invalidChars = "<>:\"/\\|?*";
  for (QChar c : invalidChars) {
    if (filename.contains(c)) {
      return false;
    }
  }

  return true;
}

void RecipeSaveAsDialog::onFileClicked(int row, int column)
{
  Q_UNUSED(column);

  if (row >= 0 && row < m_recipeTable->rowCount()) {
    QTableWidgetItem* nameItem = m_recipeTable->item(row, 0);
    if (nameItem) {
      QString recipeName = nameItem->text();
      m_nameEdit->setText(recipeName);
    }
  }
}

void RecipeSaveAsDialog::onSaveClicked()
{
  QString filename = m_nameEdit->text().trimmed();

  if (!validateFilename(filename)) {
    QMessageBox::warning(this, "Invalid Filename",
      "Please enter a valid recipe name.\n\n"
      "Invalid characters: < > : \" / \\ | ? *");
    return;
  }

  // Ensure .json extension
  if (!filename.endsWith(".json", Qt::CaseInsensitive)) {
    filename += ".json";
  }

  QString fullPath = QDir(m_folderPath).absoluteFilePath(filename);

  // Check if file already exists
  if (QFile::exists(fullPath)) {
    QMessageBox::StandardButton reply = QMessageBox::question(
      this,
      "Overwrite Existing Recipe?",
      QString("A recipe named '%1' already exists.\n\nDo you want to overwrite it?")
        .arg(m_nameEdit->text().trimmed()),
      QMessageBox::Yes | QMessageBox::No,
      QMessageBox::No
    );

    if (reply != QMessageBox::Yes) {
      return;
    }
  }

  m_saveFilePath = fullPath;
  accept();
}

void RecipeSaveAsDialog::onCancel()
{
  m_saveFilePath.clear();
  reject();
}

void RecipeSaveAsDialog::onRefresh()
{
  loadExistingRecipes();
  populateRecipeTable();
}

void RecipeSaveAsDialog::onNameTextChanged(const QString& text)
{
  QString trimmed = text.trimmed();

  if (trimmed.isEmpty()) {
    m_previewLabel->setText("File will be saved as: <i>not set</i>");
    m_previewLabel->setStyleSheet("font-size: 10px; color: #666; padding: 3px;");
    m_saveButton->setEnabled(false);
    return;
  }

  if (!validateFilename(trimmed)) {
    m_previewLabel->setText("⚠️ <b>Invalid filename</b>");
    m_previewLabel->setStyleSheet("font-size: 10px; color: #e74c3c; padding: 3px;");
    m_saveButton->setEnabled(false);
    return;
  }

  QString filename = trimmed;
  if (!filename.endsWith(".json", Qt::CaseInsensitive)) {
    filename += ".json";
  }

  m_previewLabel->setText(QString("File will be saved as: <b>%1/%2</b>")
    .arg(m_folderPath)
    .arg(filename));
  m_previewLabel->setStyleSheet("font-size: 10px; color: #27ae60; padding: 3px;");

  m_saveButton->setEnabled(true);
}
