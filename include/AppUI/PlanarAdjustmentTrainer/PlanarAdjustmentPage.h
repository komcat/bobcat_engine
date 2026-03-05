// PlanarAdjustmentPage.h - Main page with left list + right stacked widget
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include "PlanarAdjustmentData.h"
#include "PlanarAdjustmentStore.h"

struct ApplicationContext;
class TeachPointWidget;
class TiltResultWidget;
class ApplyCorrectionWidget;
class RetraceAndApplyWidget;

/**
 * @brief Main Planar Adjustment Trainer page: left nav (30%) + right content (70%)
 */
class PlanarAdjustmentPage : public QWidget
{
    Q_OBJECT

public:
    explicit PlanarAdjustmentPage(ApplicationContext* context, QWidget* parent = nullptr);

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
    PlanarAdjustmentStore m_store;
    PlanarAdjustmentSet m_currentSet;
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
    TeachPointWidget* m_teachPoint1Widget;
    TeachPointWidget* m_teachPoint2Widget;
    TiltResultWidget* m_tiltResultWidget;
    ApplyCorrectionWidget* m_applyCorrectionWidget;
    RetraceAndApplyWidget* m_retraceWidget;

    // Top bar container
    QWidget* m_topBarWidget;
};
