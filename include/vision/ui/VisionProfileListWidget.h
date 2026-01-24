// VisionProfileListWidget.h
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QFileSystemWatcher>
#include <QString>

struct ApplicationContext;

class VisionProfileListWidget : public QWidget {
    Q_OBJECT

public:
    explicit VisionProfileListWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void refreshProfileList();
    void setVisionDirectory(const QString& dir);
    QString getCurrentProfilePath() const;

signals:
    void profileLoadRequested(const QString& filePath);
    void profileDeleted(const QString& filePath);
    void profileRenamed(const QString& oldPath, const QString& newPath);

private slots:
    void onItemDoubleClicked(QListWidgetItem* item);
    void onCustomContextMenu(const QPoint& pos);
    void onDirectoryChanged(const QString& path);

private:
    void setupUI();
    void loadProfile(const QString& filePath);
    void deleteProfile(const QString& filePath);
    void renameProfile(const QString& filePath);

    ApplicationContext* m_context;
    QListWidget* m_profileList;
    QLabel* m_titleLabel;
    QString m_visionDir;
    QFileSystemWatcher* m_watcher;
};
