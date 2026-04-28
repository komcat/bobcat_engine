// QtCameraWindow.h
#pragma once

#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>

#include "camera/CameraFrameData.h"
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <queue>
#include <condition_variable>
#include <thread>
#include <future>
#include <functional>
#include "nlohmann/json.hpp"

#ifdef HAS_OPENCV
#include "vision/VisionResult.h"
#endif

class CameraManager;
class DataStore;
class UnifiedMotionManager;
class CameraConfigManager;
class QtCameraViewport;
class QtCameraQuickMovePanel;
class QtCameraTrackingPanel;
class QtCameraChartOverlay;
#ifdef HAS_OPENCV
class QtCameraVisionController;
#endif
class QtCameraBeamStreamController;
class BeamServerClient;
class QListWidget;
class QPushButton;
class QTimer;
struct ApplicationContext;

/**
 * @brief Top-level Qt window for camera live streaming with side panel controls.
 *
 * Implements CameraFrameSubscriber to receive frames from the CameraManager
 * broadcast system. Renders via QtCameraViewport. Supports single and dual
 * camera display, crosshair overlay, click-to-move, distance display,
 * magnification, and display rotation.
 *
 * Thread safety: OnNewFrame() is called from a background camera thread.
 * Frame data is mutex-protected and posted to the Qt main thread via
 * QMetaObject::invokeMethod with Qt::QueuedConnection.
 */
class QtCameraWindow : public QWidget, public CameraFrameSubscriber {
    Q_OBJECT
public:
    explicit QtCameraWindow(const std::string& instanceId = "", QWidget* parent = nullptr);
    ~QtCameraWindow() override;

    // CameraFrameSubscriber interface
    void OnNewFrame(const CameraFrameData& frameData) override;
    void OnCameraStatusChanged(const std::string& cameraId, bool connected, bool grabbing) override;
    std::string GetSubscriberId() const override;
    bool WantsFramesFromCamera(const std::string& cameraId) const override;
    int GetMinFrameIntervalMs() const override { return 33; }

    // Dependency injection (call before initialize())
    void setCameraManager(CameraManager* mgr);
    void setDataStore(DataStore* store);
    void setMotionManager(UnifiedMotionManager* mgr);
    void setCameraConfigManager(CameraConfigManager* mgr);
    void setApplicationContext(ApplicationContext* ctx);

    // Camera control
    void setTargetCamera(const std::string& cameraId);
    std::string getTargetCamera() const;
    void setDualCameraMode(bool enabled);
    bool isDualCameraMode() const;
    void setSecondaryCameraId(const std::string& cameraId);
    std::string getSecondaryCameraId() const;

    // Tools
    void setCrosshairEnabled(bool enabled);
    void setClickToMoveEnabled(bool enabled);
    void setShowDistanceEnabled(bool enabled);
    void setMagnification(double mag);
    void setDisplayRotation(int degrees);

    // Chart overlay (forwards to QtCameraChartOverlay).
    void setChartEnabled(bool enabled);
    bool isChartEnabled() const;

    // Beam streaming
    void setBeamServerClient(BeamServerClient* client);
    bool isBeamStreamActive() const;
    nlohmann::json getLatestBeamStreamResult() const;
    uint64_t getBeamStreamResultSequence() const;
    double getBeamStreamFps() const;

#ifdef HAS_OPENCV
    // Vision processing
    bool isVisionProcessingActive() const;
    VisionResult getLatestVisionResult() const;
    uint64_t getVisionResultSequence() const;
    void setVisionEnabled(bool enabled);
    void selectVisionProfile(const std::string& profilePath);

    // External vision overlay feedback: pushes the given VisionResult onto
    // the viewport for `durationMs` ms, then auto-clears. Used by callers
    // that execute vision profiles outside this window's own vision loop
    // (e.g. VisionCommandHandler::execute_profile). Thread-safe: may be
    // invoked from any thread — internally marshalled to the GUI thread.
    // Each call replaces any pending overlay and restarts the clear timer.
    void showExternalVisionOverlay(const VisionResult& result,
                                   const std::string& profileName,
                                   int durationMs);
#endif

    // Viewport access
    QtCameraViewport* viewport() const;

    // Settings persistence
    void saveSettings();
    void loadSettings();

    // Lifecycle — call after setting dependencies
    bool initialize();
    void shutdown();

public slots:
    void applyApplicationSettings();
    void refreshCameraList();

signals:
    void visibilityChanged(bool visible);

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void updateViewport();
    void onPrimaryCameraChanged(const QString& cameraId);
    void onSecondaryCameraChanged(const QString& cameraId);
    void onDualModeToggled(bool checked);
    void onCrosshairToggled(bool checked);
    void onClickToMoveToggled(bool checked);
    void onShowDistanceToggled(bool checked);
    void onMagnificationChanged(int index);
    void onRotationChanged(int index);
    void onImageClicked(int imageX, int imageY);

private:
    std::string m_instanceId;
    std::string m_subscriberId;
    bool m_initialized = false;

