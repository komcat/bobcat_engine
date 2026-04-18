// EdgeTeachWidget.h - Two-point XYZ capture widget for Edge Transfer Trainer
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <future>
#include "EdgeTransferData.h"

struct ApplicationContext;

/**
 * @brief Step-by-step widget for capturing two XYZ points from gantry-main.
 *
 * States: Idle -> TeachingP1 -> TeachingP2 -> Captured
 * After P2, computes edge metrics and emits edgeCaptured(EdgePoints).
 *
 * Shared by the Reference and Target teaching pages.
 */
class EdgeTeachWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EdgeTeachWidget(const QString& edgeName,
                             ApplicationContext* context,
                             QWidget* parent = nullptr);

    void SetDeviceName(const std::string& deviceName);   // measurement device (gantry)
    void SetCameraId(const std::string& cameraId);       // "" disables image capture
    void SetEdge(const EdgePoints& e);
    const EdgePoints& GetEdge() const { return m_edge; }

signals:
    void edgeCaptured(const EdgePoints& e);

private slots:
    void onCaptureP1();
    void onCaptureP2();
    void onReteach();
    void updatePositionDisplay();
    void onAutoNavP1();
    void onAutoNavP2();
    void onStopAutoNav();

private:
    enum WizardState { Idle, TeachingP1, TeachingP2, Captured };

    void setupUI();
    void setState(WizardState state);

    // Captures a BMP image from the configured camera into the product's
    // trainer image directory. Returns filename (relative to that dir) or "".
    std::string capturePointImage(const std::string& pointLabel);

    // Background safe-mover for Auto-Nav: SetSystemVelocity(0.5) -> MoveToAbs ->
    // SetSystemVelocity(50). Disables all auto-nav buttons while running.
    // `which` is "P1" or "P2" (used for user-facing status messages).
    void startAutoNavAsync(double tx, double ty, double tz, const QString& which);

    std::future<void> m_navFuture;
    std::string m_navActiveDevice;

    ApplicationContext* m_context;
    std::string m_deviceName;
    std::string m_cameraId;
    QString m_edgeName;
    EdgePoints m_edge;
    WizardState m_state = Idle;

    QStackedWidget* m_wizardStack;

    // Page 0: Idle
    QLabel* m_idleLabel;
    QPushButton* m_startTeachBtn;

    // Page 1: Teaching P1
    QLabel* m_p1InstructionLabel;
    QLabel* m_p1PositionLabel;
    QPushButton* m_autoNavP1BtnTeach = nullptr;
    QPushButton* m_captureP1Btn;
    QPushButton* m_cancelP1Btn;

    // Page 2: Teaching P2
    QLabel* m_p2InstructionLabel;
    QLabel* m_p2PositionLabel;
    QPushButton* m_autoNavP2BtnTeach = nullptr;
    QPushButton* m_captureP2Btn;
    QPushButton* m_cancelP2Btn;

    // Page 3: Captured summary
    QLabel* m_summaryLabel;
    QPushButton* m_autoNavP1BtnCaptured = nullptr;
    QPushButton* m_autoNavP2BtnCaptured = nullptr;
    QPushButton* m_reteachBtn;

    // Global Stop button (outside the stack) — only visible while an
    // Auto-Nav move is in progress. Invokes EmergencyStop(gantry-main).
    QPushButton* m_stopNavBtn = nullptr;

    QTimer* m_positionTimer;
};
