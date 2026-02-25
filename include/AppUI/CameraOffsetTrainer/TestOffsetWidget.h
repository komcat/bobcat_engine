// TestOffsetWidget.h - Test wizard for verifying camera offset calibration
#pragma once

#include <QWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <future>
#include "CameraOffsetData.h"

struct ApplicationContext;

/**
 * @brief Test wizard widget for verifying camera offset calibration.
 *
 * State machine:
 *   Ready -> NavigateToLowMag -> ConfirmLowMag (capture image)
 *         -> NavigateToHighMag -> ConfirmHighMag (capture image)
 *         -> ShowComparison -> ShowImages -> Ready
 *
 * 4 wizard pages:
 *   Page 0 (Ready): Pre-check offsetValid, show saved data summary + "Start Test"
 *   Page 1 (Active): Step label, instruction, live position, Confirm / Run Vision / Cancel
 *   Page 2 (Comparison): Text table - calibrated offset vs tested offset vs deviation
 *   Page 3 (Images): Side-by-side images with position overlay + "Done" button
 */
class TestOffsetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TestOffsetWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetCalibrationSet(CameraOffsetCalibrationSet* cs);

signals:
    void testCompleted(const CameraOffsetCalibrationSet::TestResult& result);

private slots:
    void onStartTest();
    void onConfirm();
    void onRunVision();
    void onCancel();
    void onDone();
    void updatePositionDisplay();

private:
    enum TestState {
        Ready,
        NavigateToLowMag,
        ConfirmLowMag,
        NavigateToHighMag,
        ConfirmHighMag,
        ShowComparison,
        ShowImages
    };

    void setupUI();
    void setState(TestState state);
    void navigateToPosition(const CameraPosition& pos, TestState nextState);

    ApplicationContext* m_context;
    CameraOffsetCalibrationSet* m_calibrationSet;
    TestState m_state;

    // Pending test data
    CameraPosition m_testedLowMagPos;
    CameraPosition m_testedHighMagPos;
    std::string m_lowMagImagePath;
    std::string m_highMagImagePath;

    // UI - Page 0 (Ready)
    QStackedWidget* m_wizardStack;
    QLabel* m_readySummaryLabel;
    QPushButton* m_startTestBtn;

    // UI - Page 1 (Active)
    QLabel* m_stepLabel;
    QLabel* m_instructionLabel;
    QLabel* m_positionLabel;
    QPushButton* m_runVisionBtn;
    QPushButton* m_confirmBtn;
    QPushButton* m_cancelBtn;

    // UI - Page 2 (Comparison)
    QLabel* m_comparisonLabel;
    QPushButton* m_showImagesBtn;

    // UI - Page 3 (Images)
    QLabel* m_lowMagImageLabel;
    QLabel* m_highMagImageLabel;
    QLabel* m_lowMagPosOverlay;
    QLabel* m_highMagPosOverlay;
    QPushButton* m_doneBtn;

    // Position polling
    QTimer m_positionTimer;

    // Async motion
    std::future<void> m_moveFuture;
};
