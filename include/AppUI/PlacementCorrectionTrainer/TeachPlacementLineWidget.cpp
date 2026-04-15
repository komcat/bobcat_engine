// TeachPlacementLineWidget.cpp - Two-point capture widget for placement correction
#include "TeachPlacementLineWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "CameraManager.h"
#include "product_position_manager.h"
#include "VisionCommandHandler.h"
#include "SequenceContext.h"
#include "SequenceEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QSignalBlocker>
#include <iostream>
#include <filesystem>
#include <chrono>

TeachPlacementLineWidget::TeachPlacementLineWidget(const QString& lineName,
                                                   ApplicationContext* context,
                                                   QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_deviceName("gantry-main")
    , m_lineName(lineName)
    , m_positionTimer(new QTimer(this))
{
    m_line.name = lineName.toStdString();
    setupUI();
    connect(m_positionTimer, &QTimer::timeout, this, &TeachPlacementLineWidget::updatePositionDisplay);
    refreshVisionProfiles();
}

void TeachPlacementLineWidget::SetDeviceName(const std::string& deviceName)
{
    m_deviceName = deviceName;
}

void TeachPlacementLineWidget::SetCameraId(const std::string& cameraId)
{
    m_cameraId = cameraId;
}

void TeachPlacementLineWidget::SetLine(const PlacementLinePoints& lp)
{
    m_line = lp;
    m_p1ExposureSpin->setValue(static_cast<int>(lp.p1Exposure));
    m_p2ExposureSpin->setValue(static_cast<int>(lp.p2Exposure));
    if (m_p1ProfileCombo) {
        QSignalBlocker b(m_p1ProfileCombo);
        selectComboItemByPath(m_p1ProfileCombo, QString::fromStdString(lp.p1VisionProfilePath));
    }
    if (m_p2ProfileCombo) {
        QSignalBlocker b(m_p2ProfileCombo);
        selectComboItemByPath(m_p2ProfileCombo, QString::fromStdString(lp.p2VisionProfilePath));
    }
    if (m_p1DetectBtn) m_p1DetectBtn->setEnabled(!lp.p1VisionProfilePath.empty());
    if (m_p2DetectBtn) m_p2DetectBtn->setEnabled(!lp.p2VisionProfilePath.empty());
    if (lp.valid) {
        setState(Captured);
    } else {
        setState(Idle);
    }
}

void TeachPlacementLineWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel(QString("Teach %1 Line").arg(m_lineName));
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    QLabel* desc = new QLabel(
        QString("Capture two XY points using %1 crosshair to define the %2 edge line.\n"
                "The angle of this line will be computed in the XY plane.")
            .arg("gantry-main").arg(m_lineName));
    desc->setStyleSheet("color: #aaa; font-size: 12px;");
    desc->setWordWrap(true);
    mainLayout->addWidget(desc);

    m_wizardStack = new QStackedWidget();

    QString blueBtnStyle =
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }";

    QString cancelBtnStyle =
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }";

    QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    QString spinStyle =
        "QSpinBox { background-color: #1a1f2e; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 4px; font-size: 13px; }"
        "QSpinBox::up-button, QSpinBox::down-button { background-color: #2d3e5f; border: none; }";

    // ========================================
    // Page 0: Idle
    // ========================================
    QWidget* idlePage = new QWidget();
    QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);

    m_idleLabel = new QLabel(QString("No line captured for %1.").arg(m_lineName));
    m_idleLabel->setStyleSheet(
        "font-size: 13px; color: #aaa; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_idleLabel->setWordWrap(true);
    idleLayout->addWidget(m_idleLabel);

    idleLayout->addStretch();

    m_startTeachBtn = new QPushButton(QString("Teach %1 Line").arg(m_lineName));
    m_startTeachBtn->setFixedHeight(40);
    m_startTeachBtn->setStyleSheet(blueBtnStyle);
    connect(m_startTeachBtn, &QPushButton::clicked, this, [this]() {
        setState(TeachingP1);
    });
    idleLayout->addWidget(m_startTeachBtn);

    m_wizardStack->addWidget(idlePage);  // index 0

    // ========================================
    // Page 1: Teaching P1
    // ========================================
    constexpr int kFieldWidth   = 360;
    constexpr int kComboWidth   = 260;
    constexpr int kSpinWidth    = 120;
    constexpr int kBtnWidth     = 140;
    constexpr int kSmallBtnSize = 28;

    QString navBtnStyle =
        "QPushButton { background-color: #16a085; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 16px; }"
        "QPushButton:hover { background-color: #1abc9c; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    QWidget* p1Page = new QWidget();
    QVBoxLayout* p1Layout = new QVBoxLayout(p1Page);
    p1Layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    p1Layout->setSpacing(8);

    m_p1InstructionLabel = new QLabel(
        QString("Step 1/2: Jog gantry to %1 edge Point 1, then press Capture").arg(m_lineName));
    m_p1InstructionLabel->setStyleSheet(
        "font-size: 13px; color: #f0c040; font-weight: bold; padding: 10px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_p1InstructionLabel->setWordWrap(true);
    m_p1InstructionLabel->setFixedWidth(kFieldWidth);
    m_p1InstructionLabel->setMinimumHeight(60);
    m_p1InstructionLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::MinimumExpanding);
    p1Layout->addWidget(m_p1InstructionLabel, 0, Qt::AlignLeft);

    QGroupBox* p1PosGroup = new QGroupBox("Current Gantry Position");
    p1PosGroup->setStyleSheet(groupStyle);
    p1PosGroup->setFixedWidth(kFieldWidth);
    QVBoxLayout* p1PosLayout = new QVBoxLayout(p1PosGroup);
    m_p1PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_p1PositionLabel->setStyleSheet("font-size: 14px; color: #00ff88; font-family: monospace; padding: 6px;");
    p1PosLayout->addWidget(m_p1PositionLabel);
    p1Layout->addWidget(p1PosGroup, 0, Qt::AlignLeft);

    QHBoxLayout* p1ExpRow = new QHBoxLayout();
    QLabel* p1ExpLabel = new QLabel("Exposure:");
    p1ExpLabel->setStyleSheet("color: #ccc; font-size: 12px;");
    p1ExpLabel->setFixedWidth(90);
    p1ExpRow->addWidget(p1ExpLabel);
    m_p1ExposureSpin = new QSpinBox();
    m_p1ExposureSpin->setRange(1, 100000);
    m_p1ExposureSpin->setValue(800);
    m_p1ExposureSpin->setSuffix(" us");
    m_p1ExposureSpin->setStyleSheet(spinStyle);
    m_p1ExposureSpin->setFixedWidth(kSpinWidth);
    p1ExpRow->addWidget(m_p1ExposureSpin);
    m_applyP1ExposureBtn = new QPushButton("Apply");
    m_applyP1ExposureBtn->setFixedSize(70, kSmallBtnSize);
    m_applyP1ExposureBtn->setStyleSheet(
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "font-size: 11px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #f39c12; }");
    connect(m_applyP1ExposureBtn, &QPushButton::clicked, this, [this]() {
        applyCameraExposure(static_cast<double>(m_p1ExposureSpin->value()));
    });
    p1ExpRow->addWidget(m_applyP1ExposureBtn);
    p1ExpRow->addStretch(1);
    p1Layout->addLayout(p1ExpRow);

    QHBoxLayout* p1ProfRow = new QHBoxLayout();
    QLabel* p1ProfLabel = new QLabel("Vision Profile:");
    p1ProfLabel->setStyleSheet("color: #ccc; font-size: 12px;");
    p1ProfLabel->setFixedWidth(90);
    p1ProfRow->addWidget(p1ProfLabel);
    m_p1ProfileCombo = new QComboBox();
    m_p1ProfileCombo->setStyleSheet(
        "QComboBox { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 4px 8px; }"
        "QComboBox QAbstractItemView { background-color: #2d3e5f; color: white; selection-background-color: #3d5a8c; }");
    m_p1ProfileCombo->setFixedWidth(kComboWidth);
    m_p1ProfileCombo->setToolTip("Optional vision profile for retrace auto-detect of P1");
    p1ProfRow->addWidget(m_p1ProfileCombo);
    m_p1RefreshProfilesBtn = new QPushButton("Refresh");
    m_p1RefreshProfilesBtn->setFixedSize(70, kSmallBtnSize);
    m_p1RefreshProfilesBtn->setToolTip("Refresh profile list");
    m_p1RefreshProfilesBtn->setStyleSheet(
        "QPushButton { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #3d5a8c; }");
    connect(m_p1RefreshProfilesBtn, &QPushButton::clicked,
            this, &TeachPlacementLineWidget::refreshVisionProfiles);
    p1ProfRow->addWidget(m_p1RefreshProfilesBtn);
    p1ProfRow->addStretch(1);
    p1Layout->addLayout(p1ProfRow);

    {
        QHBoxLayout* p1DetectRow = new QHBoxLayout();
        QLabel* spacer = new QLabel("");
        spacer->setFixedWidth(90);
        p1DetectRow->addWidget(spacer);
        m_p1DetectBtn = new QPushButton("Detect && Center");
        m_p1DetectBtn->setFixedSize(kComboWidth, kSmallBtnSize);
        m_p1DetectBtn->setToolTip("Run the selected vision profile and move gantry to detected feature");
        m_p1DetectBtn->setStyleSheet(
            "QPushButton { background-color: #8e44ad; color: white; font-weight: bold; "
            "border-radius: 4px; padding: 4px 10px; font-size: 12px; }"
            "QPushButton:hover { background-color: #9b59b6; }"
            "QPushButton:disabled { background-color: #1a1f2e; color: #555; }");
        m_p1DetectBtn->setEnabled(false);
        connect(m_p1DetectBtn, &QPushButton::clicked, this, &TeachPlacementLineWidget::onDetectP1);
        p1DetectRow->addWidget(m_p1DetectBtn);
        p1DetectRow->addStretch(1);
        p1Layout->addLayout(p1DetectRow);
    }

    connect(m_p1ProfileCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        m_line.p1VisionProfilePath = m_p1ProfileCombo->currentData().toString().toStdString();
        if (m_p1DetectBtn) m_p1DetectBtn->setEnabled(!m_line.p1VisionProfilePath.empty());
        std::cout << "[TeachPlacementLine] " << m_lineName.toStdString()
                  << " P1 vision profile = '" << m_line.p1VisionProfilePath << "'" << std::endl;
        if (m_line.valid) emit lineCaptured(m_line);
    });

    QHBoxLayout* p1BtnRow = new QHBoxLayout();
    m_autoNavP1Btn = new QPushButton("Auto-Nav to P1");
    m_autoNavP1Btn->setFixedSize(kBtnWidth, 36);
    m_autoNavP1Btn->setStyleSheet(navBtnStyle);
    m_autoNavP1Btn->setToolTip("Move gantry to the previously-taught P1 position");
    m_autoNavP1Btn->setEnabled(false);   // disabled until a P1 has been captured
    connect(m_autoNavP1Btn, &QPushButton::clicked, this, &TeachPlacementLineWidget::onAutoNavP1);
    p1BtnRow->addWidget(m_autoNavP1Btn);

    m_captureP1Btn = new QPushButton("Capture P1");
    m_captureP1Btn->setFixedSize(kBtnWidth, 36);
    m_captureP1Btn->setStyleSheet(blueBtnStyle);
    connect(m_captureP1Btn, &QPushButton::clicked, this, &TeachPlacementLineWidget::onCaptureP1);
    p1BtnRow->addWidget(m_captureP1Btn);

    m_cancelP1Btn = new QPushButton("Cancel");
    m_cancelP1Btn->setFixedSize(80, 36);
    m_cancelP1Btn->setStyleSheet(cancelBtnStyle);
    connect(m_cancelP1Btn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(m_line.valid ? Captured : Idle);
    });
    p1BtnRow->addWidget(m_cancelP1Btn);
    p1BtnRow->addStretch(1);
    p1Layout->addLayout(p1BtnRow);

    p1Layout->addStretch(1);

    m_wizardStack->addWidget(p1Page);  // index 1

    // ========================================
    // Page 2: Teaching P2
    // ========================================
    QWidget* p2Page = new QWidget();
    QVBoxLayout* p2Layout = new QVBoxLayout(p2Page);
    p2Layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    p2Layout->setSpacing(8);

    m_p2InstructionLabel = new QLabel(
        QString("Step 2/2: Jog gantry to %1 edge Point 2, then press Capture").arg(m_lineName));
    m_p2InstructionLabel->setStyleSheet(
        "font-size: 13px; color: #f0c040; font-weight: bold; padding: 10px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_p2InstructionLabel->setWordWrap(true);
    m_p2InstructionLabel->setFixedWidth(kFieldWidth);
    m_p2InstructionLabel->setMinimumHeight(60);
    m_p2InstructionLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::MinimumExpanding);
    p2Layout->addWidget(m_p2InstructionLabel, 0, Qt::AlignLeft);

    QGroupBox* p2PosGroup = new QGroupBox("Current Gantry Position");
    p2PosGroup->setStyleSheet(groupStyle);
    p2PosGroup->setFixedWidth(kFieldWidth);
    QVBoxLayout* p2PosLayout = new QVBoxLayout(p2PosGroup);
    m_p2PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_p2PositionLabel->setStyleSheet("font-size: 14px; color: #00ff88; font-family: monospace; padding: 6px;");
    p2PosLayout->addWidget(m_p2PositionLabel);
    p2Layout->addWidget(p2PosGroup, 0, Qt::AlignLeft);

    QHBoxLayout* p2ExpRow = new QHBoxLayout();
    QLabel* p2ExpLabel = new QLabel("Exposure:");
    p2ExpLabel->setStyleSheet("color: #ccc; font-size: 12px;");
    p2ExpLabel->setFixedWidth(90);
    p2ExpRow->addWidget(p2ExpLabel);
    m_p2ExposureSpin = new QSpinBox();
    m_p2ExposureSpin->setRange(1, 100000);
    m_p2ExposureSpin->setValue(800);
    m_p2ExposureSpin->setSuffix(" us");
    m_p2ExposureSpin->setStyleSheet(spinStyle);
    m_p2ExposureSpin->setFixedWidth(kSpinWidth);
    p2ExpRow->addWidget(m_p2ExposureSpin);
    m_applyP2ExposureBtn = new QPushButton("Apply");
    m_applyP2ExposureBtn->setFixedSize(70, kSmallBtnSize);
    m_applyP2ExposureBtn->setStyleSheet(
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "font-size: 11px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #f39c12; }");
    connect(m_applyP2ExposureBtn, &QPushButton::clicked, this, [this]() {
        applyCameraExposure(static_cast<double>(m_p2ExposureSpin->value()));
    });
    p2ExpRow->addWidget(m_applyP2ExposureBtn);
    p2ExpRow->addStretch(1);
    p2Layout->addLayout(p2ExpRow);

    QHBoxLayout* p2ProfRow = new QHBoxLayout();
    QLabel* p2ProfLabel = new QLabel("Vision Profile:");
    p2ProfLabel->setStyleSheet("color: #ccc; font-size: 12px;");
    p2ProfLabel->setFixedWidth(90);
    p2ProfRow->addWidget(p2ProfLabel);
    m_p2ProfileCombo = new QComboBox();
    m_p2ProfileCombo->setStyleSheet(
        "QComboBox { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 4px 8px; }"
        "QComboBox QAbstractItemView { background-color: #2d3e5f; color: white; selection-background-color: #3d5a8c; }");
    m_p2ProfileCombo->setFixedWidth(kComboWidth);
    m_p2ProfileCombo->setToolTip("Optional vision profile for retrace auto-detect of P2");
    p2ProfRow->addWidget(m_p2ProfileCombo);
    m_p2RefreshProfilesBtn = new QPushButton("Refresh");
    m_p2RefreshProfilesBtn->setFixedSize(70, kSmallBtnSize);
    m_p2RefreshProfilesBtn->setToolTip("Refresh profile list");
    m_p2RefreshProfilesBtn->setStyleSheet(
        "QPushButton { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #3d5a8c; }");
    connect(m_p2RefreshProfilesBtn, &QPushButton::clicked,
            this, &TeachPlacementLineWidget::refreshVisionProfiles);
    p2ProfRow->addWidget(m_p2RefreshProfilesBtn);
    p2ProfRow->addStretch(1);
    p2Layout->addLayout(p2ProfRow);

    {
        QHBoxLayout* p2DetectRow = new QHBoxLayout();
        QLabel* spacer = new QLabel("");
        spacer->setFixedWidth(90);
        p2DetectRow->addWidget(spacer);
        m_p2DetectBtn = new QPushButton("Detect && Center");
        m_p2DetectBtn->setFixedSize(kComboWidth, kSmallBtnSize);
        m_p2DetectBtn->setToolTip("Run the selected vision profile and move gantry to detected feature");
        m_p2DetectBtn->setStyleSheet(
            "QPushButton { background-color: #8e44ad; color: white; font-weight: bold; "
            "border-radius: 4px; padding: 4px 10px; font-size: 12px; }"
            "QPushButton:hover { background-color: #9b59b6; }"
            "QPushButton:disabled { background-color: #1a1f2e; color: #555; }");
        m_p2DetectBtn->setEnabled(false);
        connect(m_p2DetectBtn, &QPushButton::clicked, this, &TeachPlacementLineWidget::onDetectP2);
        p2DetectRow->addWidget(m_p2DetectBtn);
        p2DetectRow->addStretch(1);
        p2Layout->addLayout(p2DetectRow);
    }

    connect(m_p2ProfileCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        m_line.p2VisionProfilePath = m_p2ProfileCombo->currentData().toString().toStdString();
        if (m_p2DetectBtn) m_p2DetectBtn->setEnabled(!m_line.p2VisionProfilePath.empty());
        std::cout << "[TeachPlacementLine] " << m_lineName.toStdString()
                  << " P2 vision profile = '" << m_line.p2VisionProfilePath << "'" << std::endl;
        if (m_line.valid) emit lineCaptured(m_line);
    });

    QHBoxLayout* p2BtnRow = new QHBoxLayout();
    m_autoNavP2Btn = new QPushButton("Auto-Nav to P2");
    m_autoNavP2Btn->setFixedSize(kBtnWidth, 36);
    m_autoNavP2Btn->setStyleSheet(navBtnStyle);
    m_autoNavP2Btn->setToolTip("Move gantry to the previously-taught P2 position");
    m_autoNavP2Btn->setEnabled(false);   // disabled until a P2 has been captured
    connect(m_autoNavP2Btn, &QPushButton::clicked, this, &TeachPlacementLineWidget::onAutoNavP2);
    p2BtnRow->addWidget(m_autoNavP2Btn);

    m_captureP2Btn = new QPushButton("Capture P2");
    m_captureP2Btn->setFixedSize(kBtnWidth, 36);
    m_captureP2Btn->setStyleSheet(blueBtnStyle);
    connect(m_captureP2Btn, &QPushButton::clicked, this, &TeachPlacementLineWidget::onCaptureP2);
    p2BtnRow->addWidget(m_captureP2Btn);

    m_cancelP2Btn = new QPushButton("Cancel");
    m_cancelP2Btn->setFixedSize(80, 36);
    m_cancelP2Btn->setStyleSheet(cancelBtnStyle);
    connect(m_cancelP2Btn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(m_line.valid ? Captured : Idle);
    });
    p2BtnRow->addWidget(m_cancelP2Btn);
    p2BtnRow->addStretch(1);
    p2Layout->addLayout(p2BtnRow);

    p2Layout->addStretch(1);

    m_wizardStack->addWidget(p2Page);  // index 2

    // ========================================
    // Page 3: Captured
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
        "QPushButton:hover { background-color: #f39c12; }");
    connect(m_reteachBtn, &QPushButton::clicked, this, &TeachPlacementLineWidget::onReteach);
    capturedLayout->addWidget(m_reteachBtn);

    m_wizardStack->addWidget(capturedPage);  // index 3

    mainLayout->addWidget(m_wizardStack, 1);
}

