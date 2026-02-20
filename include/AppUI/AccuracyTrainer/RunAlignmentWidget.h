// RunAlignmentWidget.h - Run mode: move to refs, user adjusts, compute transform, move to corrected bonding
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QTextEdit>
#include <future>
#include "AccuracyTrainerData.h"

struct ApplicationContext;

/**
 * @brief Run alignment mode:
 * 1. Move to each reference point
 * 2. User adjusts to actual position
 * 3. Compute rigid transform
 * 4. Move to corrected bonding positions
 */
class RunAlignmentWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RunAlignmentWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetTrainingSet(TrainingSet* ts);

private slots:
    void onStartAlignment();
    void onCaptureActualRef();
    void onMoveToNextBonding();
    void onFinish();
    void updatePositionDisplay();

private:
    enum AlignState {
        Ready,
        MoveToRef,       // Auto-moving to a taught ref
        AdjustRef,       // Waiting for user to adjust
        Computing,       // Computing transform
        MoveToBonding,   // Moving to corrected bonding points
        Done
    };

    void setupUI();
    void setState(AlignState state);
    void moveToCurrentRef();
    void showManualAdjust(int refIdx, size_t refCount, const std::string& refName);
    void autoCaptureRef(int refIdx, const std::string& device);
    bool runVisionDetectCenter(const std::string& profilePath, const std::string& device);
    void computeTransform();
    void moveToCurrentBonding();
    void appendLog(const QString& text);

    ApplicationContext* m_context;
    TrainingSet* m_trainingSet = nullptr;

    AlignState m_state = Ready;
    int m_currentRefIndex = 0;
    int m_currentBondIndex = 0;

    // Actual reference positions (after user adjustment)
    std::vector<ReferencePoint> m_actualRefs;

    // Corrected bonding positions
    struct CorrectedBond {
        std::string name;
        double correctedX = 0.0;
        double correctedY = 0.0;
        double correctedZ = 0.0;
        double deltaX = 0.0;
        double deltaY = 0.0;
    };
    std::vector<CorrectedBond> m_correctedBonds;

    // Live frame computed from actual refs
    double m_liveOriginX = 0.0, m_liveOriginY = 0.0;
    double m_liveXAxisX = 1.0, m_liveXAxisY = 0.0;
    double m_liveYAxisX = 0.0, m_liveYAxisY = 1.0;

    // UI
    QStackedWidget* m_wizardStack;

    // Ready page
    QLabel* m_readyLabel;
    QPushButton* m_startBtn;

    // Ref adjustment page
    QLabel* m_refInstructionLabel;
    QLabel* m_refPositionLabel;
    QPushButton* m_captureRefBtn;
    QPushButton* m_cancelBtn;

    // Bonding page
    QLabel* m_bondInstructionLabel;
    QLabel* m_bondPositionLabel;
    QPushButton* m_nextBondBtn;

    // Done page
    QLabel* m_doneLabel;
    QPushButton* m_finishBtn;

    // Shared log
    QTextEdit* m_logView;

    QTimer* m_positionTimer;

    // Background motion thread (avoid blocking UI)
    std::future<void> m_moveFuture;
};
