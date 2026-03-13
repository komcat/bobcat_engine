// YawTrainerPage.h - Main page with left list + right stacked widget
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include "YawTrainerData.h"
#include "YawTrainerStore.h"

struct ApplicationContext;
class TeachLineWidget;
class YawResultWidget;
class YawApplyCorrectionWidget;
class YawRetraceWidget;

/**
 * @brief Main Yaw Trainer page: left nav (30%) + right content (70%)
 */
class YawTrainerPage : public QWidget
{
    Q_OBJECT

public:
    explicit YawTrainerPage(ApplicationContext* context, QWidget* parent = nullptr);

    void setTopBarVisible(bool visible);

signals:
    void backToMainMenu();

private slots:
    void onNavItemChanged(int currentRow);
    void onNewSet();
    void onLoadSet();
    void onDeleteSet();
    void onSaveSet();

private:
    void setupUI();
    void updateCurrentSet();
    void setSetLoaded(bool loaded);
    void updateStoreBasePath();

    ApplicationContext* m_context;
    YawTrainerStore m_store;
    YawTrainerSet m_currentSet;
    bool m_hasLoadedSet = false;

    // Left panel
    QListWidget* m_navList;
    QLabel* m_setLabel;
    QPushButton* m_newBtn;
    QPushButton* m_loadBtn;
    QPushButton* m_deleteBtn;
    QPushButton* m_saveBtn;
    QLabel* m_statusLabel;

    // Right panel
    QStackedWidget* m_contentStack;
    QWidget* m_welcomeWidget;
    TeachLineWidget* m_teachRefLineWidget;
    TeachLineWidget* m_teachTgtLineWidget;
    YawResultWidget* m_yawResultWidget;
    YawApplyCorrectionWidget* m_applyCorrectionWidget;
    YawRetraceWidget* m_retraceWidget;

    // Top bar container
    QWidget* m_topBarWidget;
};