void TeachPlacementLineWidget::setState(WizardState state)
{
    m_state = state;

    switch (state) {
    case Idle:
        m_wizardStack->setCurrentIndex(0);
        m_positionTimer->stop();
        if (m_line.valid) {
            m_idleLabel->setText(QString(
                "%1 Line:\n"
                "  P1: X=%2  Y=%3\n"
                "  P2: X=%4  Y=%5\n"
                "  Angle: %6%7")
                .arg(m_lineName)
                .arg(m_line.p1x, 0, 'f', 4).arg(m_line.p1y, 0, 'f', 4)
                .arg(m_line.p2x, 0, 'f', 4).arg(m_line.p2y, 0, 'f', 4)
                .arg(m_line.angleDeg, 0, 'f', 3)
                .arg(QChar(0x00B0)));
        } else {
            m_idleLabel->setText(QString("No line captured for %1.").arg(m_lineName));
        }
        break;

    case TeachingP1:
        m_wizardStack->setCurrentIndex(1);
        if (m_autoNavP1Btn) m_autoNavP1Btn->setEnabled(m_line.valid);
        m_positionTimer->start(100);
        break;

    case TeachingP2:
        m_wizardStack->setCurrentIndex(2);
        if (m_autoNavP2Btn) m_autoNavP2Btn->setEnabled(m_line.valid);
        m_positionTimer->start(100);
        break;

    case Captured: {
        m_wizardStack->setCurrentIndex(3);
        m_positionTimer->stop();
        double len = PlacementCorrectionMath::LineLength(m_line.p1x, m_line.p1y, m_line.p2x, m_line.p2y);
        m_summaryLabel->setText(QString(
            "%1 Line Captured\n\n"
            "  P1: X=%2  Y=%3  Z=%4\n"
            "  P2: X=%5  Y=%6  Z=%7\n\n"
            "  Line Length: %8 mm\n"
            "  Line Angle:  %9%10")
            .arg(m_lineName)
            .arg(m_line.p1x, 0, 'f', 4).arg(m_line.p1y, 0, 'f', 4).arg(m_line.p1z, 0, 'f', 4)
            .arg(m_line.p2x, 0, 'f', 4).arg(m_line.p2y, 0, 'f', 4).arg(m_line.p2z, 0, 'f', 4)
            .arg(len, 0, 'f', 3)
            .arg(m_line.angleDeg, 0, 'f', 3)
            .arg(QChar(0x00B0)));
        break;
    }
    }
}

