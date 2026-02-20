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

/**
 * @brief Main Accuracy Trainer page: left nav (30%) + right content (70%)
 */
class AccuracyTrainerPage : public QWidget
{
    Q_OBJECT

public:
    explicit AccuracyTrainerPage(ApplicationContext* context, QWidget* parent = nullptr);

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

    ApplicationContext* m_context;
    AccuracyTrainerStore m_store;
    TrainingSet m_currentSet;
    bool m_hasLoadedSet = false;

    // Left panel
    QListWidget* m_navList;
    QComboBox* m_trainingSetCombo;
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
    QWidget* m_welcomeWidget;

    // Device selection
    QComboBox* m_deviceCombo;
};
