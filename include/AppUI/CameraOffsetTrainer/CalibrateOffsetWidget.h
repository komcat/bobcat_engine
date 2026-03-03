// CalibrateOffsetWidget.h - 6-step calibration wizard for camera offset
#pragma once

#include <QWidget>
#include <QStackedWidget>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTimer>
#include <future>
#include "CameraOffsetData.h"

struct ApplicationContext;

/**
 * @brief Calibration wizard widget for measuring the XYZ offset
 *        between low-mag and high-mag cameras on gantry-main.
 *
 * State machine:
 *   Idle -> StartCalibration -> NavigateToLowMag -> SetCenterLowMag -> ConfirmLowMag
 *        -> NavigateToHighMag -> SetCenterHighMag -> ConfirmHighMag -> (save, emit) -> Idle
 *
 * 3 wizard pages:
 *   Page 0 (Idle): Summary of existing calibration + "Start Calibration" button
 *   Page 1 (Active): Shared for steps 1-6 - step label, instruction, live XYZ,
 *           vision profile combo + Detect & Center, Confirm / Cancel buttons
 *   Page 2 (Summary): Final offset result + "Done" button
 */
class CalibrateOffsetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CalibrateOffsetWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetCalibrationSet(CameraOffsetCalibrationSet* cs);

signals:
    void calibrationCompleted(const CameraOffsetCalibrationSet& updatedSet);

private slots:
    void onStartCalibration();
    void onConfirm();
    void onDetectCenter();
    void onCancel();
    void onDone();
    void refreshVisionProfiles();
    void updatePositionDisplay();

private:
    enum CalibrationState {
        Idle,
        NavigateToLowMag,
        SetCenterLowMag,
        ConfirmLowMag,
        NavigateToHighMag,
        SetCenterHighMag,
        ConfirmHighMag,
        Summary
    };

    void setupUI();
    void setState(CalibrationState state);
    void navigateToPosition(const CameraPosition& pos, CalibrationState nextState);
    QString getVisionDirectory() const;
    void updatePneumaticStatus();
    void setPneumaticGroupVisible(bool visible);
    bool hasPneumaticSlide() const;
    void onPneumaticToggle();

    ApplicationContext* m_context;
    CameraOffsetCalibrationSet* m_calibrationSet;
    CalibrationState m_state;

    // Pending data during calibration
    CameraPosition m_pendingLowMagPos;
    CameraPosition m_pendingHighMagPos;

    // UI - Page 0 (Idle)
    QStackedWidget* m_wizardStack;
    QLabel* m_idleSummaryLabel;
    QPushButton* m_startBtn;

    // UI - Page 1 (Active)
    QLabel* m_stepLabel;
    QLabel* m_instructionLabel;
    QLabel* m_positionLabel;
    QComboBox* m_visionProfileCombo;
    QPushButton* m_refreshProfilesBtn;
    QPushButton* m_detectCenterBtn;
    QPushButton* m_confirmBtn;
    QPushButton* m_cancelBtn;

    // Pneumatic control (on Page 1)
    QGroupBox* m_pneumaticGroup;
    QLabel* m_pneumaticStatusLabel;
    QPushButton* m_pneumaticBtn;

    // UI - Page 2 (Summary)
    QLabel* m_summaryLabel;
    QPushButton* m_doneBtn;

    // Position polling
    QTimer m_positionTimer;

    // Async motion / vision
    std::future<void> m_moveFuture;
    std::future<void> m_detectFuture;
};
