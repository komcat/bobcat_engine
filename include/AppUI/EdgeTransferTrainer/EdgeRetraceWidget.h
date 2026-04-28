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
#include "../../sequence/EdgeTransferRetrace.h"

struct ApplicationContext;

class EdgeRetraceWidget : public QWidget
{
    Q_OBJECT

public:
    // Outcome a recipe driver receives when the widget is used in recipe mode.
    // Alias the shared enum so signal signatures stay readable.
    using RecipeOutcome = EdgeTransferRetraceOutcome;

    explicit EdgeRetraceWidget(ApplicationContext* context, QWidget* parent = nullptr);

    // Seeds the wizard with the taught set. Call whenever the active set
    // changes (New / Load / capture). Resets the wizard to Idle.
    void SetData(const EdgeTransferSet& set);

    // Returns the retraced set in its current state (reference + target edges
    // filled in by the user's Confirm captures, with deltas/corrections
    // recomputed on entering Review). Meaningful after the Review state is
    // reached; before that the retraced edges carry the taught coordinates
    // with valid=false.
    const EdgeTransferSet& GetRetracedSet() const { return m_retracedSet; }

    // Switches the widget into "recipe mode" — hides Save/StartOver/Done,
    // shows Skip Correction + Cancel on the Review page, bypasses the
    // "recipe running" guards on auto-nav and apply-corrections, and
    // auto-starts the retrace (WaitRefP1 + auto-nav) as soon as SetData has
    // valid taught data. Must be called BEFORE SetData for the auto-start to
    // trigger.
    void SetRecipeMode(bool enabled);

signals:
    // Emitted when the user accepts the retrace result via
    // "Save Retraced as New Taught". The emitted set has its referenceEdge
    // and targetEdge replaced by the retraced values (Recalculate already
    // called); config is preserved.
    void retraceCompleted(const EdgeTransferSet& updatedSet);

    // Emitted in recipe mode when the user reaches an outcome:
    //   Applied   → Apply Corrections finished successfully
    //   Skipped   → user clicked Skip Correction on the Review page
    //   Cancelled → user clicked Cancel Retrace on a wait page OR Cancel
    //               on the Review page
    void recipeOutcomeReady(EdgeTransferRetraceOutcome outcome);

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
    void onSkipCorrection();
    void onCancelRecipe();
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
    QPushButton* m_skipCorrectionBtn = nullptr;
    QPushButton* m_cancelRecipeBtn = nullptr;
    QLabel* m_applyStatusLabel = nullptr;

    // Recipe-mode state: when true, the widget is driven by a recipe
    // plugin. Cancel + Skip emit recipeOutcomeReady; Apply emits it on
    // success. Idle is skipped; each Wait state auto-triggers Auto-Nav.
    bool m_recipeMode = false;

    // Async state for Apply Corrections: SetSystemVelocity(0.1) per axis →
    // MoveRelative → SetSystemVelocity(1.0) at the end. Runs the axes
    // flagged applyU / applyV / applyW, one at a time.
    std::future<void> m_applyFuture;
    bool m_applyInProgress = false;
};