void TeachPlacementLineWidget::onCaptureP1()
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_deviceName);

    m_line.p1x = pos.x;
    m_line.p1y = pos.y;
    m_line.p1z = pos.z;
    m_line.p1Exposure = static_cast<double>(m_p1ExposureSpin->value());
    m_line.p1ImageFile = capturePointImage("P1");
    m_line.p1VisionProfilePath = m_p1ProfileCombo
        ? m_p1ProfileCombo->currentData().toString().toStdString() : std::string();

    std::cout << "[TeachPlacementLine] " << m_lineName.toStdString()
              << " P1: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z
              << " exp=" << m_line.p1Exposure << "us" << std::endl;

    // Pre-fill P2 exposure with same value as P1
    m_p2ExposureSpin->setValue(m_p1ExposureSpin->value());

    setState(TeachingP2);
}

void TeachPlacementLineWidget::onCaptureP2()
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_deviceName);

    m_line.p2x = pos.x;
    m_line.p2y = pos.y;
    m_line.p2z = pos.z;
    m_line.p2Exposure = static_cast<double>(m_p2ExposureSpin->value());
    m_line.p2ImageFile = capturePointImage("P2");
    m_line.p2VisionProfilePath = m_p2ProfileCombo
        ? m_p2ProfileCombo->currentData().toString().toStdString() : std::string();
    m_line.valid = true;

    // Compute line angle
    m_line.angleDeg = PlacementCorrectionMath::ComputeLineAngle(
        m_line.p1x, m_line.p1y, m_line.p2x, m_line.p2y);

    std::cout << "[TeachPlacementLine] " << m_lineName.toStdString()
              << " P2: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z
              << " Angle=" << m_line.angleDeg << " deg" << std::endl;

    setState(Captured);
    emit lineCaptured(m_line);
}

