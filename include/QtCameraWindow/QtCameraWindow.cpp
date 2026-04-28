// QtCameraWindow.cpp
#include "QtCameraWindow.h"
#include "QtCameraViewport.h"
#include "QtCameraQuickMovePanel.h"
#include "QtCameraTrackingPanel.h"
#include "QtCameraChartOverlay.h"
#ifdef HAS_OPENCV
#include "QtCameraVisionController.h"
#endif
#include "QtCameraBeamStreamController.h"

#include "camera/CameraManager.h"
#include "camera/CameraConfigManager.h"
#include "motions/unified_motion_manager.h"
#include "motions/MotionTypes.h"
#include "context/ApplicationContext.h"
#include "VisionCommandHandler.h"
#include "SequenceContext.h"
#include "LogManager.h"
#include <QTimer>
#include <QInputDialog>
#include <cmath>
#include "data/DataStore.h"
#include "customserver/BeamServerClient.h"


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

    // Viewport is created first so the helper panels constructed inside
    // buildSidePanel()/buildQuickMovePanel() (chart overlay, tracking
    // panel) have a valid pointer at construction time. It's added to
    // the main layout LAST so the side panel and quick-move column sit
    // to its left in display order.
    m_viewport = new QtCameraViewport(this);

    buildSidePanel();
    mainLayout->addWidget(m_sidePanelScroll);

    buildQuickMovePanel();
    mainLayout->addWidget(m_quickMovePanel);

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

    if (m_dataStore && m_chartOverlay) {
        m_chartOverlay->refreshChannelList();
    }

    // Populate beam camera combo (cameras available at init time)
    if (m_cameraManager && m_beamController) {
        m_beamController->refreshCameraList(m_cameraManager->GetCameraIds());
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
    if (m_visionController) m_visionController->shutdown();
#endif
    if (m_beamController) m_beamController->shutdown();

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

    m_chartOverlay = new QtCameraChartOverlay(m_viewport, m_instanceId);

#ifdef HAS_OPENCV
    m_visionController = new QtCameraVisionController(m_viewport, m_instanceId);
    QWidget* visionTabWidget = m_visionController;
#else
    // OpenCV not compiled in — show a placeholder group instead.
    auto* visionStub = new QGroupBox("Vision");
    visionStub->setStyleSheet(
        "QGroupBox { color: #888888; border: 1px solid #3e3e42; border-radius: 4px; "
        "            margin-top: 6px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
    );
    auto* visionStubLayout = new QVBoxLayout(visionStub);
    visionStubLayout->setContentsMargins(6, 8, 6, 6);
    auto* visionStubLabel = new QLabel("OpenCV not available");
    visionStubLabel->setStyleSheet("color: #666666; font-style: italic;");
    visionStubLabel->setAlignment(Qt::AlignCenter);
    visionStubLayout->addWidget(visionStubLabel);
    QWidget* visionTabWidget = visionStub;
#endif

    m_beamController = new QtCameraBeamStreamController(m_viewport);

    auto* sideTabs = new QTabWidget();
    sideTabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #3e3e42; border-radius: 4px; "
        "                   background-color: #252526; top: -1px; }"
        "QTabBar::tab { background-color: #333337; color: #cccccc; "
        "               padding: 4px 8px; border: 1px solid #3e3e42; "
        "               border-bottom: none; border-top-left-radius: 3px; "
        "               border-top-right-radius: 3px; }"
        "QTabBar::tab:selected { background-color: #252526; color: #ffffff; }"
        "QTabBar::tab:hover { background-color: #3e3e42; }"
    );
    sideTabs->addTab(createToolsPage(), "Tools");
    sideTabs->addTab(createViewPage(),  "View");
    sideTabs->addTab(m_chartOverlay,    "Chart");
    sideTabs->addTab(visionTabWidget,   "Vision");
    sideTabs->addTab(m_beamController,  "Beam");
    vLayout->addWidget(sideTabs);

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

    m_quickMoveWidget = new QtCameraQuickMovePanel(m_motionManager, m_instanceId);
    outerLayout->addWidget(m_quickMoveWidget);

    m_trackingPanel = new QtCameraTrackingPanel(m_viewport, m_instanceId);
    outerLayout->addWidget(m_trackingPanel);

    outerLayout->addStretch();
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

    // Auto-focus buttons (per installed camera)
    struct AutoFocusConfig {
        std::string cameraId;
        std::string motionDevice;
        double zRangeMm;
        double zStepMm;
        std::string label;
    };
    static const std::vector<AutoFocusConfig> kAfConfigs = {
        {"main_camera",   "gantry-main", 0.1, 0.02, "AF Main"},
        {"swir_camera",   "gantry-main", 0.1, 0.01, "AF SWIR"},
        {"gantry_camera", "gantry-main", 0.1, 0.02, "AF Gantry"}
    };

    std::vector<AutoFocusConfig> activeAf;
    if (m_cameraManager) {
        std::vector<std::string> cameraIds = m_cameraManager->GetCameraIds();
        for (const auto& cfg : kAfConfigs) {
            for (const auto& id : cameraIds) {
                if (id == cfg.cameraId) {
                    activeAf.push_back(cfg);
                    break;
                }
            }
        }
    }

    if (!activeAf.empty()) {
        auto* afLayout = new QHBoxLayout();
        afLayout->setSpacing(4);
        afLayout->setContentsMargins(0, 4, 0, 0);

        auto* afLabel = new QLabel("Auto Focus:");
        afLabel->setStyleSheet("QLabel { font-weight: bold; color: #14ffec; font-size: 10px; }");
        afLayout->addWidget(afLabel);

        static const QString afBtnStyle =
            "QPushButton { background-color: #2d5a2d; color: white; "
            "              border: 1px solid #3d8b3d; border-radius: 3px; "
            "              padding: 2px 8px; font-size: 10px; font-weight: bold; }"
            "QPushButton:hover { background-color: #3d8b3d; border-color: #5cb85c; }"
            "QPushButton:disabled { background-color: #555; color: #888; border-color: #666; }";

        for (const auto& cfg : activeAf) {
            auto* btn = new QPushButton(QString::fromStdString(cfg.label));
            btn->setMinimumHeight(26);
            btn->setMaximumHeight(26);
            btn->setStyleSheet(afBtnStyle);

            connect(btn, &QPushButton::clicked, this,
                [this, camera = cfg.cameraId, device = cfg.motionDevice,
                 range = cfg.zRangeMm, step = cfg.zStepMm, btn]() {
                    runAutoFocus(camera, device, range, step, btn);
                });

            afLayout->addWidget(btn);
        }
        afLayout->addStretch();
        layout->addLayout(afLayout);
    }

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

