// ConfigBackupManager.cpp - Configuration backup and restore implementation
#include "ConfigBackupManager.h"
#include "ApplicationContext.h"
#include "products/product_position_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QFile>
#include <QFont>

#include <iostream>
#include <algorithm>

namespace fs = std::filesystem;

static const QString QSETTINGS_ORG = "Fabrinet";
static const QString QSETTINGS_APP = "BobcatEngine";

ConfigBackupManager::ConfigBackupManager(ApplicationContext* context)
  : m_context(context)
{
}

// =============================================================================
// Path helpers
// =============================================================================

fs::path ConfigBackupManager::getConfigBasePath() const
{
  return fs::path(QCoreApplication::applicationDirPath().toStdString()) / "config";
}

fs::path ConfigBackupManager::getBackupBasePath() const
{
  QSettings settings(QSETTINGS_ORG, QSETTINGS_APP);
  QString path = settings.value("Backup/DestinationPath", "").toString();
  if (path.isEmpty()) {
    return fs::path(QCoreApplication::applicationDirPath().toStdString()) / "config_backups";
  }
  return fs::path(path.toStdString());
}

std::string ConfigBackupManager::getCurrentProductName() const
{
  if (m_context && m_context->productPositionManager) {
    return m_context->productPositionManager->GetCurrentProductName();
  }
  return {};
}

void ConfigBackupManager::saveDestinationPath(const QString& path)
{
  QSettings settings(QSETTINGS_ORG, QSETTINGS_APP);
  settings.setValue("Backup/DestinationPath", path);
  settings.sync();
}

// =============================================================================
// File copy utilities
// =============================================================================

bool ConfigBackupManager::copyRootConfigFiles(const fs::path& src, const fs::path& dst)
{
  std::error_code ec;
  fs::create_directories(dst, ec);
  if (ec) {
    std::cout << "[ConfigBackup] Failed to create directory: " << dst.string()
              << " - " << ec.message() << std::endl;
    return false;
  }

  for (const auto& entry : fs::directory_iterator(src, ec)) {
    if (entry.is_regular_file()) {
      fs::copy_file(entry.path(), dst / entry.path().filename(),
                    fs::copy_options::overwrite_existing, ec);
      if (ec) {
        std::cout << "[ConfigBackup] Failed to copy "
                  << entry.path().filename().string() << ": " << ec.message() << std::endl;
        return false;
      }
    }
  }
  return true;
}

bool ConfigBackupManager::copyDirectoryRecursive(const fs::path& src, const fs::path& dst)
{
  std::error_code ec;
  fs::create_directories(dst, ec);
  if (ec) {
    std::cout << "[ConfigBackup] Failed to create directory: " << dst.string()
              << " - " << ec.message() << std::endl;
    return false;
  }

  for (const auto& entry : fs::recursive_directory_iterator(src, ec)) {
    fs::path relativePath = fs::relative(entry.path(), src, ec);
    if (ec) return false;

    fs::path destPath = dst / relativePath;

    if (entry.is_directory()) {
      fs::create_directories(destPath, ec);
      if (ec) return false;
    }
    else if (entry.is_regular_file()) {
      fs::copy_file(entry.path(), destPath,
                    fs::copy_options::overwrite_existing, ec);
      if (ec) {
        std::cout << "[ConfigBackup] Failed to copy "
                  << relativePath.string() << ": " << ec.message() << std::endl;
        return false;
      }
    }
  }
  return true;
}

// =============================================================================
// Manifest
// =============================================================================