void TeachPlacementLineWidget::onReteach()
{
    setState(TeachingP1);
}

void TeachPlacementLineWidget::applyCameraExposure(double exposureUs)
{
    if (!m_context || !m_context->cameraManager || m_cameraId.empty()) return;

    ICameraHardware::ExposureSettings expSettings;
    expSettings.auto_exposure = false;
    expSettings.auto_gain = false;
    expSettings.gain = 0.0;
    expSettings.exposure_time = exposureUs;

    bool ok = m_context->cameraManager->ApplyExposureSettings(m_cameraId, expSettings);
    std::cout << "[TeachPlacementLine] Camera \"" << m_cameraId
              << "\" exposure=" << exposureUs << "us "
              << (ok ? "OK" : "FAILED") << std::endl;
}

std::string TeachPlacementLineWidget::capturePointImage(const std::string& pointLabel)
{
    if (!m_context || !m_context->cameraManager || m_cameraId.empty()) {
        return "";
    }

    // Build image directory: config/<product>/images/trainer/
    std::string imageDir = "config/images/trainer";
    if (m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        std::filesystem::path productPath(
            m_context->productPositionManager->GetCurrentProductPath());
        imageDir = (productPath.parent_path() / "images" / "trainer").string();
    }
    std::filesystem::create_directories(imageDir);

    // Generate unique filename
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string filename = m_lineName.toStdString() + "_" + pointLabel + "_"
                         + std::to_string(ms) + ".bmp";

    m_context->cameraManager->SetImageOutputDirectory(imageDir);
    bool ok = m_context->cameraManager->CaptureImage(m_cameraId, filename);
    if (ok) {
        std::cout << "[TeachPlacementLine] Captured image: " << imageDir << "/" << filename << std::endl;
        return filename;
    }
    std::cerr << "[TeachPlacementLine] Failed to capture image for " << pointLabel << std::endl;
    return "";
}

void TeachPlacementLineWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager) {
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_deviceName);
    QString text = QString("X: %1  Y: %2  Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4);

    if (m_state == TeachingP1)
        m_p1PositionLabel->setText(text);
    else if (m_state == TeachingP2)
        m_p2PositionLabel->setText(text);
}

QString TeachPlacementLineWidget::getVisionDirectory() const
{
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        QString productFilePath = QString::fromStdString(
            m_context->productPositionManager->GetCurrentProductPath());
        QString productDir = QFileInfo(productFilePath).path();
        return productDir + "/vision";
    }
    return "config/vision";
}

void TeachPlacementLineWidget::selectComboItemByPath(QComboBox* combo, const QString& path)
{
    if (!combo) return;
    if (path.isEmpty()) {
        combo->setCurrentIndex(0);
        return;
    }
    int idx = combo->findData(path);
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
    } else {
        // Profile no longer present — keep stored path but show as missing
        combo->insertItem(combo->count(), QString("(missing) %1").arg(QFileInfo(path).baseName()), path);
        combo->setCurrentIndex(combo->count() - 1);
    }
}

void TeachPlacementLineWidget::refreshVisionProfiles()
{
    auto populate = [this](QComboBox* combo, const QString& keepPath) {
        if (!combo) return;
        combo->blockSignals(true);
        combo->clear();
        combo->addItem("(None - manual only)", QString());

        QDir dir(getVisionDirectory());
        if (dir.exists()) {
            QStringList filters; filters << "*.json";
            dir.setNameFilters(filters);
            dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
            dir.setSorting(QDir::Name);
            for (const QFileInfo& fi : dir.entryInfoList()) {
                if (fi.fileName().endsWith(".recipe.json")) continue;
                combo->addItem(fi.baseName(), fi.absoluteFilePath());
            }
        }
        selectComboItemByPath(combo, keepPath);
        combo->blockSignals(false);
    };

    populate(m_p1ProfileCombo, QString::fromStdString(m_line.p1VisionProfilePath));
    populate(m_p2ProfileCombo, QString::fromStdString(m_line.p2VisionProfilePath));
}

