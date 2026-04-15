// QtCameraWindow.h
#pragma once

#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <array>

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
class BeamServerClient;
class QListWidget;
class QPushButton;
class QTimer;
class TransformationMatrixLoader;

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

    // Chart overlay
    void setChartEnabled(bool enabled);
    bool isChartEnabled() const { return m_chartEnabled; }

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

    // Chart slots
    void onChartEnableToggled(bool checked);
    void onChartScaleIncrease();
    void onChartScaleDecrease();
    void onChartRefreshChannels();

#ifdef HAS_OPENCV
    // Vision slots
    void onVisionEnableToggled(bool checked);
    void onVisionProfileChanged(int index);
    void onVisionRefreshProfiles();
#endif

    // Beam slots
    void onBeamEnableToggled(bool checked);
    void onBeamCameraChanged(const QString& cameraId);

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

    // Side panel controls — Chart group
    QGroupBox*   m_chartGroup       = nullptr;
    QCheckBox*   m_chartEnableCheck = nullptr;
    QListWidget* m_chartChannelList = nullptr;
    QPushButton* m_chartRefreshBtn  = nullptr;
    QPushButton* m_chartScaleUpBtn  = nullptr;
    QPushButton* m_chartScaleDnBtn  = nullptr;

#ifdef HAS_OPENCV
    // Side panel controls — Vision group
    QGroupBox*   m_visionGroup        = nullptr;
    QCheckBox*   m_visionEnableCheck  = nullptr;
    QComboBox*   m_visionProfileCombo = nullptr;
    QPushButton* m_visionRefreshBtn   = nullptr;
#endif

    // Side panel controls — Beam group
    QGroupBox* m_beamGroup       = nullptr;
    QCheckBox* m_beamEnableCheck = nullptr;
    QComboBox* m_beamCameraCombo = nullptr;

    // Quick-move panel (second left panel)
    static constexpr int QUICK_MOVE_ROWS = 6;
    static constexpr int QUICK_MOVE_COLS = 4;
    static constexpr int QUICK_MOVE_COUNT = QUICK_MOVE_ROWS * QUICK_MOVE_COLS;

    struct QuickMoveButtonData {
        bool assigned = false;
        QString label;           // max 10 chars
        double relX = 0.0;      // mm
        double relY = 0.0;      // mm
        double relZ = 0.0;      // mm
    };

    QWidget* m_quickMovePanel = nullptr;
    std::array<QPushButton*, QUICK_MOVE_COUNT> m_quickMoveBtns{};
    std::array<QuickMoveButtonData, QUICK_MOVE_COUNT> m_quickMoveData{};

    void buildQuickMovePanel();
    void onQuickMoveClicked(int index);
    void onQuickMoveRightClicked(int index);
    void showQuickMoveConfigDialog(int index);
    void updateQuickMoveButton(int index);
    void saveQuickMoveSettings();
    void loadQuickMoveSettings();
    QString quickMoveKey(const QString& key) const;

    // Device tracking (session-only): register a gantry-frame zero point P0
    // from gantry-main XY, then live-monitor hex-left / hex-right positions
    // (transformed local -> global) as deltas from P0.
    QGroupBox*   m_trackingGroup       = nullptr;
    QPushButton* m_zeroP0Btn           = nullptr;
    QPushButton* m_setHexLeftBtn       = nullptr;
    QPushButton* m_setHexRightBtn      = nullptr;
    QPushButton* m_clearTrackingBtn    = nullptr;
    QLabel*      m_p0StatusLabel       = nullptr;
    QLabel*      m_hexLeftStatusLabel  = nullptr;
    QLabel*      m_hexRightStatusLabel = nullptr;
    QTimer*      m_trackingTimer       = nullptr;
    std::unique_ptr<TransformationMatrixLoader> m_matrixLoader;
    bool   m_p0Set = false;
    double m_p0X = 0.0, m_p0Y = 0.0;
    bool   m_hexLeftReg = false;
    double m_hexLeftRegDX = 0.0, m_hexLeftRegDY = 0.0;
    double m_hexLeftRegLocalX = 0.0, m_hexLeftRegLocalY = 0.0, m_hexLeftRegLocalZ = 0.0;
    bool   m_hexRightReg = false;
    double m_hexRightRegDX = 0.0, m_hexRightRegDY = 0.0;
    double m_hexRightRegLocalX = 0.0, m_hexRightRegLocalY = 0.0, m_hexRightRegLocalZ = 0.0;

    // Alert thresholds (Euclidean distance in mm). Persisted via QSettings.
    static constexpr double DEFAULT_HEX_ALERT_THRESHOLD = 0.005;
    double m_hexLeftAlertThreshold  = DEFAULT_HEX_ALERT_THRESHOLD;
    double m_hexRightAlertThreshold = DEFAULT_HEX_ALERT_THRESHOLD;
    bool   m_hexLeftAlertActive  = false;
    bool   m_hexRightAlertActive = false;

    QGroupBox* buildDeviceTrackingGroup();
    void onZeroP0Clicked();
    void onSetHexLeftClicked();
    void onSetHexRightClicked();
    void onClearTrackingClicked();
    void onTrackingTimerTick();
    bool getGantryMainXY(double& x, double& y) const;
    bool getHexLocalXYZ(const std::string& hexDevice,
                        double& lx, double& ly, double& lz) const;
    void updateTrackingStatusLabels();
    void onSetHexLeftRightClicked();
    void onSetHexRightRightClicked();
    void promptThresholdDialog(const QString& which, double& threshold);
    void applyHexButtonAlertStyle(QPushButton* btn, bool alert);
    void loadTrackingSettings();
    void saveTrackingSettings();

    // Dependencies (non-owning)
    CameraManager*       m_cameraManager       = nullptr;
    DataStore*           m_dataStore            = nullptr;
    UnifiedMotionManager* m_motionManager       = nullptr;
    CameraConfigManager* m_cameraConfigManager  = nullptr;

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

    // ---- Chart overlay data ----
    struct ChartChannelData {
        std::deque<double> values;
        double runningMin = 1e30;
        double runningMax = -1e30;
    };
    mutable std::mutex m_chartMutex;
    std::map<std::string, ChartChannelData> m_chartChannels;
    std::set<std::string> m_chartEnabledChannels;
    bool  m_chartEnabled = false;
    float m_chartScale   = 1.0f;
    std::chrono::steady_clock::time_point m_lastChartRepaint;
    static constexpr size_t CHART_MAX_POINTS = 200;
    void onDataStoreValueChanged(const std::string& channelId, float value);
    void refreshChartChannelList();
    QString chartKey(const QString& key) const;

