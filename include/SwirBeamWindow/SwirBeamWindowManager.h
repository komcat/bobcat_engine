// SwirBeamWindowManager.h
#pragma once

#include <QObject>
#include <QTimer>
#include <memory>
#include <string>

class BeamResultBroker;
class CameraConfigManager;
class CameraManager;
class PneumaticManager;
class PowerSupplyManager;
class SwirBeamWindow;
class UnifiedMotionManager;

/**
 * @brief Manager for SWIR Beam Processing window
 *
 * Manages the lifecycle of a SwirBeamWindow (pure Win32 GDI window)
 * that displays real-time SWIR camera feed with PSU and exposure controls.
 * Auto-discovers installed PSU devices from PowerSupplyManager.
 */
class SwirBeamWindowManager : public QObject {
    Q_OBJECT

public:
    explicit SwirBeamWindowManager(QObject* parent = nullptr);
    ~SwirBeamWindowManager();

    bool initialize();
    void shutdown();

    // Manager injection
    void setCameraManager(CameraManager* manager);
    void setPowerSupplyManager(PowerSupplyManager* psuManager);
    void setMotionManager(UnifiedMotionManager* motionManager);
    void setPneumaticManager(PneumaticManager* pneumaticManager);
    void setCameraConfigManager(CameraConfigManager* configMgr);
    void setBeamResultBroker(BeamResultBroker* broker);

    // Configuration
    void setTargetCamera(const std::string& cameraId);

    // Window control
    void show();
    void hide();
    bool isVisible() const;
    void toggleVisibility();

    // Window configuration
    void setWindowTitle(const std::string& title);
    void setWindowPosition(int x, int y);
    void setWindowSize(int width, int height);

    bool isInitialized() const { return m_initialized; }
    bool shouldRestoreVisible() const { return m_restoreVisible; }

signals:
    void visibilityChanged(bool visible);

private slots:
    void processMessages();
    void pollPSU();

private:
    void saveWindowGeometry();
    void loadWindowGeometry();

    CameraConfigManager* m_cameraConfigManager = nullptr;
    CameraManager* m_cameraManager = nullptr;
    PowerSupplyManager* m_psuManager = nullptr;
    PneumaticManager* m_pneumaticManager = nullptr;
    UnifiedMotionManager* m_motionManager = nullptr;
    std::shared_ptr<SwirBeamWindow> m_window;
    QTimer* m_messageTimer = nullptr;
    QTimer* m_psuPollTimer = nullptr;

    std::string m_targetCameraId = "swir_camera";
    std::string m_windowTitle = "SWIR Beam Window";
    int m_initialX = 100;
    int m_initialY = 100;
    int m_initialWidth = 1200;
    int m_initialHeight = 700;

    bool m_initialized = false;
    bool m_restoreVisible = false;
};