    // UI layout
    QtCameraViewport* m_viewport = nullptr;
    QScrollArea* m_sidePanelScroll = nullptr;

    // Side panel controls — Camera tab
    QComboBox* m_primaryCameraCombo = nullptr;
    QComboBox* m_secondaryCameraCombo = nullptr;
    QLabel*    m_secondaryCameraLabel = nullptr;
    QCheckBox* m_dualModeCheck = nullptr;

    // Side panel controls — Exposure tab
    QLabel*    m_exposureValueLabel = nullptr;

    // Side panel controls — Tools group
    QCheckBox* m_crosshairCheck = nullptr;
    QCheckBox* m_clickToMoveCheck = nullptr;
    QCheckBox* m_showDistanceCheck = nullptr;
    QComboBox* m_magnificationCombo = nullptr;
    QComboBox* m_rotationCombo = nullptr;

    // Chart overlay — extracted to QtCameraChartOverlay.
    QtCameraChartOverlay* m_chartOverlay = nullptr;

#ifdef HAS_OPENCV
    // Vision processing — extracted to QtCameraVisionController.
    QtCameraVisionController* m_visionController = nullptr;
#endif

    // Beam streaming — extracted to QtCameraBeamStreamController.
    QtCameraBeamStreamController* m_beamController = nullptr;

    // Quick-move panel (left column container hosting QtCameraQuickMovePanel
    // + the device-tracking group). Container stays here so the tracking
    // group can sit beneath the quick-move grid.
    QWidget* m_quickMovePanel = nullptr;
    QtCameraQuickMovePanel* m_quickMoveWidget = nullptr;

    void buildQuickMovePanel();

    // Device tracking — extracted to QtCameraTrackingPanel.
    QtCameraTrackingPanel* m_trackingPanel = nullptr;

    // Dependencies (non-owning)
    CameraManager*       m_cameraManager       = nullptr;
    DataStore*           m_dataStore            = nullptr;
    UnifiedMotionManager* m_motionManager       = nullptr;
    CameraConfigManager* m_cameraConfigManager  = nullptr;
    ApplicationContext*  m_appContext           = nullptr;

    // Auto-focus state
    std::future<void> m_autoFocusFuture;
    void runAutoFocus(const std::string& cameraId, const std::string& motionDevice,
                       double zRangeMm, double zStepMm, QPushButton* button);

    // Camera state
    std::string m_targetCameraId;
    std::string m_secondaryCameraId;
    bool        m_dualMode = false;

    // Primary frame buffer (written by camera thread, read by main thread)
    mutable std::mutex   m_frameMutex;
    std::vector<uint8_t> m_frameBuffer;
    uint32_t             m_frameWidth  = 0;
    uint32_t             m_frameHeight = 0;
    std::atomic<bool>    m_hasNewFrame{false};

    // Secondary frame buffer (dual camera mode)
    mutable std::mutex   m_secondaryFrameMutex;
    std::vector<uint8_t> m_secondaryFrameBuffer;
    uint32_t             m_secondaryWidth  = 0;
    uint32_t             m_secondaryHeight = 0;
    std::atomic<bool>    m_hasSecondaryNewFrame{false};

    // Acquisition FPS tracking (updated on camera thread, read on main thread)
    uint64_t m_acqFrameCount = 0;
    std::chrono::steady_clock::time_point m_acqFpsTimestamp;
    std::atomic<double> m_acquisitionFps{0.0};

    // Pixel-to-mm calibration (loaded from CameraConfigManager or fallback JSON)
    double m_pixToMmX        = 0.00248;
    double m_pixToMmY        = 0.00248;
    int    m_calibRefWidth   = 0;
    int    m_calibRefHeight  = 0;
    void loadCalibrationFactors();

    // Optional beam server client (set by setBeamServerClient(); used
    // outside the streaming controller for legacy callers).
    BeamServerClient* m_beamServerClient = nullptr;

    // Current display magnification (for click-to-move distance scaling)
    double m_magnification = 1.0;

    // Click-to-move state
    bool m_clickToMoveEnabled = false;
    void handleClickToMove(int imageX, int imageY);

    // Side panel construction helpers
    void buildSidePanel();
    QWidget*   createCameraTabs();
    QWidget*   createCameraSelectionTab();
    QWidget*   createExposureTab();
    QWidget*   createToolsPage();
    QWidget*   createViewPage();

    // Exposure control helpers
    void applyExposureValue(double exposureUs);
    void adjustExposure(double deltaUs);
    void refreshExposureDisplay();

    // QSettings key helpers
    // settingsKey: geometry / visibility (uses "GdiCameraWindow" prefix for backward compat)
    // monitorKey:  camera selection / view mode
    // toolsKey:    crosshair / click-to-move / magnification / rotation
    QString settingsKey(const QString& key) const;
    QString monitorKey(const QString& key) const;
    QString toolsKey(const QString& key) const;

    // Map magnification combo index → double value
    static double magnificationFromIndex(int index);
    static int indexFromMagnification(double mag);

    // Map rotation combo index → degrees
    static int rotationFromIndex(int index);
    static int indexFromRotation(int degrees);
};
