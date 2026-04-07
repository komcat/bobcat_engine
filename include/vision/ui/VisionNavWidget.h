// VisionNavWidget.h
// Left navigation panel for wizard-style VisionManagerPage
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QTabWidget>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QFileSystemWatcher>
#include <QDialog>

struct ApplicationContext;
struct VisionWizardState;

// ========== Plugin Selection Dialog ==========
class PluginSelectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit PluginSelectionDialog(QWidget* parent = nullptr);

    void setPluginsByCategory(const QStringList& prPlugins,
                               const QStringList& measurePlugins,
                               const QStringList& filterPlugins,
                               const QStringList& calibrationPlugins);

    QString getSelectedPlugin() const { return m_selectedPlugin; }

private:
    QTabWidget* m_tabs;
    QListWidget* m_prList;
    QListWidget* m_measureList;
    QListWidget* m_filterList;
    QListWidget* m_calibrationList;
    QString m_selectedPlugin;
};

// ========== Profile Selection Dialog ==========
class ProfileSelectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProfileSelectionDialog(const QString& visionDir, QWidget* parent = nullptr);

    QString getSelectedFilePath() const { return m_selectedFilePath; }

private:
    QListWidget* m_profileList;
    QString m_selectedFilePath;
    QString m_visionDir;
};

// ========== VisionNavWidget ==========
class VisionNavWidget : public QWidget {
    Q_OBJECT

public:
    explicit VisionNavWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void updateCautionIcons(const VisionWizardState& state);
    void setCurrentStep(int step);
    void setProfileName(const QString& name);
    void setStatusText(const QString& text);
    void setVisionDirectory(const QString& dir);
    void refreshProfileList();
    void setSelectedPluginName(const QString& name);

    // Plugin list data (stored for dialog)
    void updatePluginsByCategory(const QStringList& prPlugins,
                                  const QStringList& measurePlugins,
                                  const QStringList& filterPlugins,
                                  const QStringList& calibrationPlugins = QStringList());

signals:
    void navItemChanged(int step);
    void newProfileClicked();
    void loadProfileClicked();
    void saveProfileClicked();
    void saveAsProfileClicked();
    void deleteProfileClicked();
    void profileLoadRequested(const QString& filePath);
    void profileDeleted(const QString& filePath);
    void profileRenamed(const QString& oldPath, const QString& newPath);
    void imageModeChanged(bool isLive);
    void pluginSelected(const QString& pluginName);

private slots:
    void onNavItemChanged(int currentRow);
    void onProfileDoubleClicked(QListWidgetItem* item);
    void onProfileContextMenu(const QPoint& pos);
    void onDirectoryChanged(const QString& path);
    void onToggleProfileList();
    void onSelectPluginClicked();

private:
    void setupUI();
    void setupProfileGroup(QVBoxLayout* parentLayout);
    void setupPluginSelector(QVBoxLayout* parentLayout);
    void setupStepGroup(QVBoxLayout* parentLayout);
    void setupImageModeGroup(QVBoxLayout* parentLayout);

    void deleteProfile(const QString& filePath);
    void renameProfile(const QString& filePath);

    ApplicationContext* m_context;

    // Profile group
    QGroupBox* m_profileGroup;
    QLabel* m_profileNameLabel;
    QPushButton* m_newBtn;
    QPushButton* m_loadBtn;
    QPushButton* m_saveBtn;
    QPushButton* m_saveAsBtn;
    QPushButton* m_deleteBtn;
    QPushButton* m_toggleListBtn;
    QListWidget* m_profileList;
    bool m_profileListVisible = false;

    // Plugin selector (button + label)
    QPushButton* m_selectPluginBtn;
    QLabel* m_selectedPluginLabel;
    QStringList m_prPlugins;
    QStringList m_measurePlugins;
    QStringList m_filterPlugins;
    QStringList m_calibrationPlugins;

    // Step navigation
    QListWidget* m_navList;

    // Image mode
    QPushButton* m_liveBtn;
    QPushButton* m_frameBtn;
    bool m_isLiveMode = false;

    // Status
    QLabel* m_statusLabel;

    // File system watcher
    QString m_visionDir;
    QFileSystemWatcher* m_watcher;
};
