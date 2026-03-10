// AccuracyTrainerPage.h - Main page with left list + right stacked widget
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include "AccuracyTrainerData.h"
#include "AccuracyTrainerStore.h"

struct ApplicationContext;
class TeachBondingWidget;
class TeachReferenceWidget;
class RunAlignmentWidget;
class MultiDeviceRunAlignmentWidget;
class TeachMultiDeviceWidget;
class EditZOffsetWidget;

/**
 * @brief Main Accuracy Trainer page: left nav (30%) + right content (70%)
 */
class AccuracyTrainerPage : public QWidget
{
    Q_OBJECT

public:
    explicit AccuracyTrainerPage(ApplicationContext* context, QWidget* parent = nullptr);

    /** Hide/show the built-in top bar (back button + title).
     *  Call setTopBarVisible(false) when embedding inside TrainerContainerPage. */
    void setTopBarVisible(bool visible);

signals:
    void backToMainMenu();

private slots:
    void onNavItemChanged(int currentRow);
    void onNewTrainingSet();
    void onLoadTrainingSet();
    void onDeleteTrainingSet();
    void onSaveTrainingSet();
    void onDeviceChanged(const QString& deviceName);

private:
    void setupUI();
    void refreshTrainingSetList();
    void updateCurrentTrainingSet();
    void setTrainingSetLoaded(bool loaded);
    void updateStoreBasePath();

    ApplicationContext* m_context;
    AccuracyTrainerStore m_store;
    TrainingSet m_currentSet;
    bool m_hasLoadedSet = false;

    // Left panel
    QListWidget* m_navList;
    QLabel* m_trainingSetLabel;
    QPushButton* m_newBtn;
    QPushButton* m_loadBtn;
    QPushButton* m_deleteBtn;
    QPushButton* m_saveBtn;
    QLabel* m_statusLabel;

    // Right panel
    QStackedWidget* m_contentStack;
    TeachBondingWidget* m_teachBondingWidget;
    TeachReferenceWidget* m_teachReferenceWidget;
    RunAlignmentWidget* m_runAlignmentWidget;
    MultiDeviceRunAlignmentWidget* m_multiDeviceWidget;
    TeachMultiDeviceWidget* m_teachMultiDeviceWidget;
    EditZOffsetWidget* m_editZOffsetWidget;
    QWidget* m_welcomeWidget;

    // Device selection
    QComboBox* m_deviceCombo;

    // Top bar container (back button + title) — can be hidden when embedded
    QWidget* m_topBarWidget;
};