void TeachPlacementLineWidget::onAutoNavP1()
{
    if (!m_line.valid) return;   // button should be disabled, defend anyway
    startAutoNavAsync(m_line.p1x, m_line.p1y, m_line.p1z, "P1");
}

// Run the assigned vision profile once; on success, MoveToAbs to detected world XY
// (Z preserved). Single-shot — user can click again or press Capture afterward.
static bool runDetectAndCenter(ApplicationContext* ctx,
                                const std::string& device,
                                const std::string& cameraId,
                                const std::string& profilePath,
                                std::string& outMsg)
{
    if (!ctx || !ctx->motionManager) { outMsg = "motion manager not available"; return false; }
    if (cameraId.empty()) { outMsg = "camera ID not set on parent page"; return false; }
    if (profilePath.empty()) { outMsg = "no vision profile selected"; return false; }

    VisionCommandHandler h(ctx);
    SequenceContext sctx;
    std::map<std::string, std::string> params;
    params["profile_path"]             = profilePath;
    params["camera"]                   = cameraId;
    params["motion_device"]            = device;
    params["calculate_world_position"] = "true";

    std::string err;
    bool callOk = h.ExecuteCommand(nullptr, "vision", "execute_profile", params, err, &sctx);
    bool vOk = sctx.GetBoolean("vision.result.success", false);
    if (!callOk || !vOk) {
        outMsg = err.empty() ? std::string("vision execution failed") : err;
        return false;
    }

    // VisionCommandHandler returns success=true even on no-match. Require
    // the world-position keys to exist before trusting them.
    if (!sctx.HasValue("vision.detected.world_x_mm")
        || !sctx.HasValue("vision.detected.world_y_mm")) {
        outMsg = "vision profile ran but found no feature";
        return false;
    }

    double wx  = sctx.GetNumeric("vision.detected.world_x_mm", 0.0f);
    double wy  = sctx.GetNumeric("vision.detected.world_y_mm", 0.0f);
    double dvx = sctx.GetNumeric("vision.detected.device_x_mm", 0.0f);
    double dvy = sctx.GetNumeric("vision.detected.device_y_mm", 0.0f);
    double residual = std::hypot(wx - dvx, wy - dvy);

    auto cur = ctx->motionManager->GetCurrentPosition(device);
    bool moveOk = ctx->motionManager->MoveToAbs(device, wx, wy, cur.z);
    if (!moveOk) {
        outMsg = "MoveToAbs to detected position failed";
        return false;
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "Detected world (%.4f, %.4f) mm  residual=%.4f mm — gantry moved",
        wx, wy, residual);
    outMsg = buf;
    return true;
}

