// TeachReferenceWidget.h - Wizard for teaching reference features and computing local frame
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QStackedWidget>
#include <QTimer>
#include <future>
#include "AccuracyTrainerData.h"

struct ApplicationContext;

/**
 * @brief Step-by-step wizard for teaching 2-3 reference points and computing local frame
 *
 * Flow: Idle -> TeachRef1 -> TeachRef2 -> TeachRef3(opt) -> Compute/Summary
 * Each step optionally uses a vision profile to auto-detect and center on a feature.
 */
class TeachReferenceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TeachReferenceWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetTrainingSet(TrainingSet* ts);

signals:
    void referenceFrameComputed(const TrainingSet& updatedSet);

private slots:
    void onStartTeach();
    void onCaptureRef();
    void onSkipRef3();
    void onConfirm();
    void onRedo();
    void onCancelTeach();
    void onDetectCenter();
    void refreshVisionProfiles();
    void updatePositionDisplay();

private:
    enum WizardState { Idle, TeachRef1, TeachRef2, TeachRef3, Computed };

    void setupUI();
    void setState(WizardState state);
    void computeFrame();
    QString getVisionDirectory() const;

    ApplicationContext* m_context;
    TrainingSet* m_trainingSet = nullptr;

    WizardState m_state = Idle;
    ReferencePoint m_pendingRefs[3];
    int m_refCount = 0;

    // UI - Idle
    QStackedWidget* m_wizardStack;
    QLabel* m_currentRefsLabel;
    QPushButton* m_startTeachBtn;

    // UI - Teaching
    QLabel* m_instructionLabel;
    QLabel* m_positionLabel;
    QPushButton* m_captureBtn;
    QPushButton* m_skipBtn;
    QPushButton* m_cancelBtn;

    // UI - Vision profile (on teach page)
    QComboBox* m_visionProfileCombo;
    QPushButton* m_detectCenterBtn;
    QPushButton* m_refreshProfilesBtn;

    // UI - Summary
    QLabel* m_summaryLabel;
    QPushButton* m_confirmBtn;
    QPushButton* m_redoBtn;

    QTimer* m_positionTimer;

    // Background detect thread
    std::future<void> m_detectFuture;
};
