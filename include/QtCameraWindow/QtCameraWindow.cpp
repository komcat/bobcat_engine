// QtCameraWindow.cpp
#include "QtCameraWindow.h"
#include "QtCameraViewport.h"

#include "camera/CameraManager.h"
#include "camera/CameraConfigManager.h"
#include "motions/unified_motion_manager.h"
#include "motions/MotionTypes.h"
#include "motions/CoordinateTransformer.h"
#include "motions/TransformationMatrixLoader.h"
#include <QTimer>
#include <QInputDialog>
#include <cmath>
#include "data/DataStore.h"
#include "customserver/BeamServerClient.h"

#ifdef HAS_OPENCV
#include "vision/VisionManager.h"
#include "vision/VisionROI.h"
#include <opencv2/imgproc.hpp>
#include <QString>
#include <filesystem>
#endif

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QCloseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QMetaObject>
#include <QSettings>
#include <QLabel>
#include <QGroupBox>
#include <QScrollArea>
#include <QComboBox>
#include <QCheckBox>
#include <QScrollBar>
#include <QSizePolicy>
#include <QListWidget>
#include <QPushButton>
#include <QGridLayout>
#include <QTabWidget>
#include <QMenu>
#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>

// Fallback calibration config path (shared with GdiCameraWindow / CameraClickToMoveWidget)
static constexpr const char* CALIBRATION_CONFIG_FILE = "config/SYSTEM/val_pixelmm.json";

// ============================================================================
// Static helpers
// ============================================================================

// Magnification combo order: 1x, 10x, 20x, 50x, 100x, 200x
double QtCameraWindow::magnificationFromIndex(int index)
{
    switch (index) {
    case 0:  return 1.0;
    case 1:  return 10.0;
    case 2:  return 20.0;
    case 3:  return 50.0;
    case 4:  return 100.0;
    case 5:  return 200.0;
    default: return 1.0;
    }
}

int QtCameraWindow::indexFromMagnification(double mag)
{
    if (mag >= 200.0) return 5;
    if (mag >= 100.0) return 4;
    if (mag >=  50.0) return 3;
    if (mag >=  20.0) return 2;
    if (mag >=  10.0) return 1;
    return 0;
}

// Rotation combo order: None (0), -90 deg, +90 deg
int QtCameraWindow::rotationFromIndex(int index)
{
    switch (index) {
    case 0:  return 0;
    case 1:  return -90;
    case 2:  return 90;
    default: return 0;
    }
}

