// TeachPointWidget.cpp - Reusable XYZ capture widget for teaching a single gantry point
#include "TeachPointWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <iostream>

TeachPointWidget::TeachPointWidget(const QString& pointName,
                                   ApplicationContext* context,
                                   QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_deviceName("gantry-main")
    , m_pointName(pointName)
    , m_positionTimer(new QTimer(this))
{
    m_point.name = pointName.toStdString();
    setupUI();
    connect(m_positionTimer, &QTimer::timeout, this, &TeachPointWidget::updatePositionDisplay);
}

void TeachPointWidget::SetDeviceName(const std::string& deviceName)
{
    m_deviceName = deviceName;
}

void TeachPointWidget::SetPoint(const TaughtPoint& pt)
{
    m_point = pt;
    if (pt.valid) {
        setState(Captured);
    } else {
        setState(Idle);
    }
}

void TeachPointWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel(QString("Teach %1").arg(m_pointName));
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    QLabel* desc = new QLabel(
        QString("Jog gantry to %1 position, then press Capture to record XYZ coordinates.")
            .arg(m_pointName));
    desc->setStyleSheet("color: #aaa; font-size: 12px;");
    desc->setWordWrap(true);
    mainLayout->addWidget(desc);

    m_wizardStack = new QStackedWidget();

    // ========================================
    // Page 0: Idle
    // ========================================
    QWidget* idlePage = new QWidget();
    QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);

    m_idleLabel = new QLabel(QString("No position captured for %1.").arg(m_pointName));
    m_idleLabel->setStyleSheet(
        "font-size: 13px; color: #aaa; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_idleLabel->setWordWrap(true);
    idleLayout->addWidget(m_idleLabel);

    idleLayout->addStretch();

    m_startTeachBtn = new QPushButton(QString("Teach %1").arg(m_pointName));
    m_startTeachBtn->setFixedHeight(40);
    m_startTeachBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }"
    );
    connect(m_startTeachBtn, &QPushButton::clicked, this, [this]() {
        setState(Teaching);
    });
    idleLayout->addWidget(m_startTeachBtn);

    m_wizardStack->addWidget(idlePage);  // index 0

    // ========================================
    // Page 1: Teaching
    // ========================================
    QWidget* teachPage = new QWidget();
    QVBoxLayout* teachLayout = new QVBoxLayout(teachPage);

    m_instructionLabel = new QLabel(
        QString("Jog gantry to %1, then press Capture").arg(m_pointName));
    m_instructionLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_instructionLabel->setWordWrap(true);
    m_instructionLabel->setAlignment(Qt::AlignCenter);
    teachLayout->addWidget(m_instructionLabel);

    QGroupBox* posGroup = new QGroupBox("Current Position");
    posGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
    );
    QVBoxLayout* posLayout = new QVBoxLayout(posGroup);
    m_positionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_positionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_positionLabel->setAlignment(Qt::AlignCenter);
    posLayout->addWidget(m_positionLabel);
    teachLayout->addWidget(posGroup);

    teachLayout->addStretch();

    QHBoxLayout* teachBtnRow = new QHBoxLayout();
    m_captureBtn = new QPushButton("Capture Position");
    m_captureBtn->setFixedHeight(40);
    m_captureBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }"
    );
    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedHeight(40);
    m_cancelBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }"
    );
    teachBtnRow->addWidget(m_captureBtn);
    teachBtnRow->addWidget(m_cancelBtn);
    teachLayout->addLayout(teachBtnRow);

    connect(m_captureBtn, &QPushButton::clicked, this, &TeachPointWidget::onCapture);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(m_point.valid ? Captured : Idle);
    });

    m_wizardStack->addWidget(teachPage);  // index 1

    // ========================================
    // Page 2: Captured
    // ========================================
    QWidget* capturedPage = new QWidget();
    QVBoxLayout* capturedLayout = new QVBoxLayout(capturedPage);

    m_summaryLabel = new QLabel();
    m_summaryLabel->setStyleSheet(
        "font-size: 13px; color: #ffffff; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #27ae60; border-radius: 6px;");
    m_summaryLabel->setWordWrap(true);
    capturedLayout->addWidget(m_summaryLabel);

    capturedLayout->addStretch();

    m_reteachBtn = new QPushButton("Re-teach");
    m_reteachBtn->setFixedHeight(40);
    m_reteachBtn->setStyleSheet(
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #f39c12; }"
    );
    connect(m_reteachBtn, &QPushButton::clicked, this, &TeachPointWidget::onReteach);
    capturedLayout->addWidget(m_reteachBtn);

    m_wizardStack->addWidget(capturedPage);  // index 2

    mainLayout->addWidget(m_wizardStack, 1);
}

void TeachPointWidget::setState(WizardState state)
{
    m_state = state;

    switch (state) {
    case Idle:
        m_wizardStack->setCurrentIndex(0);
        m_positionTimer->stop();
        if (m_point.valid) {
            m_idleLabel->setText(QString("%1: X=%2  Y=%3  Z=%4")
                .arg(m_pointName)
                .arg(m_point.x, 0, 'f', 4)
                .arg(m_point.y, 0, 'f', 4)
                .arg(m_point.z, 0, 'f', 4));
        } else {
            m_idleLabel->setText(QString("No position captured for %1.").arg(m_pointName));
        }
        break;

    case Teaching:
        m_wizardStack->setCurrentIndex(1);
        m_positionTimer->start(100);
        break;

    case Captured:
        m_wizardStack->setCurrentIndex(2);
        m_positionTimer->stop();
        m_summaryLabel->setText(QString(
            "%1 Captured\n\n"
            "  X: %2\n"
            "  Y: %3\n"
            "  Z: %4")
            .arg(m_pointName)
            .arg(m_point.x, 0, 'f', 4)
            .arg(m_point.y, 0, 'f', 4)
            .arg(m_point.z, 0, 'f', 4));
        break;
    }
}

void TeachPointWidget::onCapture()
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_deviceName);

    m_point.x = pos.x;
    m_point.y = pos.y;
    m_point.z = pos.z;
    m_point.valid = true;

    std::cout << "[TeachPoint] " << m_pointName.toStdString()
              << ": X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z << std::endl;

    setState(Captured);
    emit pointCaptured(m_point);
}

void TeachPointWidget::onReteach()
{
    setState(Teaching);
}

void TeachPointWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager) {
        m_positionLabel->setText("X: ---  Y: ---  Z: ---");
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_deviceName);

    m_positionLabel->setText(QString("X: %1  Y: %2  Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4));
}
