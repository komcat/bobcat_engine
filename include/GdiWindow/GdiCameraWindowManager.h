// GdiCameraWindowManager.h
#pragma once

#include <QObject>
#include <QTimer>
#include <memory>
#include <string>

class BeamServerClient;
class CameraManager;
class DataStore;
class GdiCameraWindow;
class UnifiedMotionManager;

#ifdef HAS_OPENCV
struct VisionResult;
#endif

/**
 * @brief Manager for GDI-based camera monitor window
 *
 * Responsibilities:
 * - Creates and manages the GdiCameraWindow lifecycle
 * - Integrates with CameraManager for frame subscription
 * - Handles Qt timer for Win32 message processing
 * - Provides show/hide control
 *
 * Usage:
 *   auto manager = new GdiCameraWindowManager(parent);
 *   manager->setCameraManager(cameraManager);
 *   manager->setTargetCamera("camera_id");  // Optional
 *   manager->initialize();
 *   manager->show();
 */
class GdiCameraWindowManager : public QObject {
    Q_OBJECT

public:
    explicit GdiCameraWindowManager(QObject* parent = nullptr);
    ~GdiCameraWindowManager();

    // Initialize the window (must call setCameraManager first)
    bool initialize();

    // Shutdown and cleanup
    void shutdown();

    // Set camera manager (required before initialize)
    void setCameraManager(CameraManager* manager);

    // Set DataStore for chart overlay
    void setDataStore(DataStore* dataStore);

    // Set motion manager for click-to-move
    void setMotionManager(UnifiedMotionManager* motionManager);

    // Set beam server client for beam streaming
    void setBeamServerClient(BeamServerClient* client);

    // Set target camera ID (empty string = use any available camera)
    void setTargetCamera(const std::string& cameraId);
    std::string getTargetCamera() const;

    // Dual camera mode
    void setDualCameraMode(bool enabled);
    bool isDualCameraMode() const;
    void setSecondaryCameraId(const std::string& cameraId);
    std::string getSecondaryCameraId() const;

    // Continuous vision result access (for SequenceContext injection)
#ifdef HAS_OPENCV
    bool isVisionProcessingActive() const;
    VisionResult getLatestVisionResult() const;
    uint64_t getVisionResultSequence() const;

    // Programmatic continuous vision control (for recipe commands)
    void setVisionEnabled(bool enabled);
    void selectVisionProfile(const std::string& profilePath);
#endif

    // Window control
    void show();
    void hide();
    bool isVisible() const;
    void toggleVisibility();

    // Window configuration
    void setWindowTitle(const std::string& title);
    void setWindowPosition(int x, int y);
    void setWindowSize(int width, int height);

    // Check if initialized
    bool isInitialized() const { return m_initialized; }

    // Should the window be shown on startup (based on last session)
    bool shouldRestoreVisible() const { return m_restoreVisible; }

signals:
    void visibilityChanged(bool visible);

private slots:
    void processMessages();

public slots:
    // Reload and apply application settings (call when settings change)
    void applyApplicationSettings();

private:
    void saveWindowGeometry();
    void loadWindowGeometry();
    BeamServerClient* m_beamServerClient = nullptr;
    CameraManager* m_cameraManager = nullptr;
    DataStore* m_dataStore = nullptr;
    UnifiedMotionManager* m_motionManager = nullptr;
    std::shared_ptr<GdiCameraWindow> m_window;
    QTimer* m_messageTimer = nullptr;

    std::string m_targetCameraId;
    std::string m_windowTitle = "Camera Monitor (GDI)";
    int m_initialX = 100;
    int m_initialY = 100;
    int m_initialWidth = 800;
    int m_initialHeight = 600;

    bool m_initialized = false;
    bool m_restoreVisible = false;  // Whether window was visible in last session
};
