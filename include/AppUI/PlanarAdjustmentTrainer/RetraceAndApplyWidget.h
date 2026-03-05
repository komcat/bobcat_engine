// RetraceAndApplyWidget.h - Combined retrace P1 -> P2 -> recompute -> apply correction
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include "PlanarAdjustmentData.h"

struct ApplicationContext;

/**
 * @brief Single wizard that walks through:
 *   Step 1: User manually jogs to P1, confirms -> overwrites saved P1
 *   Step 2: System MoveRelative by taught delta to P2, user adjusts, confirms -> overwrites saved P2
 *   Step 3: Recompute angles from new P1/P2, show result, apply correction to hex
 *
 * Requires a valid training set with both points taught and correction configured.
 */
class RetraceAndApplyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RetraceAndApplyWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(PlanarAdjustmentSet* set);

signals:
    void retraceCompleted(const PlanarAdjustmentSet& updatedSet);
    void correctionApplied(double value);

private slots:
    void onStartRetrace();
    void onConfirmP1();
    void onConfirmP2();
    void onApplyCorrection();
    void onReset();
    void updatePositionDisplay();
    void updateHexPositionDisplay();

private:
    enum WizardState { Idle, WaitP1, WaitP2, ReviewApply, Done };

    void setupUI();
    void setState(WizardState state);

    ApplicationContext* m_context;
    PlanarAdjustmentSet* m_set = nullptr;

    // Taught delta (computed from original P1/P2 before retrace)
    double m_taughtDeltaX = 0.0;
    double m_taughtDeltaY = 0.0;
    double m_taughtDeltaZ = 0.0;

    WizardState m_state = Idle;

    QStackedWidget* m_wizardStack;

    // Page 0: Idle
    QLabel* m_idleSummaryLabel;
    QPushButton* m_startBtn;

    // Page 1: Wait for P1 confirm
    QLabel* m_p1InstructionLabel;
    QLabel* m_p1PositionLabel;
    QPushButton* m_confirmP1Btn;
    QPushButton* m_cancelBtn;

    // Page 2: Wait for P2 confirm
    QLabel* m_p2InstructionLabel;
    QLabel* m_p2PositionLabel;
    QPushButton* m_confirmP2Btn;
    QPushButton* m_cancelP2Btn;

    // Page 3: Review + Apply
    QLabel* m_reviewLabel;
    QLabel* m_hexPositionLabel;
    QPushButton* m_applyBtn;
    QLabel* m_resultLabel;

    // Page 4: Done
    QLabel* m_doneLabel;
    QPushButton* m_resetBtn;

    QTimer* m_positionTimer;
    QTimer* m_hexTimer;
};
