// PlacementRetraceWidget.h - Retrace wizard for placement correction
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include "PlacementCorrectionData.h"
#include "TransformationMatrixLoader.h"

struct ApplicationContext;

/**
 * @brief 6-state wizard that retraces ref/target points, computes correction
 * vector from master vector vs actual vector, and calculates PLACE_ADJ.
 *
 * States: Idle -> WaitRefP1 -> WaitRefP2 -> WaitTgtP1 -> WaitTgtP2 -> Review
 */
class PlacementRetraceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlacementRetraceWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(PlacementCorrectionSet* set);

signals:
    void retraceCompleted(const PlacementCorrectionSet& updatedSet);
    void retraceEnded();

private slots:
    void onStartRetrace();
    void onConfirmRefP1();
    void onConfirmRefP2();
    void onConfirmTgtP1();
    void onConfirmTgtP2();
    void onSaveToTarget();
    void onReset();
    void onEnd();
    void updatePositionDisplay();

private:
    enum WizardState { Idle, WaitRefP1, WaitRefP2, WaitTgtP1, WaitTgtP2, Review };

    void setupUI();
    void setState(WizardState state);
    void autoNavigateToRefP1();
    void autoNavigateToTgtP1();
    void computeRetraceResults();

    ApplicationContext* m_context;
    PlacementCorrectionSet* m_set = nullptr;

    // Master vector (from teach, saved at retrace start)
    double m_masterVecX = 0.0;   // gantry global dX
    double m_masterVecY = 0.0;   // gantry global dY
    double m_masterDeltaAngle = 0.0;

    // Taught deltas for P1->P2 navigation
    double m_refDeltaX = 0.0, m_refDeltaY = 0.0, m_refDeltaZ = 0.0;
    double m_tgtDeltaX = 0.0, m_tgtDeltaY = 0.0, m_tgtDeltaZ = 0.0;

    // Retraced line data
    PlacementLinePoints m_retracedRef;
    PlacementLinePoints m_retracedTgt;

    // Live hex position captured at review time
    double m_liveHexX = 0.0, m_liveHexY = 0.0, m_liveHexZ = 0.0;
    double m_liveHexU = 0.0, m_liveHexV = 0.0, m_liveHexW = 0.0;

    // Computed correction results
    double m_correctedX = 0.0, m_correctedY = 0.0, m_correctedZ = 0.0;
    double m_correctedU = 0.0, m_correctedV = 0.0, m_correctedW = 0.0;

    // Transform
    TransformationMatrixLoader m_matrixLoader;
    bool m_matrixLoaded = false;

    WizardState m_state = Idle;
    QStackedWidget* m_wizardStack = nullptr;
    QTimer* m_positionTimer = nullptr;

    // Page 0: Idle
    QLabel* m_idleSummaryLabel = nullptr;
    QPushButton* m_startBtn = nullptr;

    // Page 1: WaitRefP1
    QLabel* m_refP1InstructionLabel = nullptr;
    QLabel* m_refP1PositionLabel = nullptr;
    QPushButton* m_confirmRefP1Btn = nullptr;

    // Page 2: WaitRefP2
    QLabel* m_refP2InstructionLabel = nullptr;
    QLabel* m_refP2PositionLabel = nullptr;
    QPushButton* m_confirmRefP2Btn = nullptr;

    // Page 3: WaitTgtP1
    QLabel* m_tgtP1InstructionLabel = nullptr;
    QLabel* m_tgtP1PositionLabel = nullptr;
    QPushButton* m_confirmTgtP1Btn = nullptr;

    // Page 4: WaitTgtP2
    QLabel* m_tgtP2InstructionLabel = nullptr;
    QLabel* m_tgtP2PositionLabel = nullptr;
    QPushButton* m_confirmTgtP2Btn = nullptr;

    // Page 5: Review
    QLabel* m_reviewLabel = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QPushButton* m_endBtn = nullptr;
};
