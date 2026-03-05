// TeachPointWidget.h - Reusable XYZ capture widget for teaching a single gantry point
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include "PlanarAdjustmentData.h"

struct ApplicationContext;

/**
 * @brief Step-by-step widget for capturing one XYZ point from gantry-main.
 * Used twice in PlanarAdjustmentPage (Point 1 and Point 2).
 *
 * States: Idle -> Teaching -> Captured
 */
class TeachPointWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TeachPointWidget(const QString& pointName,
                              ApplicationContext* context,
                              QWidget* parent = nullptr);

    void SetDeviceName(const std::string& deviceName);
    void SetPoint(const TaughtPoint& pt);
    const TaughtPoint& GetPoint() const { return m_point; }

signals:
    void pointCaptured(const TaughtPoint& pt);

private slots:
    void onCapture();
    void onReteach();
    void updatePositionDisplay();

private:
    enum WizardState { Idle, Teaching, Captured };

    void setupUI();
    void setState(WizardState state);

    ApplicationContext* m_context;
    std::string m_deviceName;
    QString m_pointName;
    TaughtPoint m_point;
    WizardState m_state = Idle;

    QStackedWidget* m_wizardStack;

    // Page 0: Idle
    QLabel* m_idleLabel;
    QPushButton* m_startTeachBtn;

    // Page 1: Teaching
    QLabel* m_instructionLabel;
    QLabel* m_positionLabel;
    QPushButton* m_captureBtn;
    QPushButton* m_cancelBtn;

    // Page 2: Captured
    QLabel* m_summaryLabel;
    QPushButton* m_reteachBtn;

    QTimer* m_positionTimer;
};