int QtCameraWindow::indexFromRotation(int degrees)
{
    if (degrees == -90) return 1;
    if (degrees ==  90) return 2;
    return 0;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

QtCameraWindow::QtCameraWindow(const std::string& instanceId, QWidget* parent)
    : QWidget(parent)
    , m_instanceId(instanceId)
    , m_subscriberId("QtCameraWindow" + instanceId)
    , m_acqFpsTimestamp(std::chrono::steady_clock::now())
{
    setWindowFlags(Qt::Window);

    const std::string titleSuffix = instanceId.empty() ? "" : (" " + instanceId);
    setWindowTitle(QString::fromStdString("Camera Monitor (Qt)" + titleSuffix));

    setMinimumSize(800, 600);
    setStyleSheet("QWidget { background-color: #2d2d30; color: #cccccc; }");

    // Main layout: side panel (fixed) + viewport (stretch)
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    buildSidePanel();
    mainLayout->addWidget(m_sidePanelScroll);

    buildQuickMovePanel();
    mainLayout->addWidget(m_quickMovePanel);

    m_viewport = new QtCameraViewport(this);
    mainLayout->addWidget(m_viewport, 1);

    // Connect viewport signals
    connect(m_viewport, &QtCameraViewport::imageClicked,
            this, &QtCameraWindow::onImageClicked);
    // mouseMoved connected as no-op for now — slot may be added in Phase 2
}

QtCameraWindow::~QtCameraWindow()
{
    shutdown();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool QtCameraWindow::initialize()
{
    if (m_initialized) return true;

    if (m_cameraManager) {
        // CameraManager uses shared_ptr for subscribers; we wrap 'this' in a
        // non-owning shared_ptr using a custom no-op deleter so the subscriber
        // interface is satisfied without transferring ownership.
        m_cameraManager->SubscribeToFrames(
            std::shared_ptr<CameraFrameSubscriber>(this, [](CameraFrameSubscriber*) {}));

        refreshCameraList();
    }

    if (m_dataStore) {
        refreshChartChannelList();
    }

    // Populate beam camera combo (cameras available at init time)
    if (m_cameraManager && m_beamCameraCombo) {
        const std::vector<std::string> ids = m_cameraManager->GetCameraIds();
        m_beamCameraCombo->blockSignals(true);
        m_beamCameraCombo->clear();
        m_beamCameraCombo->addItem("");
        for (const auto& id : ids) {
            m_beamCameraCombo->addItem(QString::fromStdString(id));
        }
        m_beamCameraCombo->blockSignals(false);
    }

    loadSettings();

    m_initialized = true;
    return true;
}

void QtCameraWindow::shutdown()
{
    if (!m_initialized) return;
    m_initialized = false;

    saveSettings();

#ifdef HAS_OPENCV
    stopVisionProcessing();
#endif
    stopBeamStreaming();

    if (m_cameraManager) {
        m_cameraManager->UnsubscribeFromFrames(m_subscriberId);
    }
}

// ============================================================================
// Side panel construction
// ============================================================================

void QtCameraWindow::buildSidePanel()
{
    m_sidePanelScroll = new QScrollArea(this);
    m_sidePanelScroll->setFixedWidth(240);
    m_sidePanelScroll->setWidgetResizable(true);
    m_sidePanelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidePanelScroll->setStyleSheet(
        "QScrollArea { background-color: #252526; border: none; }"
        "QScrollBar:vertical { background: #3e3e42; width: 8px; }"
        "QScrollBar::handle:vertical { background: #686868; border-radius: 4px; }"
    );

    auto* container = new QWidget();
    container->setStyleSheet("QWidget { background-color: #252526; }");

    auto* vLayout = new QVBoxLayout(container);
    vLayout->setContentsMargins(4, 4, 4, 4);
    vLayout->setSpacing(4);

    vLayout->addWidget(createCameraTabs());
    vLayout->addWidget(createToolsGroup());

    m_chartGroup  = createChartGroup();
    m_visionGroup = createVisionGroup();
    m_beamGroup   = createBeamGroup();

    vLayout->addWidget(m_chartGroup);
    vLayout->addWidget(m_visionGroup);
    vLayout->addWidget(m_beamGroup);

    vLayout->addStretch();

    m_sidePanelScroll->setWidget(container);
}

// ============================================================================
// Quick-move panel (second left panel)
// ============================================================================

void QtCameraWindow::buildQuickMovePanel()
{
    m_quickMovePanel = new QWidget(this);
    m_quickMovePanel->setFixedWidth(240);
    m_quickMovePanel->setStyleSheet("QWidget { background-color: #252526; }");

    auto* outerLayout = new QVBoxLayout(m_quickMovePanel);
    outerLayout->setContentsMargins(4, 4, 4, 4);
    outerLayout->setSpacing(4);

    auto* group = new QGroupBox("Quick Move");
    group->setStyleSheet(
        "QGroupBox { color: #cccccc; border: 1px solid #3e3e42; border-radius: 4px; "
        "            margin-top: 6px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
    );

    auto* grid = new QGridLayout(group);
    grid->setSpacing(4);
    grid->setContentsMargins(6, 8, 6, 6);

    static const QString btnStyle =
        "QPushButton { background-color: #3e3e42; border: 1px solid #555; "
        "              color: #cccccc; padding: 6px; min-height: 28px; "
        "              border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #505054; }"
        "QPushButton:pressed { background-color: #007acc; }";

    static const QString unassignedStyle =
        "QPushButton { background-color: #333337; border: 1px dashed #555; "
        "              color: #666666; padding: 6px; min-height: 28px; "
        "              border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #3e3e42; }";

    for (int i = 0; i < QUICK_MOVE_COUNT; ++i) {
        auto* btn = new QPushButton("---");
        btn->setStyleSheet(unassignedStyle);
        btn->setContextMenuPolicy(Qt::CustomContextMenu);

        const int row = i / QUICK_MOVE_COLS;
        const int col = i % QUICK_MOVE_COLS;
        grid->addWidget(btn, row, col);

        m_quickMoveBtns[i] = btn;

        connect(btn, &QPushButton::clicked, this, [this, i]() {
            onQuickMoveClicked(i);
        });
        connect(btn, &QPushButton::customContextMenuRequested, this, [this, i]() {
            onQuickMoveRightClicked(i);
        });
    }

    outerLayout->addWidget(group);

    m_trackingGroup = buildDeviceTrackingGroup();
    outerLayout->addWidget(m_trackingGroup);

    outerLayout->addStretch();

    loadQuickMoveSettings();
}

// ============================================================================
// Device tracking group (session-only, gantry-frame distance overlay)
// ============================================================================

QGroupBox* QtCameraWindow::buildDeviceTrackingGroup()
{
    auto* group = new QGroupBox("Device Tracking");
    group->setStyleSheet(
        "QGroupBox { color: #cccccc; border: 1px solid #3e3e42; border-radius: 4px; "
        "            margin-top: 6px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
    );

    auto* v = new QVBoxLayout(group);
    v->setContentsMargins(6, 8, 6, 6);
    v->setSpacing(4);

    static const QString btnStyle =
        "QPushButton { background-color: #3e3e42; border: 1px solid #555; "
        "              color: #cccccc; padding: 4px; min-height: 24px; "
        "              border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #505054; }"
        "QPushButton:pressed { background-color: #007acc; }";

    m_zeroP0Btn        = new QPushButton("Zero (P0 = gantry XY)");
    m_setHexLeftBtn    = new QPushButton("Set hex-left");
    m_setHexRightBtn   = new QPushButton("Set hex-right");
    m_clearTrackingBtn = new QPushButton("Clear");
    for (auto* b : {m_zeroP0Btn, m_setHexLeftBtn, m_setHexRightBtn, m_clearTrackingBtn}) {
        b->setStyleSheet(btnStyle);
    }

    static const QString labelStyle =
        "QLabel { color: #aaaaaa; font-family: Consolas; font-size: 10px; }";
    m_p0StatusLabel       = new QLabel("P0: --");
    m_hexLeftStatusLabel  = new QLabel("hex-L: --");
    m_hexRightStatusLabel = new QLabel("hex-R: --");
    for (auto* l : {m_p0StatusLabel, m_hexLeftStatusLabel, m_hexRightStatusLabel}) {
        l->setStyleSheet(labelStyle);
        l->setWordWrap(true);
    }

    v->addWidget(m_zeroP0Btn);
    v->addWidget(m_setHexLeftBtn);
    v->addWidget(m_setHexRightBtn);
    v->addWidget(m_clearTrackingBtn);
    v->addWidget(m_p0StatusLabel);
    v->addWidget(m_hexLeftStatusLabel);
    v->addWidget(m_hexRightStatusLabel);

    connect(m_zeroP0Btn,        &QPushButton::clicked, this, &QtCameraWindow::onZeroP0Clicked);
    connect(m_setHexLeftBtn,    &QPushButton::clicked, this, &QtCameraWindow::onSetHexLeftClicked);
    connect(m_setHexRightBtn,   &QPushButton::clicked, this, &QtCameraWindow::onSetHexRightClicked);
    connect(m_clearTrackingBtn, &QPushButton::clicked, this, &QtCameraWindow::onClearTrackingClicked);

    m_setHexLeftBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    m_setHexRightBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_setHexLeftBtn, &QPushButton::customContextMenuRequested,
            this, &QtCameraWindow::onSetHexLeftRightClicked);
    connect(m_setHexRightBtn, &QPushButton::customContextMenuRequested,
            this, &QtCameraWindow::onSetHexRightRightClicked);

    loadTrackingSettings();

    m_matrixLoader = std::make_unique<TransformationMatrixLoader>();
    m_matrixLoader->LoadFromFile("config/transformation_matrix.json");

    m_trackingTimer = new QTimer(this);
    connect(m_trackingTimer, &QTimer::timeout, this, &QtCameraWindow::onTrackingTimerTick);
    m_trackingTimer->start(100);  // 10 Hz

    updateTrackingStatusLabels();
    return group;
}

bool QtCameraWindow::getGantryMainXY(double& x, double& y) const
{
    if (!m_motionManager) return false;
    PositionStruct pos = m_motionManager->GetCurrentPosition("gantry-main");
    x = pos.x;
    y = pos.y;
    return true;
}

bool QtCameraWindow::getHexLocalXYZ(const std::string& hexDevice,
                                    double& lx, double& ly, double& lz) const
{
    if (!m_motionManager) return false;
    PositionStruct pos = m_motionManager->GetCurrentPosition(hexDevice);
    lx = pos.x; ly = pos.y; lz = pos.z;
    return true;
}

void QtCameraWindow::onZeroP0Clicked()
{
    double x = 0.0, y = 0.0;
    if (!getGantryMainXY(x, y)) return;
    m_p0X = x;
    m_p0Y = y;
    m_p0Set = true;
    updateTrackingStatusLabels();
}

void QtCameraWindow::onSetHexLeftClicked()
{
    if (!m_p0Set) return;
    double gx = 0.0, gy = 0.0;
    if (!getGantryMainXY(gx, gy)) return;
    m_hexLeftRegDX = gx - m_p0X;
    m_hexLeftRegDY = gy - m_p0Y;
    getHexLocalXYZ("hex-left", m_hexLeftRegLocalX, m_hexLeftRegLocalY, m_hexLeftRegLocalZ);
    m_hexLeftReg = true;
    updateTrackingStatusLabels();
}

void QtCameraWindow::onSetHexRightClicked()
{
    if (!m_p0Set) return;
    double gx = 0.0, gy = 0.0;
    if (!getGantryMainXY(gx, gy)) return;
    m_hexRightRegDX = gx - m_p0X;
    m_hexRightRegDY = gy - m_p0Y;
    getHexLocalXYZ("hex-right", m_hexRightRegLocalX, m_hexRightRegLocalY, m_hexRightRegLocalZ);
    m_hexRightReg = true;
    updateTrackingStatusLabels();
}

void QtCameraWindow::onClearTrackingClicked()
{
    m_p0Set = false;
    m_p0X = m_p0Y = 0.0;
    m_hexLeftReg = false;
    m_hexLeftRegDX = m_hexLeftRegDY = 0.0;
    m_hexLeftRegLocalX = m_hexLeftRegLocalY = m_hexLeftRegLocalZ = 0.0;
    m_hexRightReg = false;
    m_hexRightRegDX = m_hexRightRegDY = 0.0;
    m_hexRightRegLocalX = m_hexRightRegLocalY = m_hexRightRegLocalZ = 0.0;
    updateTrackingStatusLabels();
}

void QtCameraWindow::updateTrackingStatusLabels()
{
    auto fmt3 = [](double v) {
        return QString("%1%2").arg(v >= 0 ? "+" : "").arg(QString::number(v, 'f', 3));
    };
    if (m_p0Set) {
        m_p0StatusLabel->setText(QString("P0: %1, %2").arg(fmt3(m_p0X), fmt3(m_p0Y)));
    } else {
        m_p0StatusLabel->setText("P0: --");
    }
    if (m_hexLeftReg) {
        m_hexLeftStatusLabel->setText(
            QString("hex-L reg: %1, %2").arg(fmt3(m_hexLeftRegDX), fmt3(m_hexLeftRegDY)));
    } else {
        m_hexLeftStatusLabel->setText("hex-L: --");
    }
    if (m_hexRightReg) {
        m_hexRightStatusLabel->setText(
            QString("hex-R reg: %1, %2").arg(fmt3(m_hexRightRegDX), fmt3(m_hexRightRegDY)));
    } else {
        m_hexRightStatusLabel->setText("hex-R: --");
    }
}

void QtCameraWindow::onTrackingTimerTick()
{
    if (!m_viewport) return;

    QtCameraViewport::DeviceTrackingOverlayData data;
    data.enabled = true;
    data.p0Set = m_p0Set;
    data.p0X = m_p0X;
    data.p0Y = m_p0Y;

    auto liveHexOffset = [&](const std::string& dev,
                              double regDX, double regDY,
                              double regLX, double regLY, double regLZ,
                              double& outDX, double& outDY) -> bool {
        if (!m_matrixLoader) return false;
        const auto* matrix = m_matrixLoader->GetMatrix(dev);
        if (!matrix) {
            outDX = regDX; outDY = regDY;  // matrix missing — show registered offset
            return true;
        }
        double lx = 0.0, ly = 0.0, lz = 0.0;
        if (!getHexLocalXYZ(dev, lx, ly, lz)) return false;
        CoordinateTransformer::Vector6D dLocal(
            lx - regLX, ly - regLY, lz - regLZ, 0.0, 0.0, 0.0);
        auto dGlobal = CoordinateTransformer::TransformLocalToGlobal(dLocal, *matrix);
        outDX = regDX + dGlobal.x;
        outDY = regDY + dGlobal.y;
        return true;
    };

    // Hysteresis: enter alert at dist < threshold; exit only when dist > 1.5*threshold.
    // Prevents flicker when distance hovers near the threshold.
    auto applyHysteresis = [](double dist, double threshold, bool wasActive) {
        if (wasActive) return dist < (threshold * 1.5);
        return dist < threshold;
    };

    if (m_p0Set && m_hexLeftReg) {
        if (liveHexOffset("hex-left",
                          m_hexLeftRegDX, m_hexLeftRegDY,
                          m_hexLeftRegLocalX, m_hexLeftRegLocalY, m_hexLeftRegLocalZ,
                          data.hexLeftDX, data.hexLeftDY)) {
            data.hexLeftActive = true;
            const double dist = std::hypot(data.hexLeftDX, data.hexLeftDY);
            data.hexLeftAlert = applyHysteresis(
                dist, m_hexLeftAlertThreshold, m_hexLeftAlertActive);
        }
    }
    if (m_p0Set && m_hexRightReg) {
        if (liveHexOffset("hex-right",
                          m_hexRightRegDX, m_hexRightRegDY,
                          m_hexRightRegLocalX, m_hexRightRegLocalY, m_hexRightRegLocalZ,
                          data.hexRightDX, data.hexRightDY)) {
            data.hexRightActive = true;
            const double dist = std::hypot(data.hexRightDX, data.hexRightDY);
            data.hexRightAlert = applyHysteresis(
                dist, m_hexRightAlertThreshold, m_hexRightAlertActive);
        }
    }

    if (data.hexLeftAlert != m_hexLeftAlertActive) {
        m_hexLeftAlertActive = data.hexLeftAlert;
        applyHexButtonAlertStyle(m_setHexLeftBtn, m_hexLeftAlertActive);
    }
    if (data.hexRightAlert != m_hexRightAlertActive) {
        m_hexRightAlertActive = data.hexRightAlert;
        applyHexButtonAlertStyle(m_setHexRightBtn, m_hexRightAlertActive);
    }

    m_viewport->setDeviceTrackingOverlayData(data);
    m_viewport->update();
}

void QtCameraWindow::applyHexButtonAlertStyle(QPushButton* btn, bool alert)
{
    if (!btn) return;
    static const QString normalStyle =
        "QPushButton { background-color: #3e3e42; border: 1px solid #555; "
        "              color: #cccccc; padding: 4px; min-height: 24px; "
        "              border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #505054; }"
        "QPushButton:pressed { background-color: #007acc; }";
    static const QString alertStyle =
        "QPushButton { background-color: #b00020; border: 1px solid #ff5050; "
        "              color: #ffffff; padding: 4px; min-height: 24px; "
        "              border-radius: 3px; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background-color: #c8002a; }"
        "QPushButton:pressed { background-color: #007acc; }";
    btn->setStyleSheet(alert ? alertStyle : normalStyle);
}

void QtCameraWindow::promptThresholdDialog(const QString& which, double& threshold)
{
    bool ok = false;
    const double v = QInputDialog::getDouble(
        this,
        QString("%1 alert threshold").arg(which),
        "Trigger alert when |distance| < threshold (mm):",
        threshold,
        0.0,        // min
        1000.0,     // max
        4,          // decimals
        &ok);
    if (!ok) return;
    threshold = v;
    saveTrackingSettings();
}

void QtCameraWindow::onSetHexLeftRightClicked()
{
    promptThresholdDialog("hex-left", m_hexLeftAlertThreshold);
}

void QtCameraWindow::onSetHexRightRightClicked()
{
    promptThresholdDialog("hex-right", m_hexRightAlertThreshold);
}

void QtCameraWindow::loadTrackingSettings()
{
    QSettings settings("Fabrinet", "BobcatEngine");
    m_hexLeftAlertThreshold = settings.value(
        toolsKey("HexLeftAlertThreshold"), DEFAULT_HEX_ALERT_THRESHOLD).toDouble();
    m_hexRightAlertThreshold = settings.value(
        toolsKey("HexRightAlertThreshold"), DEFAULT_HEX_ALERT_THRESHOLD).toDouble();
}

void QtCameraWindow::saveTrackingSettings()
{
    QSettings settings("Fabrinet", "BobcatEngine");
    settings.setValue(toolsKey("HexLeftAlertThreshold"),  m_hexLeftAlertThreshold);
    settings.setValue(toolsKey("HexRightAlertThreshold"), m_hexRightAlertThreshold);
}

void QtCameraWindow::onQuickMoveClicked(int index)
{
    if (index < 0 || index >= QUICK_MOVE_COUNT) return;
    const auto& data = m_quickMoveData[index];
    if (!data.assigned) return;
    if (!m_motionManager) return;

    // Execute relative moves for each non-zero axis (non-blocking for X/Y, blocking for Z)
    if (data.relX != 0.0) {
        m_motionManager->MoveRelative("gantry-main", "X", data.relX, false);
    }
    if (data.relY != 0.0) {
        m_motionManager->MoveRelative("gantry-main", "Y", data.relY, false);
    }
    if (data.relZ != 0.0) {
        m_motionManager->MoveRelative("gantry-main", "Z", data.relZ, false);
    }
}

void QtCameraWindow::onQuickMoveRightClicked(int index)
{
    if (index < 0 || index >= QUICK_MOVE_COUNT) return;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #2d2d30; color: #cccccc; border: 1px solid #3e3e42; }"
        "QMenu::item:selected { background-color: #007acc; }"
    );

    auto* configAction = menu.addAction("Configure...");
    auto* clearAction  = menu.addAction("Clear");

    QAction* chosen = menu.exec(QCursor::pos());
    if (chosen == configAction) {
        showQuickMoveConfigDialog(index);
    } else if (chosen == clearAction) {
        m_quickMoveData[index] = QuickMoveButtonData{};
        updateQuickMoveButton(index);
        saveQuickMoveSettings();
    }
}

void QtCameraWindow::showQuickMoveConfigDialog(int index)
{
    if (index < 0 || index >= QUICK_MOVE_COUNT) return;

    const auto& existing = m_quickMoveData[index];

    QDialog dlg(this);
    dlg.setWindowTitle("Configure Quick Move Button");
    dlg.setFixedSize(280, 220);
    dlg.setStyleSheet(
        "QDialog { background-color: #2d2d30; color: #cccccc; }"
        "QLabel { color: #cccccc; }"
        "QLineEdit { background-color: #3e3e42; color: #cccccc; border: 1px solid #555; "
        "            padding: 2px 4px; }"
        "QDoubleSpinBox { background-color: #3e3e42; color: #cccccc; border: 1px solid #555; "
        "                 padding: 2px 4px; }"
    );

    auto* layout = new QFormLayout(&dlg);
    layout->setSpacing(8);
    layout->setContentsMargins(12, 12, 12, 12);

    auto* labelEdit = new QLineEdit(existing.label);
    labelEdit->setMaxLength(10);
    labelEdit->setPlaceholderText("10 chars max");
    layout->addRow("Label:", labelEdit);

    auto makeSpinBox = [&](double val) {
        auto* spin = new QDoubleSpinBox();
        spin->setRange(-999.0, 999.0);
        spin->setDecimals(4);
        spin->setSuffix(" mm");
        spin->setValue(val);
        return spin;
    };

    auto* xSpin = makeSpinBox(existing.relX);
    auto* ySpin = makeSpinBox(existing.relY);
    auto* zSpin = makeSpinBox(existing.relZ);

    layout->addRow("Rel X:", xSpin);
    layout->addRow("Rel Y:", ySpin);
    layout->addRow("Rel Z:", zSpin);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->setStyleSheet(
        "QPushButton { background-color: #3e3e42; color: #cccccc; border: 1px solid #555; "
        "              padding: 4px 16px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #007acc; }"
    );
    layout->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        auto& data = m_quickMoveData[index];
        data.assigned = true;
        data.label = labelEdit->text().left(10);
        data.relX  = xSpin->value();
        data.relY  = ySpin->value();
        data.relZ  = zSpin->value();

        if (data.label.isEmpty()) {
            data.label = QString("B%1").arg(index);
        }

        updateQuickMoveButton(index);
        saveQuickMoveSettings();
    }
}

