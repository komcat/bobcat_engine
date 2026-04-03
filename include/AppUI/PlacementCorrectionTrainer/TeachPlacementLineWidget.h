// TeachPlacementLineWidget.h - Two-point capture widget for placement correction lines
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include "PlacementCorrectionData.h"

struct ApplicationContext;

/**
 * @brief Step-by-step widget for capturing two XY points from gantry-main to form a line.
 *
 * States: Idle -> TeachingP1 -> TeachingP2 -> Captured
 * After P2, computes line angle and emits lineCaptured(PlacementLinePoints).
 */
class TeachPlacementLineWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TeachPlacementLineWidget(const QString& lineName,
                                      ApplicationContext* context,
                                      QWidget* parent = nullptr);

    void SetDeviceName(const std::string& deviceName);
    void SetCameraId(const std::string& cameraId);
    void SetLine(const PlacementLinePoints& lp);
    const PlacementLinePoints& GetLine() const { return m_line; }

signals:
    void lineCaptured(const PlacementLinePoints& lp);

private slots:
    void onCaptureP1();
    void onCaptureP2();
    void onReteach();
    void updatePositionDisplay();

private:
    enum WizardState { Idle, TeachingP1, TeachingP2, Captured };

    void setupUI();
    void setState(WizardState state);

    void applyCameraExposure(double exposureUs);
    std::string capturePointImage(const std::string& pointLabel);

    ApplicationContext* m_context;
    std::string m_deviceName;
    std::string m_cameraId;
    QString m_lineName;
    PlacementLinePoints m_line;
    WizardState m_state = Idle;

    QStackedWidget* m_wizardStack;

    // Page 0: Idle
    QLabel* m_idleLabel;
    QPushButton* m_startTeachBtn;

    // Page 1: Teaching P1
    QLabel* m_p1InstructionLabel;
    QLabel* m_p1PositionLabel;
    QSpinBox* m_p1ExposureSpin;
    QPushButton* m_applyP1ExposureBtn;
    QPushButton* m_captureP1Btn;
    QPushButton* m_cancelP1Btn;

    // Page 2: Teaching P2
    QLabel* m_p2InstructionLabel;
    QLabel* m_p2PositionLabel;
    QSpinBox* m_p2ExposureSpin;
    QPushButton* m_applyP2ExposureBtn;
    QPushButton* m_captureP2Btn;
    QPushButton* m_cancelP2Btn;

    // Page 3: Captured
    QLabel* m_summaryLabel;
    QPushButton* m_reteachBtn;

    QTimer* m_positionTimer;
};
