// ConfigBackupManager.h - Configuration backup and restore manager
#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include <filesystem>
#include <string>
#include <vector>

struct ApplicationContext;

// Backup scope selection for backup/restore operations
struct BackupScope {
  bool rootConfig = true;
  bool systemFolder = true;
  bool productFolder = true;
};

// Metadata for a stored backup point
struct BackupManifest {
  QString timestamp;
  QString description;
  QString productName;
  QStringList scopes;   // "root", "SYSTEM", product name
  QString folderPath;   // absolute path to backup folder
};

/**
 * @brief Manages configuration backup and restore operations
 *
 * Handles creating timestamped backups of config/, config/SYSTEM/, and
 * product-specific folders, writing manifests, and restoring from backup points.
 * The backup destination path is persisted in QSettings.
 */
class ConfigBackupManager
{
public:
  explicit ConfigBackupManager(ApplicationContext* context);

  // --- Core operations ---

  /// Create a backup of selected scopes to destBase/<timestamp>/
  [[nodiscard]] bool performBackup(const BackupScope& scope,
                                   const QString& description,
                                   const std::filesystem::path& destBase);

  /// Restore selected scopes from a backup
  [[nodiscard]] bool performRestore(const BackupManifest& manifest,
                                    const BackupScope& scope);

  /// Delete a backup folder
  [[nodiscard]] bool deleteBackup(const BackupManifest& manifest);

  /// Scan the backup base directory for available backups (newest first)
  std::vector<BackupManifest> loadAvailableBackups();

  // --- Path helpers ---

  std::filesystem::path getConfigBasePath() const;
  std::filesystem::path getBackupBasePath() const;

  /// Get the current product name (empty if none loaded)
  std::string getCurrentProductName() const;

  // --- Persisted settings ---

  /// Save the backup destination path to QSettings
  void saveDestinationPath(const QString& path);

  // --- UI dialogs ---

  /// Show the backup dialog. Returns true if a backup was created.
  bool showBackupDialog(QWidget* parent);

  /// Show the restore dialog. Returns true if a restore was performed.
  bool showRestoreDialog(QWidget* parent);

private:
  [[nodiscard]] bool copyRootConfigFiles(const std::filesystem::path& src,
                                         const std::filesystem::path& dst);
  [[nodiscard]] bool copyDirectoryRecursive(const std::filesystem::path& src,
                                            const std::filesystem::path& dst);
  [[nodiscard]] bool writeManifest(const std::filesystem::path& backupDir,
                                   const BackupScope& scope,
                                   const QString& description);

  ApplicationContext* m_context;
};
