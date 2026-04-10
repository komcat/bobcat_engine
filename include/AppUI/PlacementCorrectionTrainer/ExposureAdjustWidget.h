// ExposureAdjustWidget.h - Per-point camera exposure adjustment wizard
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <future>
#include <functional>
#include "PlacementCorrectionData.h"

struct ApplicationContext;

/**
 * @brief 5-state wizard that navigates to each taught point (Ref P1, Ref P2,
 * Tgt P1, Tgt P2), lets the user test and adjust camera exposure, then saves
 * only the exposure value back to the training set.
 *
 * States: Idle -> RefP1 -> RefP2 -> TgtP1 -> TgtP2
 */
class ExposureAdjustWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ExposureAdjustWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(PlacementCorrectionSet* set);

signals:
    void exposureSaved(const PlacementCorrectionSet& updatedSet);
    void adjustEnded();

private slots:
    void onStart();
    void onApplyExposure();
    void onConfirmPoint();
    void onSkipPoint();
    void onEnd();
    void updatePositionDisplay();

private:
    enum PointStep { Idle, RefP1, RefP2, TgtP1, TgtP2 };

    void setupUI();
    void setStep(PointStep step);
    void navigateToPoint();
    void runMotionAsync(std::function<bool()> motionFunc, std::function<void(bool)> onComplete);
    void applyExposureToCamera(double exposureUs);
    double& currentExposureRef();
    QString stepLabel() const;
    int stepNumber() const;

    ApplicationContext* m_context;
    PlacementCorrectionSet* m_set = nullptr;
    PointStep m_step = Idle;

    QStackedWidget* m_stack = nullptr;
    QTimer* m_positionTimer = nullptr;
    std::future<bool> m_motionFuture;
    QTimer* m_motionPollTimer = nullptr;
    std::function<void(bool)> m_motionCallback;

    // Page 0: Idle
    QLabel* m_idleLabel = nullptr;
    QPushButton* m_startBtn = nullptr;

    // Page 1: Active point adjustment (shared for all 4 points)
    QLabel* m_pointTitleLabel = nullptr;
    QLabel* m_positionLabel = nullptr;
    QLabel* m_targetPosLabel = nullptr;
    QSpinBox* m_exposureSpin = nullptr;
    QPushButton* m_applyBtn = nullptr;
    QLabel* m_applyStatusLabel = nullptr;
    QPushButton* m_confirmBtn = nullptr;
    QPushButton* m_skipBtn = nullptr;
    QPushButton* m_endBtn = nullptr;
};