void QtCameraWindow::updateQuickMoveButton(int index)
{
    if (index < 0 || index >= QUICK_MOVE_COUNT) return;
    if (!m_quickMoveBtns[index]) return;

    const auto& data = m_quickMoveData[index];

    static const QString assignedStyle =
        "QPushButton { background-color: #3e3e42; border: 1px solid #555; "
        "              color: #cccccc; padding: 6px; min-height: 28px; "
        "              border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #505054; }"
        "QPushButton:pressed { background-color: #007acc; }";

    static const QString unassignedStyle =
        "QPushButton { background-color: #333337; border: 1px dashed #555; "
        "              color: #666666; padding: 6px; min-height: 28px; "
        "              border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #3e3e42; }";

    if (data.assigned) {
        m_quickMoveBtns[index]->setText(data.label);
        m_quickMoveBtns[index]->setStyleSheet(assignedStyle);
        m_quickMoveBtns[index]->setToolTip(
            QString("X:%1  Y:%2  Z:%3 mm")
                .arg(data.relX, 0, 'f', 4)
                .arg(data.relY, 0, 'f', 4)
                .arg(data.relZ, 0, 'f', 4));
    } else {
        m_quickMoveBtns[index]->setText("---");
        m_quickMoveBtns[index]->setStyleSheet(unassignedStyle);
        m_quickMoveBtns[index]->setToolTip("");
    }
}

void QtCameraWindow::saveQuickMoveSettings()
{
    QSettings settings("Fabrinet", "BobcatEngine");
    for (int i = 0; i < QUICK_MOVE_COUNT; ++i) {
        const QString prefix = quickMoveKey(QString::number(i));
        const auto& d = m_quickMoveData[i];
        settings.setValue(prefix + "/assigned", d.assigned);
        settings.setValue(prefix + "/label",    d.label);
        settings.setValue(prefix + "/relX",     d.relX);
        settings.setValue(prefix + "/relY",     d.relY);
        settings.setValue(prefix + "/relZ",     d.relZ);
    }
    settings.sync();
}

void QtCameraWindow::loadQuickMoveSettings()
{
    QSettings settings("Fabrinet", "BobcatEngine");
    for (int i = 0; i < QUICK_MOVE_COUNT; ++i) {
        const QString prefix = quickMoveKey(QString::number(i));
        auto& d = m_quickMoveData[i];
        d.assigned = settings.value(prefix + "/assigned", false).toBool();
        d.label    = settings.value(prefix + "/label", "").toString().left(10);
        d.relX     = settings.value(prefix + "/relX", 0.0).toDouble();
        d.relY     = settings.value(prefix + "/relY", 0.0).toDouble();
        d.relZ     = settings.value(prefix + "/relZ", 0.0).toDouble();
        updateQuickMoveButton(i);
    }
}

QString QtCameraWindow::quickMoveKey(const QString& key) const
{
    const QString prefix = m_instanceId.empty()
        ? QStringLiteral("QtCameraWindow")
        : QString::fromStdString("QtCameraWindow_" + m_instanceId);
    return prefix + "/QuickMove/" + key;
}

// ============================================================================
// Side panel group factories
// ============================================================================

QWidget* QtCameraWindow::createCameraTabs()
{
    auto* tabs = new QTabWidget();
    tabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #3e3e42; border-radius: 4px; "
        "                   background-color: #252526; top: -1px; }"
        "QTabBar::tab { background-color: #333337; color: #cccccc; "
        "               padding: 4px 10px; border: 1px solid #3e3e42; "
        "               border-bottom: none; border-top-left-radius: 3px; "
        "               border-top-right-radius: 3px; }"
        "QTabBar::tab:selected { background-color: #252526; color: #ffffff; }"
        "QTabBar::tab:hover { background-color: #3e3e42; }"
    );

    tabs->addTab(createCameraSelectionTab(), "Camera");
    tabs->addTab(createExposureTab(),        "Exposure");

    return tabs;
}

QWidget* QtCameraWindow::createCameraSelectionTab()
{
    auto* page = new QWidget();

    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(4);
    layout->setContentsMargins(6, 8, 6, 6);

    // Primary camera row
    layout->addWidget(new QLabel("Primary Camera:"));
    m_primaryCameraCombo = new QComboBox();
    m_primaryCameraCombo->setStyleSheet(
        "QComboBox { background-color: #3e3e42; border: 1px solid #555; "
        "            color: #cccccc; padding: 2px 4px; }"
        "QComboBox QAbstractItemView { background-color: #3e3e42; color: #cccccc; }"
    );
    layout->addWidget(m_primaryCameraCombo);

    // Dual mode checkbox
    m_dualModeCheck = new QCheckBox("Dual Camera Mode");
    layout->addWidget(m_dualModeCheck);

    // Secondary camera row (initially hidden)
    m_secondaryCameraLabel = new QLabel("Secondary Camera:");
    m_secondaryCameraLabel->setVisible(false);
    layout->addWidget(m_secondaryCameraLabel);

    m_secondaryCameraCombo = new QComboBox();
    m_secondaryCameraCombo->setStyleSheet(m_primaryCameraCombo->styleSheet());
    m_secondaryCameraCombo->setVisible(false);
    layout->addWidget(m_secondaryCameraCombo);

    layout->addStretch();

    // Connections
    connect(m_primaryCameraCombo, &QComboBox::currentTextChanged,
            this, &QtCameraWindow::onPrimaryCameraChanged);
    connect(m_secondaryCameraCombo, &QComboBox::currentTextChanged,
            this, &QtCameraWindow::onSecondaryCameraChanged);
    connect(m_dualModeCheck, &QCheckBox::toggled,
            this, &QtCameraWindow::onDualModeToggled);

    return page;
}

QWidget* QtCameraWindow::createExposureTab()
{
    auto* page = new QWidget();

    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(4);
    layout->setContentsMargins(6, 8, 6, 6);

    static const QString btnStyle =
        "QPushButton { background-color: #3e3e42; border: 1px solid #555; "
        "              color: #cccccc; padding: 4px 2px; min-height: 24px; "
        "              border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #505054; }"
        "QPushButton:pressed { background-color: #007acc; }";

    // Current exposure readout
    m_exposureValueLabel = new QLabel("Current: -- us");
    m_exposureValueLabel->setStyleSheet(
        "QLabel { color: #ffffff; background-color: #1e1e1e; "
        "         border: 1px solid #3e3e42; padding: 4px; border-radius: 3px; }"
    );
    m_exposureValueLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_exposureValueLabel);

    // Preset buttons (2 cols x 5 rows)
    auto* presetLabel = new QLabel("Presets:");
    presetLabel->setStyleSheet("color: #cccccc;");
    layout->addWidget(presetLabel);

    struct Preset { const char* label; double us; };
    static const Preset kPresets[] = {
        {  "30 us",    30.0 },
        {  "50 us",    50.0 },
        { "250 us",   250.0 },
        { "300 us",   300.0 },
        { "500 us",   500.0 },
        {   "5k us",  5000.0 },
        {   "8k us",  8000.0 },
        {  "10k us", 10000.0 },
        {  "15k us", 15000.0 },
        {  "20k us", 20000.0 },
    };

    auto* presetGrid = new QGridLayout();
    presetGrid->setSpacing(4);
    constexpr int kPresetCols = 2;
    const int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
    for (int i = 0; i < kPresetCount; ++i) {
        auto* btn = new QPushButton(kPresets[i].label);
        btn->setStyleSheet(btnStyle);
        const double us = kPresets[i].us;
        connect(btn, &QPushButton::clicked, this, [this, us]() {
            applyExposureValue(us);
        });
        presetGrid->addWidget(btn, i / kPresetCols, i % kPresetCols);
    }
    layout->addLayout(presetGrid);

    // Fine adjust row: -100, -10, +10, +100
    auto* adjustLabel = new QLabel("Adjust:");
    adjustLabel->setStyleSheet("color: #cccccc;");
    layout->addWidget(adjustLabel);

    auto* adjustRow = new QHBoxLayout();
    adjustRow->setSpacing(4);

    struct Step { const char* label; double delta; };
    static const Step kSteps[] = {
        { "-100", -100.0 },
        {  "-10",  -10.0 },
        {  "+10",  +10.0 },
        { "+100", +100.0 },
    };
    for (const auto& s : kSteps) {
        auto* btn = new QPushButton(s.label);
        btn->setStyleSheet(btnStyle);
        const double delta = s.delta;
        connect(btn, &QPushButton::clicked, this, [this, delta]() {
            adjustExposure(delta);
        });
        adjustRow->addWidget(btn);
    }
    layout->addLayout(adjustRow);

    layout->addStretch();

    return page;
}

// ============================================================================
// Exposure control helpers
// ============================================================================

void QtCameraWindow::applyExposureValue(double exposureUs)
{
    if (!m_cameraManager) return;
    if (m_targetCameraId.empty()) {
        if (m_exposureValueLabel) {
            m_exposureValueLabel->setText("No camera selected");
        }
        return;
    }

    auto settings = m_cameraManager->GetExposureSettings(m_targetCameraId);
    settings.auto_exposure = false;
    settings.exposure_time = exposureUs;

    if (m_cameraManager->ApplyExposureSettings(m_targetCameraId, settings)) {
        refreshExposureDisplay();
    } else if (m_exposureValueLabel) {
        m_exposureValueLabel->setText(QString("Failed: %1 us").arg(exposureUs, 0, 'f', 0));
    }
}

void QtCameraWindow::adjustExposure(double deltaUs)
{
    if (!m_cameraManager) return;
    if (m_targetCameraId.empty()) return;

    auto settings = m_cameraManager->GetExposureSettings(m_targetCameraId);
    double next = settings.exposure_time + deltaUs;
    if (next < 1.0) next = 1.0;
    applyExposureValue(next);
}

void QtCameraWindow::refreshExposureDisplay()
{
    if (!m_exposureValueLabel) return;
    if (!m_cameraManager || m_targetCameraId.empty()) {
        m_exposureValueLabel->setText("Current: -- us");
        return;
    }
    const auto settings = m_cameraManager->GetExposureSettings(m_targetCameraId);
    m_exposureValueLabel->setText(
        QString("Current: %1 us").arg(settings.exposure_time, 0, 'f', 0));
}

QGroupBox* QtCameraWindow::createToolsGroup()
{
    auto* group = new QGroupBox("Tools");
    group->setStyleSheet(
        "QGroupBox { color: #cccccc; border: 1px solid #3e3e42; border-radius: 4px; "
        "            margin-top: 6px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
    );

    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(4);
    layout->setContentsMargins(6, 8, 6, 6);

    m_crosshairCheck = new QCheckBox("Crosshair");
    m_clickToMoveCheck = new QCheckBox("Click-to-Move");
    m_showDistanceCheck = new QCheckBox("Show Distance");

    layout->addWidget(m_crosshairCheck);
    layout->addWidget(m_clickToMoveCheck);
    layout->addWidget(m_showDistanceCheck);

    // Magnification
    layout->addWidget(new QLabel("Magnification:"));
    m_magnificationCombo = new QComboBox();
    m_magnificationCombo->setStyleSheet(
        "QComboBox { background-color: #3e3e42; border: 1px solid #555; "
        "            color: #cccccc; padding: 2px 4px; }"
        "QComboBox QAbstractItemView { background-color: #3e3e42; color: #cccccc; }"
    );
    m_magnificationCombo->addItems({"1x", "10x", "20x", "50x", "100x", "200x"});
    layout->addWidget(m_magnificationCombo);

    // Display rotation
    layout->addWidget(new QLabel("Display Rotation:"));
    m_rotationCombo = new QComboBox();
    m_rotationCombo->setStyleSheet(m_magnificationCombo->styleSheet());
    m_rotationCombo->addItems({"None", "-90 deg", "+90 deg"});
    layout->addWidget(m_rotationCombo);

    // Connections
    connect(m_crosshairCheck, &QCheckBox::toggled,
            this, &QtCameraWindow::onCrosshairToggled);
    connect(m_clickToMoveCheck, &QCheckBox::toggled,
            this, &QtCameraWindow::onClickToMoveToggled);
    connect(m_showDistanceCheck, &QCheckBox::toggled,
            this, &QtCameraWindow::onShowDistanceToggled);
    connect(m_magnificationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QtCameraWindow::onMagnificationChanged);
    connect(m_rotationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QtCameraWindow::onRotationChanged);

    return group;
}

