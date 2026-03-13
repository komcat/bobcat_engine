// ZOffsetPage.h - Main Z Offset Trainer page with left list + right stacked widget
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include "ZOffsetData.h"
#include "ZOffsetStore.h"

struct ApplicationContext;
class TeachZPointWidget;
class ZOffsetResultWidget;
class ZOffsetApplyWidget;
class ZOffsetRetraceWidget;

/**
 * @brief Main Z Offset Trainer page: left nav (30%) + right content (70%)
 */
class ZOffsetPage : public QWidget
{
    Q_OBJECT

public:
    explicit ZOffsetPage(ApplicationContext* context, QWidget* parent = nullptr);

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
    ZOffsetStore m_store;
    ZOffsetSet m_currentSet;
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
    TeachZPointWidget* m_teachRefWidget;
    TeachZPointWidget* m_teachTgtWidget;
    ZOffsetResultWidget* m_resultWidget;
    ZOffsetApplyWidget* m_applyWidget;
    ZOffsetRetraceWidget* m_retraceWidget;

    // Top bar container
    QWidget* m_topBarWidget;
};
