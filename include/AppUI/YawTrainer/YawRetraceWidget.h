// YawRetraceWidget.h - 7-state wizard for 4-point retrace (2 lines x 2 points)
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include "YawTrainerData.h"

struct ApplicationContext;

/**
 * @brief Retrace wizard that recaptures both reference and target lines (4 points total),
 * recomputes the delta angle, and applies correction to the hex.
 *
 * States: Idle -> WaitRefP1 -> WaitRefP2 -> WaitTgtP1 -> WaitTgtP2 -> ReviewApply -> Done
 *
 * For each line, the taught delta between P1 and P2 is saved before overwrite.
 * After confirming P1, the system auto-moves by the taught delta toward P2.
 */
class YawRetraceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit YawRetraceWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(YawTrainerSet* set);

signals:
    void retraceCompleted(const YawTrainerSet& updatedSet);
    void correctionApplied(double value);

private slots:
    void onStartRetrace();
    void onConfirmRefP1();
    void onConfirmRefP2();
    void onConfirmTgtP1();
    void onConfirmTgtP2();
    void onApplyCorrection();
    void onReset();
    void updatePositionDisplay();
    void updateHexPositionDisplay();

private:
    enum WizardState { Idle, WaitRefP1, WaitRefP2, WaitTgtP1, WaitTgtP2, ReviewApply, Done };

    void setupUI();
    void setState(WizardState state);
    void autoNavigateToRefP1();
    void autoNavigateToTgtP1();

    ApplicationContext* m_context;
    YawTrainerSet* m_set = nullptr;

    // Taught deltas for reference line (P1->P2)
    double m_refDeltaX = 0.0;
    double m_refDeltaY = 0.0;
    double m_refDeltaZ = 0.0;

    // Taught deltas for target line (P1->P2)
    double m_tgtDeltaX = 0.0;
    double m_tgtDeltaY = 0.0;
    double m_tgtDeltaZ = 0.0;

    WizardState m_state = Idle;

    QStackedWidget* m_wizardStack;

    // Page 0: Idle
    QLabel* m_idleSummaryLabel;
    QPushButton* m_startBtn;

    // Page 1: Wait RefP1
    QLabel* m_refP1InstructionLabel;
    QLabel* m_refP1PositionLabel;
    QPushButton* m_confirmRefP1Btn;
    QPushButton* m_cancelBtn;

    // Page 2: Wait RefP2
    QLabel* m_refP2InstructionLabel;
    QLabel* m_refP2PositionLabel;
    QPushButton* m_confirmRefP2Btn;
    QPushButton* m_cancelRefP2Btn;

    // Page 3: Wait TgtP1
    QLabel* m_tgtP1InstructionLabel;
    QLabel* m_tgtP1PositionLabel;
    QPushButton* m_confirmTgtP1Btn;
    QPushButton* m_cancelTgtP1Btn;

    // Page 4: Wait TgtP2
    QLabel* m_tgtP2InstructionLabel;
    QLabel* m_tgtP2PositionLabel;
    QPushButton* m_confirmTgtP2Btn;
    QPushButton* m_cancelTgtP2Btn;

    // Page 5: Review + Apply
    QLabel* m_reviewLabel;
    QLabel* m_hexPositionLabel;
    QPushButton* m_applyBtn;
    QLabel* m_resultLabel;

    // Page 6: Done
    QLabel* m_doneLabel;
    QPushButton* m_resetBtn;

    QTimer* m_positionTimer;
    QTimer* m_hexTimer;
};