QGroupBox* QtCameraWindow::createChartGroup()
{
    static const char* kGroupStyle =
        "QGroupBox { color: #cccccc; border: 1px solid #3e3e42; border-radius: 4px; "
        "            margin-top: 6px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }";

    static const char* kButtonStyle =
        "QPushButton { background-color: #3e3e42; border: 1px solid #555; "
        "              color: #cccccc; padding: 2px 6px; border-radius: 2px; }"
        "QPushButton:hover { background-color: #505050; }"
        "QPushButton:pressed { background-color: #2d2d30; }";

    auto* group = new QGroupBox("Chart Overlay");
    group->setStyleSheet(kGroupStyle);

    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(4);
    layout->setContentsMargins(6, 8, 6, 6);

    // Enable checkbox
    m_chartEnableCheck = new QCheckBox("Enable Chart");
    layout->addWidget(m_chartEnableCheck);

    // Channel list with checkboxes
    m_chartChannelList = new QListWidget();
    m_chartChannelList->setMaximumHeight(120);
    m_chartChannelList->setSelectionMode(QAbstractItemView::NoSelection);
    m_chartChannelList->setStyleSheet(
        "QListWidget { background-color: #1e1e1e; border: 1px solid #3e3e42; "
        "              color: #cccccc; font-size: 11px; }"
        "QListWidget::item { padding: 2px 4px; }"
        "QListWidget::item:hover { background-color: #2a2d2e; }"
        "QListWidget::indicator { width: 14px; height: 14px; }"
        "QListWidget::indicator:unchecked { border: 1px solid #666; background: #1e1e1e; }"
        "QListWidget::indicator:checked { border: 1px solid #0078d4; background: #0078d4; }"
    );
    layout->addWidget(m_chartChannelList);

    // Refresh channels button
    m_chartRefreshBtn = new QPushButton("Refresh Channels");
    m_chartRefreshBtn->setStyleSheet(kButtonStyle);
    layout->addWidget(m_chartRefreshBtn);

    // Scale row: label + "+" and "-" buttons
    auto* scaleRow = new QHBoxLayout();
    auto* scaleLabel = new QLabel("Scale:");
    scaleLabel->setStyleSheet("color: #cccccc;");
    m_chartScaleUpBtn = new QPushButton("+");
    m_chartScaleUpBtn->setStyleSheet(kButtonStyle);
    m_chartScaleUpBtn->setFixedWidth(28);
    m_chartScaleDnBtn = new QPushButton("-");
    m_chartScaleDnBtn->setStyleSheet(kButtonStyle);
    m_chartScaleDnBtn->setFixedWidth(28);

    scaleRow->addWidget(scaleLabel);
    scaleRow->addStretch();
    scaleRow->addWidget(m_chartScaleUpBtn);
    scaleRow->addWidget(m_chartScaleDnBtn);
    layout->addLayout(scaleRow);

    // Connections
    connect(m_chartEnableCheck, &QCheckBox::toggled,
            this, &QtCameraWindow::onChartEnableToggled);
    connect(m_chartRefreshBtn, &QPushButton::clicked,
            this, &QtCameraWindow::onChartRefreshChannels);
    connect(m_chartScaleUpBtn, &QPushButton::clicked,
            this, &QtCameraWindow::onChartScaleIncrease);
    connect(m_chartScaleDnBtn, &QPushButton::clicked,
            this, &QtCameraWindow::onChartScaleDecrease);
    connect(m_chartChannelList, &QListWidget::itemChanged,
            this, [this](QListWidgetItem* item) {
        std::string channelId = item->text().toStdString();
        std::lock_guard<std::mutex> lock(m_chartMutex);
        if (item->checkState() == Qt::Checked) {
            m_chartEnabledChannels.insert(channelId);
        } else {
            m_chartEnabledChannels.erase(channelId);
        }
        if (m_viewport) m_viewport->update();
    });

    return group;
}

QGroupBox* QtCameraWindow::createVisionGroup()
{
#ifdef HAS_OPENCV
    static const char* kGroupStyle =
        "QGroupBox { color: #cccccc; border: 1px solid #3e3e42; border-radius: 4px; "
        "            margin-top: 6px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }";

    static const char* kButtonStyle =
        "QPushButton { background-color: #3e3e42; border: 1px solid #555; "
        "              color: #cccccc; padding: 2px 6px; border-radius: 2px; }"
        "QPushButton:hover { background-color: #505050; }"
        "QPushButton:pressed { background-color: #2d2d30; }";

    static const char* kComboStyle =
        "QComboBox { background-color: #3e3e42; border: 1px solid #555; "
        "            color: #cccccc; padding: 2px 4px; }"
        "QComboBox QAbstractItemView { background-color: #3e3e42; color: #cccccc; }";

    auto* group = new QGroupBox("Vision");
    group->setStyleSheet(kGroupStyle);

    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(4);
    layout->setContentsMargins(6, 8, 6, 6);

    m_visionEnableCheck = new QCheckBox("Enable Vision");
    layout->addWidget(m_visionEnableCheck);

    layout->addWidget(new QLabel("Profile:"));

    m_visionProfileCombo = new QComboBox();
    m_visionProfileCombo->setStyleSheet(kComboStyle);
    layout->addWidget(m_visionProfileCombo);

    m_visionRefreshBtn = new QPushButton("Refresh Profiles");
    m_visionRefreshBtn->setStyleSheet(kButtonStyle);
    layout->addWidget(m_visionRefreshBtn);

    connect(m_visionEnableCheck, &QCheckBox::toggled,
            this, &QtCameraWindow::onVisionEnableToggled);
    connect(m_visionProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QtCameraWindow::onVisionProfileChanged);
    connect(m_visionRefreshBtn, &QPushButton::clicked,
            this, &QtCameraWindow::onVisionRefreshProfiles);

    // Populate profiles on creation
    discoverVisionProfiles();
    populateVisionProfileCombo();

    return group;
#else
    auto* group = new QGroupBox("Vision");
    group->setStyleSheet(
        "QGroupBox { color: #888888; border: 1px solid #3e3e42; border-radius: 4px; "
        "            margin-top: 6px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
    );
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(6, 8, 6, 6);
    auto* label = new QLabel("OpenCV not available");
    label->setStyleSheet("color: #666666; font-style: italic;");
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
    return group;
#endif
}

QGroupBox* QtCameraWindow::createBeamGroup()
{
    static const char* kGroupStyle =
        "QGroupBox { color: #cccccc; border: 1px solid #3e3e42; border-radius: 4px; "
        "            margin-top: 6px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }";

    static const char* kComboStyle =
        "QComboBox { background-color: #3e3e42; border: 1px solid #555; "
        "            color: #cccccc; padding: 2px 4px; }"
        "QComboBox QAbstractItemView { background-color: #3e3e42; color: #cccccc; }";

    auto* group = new QGroupBox("Beam Streaming");
    group->setStyleSheet(kGroupStyle);

    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(4);
    layout->setContentsMargins(6, 8, 6, 6);

    m_beamEnableCheck = new QCheckBox("Enable Beam Stream");
    layout->addWidget(m_beamEnableCheck);

    layout->addWidget(new QLabel("Camera:"));

    m_beamCameraCombo = new QComboBox();
    m_beamCameraCombo->setStyleSheet(kComboStyle);
    layout->addWidget(m_beamCameraCombo);

    connect(m_beamEnableCheck, &QCheckBox::toggled,
            this, &QtCameraWindow::onBeamEnableToggled);
    connect(m_beamCameraCombo, &QComboBox::currentTextChanged,
            this, &QtCameraWindow::onBeamCameraChanged);

    return group;
}

// ============================================================================
// CameraFrameSubscriber interface
// ============================================================================

void QtCameraWindow::OnNewFrame(const CameraFrameData& frameData)
{
    if (!frameData.IsValid()) return;

    // Determine primary vs. secondary
    bool isPrimary   = false;
    bool isSecondary = false;

    if (m_dualMode) {
        if (m_targetCameraId.empty() || frameData.cameraId == m_targetCameraId) {
            isPrimary = true;
        } else if (!m_secondaryCameraId.empty() && frameData.cameraId == m_secondaryCameraId) {
            isSecondary = true;
        }
    } else {
        if (m_targetCameraId.empty() || frameData.cameraId == m_targetCameraId) {
            isPrimary = true;
        }
    }

    if (!isPrimary && !isSecondary) return;

    const size_t requiredBytes =
        static_cast<size_t>(frameData.width) * frameData.height * 3u;

    if (isPrimary) {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (m_frameBuffer.size() < requiredBytes) {
            m_frameBuffer.resize(requiredBytes);
        }
        m_frameWidth  = frameData.width;
        m_frameHeight = frameData.height;
        std::memcpy(m_frameBuffer.data(), frameData.imageData.data(), requiredBytes);
        m_hasNewFrame = true;

        // Track acquisition FPS
        ++m_acqFrameCount;
        const auto now     = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now - m_acqFpsTimestamp).count();
        if (elapsed >= 1000) {
            m_acquisitionFps.store(
                static_cast<double>(m_acqFrameCount) * 1000.0 / static_cast<double>(elapsed));
            m_acqFrameCount   = 0;
            m_acqFpsTimestamp = now;
        }
    } else {
        // isSecondary
        std::lock_guard<std::mutex> lock(m_secondaryFrameMutex);
        if (m_secondaryFrameBuffer.size() < requiredBytes) {
            m_secondaryFrameBuffer.resize(requiredBytes);
        }
        m_secondaryWidth  = frameData.width;
        m_secondaryHeight = frameData.height;
        std::memcpy(m_secondaryFrameBuffer.data(), frameData.imageData.data(), requiredBytes);
        m_hasSecondaryNewFrame = true;
    }

    // Feed vision frame queue (primary frames only)
#ifdef HAS_OPENCV
    if (isPrimary && m_visionEnabled.load() && m_visionThreadRunning.load()) {
        std::lock_guard<std::mutex> vLock(m_visionFrameQueueMutex);
        if (m_visionFrameQueue.size() < 2) {
            m_visionFrameQueue.push(std::vector<uint8_t>(
                frameData.imageData.begin(), frameData.imageData.end()));
            m_visionQueuedWidth  = frameData.width;
            m_visionQueuedHeight = frameData.height;
        }
        m_visionFrameQueueCV.notify_one();
    }
#endif

    // Feed beam stream queue (camera-id filtered)
    if (m_beamStreamEnabled.load() && m_beamStreamThreadRunning.load() &&
        frameData.cameraId == m_beamStreamCameraId) {
        std::lock_guard<std::mutex> bLock(m_beamStreamQueueMutex);
        if (m_beamStreamQueue.size() < 2) {
            m_beamStreamQueue.push(std::vector<uint8_t>(
                frameData.imageData.begin(), frameData.imageData.end()));
            m_beamStreamQueuedWidth  = frameData.width;
            m_beamStreamQueuedHeight = frameData.height;
        }
        m_beamStreamQueueCV.notify_one();
    }

    // Marshal the UI update to the Qt main thread
    QMetaObject::invokeMethod(this, "updateViewport", Qt::QueuedConnection);
}

void QtCameraWindow::OnCameraStatusChanged(const std::string& cameraId,
                                           bool connected,
                                           bool /*grabbing*/)
{
    std::cout << "[QtCameraWindow] Camera status changed: " << cameraId
              << " connected=" << connected << std::endl;
}