#ifdef HAS_OPENCV
    // ---- Vision processing ----
    std::atomic<bool> m_visionEnabled{false};
    std::atomic<bool> m_visionThreadRunning{false};
    std::thread m_visionThread;
    std::queue<std::vector<uint8_t>> m_visionFrameQueue;
    uint32_t m_visionQueuedWidth  = 0;
    uint32_t m_visionQueuedHeight = 0;
    std::mutex m_visionFrameQueueMutex;
    std::condition_variable m_visionFrameQueueCV;
    std::string m_activeVisionProfile;
    std::mutex m_visionProfileMutex;

    // Vision results (thread-safe)
    VisionResult m_lastVisionResult;
    std::string  m_visionProfileName;
    mutable std::mutex m_visionResultMutex;
    std::atomic<uint64_t> m_visionResultSequence{0};

    // Profile discovery
    struct VisionProfileInfo { std::string fullPath; std::string displayName; };
    std::vector<VisionProfileInfo> m_availableProfiles;

    void startVisionProcessing();
    void stopVisionProcessing();
    void visionThreadLoop();
    void discoverVisionProfiles();
    void populateVisionProfileCombo();
    QString visionKey(const QString& key) const;
#endif

    // ---- Beam streaming ----
    BeamServerClient* m_beamServerClient = nullptr;
    std::unique_ptr<BeamServerClient> m_beamStreamClient;
    std::atomic<bool> m_beamStreamEnabled{false};
    std::atomic<bool> m_beamStreamThreadRunning{false};
    std::thread m_beamStreamThread;
    std::queue<std::vector<uint8_t>> m_beamStreamQueue;
    uint32_t m_beamStreamQueuedWidth  = 0;
    uint32_t m_beamStreamQueuedHeight = 0;
    std::mutex m_beamStreamQueueMutex;
    std::condition_variable m_beamStreamQueueCV;
    std::string m_beamStreamCameraId;

    struct BeamStreamConfig {
        std::string host   = "127.0.0.1";
        int    port        = 5567;
        double umPerPxX    = 0.25;
        double umPerPxY    = 0.25;
        bool   rotate180   = false;
        int    intervalMs  = 100;
    };
    BeamStreamConfig m_beamStreamConfig;

    nlohmann::json m_lastBeamResult;
    mutable std::mutex m_beamResultMutex;
    std::atomic<uint64_t> m_beamResultSequence{0};
    std::atomic<double>   m_beamStreamFps{0.0};

    void startBeamStreaming();
    void stopBeamStreaming();
    void beamStreamThreadLoop();
    void loadBeamStreamConfig();

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
    QGroupBox* createToolsGroup();
    QGroupBox* createChartGroup();
    QGroupBox* createVisionGroup();
    QGroupBox* createBeamGroup();

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