QWidget* QtCameraWindow::createToolsPage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(4);
    layout->setContentsMargins(6, 8, 6, 6);

    m_crosshairCheck    = new QCheckBox("Crosshair");
    m_clickToMoveCheck  = new QCheckBox("Click-to-Move");
    m_showDistanceCheck = new QCheckBox("Show Distance");

    layout->addWidget(m_crosshairCheck);
    layout->addWidget(m_clickToMoveCheck);
    layout->addWidget(m_showDistanceCheck);
    layout->addStretch();

    connect(m_crosshairCheck, &QCheckBox::toggled,
            this, &QtCameraWindow::onCrosshairToggled);
    connect(m_clickToMoveCheck, &QCheckBox::toggled,
            this, &QtCameraWindow::onClickToMoveToggled);
    connect(m_showDistanceCheck, &QCheckBox::toggled,
            this, &QtCameraWindow::onShowDistanceToggled);

    return page;
}

QWidget* QtCameraWindow::createViewPage()
{
    static const char* kComboStyle =
        "QComboBox { background-color: #3e3e42; border: 1px solid #555; "
        "            color: #cccccc; padding: 2px 4px; }"
        "QComboBox QAbstractItemView { background-color: #3e3e42; color: #cccccc; }";

    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(4);
    layout->setContentsMargins(6, 8, 6, 6);

    layout->addWidget(new QLabel("Magnification:"));
    m_magnificationCombo = new QComboBox();
    m_magnificationCombo->setStyleSheet(kComboStyle);
    m_magnificationCombo->addItems({"1x", "10x", "20x", "50x", "100x", "200x"});
    layout->addWidget(m_magnificationCombo);

    layout->addWidget(new QLabel("Display Rotation:"));
    m_rotationCombo = new QComboBox();
    m_rotationCombo->setStyleSheet(kComboStyle);
    m_rotationCombo->addItems({"None", "-90 deg", "+90 deg"});
    layout->addWidget(m_rotationCombo);
    layout->addStretch();

    connect(m_magnificationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QtCameraWindow::onMagnificationChanged);
    connect(m_rotationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QtCameraWindow::onRotationChanged);

    return page;
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
    if (isPrimary && m_visionController) {
        std::vector<uint8_t> rgb(frameData.imageData.begin(), frameData.imageData.end());
        m_visionController->enqueueFrame(rgb, frameData.width, frameData.height);
    }
#endif

    // Feed beam stream queue (camera-id filtered inside the controller)
    if (m_beamController) {
        if (isPrimary) {
            m_beamController->setCurrentPrimaryFrameSize(frameData.width, frameData.height);
        }
        std::vector<uint8_t> rgb(frameData.imageData.begin(), frameData.imageData.end());
        m_beamController->enqueueFrame(rgb, frameData.width, frameData.height,
                                       frameData.cameraId);
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
    if (m_chartOverlay) {
        m_chartOverlay->pushSnapshotsToViewport();
    }

#ifdef HAS_OPENCV
    // Push vision overlay data
    if (m_visionController) {
        m_visionController->pushResultToViewport();
    }
#endif

    // Push beam overlay data
    if (m_beamController) {
        m_beamController->pushOverlayToViewport();
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

    // Keep Click-to-Move locked whenever a rotation is active.
    const bool rotated = (degrees != 0);
    if (rotated && m_clickToMoveEnabled) {
        setClickToMoveEnabled(false);
    }
    if (m_clickToMoveCheck) {
        m_clickToMoveCheck->setEnabled(!rotated);
        m_clickToMoveCheck->setToolTip(rotated
            ? QStringLiteral("Click-to-Move is disabled while display rotation is active.")
            : QString());
    }
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
    if (m_beamController) {
        m_beamController->refreshCameraList(ids);
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
    if (m_chartOverlay) {
        m_chartOverlay->setDataStore(store);
    }
}

void QtCameraWindow::setMotionManager(UnifiedMotionManager* mgr)
{
    m_motionManager = mgr;
    if (m_quickMoveWidget) {
        m_quickMoveWidget->setMotionManager(mgr);
    }
    if (m_trackingPanel) {
        m_trackingPanel->setMotionManager(mgr);
    }
}

void QtCameraWindow::setCameraConfigManager(CameraConfigManager* mgr)
{
    m_cameraConfigManager = mgr;
    // Reload calibration now that per-camera config is available
    loadCalibrationFactors();
}

void QtCameraWindow::setApplicationContext(ApplicationContext* ctx)
{
    m_appContext = ctx;
}

void QtCameraWindow::runAutoFocus(const std::string& cameraId, const std::string& motionDevice,
                                   double zRangeMm, double zStepMm, QPushButton* button)
{
    if (m_autoFocusFuture.valid() &&
        m_autoFocusFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        if (m_appContext && m_appContext->logManager) {
            m_appContext->logManager->Warning("Auto focus already running", "QtCameraWindow");
        }
        return;
    }

    if (!m_appContext || !m_appContext->motionManager || !m_appContext->cameraManager) {
        if (m_appContext && m_appContext->logManager) {
            m_appContext->logManager->Warning(
                "Auto focus unavailable: motion or camera manager missing", "QtCameraWindow");
        }
        return;
    }

    if (button) button->setEnabled(false);

    if (m_appContext->logManager) {
        m_appContext->logManager->Info(
            "Starting auto focus: camera=" + cameraId + " device=" + motionDevice +
            " range=" + std::to_string(zRangeMm) + "mm step=" + std::to_string(zStepMm) + "mm",
            "QtCameraWindow");
    }

    ApplicationContext* ctx = m_appContext;
    m_autoFocusFuture = std::async(std::launch::async,
        [ctx, cameraId, motionDevice, zRangeMm, zStepMm, button]() {
            VisionCommandHandler visionHandler(ctx);
            SequenceContext seqCtx;
            std::string error;

            std::map<std::string, std::string> params = {
                {"motion_device", motionDevice},
                {"camera", cameraId},
                {"z_range_mm", std::to_string(zRangeMm)},
                {"z_step_mm", std::to_string(zStepMm)},
                {"settle_time_ms", "100"},
                {"use_center_roi", "true"},
                {"roi_width", "300"},
                {"roi_height", "300"},
                {"context_prefix", "autofocus"}
            };

            bool ok = visionHandler.ExecuteCommand(
                nullptr, "vision", "auto_focus", params, error, &seqCtx);

            if (ctx && ctx->logManager) {
                if (ok) {
                    ctx->logManager->Info("Auto focus completed successfully", "QtCameraWindow");
                } else {
                    ctx->logManager->Warning("Auto focus failed: " + error, "QtCameraWindow");
                }
            }

            if (button) {
                QMetaObject::invokeMethod(button, [button]() {
                    button->setEnabled(true);
                }, Qt::QueuedConnection);
            }
        });
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

    // Chart overlay — extracted to QtCameraChartOverlay (writes its own
    // settings under the same keys for backward compatibility).
    if (m_chartOverlay) {
        m_chartOverlay->saveSettings();
    }

#ifdef HAS_OPENCV
    // Vision overlay — extracted to QtCameraVisionController.
    if (m_visionController) {
        m_visionController->saveSettings();
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

    // Chart overlay — extracted to QtCameraChartOverlay.
    if (m_chartOverlay) {
        m_chartOverlay->loadSettings();
    }

#ifdef HAS_OPENCV
    // Vision overlay — extracted to QtCameraVisionController.
    if (m_visionController) {
        m_visionController->loadSettings();
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

    // Click-to-move math assumes unrotated display. Force-disable and lock the
    // checkbox whenever a rotation is active so users can't issue mis-directed
    // moves from a rotated view.
    const bool rotated = (deg != 0);
    if (rotated && m_clickToMoveEnabled) {
        setClickToMoveEnabled(false);
    }
    if (m_clickToMoveCheck) {
        m_clickToMoveCheck->setEnabled(!rotated);
        m_clickToMoveCheck->setToolTip(rotated
            ? QStringLiteral("Click-to-Move is disabled while display rotation is active.")
            : QString());
    }
}

void QtCameraWindow::onImageClicked(int imageX, int imageY)
{
    if (m_clickToMoveEnabled) {
        handleClickToMove(imageX, imageY);
    }
}

// ============================================================================
// Chart overlay — public API forwarding to QtCameraChartOverlay.
// ============================================================================

void QtCameraWindow::setChartEnabled(bool enabled)
{
    if (m_chartOverlay) m_chartOverlay->setChartEnabled(enabled);
}

bool QtCameraWindow::isChartEnabled() const
{
    return m_chartOverlay && m_chartOverlay->isChartEnabled();
}

// ============================================================================
// Vision processing — public API forwarding to QtCameraVisionController.
// ============================================================================

#ifdef HAS_OPENCV

bool QtCameraWindow::isVisionProcessingActive() const
{
    return m_visionController && m_visionController->isVisionProcessingActive();
}

VisionResult QtCameraWindow::getLatestVisionResult() const
{
    if (!m_visionController) return VisionResult();
    return m_visionController->getLatestVisionResult();
}

uint64_t QtCameraWindow::getVisionResultSequence() const
{
    return m_visionController ? m_visionController->getVisionResultSequence() : 0;
}

void QtCameraWindow::setVisionEnabled(bool enabled)
{
    if (m_visionController) m_visionController->setVisionEnabled(enabled);
}

void QtCameraWindow::selectVisionProfile(const std::string& profilePath)
{
    if (m_visionController) m_visionController->selectVisionProfile(profilePath);
}

void QtCameraWindow::showExternalVisionOverlay(const VisionResult& result,
                                               const std::string& profileName,
                                               int durationMs)
{
    if (m_visionController) {
        m_visionController->showExternalOverlay(result, profileName, durationMs);
    }
}

#endif // HAS_OPENCV

// ============================================================================
// Beam streaming — public API forwarding to QtCameraBeamStreamController.
// ============================================================================

void QtCameraWindow::setBeamServerClient(BeamServerClient* client)
{
    m_beamServerClient = client;
}

bool QtCameraWindow::isBeamStreamActive() const
{
    return m_beamController && m_beamController->isStreamActive();
}

nlohmann::json QtCameraWindow::getLatestBeamStreamResult() const
{
    return m_beamController ? m_beamController->getLatestResult() : nlohmann::json();
}

uint64_t QtCameraWindow::getBeamStreamResultSequence() const
{
    return m_beamController ? m_beamController->getResultSequence() : 0;
}

double QtCameraWindow::getBeamStreamFps() const
{
    return m_beamController ? m_beamController->getFps() : 0.0;
}
