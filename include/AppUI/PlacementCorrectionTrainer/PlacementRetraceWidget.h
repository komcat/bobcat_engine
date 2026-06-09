// PlacementRetraceWidget.h - Retrace wizard for placement correction
//
// This widget is used in two contexts:
//   1. Standalone: inside PlacementCorrectionPage (the trainer UI, F8 shortcut)
//   2. Recipe execution: embedded in RunProgramPage's execution stack (index 6)
//
// RECIPE INTEGRATION FLOW:
//   Recipe step "Placement Correction Retrace"
//     → PlacementCorrectionProcesses.cpp builds: system.PlacementCorrectionRetrace
//     → SystemCommandHandler invokes m_placementRetraceCallback (blocking)
//     → RunProgramPage callback: emits showPlacementRetrace signal, enters QEventLoop
//     → RunProgramPage::onShowPlacementRetrace: loads set, calls SetData + AutoStart
//     → This widget runs the 6-state wizard
//     → On save: emits retraceCompleted (does NOT quit event loop — user stays here)
//     → On "Continue" click: emits retraceSkipped → RunProgramPage quits event loop
//     → On "End" click: emits retraceEnded → RunProgramPage quits loop with false
//     → SystemCommandHandler returns true/false → recipe continues or stops
//
// SIGNALS AND RECIPE ADVANCEMENT:
//   retraceCompleted — position saved, but recipe does NOT advance yet.
//     The user can still "Move to Target" or click "Continue".
//   retraceSkipped   — user clicked Continue (after save) or Skip (no save).
//     This is what actually advances the recipe to the next step.
//   retraceEnded     — user clicked End. Recipe stops (returns false).
//
// IMPORTANT: If you change the signal flow or add new signals, update the
// corresponding handlers in RunProgramPage.cpp (onPlacementRetraceCompleted,
// onPlacementRetraceSkipped, onPlacementRetraceEnded) to keep recipe
// advancement behavior correct.
//
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <future>
#include <functional>
#include <atomic>
#include "PlacementCorrectionData.h"
#include "PlacementRetraceLogger.h"
#include "TransformationMatrixLoader.h"

struct ApplicationContext;

/**
 * @brief 6-state wizard that retraces ref/target points, computes correction
 * vector from master vector vs actual vector, and calculates PLACE_ADJ.
 *
 * States: Idle -> WaitRefP1 -> WaitRefP2 -> WaitNrmP1 -> WaitNrmP2 -> Review
 */
class PlacementRetraceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlacementRetraceWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(PlacementCorrectionSet* set);

    // Programmatically trigger start (for recipe integration — skips Idle page)
    void AutoStart();

    // Set the mode string written to the retrace log for subsequent runs.
    // "manual" (default, standalone trainer), "autoRunAll", or "recipe".
    // Callers (e.g. RunProgramPage) should set this before SetData.
    void SetMode(const std::string& mode) { m_logMode = mode; }

    // Recipe automation flags — set after SetData() but before AutoStart().
    // Auto-detect: enables m_autoRunAll so vision auto-detects all 4 points.
    // On vision failure, pauses for manual confirm, then resumes auto-detecting.
    void SetAutoDetect(bool enabled) { m_autoDetectFromRecipe = enabled; }
    // Auto-save-continue: after computation in Review, automatically saves
    // corrected position, moves to target, and emits retraceSkipped.
    void SetAutoSaveContinue(bool enabled) { m_autoSaveContinue = enabled; }

signals:
    void retraceCompleted(const PlacementCorrectionSet& updatedSet);
    void retraceSkipped();   // User reviewed results but chose to continue without saving
    void retraceEnded();
    // Emitted when any manual tune offset spinbox is edited. The current
    // m_set already reflects the new values (pointer semantics); the host
    // page should persist the training set JSON on this signal.
    void offsetsPersistRequested();