std::string QtCameraWindow::GetSubscriberId() const
{
    return m_subscriberId;
}

bool QtCameraWindow::WantsFramesFromCamera(const std::string& cameraId) const
{
    if (m_targetCameraId.empty()) return true;
    if (cameraId == m_targetCameraId) return true;
    if (m_dualMode && !m_secondaryCameraId.empty() && cameraId == m_secondaryCameraId) return true;
    return false;
}

// ============================================================================
// Main-thread viewport update slot
// ============================================================================

void QtCameraWindow::updateViewport()
{
    if (!m_viewport) return;

    if (m_hasNewFrame.exchange(false)) {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (m_frameWidth > 0 && m_frameHeight > 0 && !m_frameBuffer.empty()) {
            m_viewport->setFrame(m_frameBuffer.data(),
                                 static_cast<int>(m_frameWidth),
                                 static_cast<int>(m_frameHeight));
        }
    }

    if (m_hasSecondaryNewFrame.exchange(false)) {
        std::lock_guard<std::mutex> lock(m_secondaryFrameMutex);
        if (m_secondaryWidth > 0 && m_secondaryHeight > 0 && !m_secondaryFrameBuffer.empty()) {
            m_viewport->setSecondaryFrame(m_secondaryFrameBuffer.data(),
                                          static_cast<int>(m_secondaryWidth),
                                          static_cast<int>(m_secondaryHeight));
        }
    }

    m_viewport->setAcquisitionFps(m_acquisitionFps.load());

    // Push chart overlay data
    if (m_chartEnabled) {
        std::lock_guard<std::mutex> lock(m_chartMutex);
        std::vector<QtCameraViewport::ChartChannelSnapshot> snapshots;
        for (const auto& chId : m_chartEnabledChannels) {
            auto it = m_chartChannels.find(chId);
            if (it != m_chartChannels.end() && !it->second.values.empty()) {
                snapshots.push_back({chId, it->second.values});
            }
        }
        m_viewport->setChartOverlayData(true, m_chartScale, snapshots);
    } else {
        m_viewport->setChartOverlayData(false, 1.0f, {});
    }

#ifdef HAS_OPENCV
    // Push vision overlay data
    if (m_visionEnabled.load()) {
        std::lock_guard<std::mutex> lock(m_visionResultMutex);
        m_viewport->setVisionOverlayData(true, m_lastVisionResult, m_visionProfileName);
    } else {
        m_viewport->setVisionOverlayData(false, {}, "");
    }
#endif

    // Push beam overlay data
    if (m_beamStreamEnabled.load()) {
        std::lock_guard<std::mutex> lock(m_beamResultMutex);
        m_viewport->setBeamOverlayData(true, m_lastBeamResult, m_beamStreamFps.load(),
                                        m_beamStreamConfig.umPerPxX, m_beamStreamConfig.umPerPxY);
    } else {
        m_viewport->setBeamOverlayData(false, {}, 0.0, 0.25, 0.25);
    }
}

// ============================================================================
// Camera control
// ============================================================================

void QtCameraWindow::setTargetCamera(const std::string& cameraId)
{
    if (m_targetCameraId == cameraId) return;
    m_targetCameraId = cameraId;

    loadCalibrationFactors();

    // Sync the primary combo without triggering the slot
    if (m_primaryCameraCombo) {
        m_primaryCameraCombo->blockSignals(true);
        const int idx = m_primaryCameraCombo->findText(
            QString::fromStdString(cameraId));
        if (idx >= 0) m_primaryCameraCombo->setCurrentIndex(idx);
        m_primaryCameraCombo->blockSignals(false);
    }

    refreshExposureDisplay();
}

std::string QtCameraWindow::getTargetCamera() const
{
    return m_targetCameraId;
}

void QtCameraWindow::setDualCameraMode(bool enabled)
{
    m_dualMode = enabled;

    if (m_dualModeCheck) {
        m_dualModeCheck->blockSignals(true);
        m_dualModeCheck->setChecked(enabled);
        m_dualModeCheck->blockSignals(false);
    }

    if (m_secondaryCameraCombo) m_secondaryCameraCombo->setVisible(enabled);
    if (m_secondaryCameraLabel)  m_secondaryCameraLabel->setVisible(enabled);

    if (m_viewport) m_viewport->setDualMode(enabled);
}

bool QtCameraWindow::isDualCameraMode() const
{
    return m_dualMode;
}

void QtCameraWindow::setSecondaryCameraId(const std::string& cameraId)
{
    m_secondaryCameraId = cameraId;

    if (m_secondaryCameraCombo) {
        m_secondaryCameraCombo->blockSignals(true);
        const int idx = m_secondaryCameraCombo->findText(
            QString::fromStdString(cameraId));
        if (idx >= 0) m_secondaryCameraCombo->setCurrentIndex(idx);
        m_secondaryCameraCombo->blockSignals(false);
    }
}

std::string QtCameraWindow::getSecondaryCameraId() const
{
    return m_secondaryCameraId;
}

// ============================================================================
// Tools
// ============================================================================

void QtCameraWindow::setCrosshairEnabled(bool enabled)
{
    if (m_crosshairCheck) {
        m_crosshairCheck->blockSignals(true);
        m_crosshairCheck->setChecked(enabled);
        m_crosshairCheck->blockSignals(false);
    }
    if (m_viewport) m_viewport->setCrosshairEnabled(enabled);
}

void QtCameraWindow::setClickToMoveEnabled(bool enabled)
{
    m_clickToMoveEnabled = enabled;
    if (m_clickToMoveCheck) {
        m_clickToMoveCheck->blockSignals(true);
        m_clickToMoveCheck->setChecked(enabled);
        m_clickToMoveCheck->blockSignals(false);
    }
    if (m_viewport) m_viewport->setClickToMoveEnabled(enabled);
}

void QtCameraWindow::setShowDistanceEnabled(bool enabled)
{
    if (m_showDistanceCheck) {
        m_showDistanceCheck->blockSignals(true);
        m_showDistanceCheck->setChecked(enabled);
        m_showDistanceCheck->blockSignals(false);
    }
    if (m_viewport) m_viewport->setShowDistanceEnabled(enabled);
}

void QtCameraWindow::setMagnification(double mag)
{
    m_magnification = mag;
    if (m_magnificationCombo) {
        const int idx = indexFromMagnification(mag);
        m_magnificationCombo->blockSignals(true);
        m_magnificationCombo->setCurrentIndex(idx);
        m_magnificationCombo->blockSignals(false);
    }
    if (m_viewport) m_viewport->setMagnification(mag);
}

void QtCameraWindow::setDisplayRotation(int degrees)
{
    if (m_rotationCombo) {
        const int idx = indexFromRotation(degrees);
        m_rotationCombo->blockSignals(true);
        m_rotationCombo->setCurrentIndex(idx);
        m_rotationCombo->blockSignals(false);
    }
    if (m_viewport) m_viewport->setDisplayRotation(degrees);
}

// ============================================================================
// Viewport access
// ============================================================================

QtCameraViewport* QtCameraWindow::viewport() const
{
    return m_viewport;
}

// ============================================================================
// Camera list refresh
// ============================================================================

void QtCameraWindow::refreshCameraList()
{
    if (!m_cameraManager) return;

    const std::vector<std::string> ids = m_cameraManager->GetCameraIds();

    if (m_primaryCameraCombo) {
        m_primaryCameraCombo->blockSignals(true);
        const QString prevPrimary = m_primaryCameraCombo->currentText();
        m_primaryCameraCombo->clear();
        m_primaryCameraCombo->addItem("");  // empty = no specific camera
        for (const auto& id : ids) {
            m_primaryCameraCombo->addItem(QString::fromStdString(id));
        }
        const int restorePrimary = m_primaryCameraCombo->findText(prevPrimary);
        if (restorePrimary >= 0) m_primaryCameraCombo->setCurrentIndex(restorePrimary);
        m_primaryCameraCombo->blockSignals(false);
    }

    if (m_secondaryCameraCombo) {
        m_secondaryCameraCombo->blockSignals(true);
        const QString prevSecondary = m_secondaryCameraCombo->currentText();
        m_secondaryCameraCombo->clear();
        m_secondaryCameraCombo->addItem("");
        for (const auto& id : ids) {
            m_secondaryCameraCombo->addItem(QString::fromStdString(id));
        }
        const int restoreSecondary = m_secondaryCameraCombo->findText(prevSecondary);
        if (restoreSecondary >= 0) m_secondaryCameraCombo->setCurrentIndex(restoreSecondary);
        m_secondaryCameraCombo->blockSignals(false);
    }

    // Also refresh beam camera combo
    if (m_beamCameraCombo) {
        m_beamCameraCombo->blockSignals(true);
        const QString prevBeam = m_beamCameraCombo->currentText();
        m_beamCameraCombo->clear();
        m_beamCameraCombo->addItem("");
        for (const auto& id : ids) {
            m_beamCameraCombo->addItem(QString::fromStdString(id));
        }
        const int restoreBeam = m_beamCameraCombo->findText(prevBeam);
        if (restoreBeam >= 0) m_beamCameraCombo->setCurrentIndex(restoreBeam);
        m_beamCameraCombo->blockSignals(false);
    }
}

// ============================================================================
// Calibration
// ============================================================================

void QtCameraWindow::loadCalibrationFactors()
{
    // 1. Try per-camera calibration from CameraConfigManager
    if (m_cameraConfigManager && !m_targetCameraId.empty()) {
        CameraConfigData::PixelCalibration cal;
        if (m_cameraConfigManager->GetCalibration(m_targetCameraId, cal)) {
            m_pixToMmX       = cal.pixelToMmX;
            m_pixToMmY       = cal.pixelToMmY;
            m_calibRefWidth  = cal.referenceWidth;
            m_calibRefHeight = cal.referenceHeight;
            std::cout << "[QtCameraWindow] Per-camera calibration for '" << m_targetCameraId
                      << "': X=" << m_pixToMmX << " Y=" << m_pixToMmY;
            if (m_calibRefWidth > 0 && m_calibRefHeight > 0) {
                std::cout << " RefRes=" << m_calibRefWidth << "x" << m_calibRefHeight;
            }
            std::cout << std::endl;

            if (m_viewport) {
                m_viewport->setCalibrationFactors(
                    m_pixToMmX, m_pixToMmY, m_calibRefWidth, m_calibRefHeight);
            }
            return;
        }
        std::cout << "[QtCameraWindow] No per-camera calibration for '" << m_targetCameraId
                  << "', falling back to " << CALIBRATION_CONFIG_FILE << std::endl;
    }

    // 2. Fallback: load from global JSON config
    const std::vector<std::string> paths = {
        CALIBRATION_CONFIG_FILE,
        std::string("./") + CALIBRATION_CONFIG_FILE,
        std::string("../") + CALIBRATION_CONFIG_FILE
    };

    for (const auto& path : paths) {
        std::ifstream file(path);
        if (!file.is_open()) continue;

        // No-exception JSON parse: check stream state after >>
        nlohmann::json j;
        try {
            file >> j;
        } catch (...) {
            std::cerr << "[QtCameraWindow] Failed to parse " << path << std::endl;
            continue;
        }

        if (j.contains("calibration")) {
            const auto& cal  = j["calibration"];
            m_pixToMmX       = cal.value("pixel_to_mm_x",    0.00248);
            m_pixToMmY       = cal.value("pixel_to_mm_y",    0.00248);
            m_calibRefWidth  = cal.value("reference_width",  0);
            m_calibRefHeight = cal.value("reference_height", 0);

            std::cout << "[QtCameraWindow] Calibration from " << path
                      << ": X=" << m_pixToMmX << " Y=" << m_pixToMmY;
            if (m_calibRefWidth > 0 && m_calibRefHeight > 0) {
                std::cout << " RefRes=" << m_calibRefWidth << "x" << m_calibRefHeight;
            }
            std::cout << std::endl;

            if (m_viewport) {
                m_viewport->setCalibrationFactors(
                    m_pixToMmX, m_pixToMmY, m_calibRefWidth, m_calibRefHeight);
            }
            return;
        }
    }

    std::cout << "[QtCameraWindow] Using default calibration: X="
              << m_pixToMmX << " Y=" << m_pixToMmY << std::endl;

    if (m_viewport) {
        m_viewport->setCalibrationFactors(
            m_pixToMmX, m_pixToMmY, m_calibRefWidth, m_calibRefHeight);
    }
}

