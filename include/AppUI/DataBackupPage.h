// DataBackupPage.h - Product data backup and restore management
#pragma once

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QListWidget>
#include <QTextEdit>
#include <QTimer>
#include <QJsonObject>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

struct ApplicationContext;

class DataBackupPage : public QWidget
{
  Q_OBJECT

public:
  explicit DataBackupPage(ApplicationContext* context, QWidget* parent = nullptr);

public slots:
  void onProductChanged();

signals:
  void backToMainMenu();

private:
  struct BackupResult {
    bool localOk  = false;
    bool remoteOk = false;
    bool skipped  = false;
    QString localPath;
    QString remotePath;
    QString error;
  };

private slots:
  void onCreateSavePoint();
  void onAutoBackupTimer();
  void onBackupSelected(int row);
  void onRestoreClicked();
  void onDeleteClicked();
  void onVerifyClicked();
  void onAutoBackupToggled(bool enabled);
  void onShowChanges();
  void onBackupComplete(const BackupResult& result, const QString& type);

private:
  void setupUI();

  [[nodiscard]] BackupResult performBackupWork(const QString& type, const QString& name,
                                                const QString& description,
                                                const std::filesystem::path& productDir,
                                                const std::filesystem::path& localBase,
                                                const std::filesystem::path& remoteBase,
                                                const QString& folderName);
  [[nodiscard]] BackupResult performAutoBackupWork(
      const std::filesystem::path& productDir,
      const std::filesystem::path& localBase,
      const std::filesystem::path& remoteBase,
      const QString& folderName,
      const std::filesystem::path& latestManifestPath);
  void startBackupAsync(const QString& type, const QString& name, const QString& description);
  void startAutoBackupAsync();
  [[nodiscard]] bool restoreFromBackup(const QString& backupPath);
  [[nodiscard]] bool copyDirectoryRecursive(const std::filesystem::path& src,
                                             const std::filesystem::path& dst);
  QString computeFileHash(const QString& filePath);
  QJsonObject computeDirectoryHashes(const std::filesystem::path& dir,
                                      const std::filesystem::path& baseDir);
  [[nodiscard]] bool writeManifest(const std::filesystem::path& backupDir,
                                    const QString& type,
                                    const QString& name,
                                    const QString& description,
                                    const QJsonObject& fileHashes);
  struct FileChangeSet {
    QStringList added;      // files in current product but not in backup
    QStringList modified;   // files in both but hash differs
    QStringList deleted;    // files in backup but not in current product
  };
  FileChangeSet computeChangesVsBackup(int backupIndex) const;

  void refreshBackupList();
  void updateDetailPanel(int index);
  void pruneAutoBackups();
  void clearDetailPanel();

  std::filesystem::path getProductDirPath() const;
  std::filesystem::path getBackupBasePath() const;
  std::filesystem::path getAppDataBackupPath() const;
  QString getCurrentProductName() const;
  void saveAutoBackupSettings();
  void loadAutoBackupSettings();

  ApplicationContext* m_context;

  // Top section
  QLabel*      m_productLabel;
  QLineEdit*   m_descriptionEdit;
  QPushButton* m_createBtn;
  QPushButton* m_showChangesBtn;
  QPushButton* m_backBtn;
  QCheckBox*   m_autoBackupCheck;
  QSpinBox*    m_autoIntervalSpin;
  QLabel*      m_autoStatusLabel;

  // Backup list (left)
  QListWidget* m_backupList;
  QLabel*      m_backupCountLabel;

  // Detail panel (right)
  QLabel*      m_detailTimestamp;
  QLabel*      m_detailType;
  QLabel*      m_detailDescription;
  QLabel*      m_detailFileCount;
  QTextEdit*   m_detailFileList;
  QPushButton* m_restoreBtn;
  QPushButton* m_deleteBtn;
  QPushButton* m_verifyBtn;
  QLabel*      m_statusLabel;

  // Auto-backup
  QTimer* m_autoBackupTimer;

  // Background worker
  std::future<BackupResult> m_backupFuture;
  bool m_backupInProgress = false;

  // State
  struct BackupEntry {
    QString folderPath;
    QString timestamp;
    QString type;         // "save_point", "auto", "pre_revert"
    QString name;
    QString description;
    int     fileCount = 0;
  };
  std::vector<BackupEntry> m_backups;
  int m_selectedIndex = -1;

  static constexpr int MAX_AUTO_BACKUPS        = 10;
  static constexpr int MAX_PRE_REVERT_BACKUPS  = 3;
  static constexpr int DEFAULT_AUTO_INTERVAL_MIN = 30;
};
