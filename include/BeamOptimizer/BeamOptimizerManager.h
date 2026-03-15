// BeamOptimizerManager.h
// Subscribes to BeamResultBroker and coordinates hardware
// (hexapod motion, gantry focus, SWIR camera exposure, power supplies)
// for beam optimization routines.
#pragma once

#include "common/ErrorCode.h"
#include "SwirBeamWindow/SwirBeamDetector.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
class BeamResultBroker;
class CameraManager;
class LogManager;
class PowerSupplyManager;
class UnifiedMotionManager;

struct BeamOptimizerConfig {
    std::string hexDevice = "hex-right";
    std::string gantryDevice = "gantry-main";
    std::string cameraId = "swir_camera";
    std::string psuDevice;         // empty = auto-discover first
    int psuChannel = 1;
};

class BeamOptimizerManager {
public:
    BeamOptimizerManager() = default;
    ~BeamOptimizerManager();

    // Non-copyable
    BeamOptimizerManager(const BeamOptimizerManager&) = delete;
    BeamOptimizerManager& operator=(const BeamOptimizerManager&) = delete;

    // Lifecycle
    [[nodiscard]] ErrorCode Initialize();
    void Shutdown();

    // Dependency injection (call before Initialize)
    void SetMotionManager(UnifiedMotionManager* manager);
    void SetCameraManager(CameraManager* manager);
    void SetPowerSupplyManager(PowerSupplyManager* manager);
    void SetLogManager(LogManager* manager);
    void SetBeamResultBroker(BeamResultBroker* broker);

    // Configuration
    void SetConfig(const BeamOptimizerConfig& config);
    void SetHexDevice(const std::string& deviceId);

    // Beam results (thread-safe snapshot)
    std::vector<BeamDetectionResult> GetLatestBeamResults() const;
    std::optional<BeamDetectionResult> GetBeamByLabel(const std::string& label) const;

    // Motion control
    [[nodiscard]] ErrorCode MoveHexAbsolute(double x, double y, double z,
                                             double u, double v, double w);
    [[nodiscard]] ErrorCode MoveHexRelative(const std::string& axis, double distance);
    [[nodiscard]] ErrorCode MoveHexToNamedPosition(const std::string& positionName);
    [[nodiscard]] ErrorCode MoveGantryZ(double zPosition);
    [[nodiscard]] ErrorCode GetHexPosition(double& x, double& y, double& z,
                                            double& u, double& v, double& w);

    // Camera control
    [[nodiscard]] ErrorCode SetExposure(double microseconds);
    [[nodiscard]] ErrorCode SetGain(double gain);
    [[nodiscard]] ErrorCode CaptureImage(const std::string& filename = "");

    // PSU control
    [[nodiscard]] ErrorCode SetPSUCurrent(double milliamps);
    [[nodiscard]] ErrorCode SetPSUVoltage(double volts);
    [[nodiscard]] ErrorCode SetPSUOutput(bool enabled);
    [[nodiscard]] ErrorCode ReadPSUCurrent(double& outMilliamps);

private:
    void OnBeamResultsUpdated(const std::vector<BeamDetectionResult>& results);
    void AutoDiscoverPSU();

    UnifiedMotionManager* m_motionManager = nullptr;
    CameraManager* m_cameraManager = nullptr;
    PowerSupplyManager* m_psuManager = nullptr;
    LogManager* m_logManager = nullptr;
    BeamResultBroker* m_broker = nullptr;
    uint32_t m_brokerSubscriptionId = 0;

    BeamOptimizerConfig m_config;
    mutable std::mutex m_resultsMutex;
    std::vector<BeamDetectionResult> m_latestResults;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_shuttingDown{false};
};
