// EdgeRetraceWidget.h - Retrace wizard for Edge Transfer
//
// Re-measures the 4 taught gantry points (Ref P1/P2, Tgt P1/P2) with user
// manual confirmation, then shows a side-by-side review of taught vs.
// retraced values and recomputes deltas / corrections.
//
// States:
//   Idle -> WaitRefP1 -> WaitRefP2 -> WaitTgtP1 -> WaitTgtP2 -> Review
//
// Each "Wait*" state offers:
//   * Auto-Nav (gantry-main at 0.5 mm/s) to the taught coordinate for that point
//   * STOP (EmergencyStop gantry-main) - only while Auto-Nav is running
//   * Confirm (captures current gantry position as the retraced value)
//   * Cancel (abort back to Idle)
//
// The Review page shows per-point deltas (retraced - taught), the new edge
// metrics, and the new suggested corrections. "Save Retraced as New Taught"
// emits retraceCompleted(updatedSet) so the host page can overwrite the
// current set; "Done" just closes the wizard.
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <future>
#include <string>
#include "EdgeTransferData.h"

struct ApplicationContext;

class EdgeRetraceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EdgeRetraceWidget(ApplicationContext* context, QWidget* parent = nullptr);

    // Seeds the wizard with the taught set. Call whenever the active set
    // changes (New / Load / capture). Resets the wizard to Idle.
    void SetData(const EdgeTransferSet& set);

signals:
    // Emitted when the user accepts the retrace result via
    // "Save Retraced as New Taught". The emitted set has its referenceEdge
    // and targetEdge replaced by the retraced values (Recalculate already
    // called); config is preserved.
    void retraceCompleted(const EdgeTransferSet& updatedSet);

private slots:
    void onStartRetrace();
    void onCancel();
    void onAutoNav();
    void onStopAutoNav();
    void onConfirm();
    void onSaveRetraced();
    void onStartOver();
    void onDone();
    void onApplyCorrections();
    void updatePositionDisplay();

private:
    enum WizardState {
        Idle,
        WaitRefP1,
        WaitRefP2,
        WaitTgtP1,
        WaitTgtP2,
        Review
    };

    void setupUI();
    void setState(WizardState s);
    void refreshWaitPage();          // fills instruction / taught / confirm text for current state
    void refreshReview();

    // Background safe-move: SetSystemVelocity(0.5) -> MoveToAbs -> SetSystemVelocity(50).
    void startAutoNavAsync(double tx, double ty, double tz, const QString& which);

    // Returns taught XYZ for the current WaitX state; false if not applicable.
    bool taughtForCurrentState(double& tx, double& ty, double& tz) const;
    // Writes captured XYZ back to the right retraced-edge point.
    void storeCurrentCapture(double x, double y, double z);

    ApplicationContext* m_context;

    // Inputs / outputs
    EdgeTransferSet m_taughtSet;     // unchanged until the user saves
    EdgeTransferSet m_retracedSet;   // built up across wait states

    WizardState m_state = Idle;
    QStackedWidget* m_wizardStack = nullptr;
    QTimer* m_posTimer = nullptr;

    // Nav state (shared across wait pages — one QStackedWidget slot holds them all)
    std::future<void> m_navFuture;
    std::string m_navActiveDevice;

    // -------- Page 0: Idle --------
    QLabel* m_idleLabel = nullptr;
    QPushButton* m_startBtn = nullptr;

    // -------- Pages 1..4: single shared "wait" page (we swap labels per state) --------
    QLabel* m_waitTitleLabel = nullptr;
    QLabel* m_waitTaughtLabel = nullptr;
    QLabel* m_waitLiveLabel = nullptr;
    QLabel* m_confirmHeaderLabel = nullptr;
    QPushButton* m_autoNavBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QPushButton* m_confirmBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;

    // -------- Page 5: Review --------
    QLabel* m_reviewLabel = nullptr;
    QPushButton* m_applyCorrectionsBtn = nullptr;
    QPushButton* m_saveRetracedBtn = nullptr;
    QPushButton* m_startOverBtn = nullptr;
    QPushButton* m_doneBtn = nullptr;
    QLabel* m_applyStatusLabel = nullptr;

    // Async state for Apply Corrections: SetSystemVelocity(0.1) per axis →
    // MoveRelative → SetSystemVelocity(1.0) at the end. Runs the axes
    // flagged applyU / applyV / applyW, one at a time.
    std::future<void> m_applyFuture;
    bool m_applyInProgress = false;
};