private slots:
    void onStartRetrace();
    void onAutoRunAll();
    void onConfirmRefP1();
    void onConfirmRefP2();
    void onConfirmNrmP1();
    void onConfirmNrmP2();
    void onAutoDetectRefP1();
    void onAutoDetectRefP2();
    void onAutoDetectNrmP1();
    void onAutoDetectNrmP2();
    void onSaveToTarget();
    void onMoveToTarget();
    void onSkip();
    void onReset();
    void onEnd();
    void updatePositionDisplay();
    void onTuneOffsetChanged();

private:
    enum WizardState { Idle, WaitRefP1, WaitRefP2, WaitNrmP1, WaitNrmP2, Review };

    void setupUI();
    void setState(WizardState state);
    void autoNavigateToRefP1();
    void autoNavigateToNrmP1();
    void applyCameraExposureAsync(double exposureUs);
    void loadReferenceImage(QLabel* label, const std::string& imageFile);
    std::string getImageDirectory() const;
    void runMotionAsync(std::function<bool()> motionFunc, std::function<void(bool)> onComplete);
    void computeRetraceResults();
    void setHexVelocity(double velocityMmPerSec);
    void loadTuneOffsetsFromConfig();
    void updateTunePreview();

    // Vision auto-detect: iterate detect→move→detect until residual<5um or 5 iters.
    // step: 1=RefP1, 2=RefP2, 3=NrmP1, 4=NrmP2
    void runAutoDetect(int step, const std::string& profilePath);

    // Auto-save-continue chain: save -> move to target -> emit retraceSkipped.
    // Triggered from setState(Review) when m_autoSaveContinue is true.
    // Falls back to manual review on save or move failure.
    void performAutoSaveContinue();
    // Show/hide auto-detect buttons based on whether a profile path is set on the
    // current step's PlacementLinePoints. Called from setState entries.
    void updateAutoDetectVisibility();

    // Async position polling to avoid blocking UI thread with hardware queries
    std::future<void> m_positionFuture;
    double m_cachedPosX = 0.0, m_cachedPosY = 0.0, m_cachedPosZ = 0.0;
    std::atomic<bool> m_positionReady{false};

    ApplicationContext* m_context;
    PlacementCorrectionSet* m_set = nullptr;

    // Master vector (from teach, saved at retrace start)
    double m_masterVecX = 0.0;   // gantry global dX
    double m_masterVecY = 0.0;   // gantry global dY
    double m_masterDeltaAngle = 0.0;

    // Taught deltas for P1->P2 navigation
    double m_refDeltaX = 0.0, m_refDeltaY = 0.0, m_refDeltaZ = 0.0;
    double m_nrmDeltaX = 0.0, m_nrmDeltaY = 0.0, m_nrmDeltaZ = 0.0;

    // Retraced line data
    PlacementLinePoints m_retracedRef;
    PlacementLinePoints m_retracedNrm;

    // Live hex position captured at review time
    double m_liveHexX = 0.0, m_liveHexY = 0.0, m_liveHexZ = 0.0;
    double m_liveHexU = 0.0, m_liveHexV = 0.0, m_liveHexW = 0.0;

    // Computed correction results
    double m_correctedX = 0.0, m_correctedY = 0.0, m_correctedZ = 0.0;
    double m_correctedU = 0.0, m_correctedV = 0.0, m_correctedW = 0.0;

    // Extra correction fields captured by computeRetraceResults for the log
    double m_logFrameDeltaTheta = 0.0;
    double m_logUCorrection = 0.0;   // applied U delta = Am − At (post reverseSign)
    double m_logRotatedOffsetX = 0.0, m_logRotatedOffsetY = 0.0;
    double m_logIdealTargetX = 0.0,   m_logIdealTargetY = 0.0;
    double m_logCorrGlobalX = 0.0,    m_logCorrGlobalY = 0.0;
    double m_logCorrLocalX = 0.0, m_logCorrLocalY = 0.0, m_logCorrLocalZ = 0.0;

    // Per-step meta (refP1, refP2, nrmP1, nrmP2). Populated by confirm/auto-detect.
    RetracePointMeta m_stepMeta[4];

    // Recipe automation flags (reset in SetData, set by RunProgramPage before AutoStart)
    bool m_autoDetectFromRecipe = false;  // Enable auto-detect chain with manual fallback
    bool m_autoSaveContinue = false;      // Auto-save + move + continue after Review computation

    // Log dispatch state
    std::string m_logMode = "manual";   // "manual" | "autoRunAll" | "recipe"
    bool m_loggedThisRun = false;       // prevents duplicate log on save→continue

    // Helper: build and append a log entry for the given outcome.
    void writeLogEntry(const std::string& outcome,
                       const std::string& failureStep = std::string(),
                       const std::string& failureDetail = std::string());

    // Transform
    TransformationMatrixLoader m_matrixLoader;
    bool m_matrixLoaded = false;

    WizardState m_state = Idle;
    QStackedWidget* m_wizardStack = nullptr;
    QTimer* m_positionTimer = nullptr;
    std::future<bool> m_motionFuture;
    QTimer* m_motionPollTimer = nullptr;
    std::function<void(bool)> m_motionCallback;

    // Page 0: Idle
    QLabel* m_idleSummaryLabel = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_autoRunAllBtn = nullptr;

    // Auto-Run-All chaining flag — when true, completed auto-nav / P2-move
    // events automatically trigger the next auto-detect step.
    bool m_autoRunAll = false;

    // Page 1: WaitRefP1
    QLabel* m_refP1InstructionLabel = nullptr;
    QLabel* m_refP1PositionLabel = nullptr;
    QLabel* m_refP1ImageLabel = nullptr;
    QPushButton* m_confirmRefP1Btn = nullptr;
    QPushButton* m_autoDetectRefP1Btn = nullptr;

    // Page 2: WaitRefP2
    QLabel* m_refP2InstructionLabel = nullptr;
    QLabel* m_refP2PositionLabel = nullptr;
    QLabel* m_refP2ImageLabel = nullptr;
    QPushButton* m_confirmRefP2Btn = nullptr;
    QPushButton* m_autoDetectRefP2Btn = nullptr;

    // Page 3: WaitNrmP1
    QLabel* m_nrmP1InstructionLabel = nullptr;
    QLabel* m_nrmP1PositionLabel = nullptr;
    QLabel* m_nrmP1ImageLabel = nullptr;
    QPushButton* m_confirmNrmP1Btn = nullptr;
    QPushButton* m_autoDetectNrmP1Btn = nullptr;

    // Page 4: WaitNrmP2
    QLabel* m_nrmP2InstructionLabel = nullptr;
    QLabel* m_nrmP2PositionLabel = nullptr;
    QLabel* m_nrmP2ImageLabel = nullptr;
    QPushButton* m_confirmNrmP2Btn = nullptr;
    QPushButton* m_autoDetectNrmP2Btn = nullptr;

    // Vision auto-detect background worker
    std::future<void> m_visionFuture;
    std::future<void> m_exposureFuture;

    // Page 5: Review
    QLabel* m_reviewLabel = nullptr;
    QCheckBox* m_applyUCheckbox = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_moveToTargetBtn = nullptr;
    QPushButton* m_skipBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QPushButton* m_endBtn = nullptr;

    // Manual offset tuner (on Review page; applied at Save time)
    QDoubleSpinBox* m_tuneOffsetXSpin = nullptr;
    QDoubleSpinBox* m_tuneOffsetYSpin = nullptr;
    QDoubleSpinBox* m_tuneOffsetZSpin = nullptr;
    QDoubleSpinBox* m_tuneOffsetUSpin = nullptr;
    QDoubleSpinBox* m_tuneOffsetVSpin = nullptr;
    QDoubleSpinBox* m_tuneOffsetWSpin = nullptr;
    QLabel* m_tunePreviewLabel = nullptr;
    QPushButton* m_tuneResetBtn = nullptr;

    // Hex velocity preset buttons
    QPushButton* m_vel01Btn = nullptr;
    QPushButton* m_vel005Btn = nullptr;
};