void TeachPlacementLineWidget::onDetectP1()
{
    std::string msg;
    bool ok = runDetectAndCenter(m_context, m_deviceName, m_cameraId,
                                 m_line.p1VisionProfilePath, msg);
    std::cout << "[TeachPlacementLine] " << m_lineName.toStdString()
              << " Detect P1: " << (ok ? "OK" : "FAIL") << " — " << msg << std::endl;
    if (!ok) {
        QMessageBox::warning(this, "Detect & Center Failed",
            QString::fromStdString(msg));
    }
}

void TeachPlacementLineWidget::onDetectP2()
{
    std::string msg;
    bool ok = runDetectAndCenter(m_context, m_deviceName, m_cameraId,
                                 m_line.p2VisionProfilePath, msg);
    std::cout << "[TeachPlacementLine] " << m_lineName.toStdString()
              << " Detect P2: " << (ok ? "OK" : "FAIL") << " — " << msg << std::endl;
    if (!ok) {
        QMessageBox::warning(this, "Detect & Center Failed",
            QString::fromStdString(msg));
    }
}

void TeachPlacementLineWidget::onAutoNavP2()
{
    if (!m_line.valid) return;
    startAutoNavAsync(m_line.p2x, m_line.p2y, m_line.p2z, "P2");
}

