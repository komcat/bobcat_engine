// TeachBondingWidget.h - Wizard for teaching bonding positions via corner capture
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QStackedWidget>
#include <QTimer>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <future>
#include "AccuracyTrainerData.h"

struct ApplicationContext;

/**
 * @brief Step-by-step wizard for teaching bonding positions
 *
 * Flow: Idle -> TeachCorner1 -> TeachCorner2 -> TeachCorner3(opt) -> Summary
 * Tune: Idle -> Tuning -> Idle (adjust X/Y of existing point)
 */
class TeachBondingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TeachBondingWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetTrainingSet(TrainingSet* ts);

signals:
    void bondingPointAdded(const BondingPoint& bp);
    void bondingPointRemoved(int index);
    void bondingPointModified(int index);

private slots:
    void onAddNew();
    void onRemoveSelected();
    void onReteachSelected();
    void onTuneSelected();
    void onCaptureCorner();
    void onSkipCorner3();
    void onConfirmBonding();
    void onRedoBonding();
    void onCancelTeach();
    void onApplyTuneOffset();
    void onTuneGlobalChanged();
    void onTuneLocalChanged();
    void onSaveTune();
    void onCancelTune();
    void updatePositionDisplay();

private:
    enum WizardState { Idle, TeachCorner1, TeachCorner2, TeachCorner3, Summary, Tuning };

    void setupUI();
    void setState(WizardState state);
    void refreshList();
    bool m_tuneSyncing = false;  // prevents recursive global<->local sync

    ApplicationContext* m_context;
    TrainingSet* m_trainingSet = nullptr;

    WizardState m_state = Idle;
    BondingPoint m_pendingPoint;
    int m_tuneIndex = -1;
    int m_reteachIndex = -1;  // >=0 when reteaching an existing point

    // Async move for reteach
    std::future<void> m_moveFuture;

    // UI - Idle page
    QListWidget* m_bondingList;
    QPushButton* m_addNewBtn;
    QPushButton* m_reteachBtn;
    QPushButton* m_removeBtn;
    QPushButton* m_tuneBtn;

    // UI - Teaching pages
    QStackedWidget* m_wizardStack;
    QLabel* m_instructionLabel;
    QLabel* m_positionLabel;
    QPushButton* m_captureBtn;
    QPushButton* m_skipBtn;
    QPushButton* m_cancelBtn;
    QLineEdit* m_nameEdit;

    // UI - Summary page
    QLabel* m_summaryLabel;
    QPushButton* m_confirmBtn;
    QPushButton* m_redoBtn;

    // UI - Tune page
    QLabel* m_tuneInfoLabel;
    QDoubleSpinBox* m_tuneAbsX;
    QDoubleSpinBox* m_tuneAbsY;
    QDoubleSpinBox* m_tuneAbsZ;
    QDoubleSpinBox* m_tuneLocalX;
    QDoubleSpinBox* m_tuneLocalY;
    QDoubleSpinBox* m_tuneOffsetX;
    QDoubleSpinBox* m_tuneOffsetY;
    QPushButton* m_tuneApplyOffsetBtn;
    QPushButton* m_tuneSaveBtn;
    QPushButton* m_tuneCancelBtn;

    // Position polling
    QTimer* m_positionTimer;
};