bool ConfigBackupManager::writeManifest(const fs::path& backupDir,
                                        const BackupScope& scope,
                                        const QString& description)
{
  QJsonObject manifest;
  manifest["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  manifest["description"] = description;

  std::string productName = getCurrentProductName();
  manifest["productName"] = QString::fromStdString(productName);

  QJsonArray scopeArray;
  if (scope.rootConfig) scopeArray.append("root");
  if (scope.systemFolder) scopeArray.append("SYSTEM");
  if (scope.productFolder && !productName.empty())
    scopeArray.append(QString::fromStdString(productName));
  manifest["scopes"] = scopeArray;

  QJsonDocument doc(manifest);
  QString manifestPath = QString::fromStdString((backupDir / "manifest.json").string());
  QFile file(manifestPath);
  if (!file.open(QIODevice::WriteOnly)) {
    std::cout << "[ConfigBackup] Failed to write manifest: "
              << manifestPath.toStdString() << std::endl;
    return false;
  }
  file.write(doc.toJson(QJsonDocument::Indented));
  file.close();
  return true;
}

// =============================================================================
// Core operations
// =============================================================================

bool ConfigBackupManager::performBackup(const BackupScope& scope,
                                        const QString& description,
                                        const fs::path& destBase)
{
  auto configBase = getConfigBasePath();

  // Generate timestamped folder name
  QDateTime now = QDateTime::currentDateTime();
  QString timestamp = now.toString("yyyy-MM-dd_HHmmss");
  fs::path backupDir = destBase / timestamp.toStdString();

  std::error_code ec;
  fs::create_directories(backupDir, ec);
  if (ec) {
    std::cout << "[ConfigBackup] Failed to create backup directory: "
              << backupDir.string() << " - " << ec.message() << std::endl;
    return false;
  }

  bool success = true;

  // Scope 1: Root config files (non-directory entries in config/)
  if (scope.rootConfig) {
    success = success && copyRootConfigFiles(configBase, backupDir);
  }

  // Scope 2: SYSTEM folder
  if (scope.systemFolder) {
    fs::path systemSrc = configBase / "SYSTEM";
    if (fs::exists(systemSrc, ec)) {
      success = success && copyDirectoryRecursive(systemSrc, backupDir / "SYSTEM");
    }
  }

  // Scope 3: Active product folder
  if (scope.productFolder) {
    std::string productName = getCurrentProductName();
    if (!productName.empty()) {
      fs::path productSrc = configBase / productName;
      if (fs::exists(productSrc, ec)) {
        success = success && copyDirectoryRecursive(productSrc, backupDir / productName);
      }
    }
  }

  // Write manifest
  if (success) {
    success = writeManifest(backupDir, scope, description);
  }

  if (!success) {
    // Cleanup failed backup attempt
    fs::remove_all(backupDir, ec);
  }

  return success;
}

std::vector<BackupManifest> ConfigBackupManager::loadAvailableBackups()
{
  std::vector<BackupManifest> backups;
  std::error_code ec;

  auto backupBase = getBackupBasePath();
  if (!fs::exists(backupBase, ec)) return backups;

  for (const auto& entry : fs::directory_iterator(backupBase, ec)) {
    if (!entry.is_directory()) continue;

    fs::path manifestPath = entry.path() / "manifest.json";
    if (!fs::exists(manifestPath, ec)) continue;

    QFile file(QString::fromStdString(manifestPath.string()));
    if (!file.open(QIODevice::ReadOnly)) continue;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (doc.isNull()) continue;

    QJsonObject obj = doc.object();
    BackupManifest m;
    m.timestamp = obj["timestamp"].toString();
    m.description = obj["description"].toString();
    m.productName = obj["productName"].toString();
    m.folderPath = QString::fromStdString(entry.path().string());

    QJsonArray scopeArr = obj["scopes"].toArray();
    for (const auto& s : scopeArr) {
      m.scopes.append(s.toString());
    }

    backups.push_back(m);
  }

  // Sort by timestamp descending (newest first)
  std::sort(backups.begin(), backups.end(),
    [](const BackupManifest& a, const BackupManifest& b) {
      return a.timestamp > b.timestamp;
    });

  return backups;
}

bool ConfigBackupManager::performRestore(const BackupManifest& manifest,
                                         const BackupScope& scope)
{
  auto configBase = getConfigBasePath();
  fs::path backupDir(manifest.folderPath.toStdString());
  std::error_code ec;

  bool success = true;

  // Restore root config files
  if (scope.rootConfig && manifest.scopes.contains("root")) {
    for (const auto& entry : fs::directory_iterator(backupDir, ec)) {
      if (entry.is_regular_file() &&
          entry.path().filename().string() != "manifest.json") {
        fs::copy_file(entry.path(), configBase / entry.path().filename(),
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
          std::cout << "[ConfigRestore] Failed to restore "
                    << entry.path().filename().string() << ": " << ec.message() << std::endl;
          success = false;
          break;
        }
      }
    }
  }

  // Restore SYSTEM folder
  if (scope.systemFolder && manifest.scopes.contains("SYSTEM")) {
    fs::path systemSrc = backupDir / "SYSTEM";
    if (fs::exists(systemSrc, ec)) {
      success = success && copyDirectoryRecursive(systemSrc, configBase / "SYSTEM");
    }
  }

  // Restore product folder
  if (scope.productFolder) {
    for (const auto& s : manifest.scopes) {
      if (s != "root" && s != "SYSTEM") {
        fs::path productSrc = backupDir / s.toStdString();
        if (fs::exists(productSrc, ec)) {
          success = success && copyDirectoryRecursive(productSrc, configBase / s.toStdString());
        }
        break;
      }
    }
  }

  return success;
}

bool ConfigBackupManager::deleteBackup(const BackupManifest& manifest)
{
  std::error_code ec;
  fs::remove_all(fs::path(manifest.folderPath.toStdString()), ec);
  if (ec) {
    std::cout << "[ConfigBackup] Failed to delete backup: " << ec.message() << std::endl;
    return false;
  }
  return true;
}

// =============================================================================
// UI Dialogs
// =============================================================================

bool ConfigBackupManager::showBackupDialog(QWidget* parent)
{
  QDialog dialog(parent);
  dialog.setWindowTitle("Backup Configuration");
  dialog.setMinimumSize(500, 420);
  dialog.setStyleSheet(
    "QDialog { background-color: #27272a; border: 1px solid #3f3f46; }"
    "QLabel { color: #d4d4d8; }"
    "QCheckBox { color: #d4d4d8; spacing: 8px; }"
    "QCheckBox::indicator { width: 16px; height: 16px; }"
    "QLineEdit { background-color: #18181b; color: white; border: 1px solid #52525b; "
    "  border-radius: 4px; padding: 6px; }"
  );

  QVBoxLayout* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(12);

  // Title
  QLabel* titleLabel = new QLabel("Select scopes to back up:");
  QFont titleFont = titleLabel->font();
  titleFont.setPointSize(12);
  titleFont.setBold(true);
  titleLabel->setFont(titleFont);
  layout->addWidget(titleLabel);

  // Scope checkboxes
  QCheckBox* rootCheck = new QCheckBox("Root config files (motor_devices, camera_config, etc.)");
  rootCheck->setChecked(true);
  layout->addWidget(rootCheck);

  QCheckBox* systemCheck = new QCheckBox("SYSTEM calibration files (applicationSettings, offsets, delays)");
  systemCheck->setChecked(true);
  layout->addWidget(systemCheck);

  // Product checkbox - disabled if no product loaded
  std::string productName = getCurrentProductName();

  QCheckBox* productCheck = new QCheckBox(
    productName.empty()
      ? "Active product (no product loaded)"
      : QString("Active product: \"%1\"").arg(QString::fromStdString(productName)));
  productCheck->setChecked(!productName.empty());
  productCheck->setEnabled(!productName.empty());
  layout->addWidget(productCheck);

  layout->addSpacing(5);

  // Description
  QLabel* descLabel = new QLabel("Description (optional):");
  layout->addWidget(descLabel);

  QLineEdit* descEdit = new QLineEdit();
  descEdit->setPlaceholderText("e.g., Before motor recalibration");
  layout->addWidget(descEdit);

  layout->addSpacing(5);

  // Backup destination
  QLabel* destLabel = new QLabel("Backup destination:");
  layout->addWidget(destLabel);

  QHBoxLayout* destLayout = new QHBoxLayout();
  QLineEdit* destEdit = new QLineEdit();
  destEdit->setText(QString::fromStdString(getBackupBasePath().string()));
  destLayout->addWidget(destEdit);

  QPushButton* browseBtn = new QPushButton("Browse...");
  browseBtn->setStyleSheet(
    "QPushButton { background-color: #3f3f46; color: white; border: none; "
    "  border-radius: 4px; padding: 6px 12px; font-weight: bold; }"
    "QPushButton:hover { background-color: #52525b; }"
  );
  QObject::connect(browseBtn, &QPushButton::clicked, &dialog, [&destEdit, &dialog]() {
    QString dir = QFileDialog::getExistingDirectory(&dialog, "Select Backup Destination",
      destEdit->text());
    if (!dir.isEmpty()) {
      destEdit->setText(dir);
    }
  });
  destLayout->addWidget(browseBtn);
  layout->addLayout(destLayout);

  // Preview path
  QLabel* previewLabel = new QLabel();
  previewLabel->setStyleSheet("color: #71717a; font-size: 11px;");
  auto updatePreview = [&previewLabel, &destEdit]() {
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
    previewLabel->setText("Will create: " + destEdit->text() + "/" + timestamp + "/");
  };
  updatePreview();
  QObject::connect(destEdit, &QLineEdit::textChanged, &dialog, updatePreview);
  layout->addWidget(previewLabel);

  layout->addStretch();

  // Buttons
  QHBoxLayout* btnLayout = new QHBoxLayout();
  btnLayout->addStretch();

  QPushButton* cancelBtn = new QPushButton("Cancel");
  cancelBtn->setStyleSheet(
    "QPushButton { background-color: #3f3f46; color: white; border: none; "
    "  border-radius: 4px; padding: 10px 20px; font-weight: bold; }"
    "QPushButton:hover { background-color: #52525b; }"
  );
  QObject::connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
  btnLayout->addWidget(cancelBtn);

  QPushButton* createBtn = new QPushButton("Create Backup");
  createBtn->setStyleSheet(
    "QPushButton { background-color: #166534; color: white; border: none; "
    "  border-radius: 4px; padding: 10px 20px; font-weight: bold; }"
    "QPushButton:hover { background-color: #15803d; }"
  );
  QObject::connect(createBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
  btnLayout->addWidget(createBtn);

  layout->addLayout(btnLayout);

  if (dialog.exec() != QDialog::Accepted) return false;

  // Validate at least one scope selected
  if (!rootCheck->isChecked() && !systemCheck->isChecked() && !productCheck->isChecked()) {
    QMessageBox::warning(parent, "No Scope Selected",
      "Please select at least one scope to back up.");
    return false;
  }

  BackupScope scope;
  scope.rootConfig = rootCheck->isChecked();
  scope.systemFolder = systemCheck->isChecked();
  scope.productFolder = productCheck->isChecked();

  fs::path destBase = fs::path(destEdit->text().toStdString());

  bool success = performBackup(scope, descEdit->text(), destBase);

  if (success) {
    saveDestinationPath(destEdit->text());
    std::cout << "[ConfigBackup] Backup created successfully at: "
              << destBase.string() << std::endl;
    QMessageBox::information(parent, "Backup Created",
      "Configuration backup created successfully.");
  } else {
    QMessageBox::warning(parent, "Backup Failed",
      "Failed to create configuration backup.\nCheck the log for details.");
  }

  return success;
}

bool ConfigBackupManager::showRestoreDialog(QWidget* parent)
{
  auto backups = loadAvailableBackups();

  QDialog dialog(parent);
  dialog.setWindowTitle("Restore Configuration");
  dialog.setMinimumSize(550, 520);
  dialog.setStyleSheet(
    "QDialog { background-color: #27272a; border: 1px solid #3f3f46; }"
    "QLabel { color: #d4d4d8; }"
    "QCheckBox { color: #d4d4d8; spacing: 8px; }"
    "QCheckBox::indicator { width: 16px; height: 16px; }"
    "QListWidget { background-color: #18181b; color: white; border: 1px solid #52525b; "
    "  border-radius: 4px; }"
    "QListWidget::item { padding: 8px; }"
    "QListWidget::item:selected { background-color: #3b82f6; }"
  );

  QVBoxLayout* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(12);

  // Location label
  QLabel* locLabel = new QLabel("Backup location: " +
    QString::fromStdString(getBackupBasePath().string()));
  locLabel->setStyleSheet("color: #71717a; font-size: 11px;");
  layout->addWidget(locLabel);

  // Title
  QLabel* titleLabel = new QLabel("Available backups:");
  QFont titleFont = titleLabel->font();
  titleFont.setPointSize(12);
  titleFont.setBold(true);
  titleLabel->setFont(titleFont);
  layout->addWidget(titleLabel);

  // Backup list
  QListWidget* listWidget = new QListWidget();
  listWidget->setMinimumHeight(180);

  if (backups.empty()) {
    QListWidgetItem* emptyItem = new QListWidgetItem("No backups found");
    emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable);
    emptyItem->setForeground(QColor("#71717a"));
    listWidget->addItem(emptyItem);
  } else {
    for (size_t i = 0; i < backups.size(); ++i) {
      const auto& b = backups[i];
      QString text = b.timestamp;
      if (!b.description.isEmpty()) {
        text += "  -  " + b.description;
      }
      text += "\nScopes: " + b.scopes.join(", ");
      if (!b.productName.isEmpty()) {
        text += "  |  Product: " + b.productName;
      }
      QListWidgetItem* item = new QListWidgetItem(text);
      item->setData(Qt::UserRole, static_cast<int>(i));
      listWidget->addItem(item);
    }
  }
  layout->addWidget(listWidget);

  // Scope checkboxes for restore
  QLabel* scopeLabel = new QLabel("Restore scopes:");
  scopeLabel->setStyleSheet("font-weight: bold;");
  layout->addWidget(scopeLabel);

  QCheckBox* rootCheck = new QCheckBox("Root config files");
  rootCheck->setChecked(true);
  rootCheck->setEnabled(false);
  layout->addWidget(rootCheck);

  QCheckBox* systemCheck = new QCheckBox("SYSTEM calibration files");
  systemCheck->setChecked(true);
  systemCheck->setEnabled(false);
  layout->addWidget(systemCheck);

  QCheckBox* productCheck = new QCheckBox("Product folder");
  productCheck->setChecked(false);
  productCheck->setEnabled(false);
  layout->addWidget(productCheck);

  // Update scope checkboxes when selection changes
  QObject::connect(listWidget, &QListWidget::currentRowChanged, &dialog,
    [&backups, rootCheck, systemCheck, productCheck](int row) {
      if (row < 0 || row >= static_cast<int>(backups.size())) {
        rootCheck->setEnabled(false);
        systemCheck->setEnabled(false);
        productCheck->setEnabled(false);
        return;
      }
      const auto& b = backups[static_cast<size_t>(row)];
      bool hasRoot = b.scopes.contains("root");
      bool hasSystem = b.scopes.contains("SYSTEM");
      bool hasProduct = false;
      QString productScope;
      for (const auto& s : b.scopes) {
        if (s != "root" && s != "SYSTEM") {
          hasProduct = true;
          productScope = s;
          break;
        }
      }

      rootCheck->setEnabled(hasRoot);
      rootCheck->setChecked(hasRoot);
      systemCheck->setEnabled(hasSystem);
      systemCheck->setChecked(hasSystem);
      productCheck->setEnabled(hasProduct);
      productCheck->setChecked(hasProduct);
      if (hasProduct) {
        productCheck->setText("Product: \"" + productScope + "\"");
      } else {
        productCheck->setText("Product folder (not in this backup)");
      }
    });

  // Warning
  QLabel* warnLabel = new QLabel("Warning: This will overwrite current config files.");
  warnLabel->setStyleSheet("color: #fbbf24; font-weight: bold; margin-top: 5px;");
  layout->addWidget(warnLabel);

  // Buttons
  QHBoxLayout* btnLayout = new QHBoxLayout();

  QPushButton* deleteBtn = new QPushButton("Delete Backup");
  deleteBtn->setStyleSheet(
    "QPushButton { background-color: #7f1d1d; color: white; border: none; "
    "  border-radius: 4px; padding: 10px 20px; font-weight: bold; }"
    "QPushButton:hover { background-color: #991b1b; }"
  );
  btnLayout->addWidget(deleteBtn);

  btnLayout->addStretch();

  QPushButton* cancelBtn = new QPushButton("Cancel");
  cancelBtn->setStyleSheet(
    "QPushButton { background-color: #3f3f46; color: white; border: none; "
    "  border-radius: 4px; padding: 10px 20px; font-weight: bold; }"
    "QPushButton:hover { background-color: #52525b; }"
  );
  QObject::connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
  btnLayout->addWidget(cancelBtn);

  QPushButton* restoreBtn = new QPushButton("Restore");
  restoreBtn->setStyleSheet(
    "QPushButton { background-color: #78350f; color: white; border: none; "
    "  border-radius: 4px; padding: 10px 20px; font-weight: bold; }"
    "QPushButton:hover { background-color: #92400e; }"
    "QPushButton:disabled { background-color: #3f3f46; color: #71717a; }"
  );
  restoreBtn->setEnabled(false);
  btnLayout->addWidget(restoreBtn);

  layout->addLayout(btnLayout);

  // Enable/disable restore button based on selection
  QObject::connect(listWidget, &QListWidget::currentRowChanged, &dialog,
    [restoreBtn, &backups](int row) {
      restoreBtn->setEnabled(row >= 0 && row < static_cast<int>(backups.size()));
    });

  bool restored = false;

  // Delete backup handler
  QObject::connect(deleteBtn, &QPushButton::clicked, &dialog,
    [&dialog, listWidget, &backups, this]() {
      int row = listWidget->currentRow();
      if (row < 0 || row >= static_cast<int>(backups.size())) {
        QMessageBox::warning(&dialog, "No Selection", "Please select a backup to delete.");
        return;
      }

      QMessageBox::StandardButton reply = QMessageBox::question(&dialog,
        "Delete Backup",
        "Are you sure you want to permanently delete this backup?\n\n" +
        backups[static_cast<size_t>(row)].timestamp,
        QMessageBox::Yes | QMessageBox::No);

      if (reply == QMessageBox::Yes) {
        if (deleteBackup(backups[static_cast<size_t>(row)])) {
          backups.erase(backups.begin() + row);
          delete listWidget->takeItem(row);
          std::cout << "[ConfigRestore] Backup deleted" << std::endl;
        } else {
          QMessageBox::warning(&dialog, "Delete Failed",
            "Failed to delete backup folder.");
        }
      }
    });

  // Restore handler
  QObject::connect(restoreBtn, &QPushButton::clicked, &dialog,
    [&dialog, listWidget, &backups, rootCheck, systemCheck, productCheck, &restored, this]() {
      int row = listWidget->currentRow();
      if (row < 0 || row >= static_cast<int>(backups.size())) return;

      QMessageBox::StandardButton reply = QMessageBox::warning(&dialog,
        "Confirm Restore",
        "Are you sure you want to restore from this backup?\n\n"
        "  " + backups[static_cast<size_t>(row)].timestamp + "\n\n"
        "This will overwrite your current configuration files.",
        QMessageBox::Yes | QMessageBox::No);

      if (reply == QMessageBox::Yes) {
        BackupScope scope;
        scope.rootConfig = rootCheck->isChecked();
        scope.systemFolder = systemCheck->isChecked();
        scope.productFolder = productCheck->isChecked();

        bool success = performRestore(backups[static_cast<size_t>(row)], scope);

        if (success) {
          restored = true;
          std::cout << "[ConfigRestore] Configuration restored successfully" << std::endl;
          QMessageBox::information(&dialog, "Restore Complete",
            "Configuration has been restored.\n"
            "Some changes may require restarting the application.");
          dialog.accept();
        } else {
          QMessageBox::warning(&dialog, "Restore Failed",
            "Some files could not be restored.\nCheck the log for details.");
        }
      }
    });

  dialog.exec();
  return restored;
}