// ============================================================================
// Click-to-move
// ============================================================================

void QtCameraWindow::handleClickToMove(int imageX, int imageY)
{
    if (!m_motionManager) {
        std::cerr << "[QtCameraWindow] Cannot click-to-move: no motion manager" << std::endl;
        return;
    }
    if (!m_viewport) {
        std::cerr << "[QtCameraWindow] Cannot click-to-move: no viewport" << std::endl;
        return;
    }

    const int dispW = m_viewport->displayedFrameWidth();
    const int dispH = m_viewport->displayedFrameHeight();
    if (dispW <= 0 || dispH <= 0) {
        std::cerr << "[QtCameraWindow] Cannot click-to-move: no valid image displayed" << std::endl;
        return;
    }

    // imageX/imageY are already in image-pixel coordinates (mapped by viewport)
    // Compute offset from image center
    const int imgCenterX = dispW / 2;
    const int imgCenterY = dispH / 2;

    const int pixelOffsetX = imageX - imgCenterX;
    const int pixelOffsetY = imageY - imgCenterY;

    // Effective pixel-to-mm (auto-scale if calibrated at a different reference resolution)
    double effectivePixToMmX = m_pixToMmX;
    double effectivePixToMmY = m_pixToMmY;

    const uint32_t frameW = m_frameWidth;
    const uint32_t frameH = m_frameHeight;

    if (m_calibRefWidth > 0 && m_calibRefHeight > 0 && frameW > 0 && frameH > 0) {
        effectivePixToMmX = m_pixToMmX *
            static_cast<double>(m_calibRefWidth) / static_cast<double>(frameW);
        effectivePixToMmY = m_pixToMmY *
            static_cast<double>(m_calibRefHeight) / static_cast<double>(frameH);
    }

    const double mag = (m_magnification > 0.0) ? m_magnification : 1.0;
    const double offsetMmX = (static_cast<double>(pixelOffsetX) * effectivePixToMmX) / mag;
    const double offsetMmY = (static_cast<double>(pixelOffsetY) * effectivePixToMmY) / mag;

    std::cout << "[QtCameraWindow] Click pixel offset=(" << pixelOffsetX << ","
              << pixelOffsetY << ") mm=(" << offsetMmX << "," << offsetMmY << ")" << std::endl;

    const PositionStruct currentPos = m_motionManager->GetCurrentPosition("gantry-main");

    const double newX = currentPos.x + offsetMmX;
    const double newY = currentPos.y + offsetMmY;
    const double newZ = currentPos.z;

    std::cout << "[QtCameraWindow] Move: current=(" << currentPos.x << "," << currentPos.y
              << "," << currentPos.z << ") target=(" << newX << "," << newY
              << "," << newZ << ")" << std::endl;

    const bool ok = m_motionManager->MoveToAbs("gantry-main", newX, newY, newZ, 10.0, false);
    if (!ok) {
        std::cerr << "[QtCameraWindow] MoveToAbs failed" << std::endl;
    }
}

// ============================================================================
// Dependency injection
// ============================================================================

void QtCameraWindow::setCameraManager(CameraManager* mgr)
{
    m_cameraManager = mgr;
}

void QtCameraWindow::setDataStore(DataStore* store)
{
    m_dataStore = store;
    if (m_dataStore) {
        m_dataStore->SubscribeToAll([this](const std::string& channelId, float value) {
            onDataStoreValueChanged(channelId, value);
        });
    }
}

void QtCameraWindow::setMotionManager(UnifiedMotionManager* mgr)
{
    m_motionManager = mgr;
}

void QtCameraWindow::setCameraConfigManager(CameraConfigManager* mgr)
{
    m_cameraConfigManager = mgr;
    // Reload calibration now that per-camera config is available
    loadCalibrationFactors();
}

// ============================================================================
// Settings
// ============================================================================

QString QtCameraWindow::settingsKey(const QString& key) const
{
    const QString prefix = m_instanceId.empty()
        ? QStringLiteral("GdiCameraWindow")
        : QStringLiteral("GdiCameraWindow") + QString::fromStdString(m_instanceId);
    return prefix + "/" + key;
}

QString QtCameraWindow::monitorKey(const QString& key) const
{
    const QString prefix = m_instanceId.empty()
        ? QStringLiteral("CameraMonitor")
        : QStringLiteral("CameraMonitor") + QString::fromStdString(m_instanceId);
    return prefix + "/" + key;
}

QString QtCameraWindow::toolsKey(const QString& key) const
{
    const QString prefix = m_instanceId.empty()
        ? QStringLiteral("Tools")
        : QStringLiteral("Tools") + QString::fromStdString(m_instanceId);
    return prefix + "/" + key;
}

void QtCameraWindow::saveSettings()
{
    QSettings settings("Fabrinet", "BobcatEngine");

    // Geometry — use saveGeometry() for reliable cross-platform persistence
    settings.setValue(settingsKey("geometry"), saveGeometry());
    settings.setValue(settingsKey("visible"), isVisible());

    // Camera selection
    settings.setValue(monitorKey("PrimaryCamera"),
                      QString::fromStdString(m_targetCameraId));
    settings.setValue(monitorKey("ViewMode"),
                      m_dualMode ? QStringLiteral("dual") : QStringLiteral("single"));
    settings.setValue(monitorKey("SecondaryCamera"),
                      QString::fromStdString(m_secondaryCameraId));

    // Tools
    const bool crosshair  = m_crosshairCheck   ? m_crosshairCheck->isChecked()   : false;
    const bool clickMove  = m_clickToMoveCheck ? m_clickToMoveCheck->isChecked() : false;
    const bool showDist   = m_showDistanceCheck? m_showDistanceCheck->isChecked(): false;

    settings.setValue(toolsKey("CrosshairEnabled"),    crosshair ? "true" : "false");
    settings.setValue(toolsKey("ClickToMoveEnabled"),  clickMove ? "true" : "false");
    settings.setValue(toolsKey("ShowDistanceEnabled"), showDist  ? "true" : "false");
    settings.setValue(toolsKey("Magnification"),       m_magnification);

    const int rotDeg = m_viewport ? m_viewport->displayRotation() : 0;
    settings.setValue(toolsKey("DisplayRotation"), rotDeg);

    // Chart overlay
    settings.setValue(chartKey("Enabled"), m_chartEnabled);
    settings.setValue(chartKey("Scale"), static_cast<double>(m_chartScale));
    QStringList enabledChannels;
    {
        std::lock_guard<std::mutex> lock(m_chartMutex);
        for (const auto& ch : m_chartEnabledChannels) {
            enabledChannels.append(QString::fromStdString(ch));
        }
    }
    settings.setValue(chartKey("EnabledChannels"), enabledChannels);

#ifdef HAS_OPENCV
    // Vision overlay
    settings.setValue(visionKey("Enabled"), m_visionEnabled.load());
    {
        std::lock_guard<std::mutex> lock(m_visionProfileMutex);
        settings.setValue(visionKey("ActiveProfile"),
                          QString::fromStdString(m_activeVisionProfile));
    }
#endif

    settings.sync();
}

void QtCameraWindow::loadSettings()
{
    QSettings settings("Fabrinet", "BobcatEngine");

    // Geometry — use restoreGeometry() for reliable cross-platform persistence
    const QByteArray geom = settings.value(settingsKey("geometry")).toByteArray();
    if (!geom.isEmpty()) {
        restoreGeometry(geom);
    } else if (settings.contains(settingsKey("x"))) {
        // Backward compatibility: load old manual x/y/w/h format
        int sx = settings.value(settingsKey("x"),      100).toInt();
        int sy = settings.value(settingsKey("y"),      100).toInt();
        int sw = settings.value(settingsKey("width"),  800).toInt();
        int sh = settings.value(settingsKey("height"), 600).toInt();
        if (sx < -100) sx = 100;
        if (sy < -100) sy = 100;
        if (sw < 200)  sw = 800;
        if (sh < 150)  sh = 600;
        setGeometry(sx, sy, sw, sh);
    }

    // Camera selection
    const QString primaryCamera =
        settings.value(monitorKey("PrimaryCamera"), "").toString();
    const QString viewMode =
        settings.value(monitorKey("ViewMode"), "single").toString();
    const QString secondaryCamera =
        settings.value(monitorKey("SecondaryCamera"), "").toString();

    if (!primaryCamera.isEmpty()) {
        setTargetCamera(primaryCamera.toStdString());
    }

    setDualCameraMode(viewMode == "dual");

    if (!secondaryCamera.isEmpty()) {
        setSecondaryCameraId(secondaryCamera.toStdString());
    }

    // Tools
    const bool crosshair = settings.value(toolsKey("CrosshairEnabled"),    "false").toString() == "true";
    const bool clickMove = settings.value(toolsKey("ClickToMoveEnabled"),  "false").toString() == "true";
    const bool showDist  = settings.value(toolsKey("ShowDistanceEnabled"), "false").toString() == "true";

    setCrosshairEnabled(crosshair);
    setClickToMoveEnabled(clickMove);
    setShowDistanceEnabled(showDist);

    const double mag = settings.value(toolsKey("Magnification"), 1.0).toDouble();
    setMagnification(mag > 0.0 ? mag : 1.0);

    // DisplayRotation: support legacy "true"/"false" from old GDI setting
    QVariant rotVal = settings.value(toolsKey("DisplayRotation"), 0);
    int rot = 0;
    if (rotVal.toString() == "true") {
        rot = -90;
    } else if (rotVal.toString() == "false") {
        rot = 0;
    } else {
        rot = rotVal.toInt();
    }
    setDisplayRotation(rot);

    // Chart overlay
    m_chartEnabled = settings.value(chartKey("Enabled"), false).toBool();
    m_chartScale   = settings.value(chartKey("Scale"), 1.0).toFloat();
    {
        std::lock_guard<std::mutex> lock(m_chartMutex);
        const QStringList savedChannels = settings.value(chartKey("EnabledChannels")).toStringList();
        for (const auto& ch : savedChannels) {
            m_chartEnabledChannels.insert(ch.toStdString());
        }
    }
    if (m_chartEnableCheck) m_chartEnableCheck->setChecked(m_chartEnabled);

#ifdef HAS_OPENCV
    // Vision overlay
    const bool visionEnabled = settings.value(visionKey("Enabled"), false).toBool();
    const QString savedProfile = settings.value(visionKey("ActiveProfile"), "").toString();

    if (!savedProfile.isEmpty()) {
        std::lock_guard<std::mutex> lock(m_visionProfileMutex);
        m_activeVisionProfile = savedProfile.toStdString();
    }

    // Restore combo selection
    if (m_visionProfileCombo && !savedProfile.isEmpty()) {
        for (int i = 0; i < m_visionProfileCombo->count(); ++i) {
            if (m_visionProfileCombo->itemData(i).toString() == savedProfile) {
                m_visionProfileCombo->blockSignals(true);
                m_visionProfileCombo->setCurrentIndex(i);
                m_visionProfileCombo->blockSignals(false);
                break;
            }
        }
    }

    if (m_visionEnableCheck) {
        m_visionEnableCheck->blockSignals(true);
        m_visionEnableCheck->setChecked(visionEnabled);
        m_visionEnableCheck->blockSignals(false);
    }
    if (visionEnabled) {
        m_visionEnabled.store(true);
        startVisionProcessing();
    }
#endif
}

// ============================================================================
// applyApplicationSettings
// ============================================================================

void QtCameraWindow::applyApplicationSettings()
{
    loadSettings();
}

// ============================================================================
// Qt event overrides
// ============================================================================

void QtCameraWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    hide();
    emit visibilityChanged(false);
    event->accept();
}

void QtCameraWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    emit visibilityChanged(true);
}

void QtCameraWindow::hideEvent(QHideEvent* event)
{
    saveSettings();
    QWidget::hideEvent(event);
    emit visibilityChanged(false);
}

// ============================================================================
// Private slots — combo/checkbox handlers
// ============================================================================

void QtCameraWindow::onPrimaryCameraChanged(const QString& cameraId)
{
    setTargetCamera(cameraId.toStdString());
    refreshExposureDisplay();
}