// Background safe-mover for Auto-Nav: SetSystemVelocity(10) → MoveToAbs →
// SetSystemVelocity(50). Disables both nav buttons while running. Marshals
// completion back to UI thread via QMetaObject::invokeMethod.
void TeachPlacementLineWidget::startAutoNavAsync(double tx, double ty, double tz,
                                                  const QString& which)
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Auto-Nav", "Motion manager not available.");
        return;
    }
    if (m_context->sequenceEngine && m_context->sequenceEngine->IsExecuting()) {
        QMessageBox::warning(this, "Auto-Nav Blocked",
            "A recipe is currently running. Stop the program before Auto-Nav.");
        return;
    }
    if (!m_navActiveDevice.empty()) return;   // double-click guard

    constexpr double kSafeVel    = 10.0;  // mm/s during the move
    constexpr double kDefaultVel = 50.0;  // mm/s restored after

    auto setNavEnabled = [this](bool en) {
        if (m_autoNavP1Btn) m_autoNavP1Btn->setEnabled(en && m_line.valid);
        if (m_autoNavP2Btn) m_autoNavP2Btn->setEnabled(en && m_line.valid);
    };
    setNavEnabled(false);
    m_navActiveDevice = m_deviceName;

    auto* mm = m_context->motionManager;
    std::string device = m_deviceName;
    std::string lineName = m_lineName.toStdString();
    QString whichCopy = which;

    m_navFuture = std::async(std::launch::async,
        [this, mm, device, tx, ty, tz, whichCopy, lineName, setNavEnabled]() {
        bool velSlowOk    = mm->SetSystemVelocity(device, kSafeVel);
        bool moveOk       = mm->MoveToAbs(device, tx, ty, tz);
        bool velRestoreOk = mm->SetSystemVelocity(device, kDefaultVel);

        std::cout << "[TeachPlacementLine] " << lineName
                  << " Auto-Nav to " << whichCopy.toStdString()
                  << " (" << tx << ", " << ty << ", " << tz << ")"
                  << "  velSlow="    << (velSlowOk ? "OK" : "FAIL")
                  << "  move="       << (moveOk ? "OK" : "FAIL")
                  << "  velRestore=" << (velRestoreOk ? "OK" : "FAIL") << std::endl;

        QMetaObject::invokeMethod(this,
            [this, moveOk, whichCopy, setNavEnabled]() {
            m_navActiveDevice.clear();
            setNavEnabled(true);
            if (!moveOk) {
                QMessageBox::warning(this, "Auto-Nav Failed",
                    QString("MoveToAbs to %1 failed.").arg(whichCopy));
            }
        }, Qt::QueuedConnection);
    });
}
