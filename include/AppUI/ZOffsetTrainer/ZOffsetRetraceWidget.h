// ZOffsetRetraceWidget.h - Combined retrace: recapture ref+target -> recompute -> apply Z correction
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include "ZOffsetData.h"

struct ApplicationContext;

/**
 * @brief Single wizard that walks through:
 *   Step 1: User jogs to reference surface, confirms -> overwrites saved reference
 *   Step 2: User jogs to target surface, confirms -> overwrites saved target
 *   Step 3: Recompute Z diff, show result, apply correction to hex Z
 *
 * Requires a valid training set with both points taught and correction configured.
 */
class ZOffsetRetraceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ZOffsetRetraceWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(ZOffsetSet* set);

signals:
    void retraceCompleted(const ZOffsetSet& updatedSet);
    void correctionApplied(double valueMm);

private slots:
    void onStartRetrace();
    void onConfirmReference();
    void onConfirmTarget();
    void onApplyCorrection();
    void onReset();
    void updatePositionDisplay();
    void updateHexPositionDisplay();

private:
    enum WizardState { Idle, WaitReference, WaitTarget, ReviewApply, Done };

    void setupUI();
    void setState(WizardState state);

    ApplicationContext* m_context;
    ZOffsetSet* m_set = nullptr;

    WizardState m_state = Idle;

    QStackedWidget* m_wizardStack;

    // Page 0: Idle
    QLabel* m_idleSummaryLabel;
    QPushButton* m_startBtn;

    // Page 1: Wait for Reference confirm
    QLabel* m_refInstructionLabel;
    QLabel* m_refPositionLabel;
    QPushButton* m_confirmRefBtn;
    QPushButton* m_cancelBtn;

    // Page 2: Wait for Target confirm
    QLabel* m_tgtInstructionLabel;
    QLabel* m_tgtPositionLabel;
    QPushButton* m_confirmTgtBtn;
    QPushButton* m_cancelTgtBtn;

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