void QtCameraWindow::onSecondaryCameraChanged(const QString& cameraId)
{
    setSecondaryCameraId(cameraId.toStdString());
}

void QtCameraWindow::onDualModeToggled(bool checked)
{
    setDualCameraMode(checked);
}

void QtCameraWindow::onCrosshairToggled(bool checked)
{
    if (m_viewport) m_viewport->setCrosshairEnabled(checked);
}

void QtCameraWindow::onClickToMoveToggled(bool checked)
{
    m_clickToMoveEnabled = checked;
    if (m_viewport) m_viewport->setClickToMoveEnabled(checked);
}

void QtCameraWindow::onShowDistanceToggled(bool checked)
{
    if (m_viewport) m_viewport->setShowDistanceEnabled(checked);
}

void QtCameraWindow::onMagnificationChanged(int index)
{
    const double mag = magnificationFromIndex(index);
    m_magnification = mag;
    if (m_viewport) m_viewport->setMagnification(mag);
}

void QtCameraWindow::onRotationChanged(int index)
{
    const int deg = rotationFromIndex(index);
    if (m_viewport) m_viewport->setDisplayRotation(deg);
}

void QtCameraWindow::onImageClicked(int imageX, int imageY)
{
    if (m_clickToMoveEnabled) {
        handleClickToMove(imageX, imageY);
    }
}

// ============================================================================
// Chart overlay — data feed
// ============================================================================

void QtCameraWindow::onDataStoreValueChanged(const std::string& channelId, float value)
{
    bool isNewChannel = false;
    {
        std::lock_guard<std::mutex> lock(m_chartMutex);

        // Detect new channel (first time we see this channelId)
        isNewChannel = (m_chartChannels.find(channelId) == m_chartChannels.end());

        auto& ch = m_chartChannels[channelId];
        ch.values.push_back(static_cast<double>(value));
        if (ch.values.size() > CHART_MAX_POINTS)
            ch.values.pop_front();
        if (value < ch.runningMin) ch.runningMin = value;
        if (value > ch.runningMax) ch.runningMax = value;
    }

    // Auto-add new channels to the list widget (must run on Qt thread)
    if (isNewChannel) {
        QMetaObject::invokeMethod(this, [this]() {
            refreshChartChannelList();
        }, Qt::QueuedConnection);
    }

    // Throttle repaints to ~20fps — push fresh chart data to viewport
    if (m_chartEnabled && m_chartEnabledChannels.count(channelId)) {
        const auto now     = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now - m_lastChartRepaint).count();
        if (elapsed >= 50) {
            m_lastChartRepaint = now;
            QMetaObject::invokeMethod(this, [this]() {
                if (!m_viewport) return;
                std::lock_guard<std::mutex> lock(m_chartMutex);
                std::vector<QtCameraViewport::ChartChannelSnapshot> snapshots;
                for (const auto& chId : m_chartEnabledChannels) {
                    auto it = m_chartChannels.find(chId);
                    if (it != m_chartChannels.end() && !it->second.values.empty()) {
                        snapshots.push_back({chId, it->second.values});
                    }
                }
                m_viewport->setChartOverlayData(true, m_chartScale, snapshots);
                m_viewport->update();
            }, Qt::QueuedConnection);
        }
    }
}

// ============================================================================
// Chart overlay — channel list
// ============================================================================

void QtCameraWindow::refreshChartChannelList()
{
    if (!m_chartChannelList || !m_dataStore) return;

    m_chartChannelList->clear();
    const auto channels = m_dataStore->GetAvailableChannels();

    for (const auto& ch : channels) {
        auto* item = new QListWidgetItem(QString::fromStdString(ch));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        {
            std::lock_guard<std::mutex> lock(m_chartMutex);
            item->setCheckState(m_chartEnabledChannels.count(ch) ? Qt::Checked : Qt::Unchecked);
        }
        m_chartChannelList->addItem(item);
    }
}

// ============================================================================
// Chart overlay — settings key helper
// ============================================================================

QString QtCameraWindow::chartKey(const QString& key) const
{
    return QString("ChartOverlay%1/%2")
        .arg(QString::fromStdString(m_instanceId), key);
}

// ============================================================================
// Chart overlay — public API
// ============================================================================

void QtCameraWindow::setChartEnabled(bool enabled)
{
    m_chartEnabled = enabled;
    if (m_chartEnableCheck) m_chartEnableCheck->setChecked(enabled);
    if (m_viewport) m_viewport->update();
}

// ============================================================================
// Private slots — chart handlers
// ============================================================================

void QtCameraWindow::onChartEnableToggled(bool checked)
{
    m_chartEnabled = checked;
    if (m_viewport) m_viewport->update();
    std::cout << "[QtCameraWindow] Chart overlay " << (checked ? "enabled" : "disabled") << std::endl;
}

void QtCameraWindow::onChartScaleIncrease()
{
    m_chartScale = std::min(m_chartScale + 0.25f, 3.0f);
    if (m_viewport) m_viewport->update();
}

void QtCameraWindow::onChartScaleDecrease()
{
    m_chartScale = std::max(m_chartScale - 0.25f, 0.5f);
    if (m_viewport) m_viewport->update();
}

void QtCameraWindow::onChartRefreshChannels()
{
    refreshChartChannelList();
}

// ============================================================================
// Vision processing — thread methods
// ============================================================================

#ifdef HAS_OPENCV

void QtCameraWindow::startVisionProcessing()
{
    if (m_visionThreadRunning.load()) return;
    m_visionThreadRunning.store(true);
    m_visionThread = std::thread(&QtCameraWindow::visionThreadLoop, this);
    std::cout << "[QtCameraWindow] Vision processing thread started" << std::endl;
}

void QtCameraWindow::stopVisionProcessing()
{
    if (!m_visionThreadRunning.load()) return;
    m_visionThreadRunning.store(false);
    m_visionFrameQueueCV.notify_all();
    if (m_visionThread.joinable()) {
        m_visionThread.join();
    }
    // Clear frame queue
    {
        std::lock_guard<std::mutex> lock(m_visionFrameQueueMutex);
        while (!m_visionFrameQueue.empty()) m_visionFrameQueue.pop();
    }
    std::cout << "[QtCameraWindow] Vision processing thread stopped" << std::endl;
}

void QtCameraWindow::visionThreadLoop()
{
    for (int iter = 0; iter < 10000000 && m_visionThreadRunning.load(); ++iter) {
        std::vector<uint8_t> frameData;
        uint32_t width  = 0;
        uint32_t height = 0;

        // Wait for a frame with 100ms timeout
        {
            std::unique_lock<std::mutex> lock(m_visionFrameQueueMutex);
            m_visionFrameQueueCV.wait_for(lock, std::chrono::milliseconds(100),
                [this] { return !m_visionFrameQueue.empty() || !m_visionThreadRunning.load(); });

            if (!m_visionThreadRunning.load()) break;
            if (m_visionFrameQueue.empty()) continue;

            frameData = std::move(m_visionFrameQueue.front());
            m_visionFrameQueue.pop();
            width  = m_visionQueuedWidth;
            height = m_visionQueuedHeight;
        }

        if (width == 0 || height == 0 || frameData.empty()) continue;

        // Get active profile path
        std::string profilePath;
        {
            std::lock_guard<std::mutex> lock(m_visionProfileMutex);
            profilePath = m_activeVisionProfile;
        }
        if (profilePath.empty()) continue;

        // Convert RGB to BGR cv::Mat
        cv::Mat rgbImage(static_cast<int>(height), static_cast<int>(width),
                         CV_8UC3, frameData.data());
        cv::Mat bgrImage;
        cv::cvtColor(rgbImage, bgrImage, cv::COLOR_RGB2BGR);

        // Load profile JSON (no exceptions from application code; wrap third-party parse)
        std::ifstream profileFile(profilePath);
        if (!profileFile.is_open()) {
            std::cerr << "[QtCameraWindow] Vision: cannot open profile: " << profilePath << std::endl;
            continue;
        }

        nlohmann::json profileJson;
        try {
            profileFile >> profileJson;
        } catch (const std::exception& ex) {
            std::cerr << "[QtCameraWindow] Vision: failed to parse profile JSON: "
                      << ex.what() << std::endl;
            continue;
        }

        if (!profileJson.contains("plugin") || !profileJson["plugin"].contains("name")) {
            std::cerr << "[QtCameraWindow] Vision: profile missing plugin.name field" << std::endl;
            continue;
        }

        const std::string pluginName = profileJson["plugin"]["name"].get<std::string>();
        const nlohmann::json pluginParams = profileJson["plugin"].value("parameters",
                                                                         nlohmann::json::object());

        // Handle ROI if present and enabled
        if (profileJson.contains("roi") && profileJson["roi"].value("enabled", false)) {
            VisionROI roi = VisionROI::fromJson(profileJson["roi"]);
            if (roi.isValid()) {
                cv::Mat cropped = roi.cropImage(bgrImage);
                if (!cropped.empty()) {
                    VisionManager::instance().setInputImage(cropped);
                    VisionManager::instance().setROI(roi);
                    VisionManager::instance().setROIEnabled(true);
                } else {
                    VisionManager::instance().setInputImage(bgrImage);
                    VisionManager::instance().setROIEnabled(false);
                }
            } else {
                VisionManager::instance().setInputImage(bgrImage);
                VisionManager::instance().setROIEnabled(false);
            }
        } else {
            VisionManager::instance().setInputImage(bgrImage);
            VisionManager::instance().setROIEnabled(false);
        }

        VisionResult result = VisionManager::instance().executePlugin(
            QString::fromStdString(pluginName), pluginParams);

        // Extract display name from profile path (filename without extension)
        std::string displayName;
        {
            std::filesystem::path p(profilePath);
            displayName = p.stem().string();
        }

        // Store result
        {
            std::lock_guard<std::mutex> lock(m_visionResultMutex);
            m_lastVisionResult = result;
            m_visionProfileName = displayName;
        }
        m_visionResultSequence.fetch_add(1, std::memory_order_relaxed);

        // Trigger viewport repaint on Qt main thread
        if (m_viewport) {
            QMetaObject::invokeMethod(m_viewport, "update", Qt::QueuedConnection);
        }
    }
}

void QtCameraWindow::discoverVisionProfiles()
{
    m_availableProfiles.clear();

    // Helper lambda: scan a directory for .json profiles (skip .recipe.json)
    auto scanDir = [this](const std::string& dir) {
        std::error_code ec;
        std::filesystem::directory_iterator it(dir, ec);
        if (ec) return;

        for (const auto& entry : it) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;

            // Skip recipe files
            const std::string fname = entry.path().filename().string();
            if (fname.find(".recipe.json") != std::string::npos) continue;

            VisionProfileInfo info;
            info.fullPath    = entry.path().string();
            info.displayName = entry.path().stem().string();

            // Avoid duplicates
            bool exists = false;
            for (const auto& existing : m_availableProfiles) {
                if (existing.displayName == info.displayName) { exists = true; break; }
            }
            if (!exists) {
                m_availableProfiles.push_back(info);
            }
        }
    };

    // 1. Scan standard config/vision/ paths
    const std::vector<std::string> searchDirs = {
        "config/vision",
        "./config/vision",
        "../config/vision"
    };

    for (const auto& dir : searchDirs) {
        scanDir(dir);
        if (!m_availableProfiles.empty()) break;
    }

    // 2. Scan machine-specific paths: config/<MACHINE>/vision/
    const std::vector<std::string> configRoots = {"config", "./config", "../config"};
    for (const auto& root : configRoots) {
        std::error_code ec;
        std::filesystem::directory_iterator rootIt(root, ec);
        if (ec) continue;

        for (const auto& entry : rootIt) {
            if (!entry.is_directory()) continue;
            const std::string dirName = entry.path().filename().string();
            if (dirName == "." || dirName == ".." || dirName == "system" || dirName == "SYSTEM") continue;

            // Check if this subdirectory has a vision/ folder
            const std::string visionDir = entry.path().string() + "/vision";
            scanDir(visionDir);
        }

        if (!m_availableProfiles.empty()) break;
    }

    std::sort(m_availableProfiles.begin(), m_availableProfiles.end(),
        [](const VisionProfileInfo& a, const VisionProfileInfo& b) {
            return a.displayName < b.displayName;
        });

    std::cout << "[QtCameraWindow] Vision profiles discovered: "
              << m_availableProfiles.size() << std::endl;
}

