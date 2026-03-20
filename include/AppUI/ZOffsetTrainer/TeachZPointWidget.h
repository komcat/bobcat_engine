// TeachZPointWidget.h - Z-focused capture widget for teaching a surface point
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include "ZOffsetData.h"

struct ApplicationContext;

/**
 * @brief Step-by-step widget for capturing one Z point from gantry-main.
 * Used twice in ZOffsetPage (Reference and Target surfaces).
 *
 * States: Idle -> Teaching -> Captured
 */
class TeachZPointWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TeachZPointWidget(const QString& pointName,
                               ApplicationContext* context,
                               QWidget* parent = nullptr);

    void SetDeviceName(const std::string& deviceName);
    void SetPoint(const ZOffsetPoint& pt);
    const ZOffsetPoint& GetPoint() const { return m_point; }

signals:
    void pointCaptured(const ZOffsetPoint& pt);

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
    ZOffsetPoint m_point;
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