void QtCameraWindow::populateVisionProfileCombo()
{
    if (!m_visionProfileCombo) return;

    m_visionProfileCombo->blockSignals(true);
    m_visionProfileCombo->clear();
    for (const auto& profile : m_availableProfiles) {
        m_visionProfileCombo->addItem(
            QString::fromStdString(profile.displayName),
            QString::fromStdString(profile.fullPath));
    }
    m_visionProfileCombo->blockSignals(false);
}

// ============================================================================
// Vision processing — settings key helper
// ============================================================================

QString QtCameraWindow::visionKey(const QString& key) const
{
    return QString("Vision%1/%2")
        .arg(QString::fromStdString(m_instanceId), key);
}

// ============================================================================
// Vision processing — public API
// ============================================================================

bool QtCameraWindow::isVisionProcessingActive() const
{
    return m_visionEnabled.load() && m_visionThreadRunning.load();
}

VisionResult QtCameraWindow::getLatestVisionResult() const
{
    std::lock_guard<std::mutex> lock(m_visionResultMutex);
    return m_lastVisionResult;
}

uint64_t QtCameraWindow::getVisionResultSequence() const
{
    return m_visionResultSequence.load();
}

void QtCameraWindow::setVisionEnabled(bool enabled)
{
    if (enabled) {
        m_visionEnabled.store(true);
        startVisionProcessing();
        if (m_visionEnableCheck) {
            m_visionEnableCheck->blockSignals(true);
            m_visionEnableCheck->setChecked(true);
            m_visionEnableCheck->blockSignals(false);
        }
    } else {
        stopVisionProcessing();
        m_visionEnabled.store(false);
        if (m_visionEnableCheck) {
            m_visionEnableCheck->blockSignals(true);
            m_visionEnableCheck->setChecked(false);
            m_visionEnableCheck->blockSignals(false);
        }
    }
}

void QtCameraWindow::selectVisionProfile(const std::string& profilePath)
{
    std::lock_guard<std::mutex> lock(m_visionProfileMutex);
    m_activeVisionProfile = profilePath;
}

// ============================================================================
// Vision processing — private slots
// ============================================================================

void QtCameraWindow::onVisionEnableToggled(bool checked)
{
    if (checked) {
        // Auto-select first profile if none selected
        if (m_visionProfileCombo && m_visionProfileCombo->count() > 0 &&
            m_visionProfileCombo->currentIndex() < 0) {
            m_visionProfileCombo->setCurrentIndex(0);
        }
        m_visionEnabled.store(true);
        startVisionProcessing();
    } else {
        stopVisionProcessing();
        m_visionEnabled.store(false);
    }
    std::cout << "[QtCameraWindow] Vision processing "
              << (checked ? "enabled" : "disabled") << std::endl;
}

void QtCameraWindow::onVisionProfileChanged(int index)
{
    if (!m_visionProfileCombo || index < 0) return;
    const std::string fullPath =
        m_visionProfileCombo->itemData(index).toString().toStdString();
    {
        std::lock_guard<std::mutex> lock(m_visionProfileMutex);
        m_activeVisionProfile = fullPath;
    }
    std::cout << "[QtCameraWindow] Vision profile selected: " << fullPath << std::endl;
}

void QtCameraWindow::onVisionRefreshProfiles()
{
    discoverVisionProfiles();
    populateVisionProfileCombo();
}

#endif // HAS_OPENCV

// ============================================================================
// Beam streaming — public API
// ============================================================================

void QtCameraWindow::setBeamServerClient(BeamServerClient* client)
{
    m_beamServerClient = client;
}

bool QtCameraWindow::isBeamStreamActive() const
{
    return m_beamStreamEnabled.load() && m_beamStreamThreadRunning.load();
}

nlohmann::json QtCameraWindow::getLatestBeamStreamResult() const
{
    std::lock_guard<std::mutex> lock(m_beamResultMutex);
    return m_lastBeamResult;
}

uint64_t QtCameraWindow::getBeamStreamResultSequence() const
{
    return m_beamResultSequence.load();
}

double QtCameraWindow::getBeamStreamFps() const
{
    return m_beamStreamFps.load();
}

// ============================================================================
// Beam streaming — config load
// ============================================================================

void QtCameraWindow::loadBeamStreamConfig()
{
    QSettings settings("Fabrinet", "BobcatEngine");
    m_beamStreamConfig.host       = settings.value("BeamServer/Host",       "127.0.0.1").toString().toStdString();
    m_beamStreamConfig.port       = settings.value("BeamServer/StreamPort", 5567).toInt();
    m_beamStreamConfig.umPerPxX   = settings.value("BeamServer/UmPerPxX",   0.25).toDouble();
    m_beamStreamConfig.umPerPxY   = settings.value("BeamServer/UmPerPxY",   0.25).toDouble();
    m_beamStreamConfig.rotate180  = settings.value("BeamServer/Rotate180",  false).toBool();
    m_beamStreamConfig.intervalMs = settings.value("BeamServer/IntervalMs", 100).toInt();

    // Fallback: config/beam_stream.json
    const std::vector<std::string> paths = {
        "config/beam_stream.json",
        "./config/beam_stream.json",
        "../config/beam_stream.json"
    };
    for (const auto& path : paths) {
        std::ifstream f(path);
        if (!f.is_open()) continue;
        nlohmann::json j;
        try {
            f >> j;
        } catch (...) {
            continue;
        }
        m_beamStreamConfig.host       = j.value("host",        m_beamStreamConfig.host);
        m_beamStreamConfig.port       = j.value("port",        m_beamStreamConfig.port);
        m_beamStreamConfig.umPerPxX   = j.value("um_per_px_x", m_beamStreamConfig.umPerPxX);
        m_beamStreamConfig.umPerPxY   = j.value("um_per_px_y", m_beamStreamConfig.umPerPxY);
        m_beamStreamConfig.rotate180  = j.value("rotate180",   m_beamStreamConfig.rotate180);
        m_beamStreamConfig.intervalMs = j.value("interval_ms", m_beamStreamConfig.intervalMs);
        break;
    }
}

// ============================================================================
// Beam streaming — thread methods
// ============================================================================

void QtCameraWindow::startBeamStreaming()
{
    if (m_beamStreamThreadRunning.load()) return;

    loadBeamStreamConfig();

    m_beamStreamClient = std::make_unique<BeamServerClient>();
    if (!m_beamStreamClient->Connect(m_beamStreamConfig.host, m_beamStreamConfig.port)) {
        std::cerr << "[QtCameraWindow] Beam stream: failed to connect to "
                  << m_beamStreamConfig.host << ":" << m_beamStreamConfig.port << std::endl;
        m_beamStreamClient.reset();
        return;
    }

    // Get current frame dimensions from primary buffer
    uint32_t frameW = 0;
    uint32_t frameH = 0;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        frameW = m_frameWidth;
        frameH = m_frameHeight;
    }

    // Send STREAM_START
    const nlohmann::json startResp = m_beamStreamClient->SendStreamStart(
        static_cast<int>(frameW), static_cast<int>(frameH),
        "mono8",
        m_beamStreamConfig.rotate180,
        m_beamStreamConfig.umPerPxX,
        m_beamStreamConfig.umPerPxY,
        m_beamStreamConfig.intervalMs);

    if (startResp.empty()) {
        std::cerr << "[QtCameraWindow] Beam stream: STREAM_START failed" << std::endl;
        m_beamStreamClient->Disconnect();
        m_beamStreamClient.reset();
        return;
    }

    m_beamStreamEnabled.store(true);
    m_beamStreamThreadRunning.store(true);
    m_beamStreamThread = std::thread(&QtCameraWindow::beamStreamThreadLoop, this);
    std::cout << "[QtCameraWindow] Beam streaming started ("
              << m_beamStreamConfig.host << ":" << m_beamStreamConfig.port << ")" << std::endl;
}

void QtCameraWindow::stopBeamStreaming()
{
    if (!m_beamStreamThreadRunning.load()) return;
    m_beamStreamEnabled.store(false);
    m_beamStreamThreadRunning.store(false);
    m_beamStreamQueueCV.notify_all();
    if (m_beamStreamThread.joinable()) {
        m_beamStreamThread.join();
    }

    // Send STREAM_STOP if still connected
    if (m_beamStreamClient && m_beamStreamClient->IsConnected()) {
        m_beamStreamClient->SendStreamStop();
        m_beamStreamClient->Disconnect();
    }
    m_beamStreamClient.reset();

    // Clear queue
    {
        std::lock_guard<std::mutex> lock(m_beamStreamQueueMutex);
        while (!m_beamStreamQueue.empty()) m_beamStreamQueue.pop();
    }

    std::cout << "[QtCameraWindow] Beam streaming stopped" << std::endl;
}

void QtCameraWindow::beamStreamThreadLoop()
{
    uint32_t seqNum = 0;
    auto fpsStart   = std::chrono::steady_clock::now();
    uint32_t fpsCount = 0;

    for (int iter = 0; iter < 10000000 && m_beamStreamThreadRunning.load(); ++iter) {
        std::vector<uint8_t> frameData;
        uint32_t width  = 0;
        uint32_t height = 0;

        {
            std::unique_lock<std::mutex> lock(m_beamStreamQueueMutex);
            m_beamStreamQueueCV.wait_for(lock,
                std::chrono::milliseconds(m_beamStreamConfig.intervalMs),
                [this] {
                    return !m_beamStreamQueue.empty() || !m_beamStreamThreadRunning.load();
                });

            if (!m_beamStreamThreadRunning.load()) break;
            if (m_beamStreamQueue.empty()) continue;

            frameData = std::move(m_beamStreamQueue.front());
            m_beamStreamQueue.pop();
            width  = m_beamStreamQueuedWidth;
            height = m_beamStreamQueuedHeight;
        }

        if (width == 0 || height == 0 || frameData.empty()) continue;
        if (!m_beamStreamClient || !m_beamStreamClient->IsConnected()) break;

        // Convert RGB to mono8 grayscale: simple average of R,G,B channels
        const size_t pixelCount = static_cast<size_t>(width) * height;
        std::vector<uint8_t> grayData(pixelCount);
        for (size_t i = 0; i < pixelCount; ++i) {
            const uint32_t r = frameData[i * 3 + 0];
            const uint32_t g = frameData[i * 3 + 1];
            const uint32_t b = frameData[i * 3 + 2];
            grayData[i] = static_cast<uint8_t>((r + g + b) / 3u);
        }

        const nlohmann::json resp = m_beamStreamClient->SendStreamFrame(grayData, seqNum++);

        if (!resp.empty()) {
            {
                std::lock_guard<std::mutex> lock(m_beamResultMutex);
                m_lastBeamResult = resp;
            }
            m_beamResultSequence.fetch_add(1, std::memory_order_relaxed);

            // Trigger viewport repaint
            if (m_viewport) {
                QMetaObject::invokeMethod(m_viewport, "update", Qt::QueuedConnection);
            }
        }

        // Update FPS counter
        ++fpsCount;
        const auto now     = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now - fpsStart).count();
        if (elapsed >= 1000) {
            m_beamStreamFps.store(
                static_cast<double>(fpsCount) * 1000.0 / static_cast<double>(elapsed));
            fpsCount  = 0;
            fpsStart  = now;
        }
    }
}

// ============================================================================
// Beam streaming — private slots
// ============================================================================

void QtCameraWindow::onBeamEnableToggled(bool checked)
{
    if (checked) {
        // Auto-select first camera if none selected
        if (m_beamCameraCombo && m_beamStreamCameraId.empty() &&
            m_beamCameraCombo->count() > 1) {
            m_beamCameraCombo->setCurrentIndex(1); // skip empty first entry
        }
        m_beamStreamEnabled.store(true);
        startBeamStreaming();
    } else {
        stopBeamStreaming();
        m_beamStreamEnabled.store(false);
    }
    std::cout << "[QtCameraWindow] Beam streaming "
              << (checked ? "enabled" : "disabled") << std::endl;
}

void QtCameraWindow::onBeamCameraChanged(const QString& cameraId)
{
    m_beamStreamCameraId = cameraId.toStdString();
}
