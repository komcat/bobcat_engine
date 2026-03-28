// BeamOptimizerManager.cpp
#include "BeamOptimizerManager.h"
#include "BeamResultBroker.h"
#include "CameraManager.h"
#include "ICameraHardware.h"
#include "LogManager.h"
#include "PowerSupplyManager.h"
#include "unified_motion_manager.h"

BeamOptimizerManager::~BeamOptimizerManager()
{
    Shutdown();
}

ErrorCode BeamOptimizerManager::Initialize()
{
    if (m_initialized.load()) return ErrorCode::Success;

    if (!m_broker) {
        if (m_logManager) m_logManager->Warning("BeamOptimizerManager: No broker set", "BeamOptimizer");
        return ErrorCode::NullPointer;
    }

    // Subscribe to beam results
    m_brokerSubscriptionId = m_broker->Subscribe(
        [this](const std::vector<BeamDetectionResult>& results) {
            OnBeamResultsUpdated(results);
        });

    // Auto-discover PSU device if not configured
    if (m_config.psuDevice.empty() && m_psuManager) {
        AutoDiscoverPSU();
    }

    m_initialized.store(true);
    if (m_logManager) {
        m_logManager->Info("BeamOptimizerManager initialized (hex=" + m_config.hexDevice +
                          ", gantry=" + m_config.gantryDevice +
                          ", camera=" + m_config.cameraId +
                          ", psu=" + (m_config.psuDevice.empty() ? "none" : m_config.psuDevice) + ")",
                          "BeamOptimizer");
    }
    return ErrorCode::Success;
}

void BeamOptimizerManager::Shutdown()
{
    if (!m_initialized.load()) return;
    m_shuttingDown.store(true);

    // Unsubscribe from broker
    if (m_broker && m_brokerSubscriptionId != 0) {
        m_broker->Unsubscribe(m_brokerSubscriptionId);
        m_brokerSubscriptionId = 0;
    }

    // Clear cached results
    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_latestResults.clear();
    }

    m_initialized.store(false);
    if (m_logManager) m_logManager->Info("BeamOptimizerManager shut down", "BeamOptimizer");
}

// --- Dependency injection ---

void BeamOptimizerManager::SetMotionManager(UnifiedMotionManager* manager) { m_motionManager = manager; }
void BeamOptimizerManager::SetCameraManager(CameraManager* manager) { m_cameraManager = manager; }
void BeamOptimizerManager::SetPowerSupplyManager(PowerSupplyManager* manager) { m_psuManager = manager; }
void BeamOptimizerManager::SetLogManager(LogManager* manager) { m_logManager = manager; }
void BeamOptimizerManager::SetBeamResultBroker(BeamResultBroker* broker) { m_broker = broker; }

// --- Configuration ---

void BeamOptimizerManager::SetConfig(const BeamOptimizerConfig& config) { m_config = config; }

void BeamOptimizerManager::SetHexDevice(const std::string& deviceId)
{
    m_config.hexDevice = deviceId;
    if (m_logManager) m_logManager->Info("BeamOptimizer hex device set to: " + deviceId, "BeamOptimizer");
}

// --- Beam results ---

void BeamOptimizerManager::OnBeamResultsUpdated(const std::vector<BeamDetectionResult>& results)
{
    // Runs on detection thread — fast copy only, no hardware calls
    if (m_shuttingDown.load()) return;
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    m_latestResults = results;
}

std::vector<BeamDetectionResult> BeamOptimizerManager::GetLatestBeamResults() const
{
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_latestResults;
}

std::optional<BeamDetectionResult> BeamOptimizerManager::GetBeamByLabel(const std::string& label) const
{
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    for (const auto& result : m_latestResults) {
        if (result.label == label) return result;
    }
    return std::nullopt;
}

// --- Motion control ---

ErrorCode BeamOptimizerManager::MoveHexAbsolute(double x, double y, double z,
                                                  double u, double v, double w)
{
    if (!m_motionManager) return ErrorCode::NullPointer;

    PositionStruct pos;
    pos.x = x; pos.y = y; pos.z = z;
    pos.u = u; pos.v = v; pos.w = w;

    if (!m_motionManager->MoveToAbs6(m_config.hexDevice, pos, true)) {
        return ErrorCode::MotionAxisError;
    }
    return ErrorCode::Success;
}

ErrorCode BeamOptimizerManager::MoveHexRelative(const std::string& axis, double distance)
{
    if (!m_motionManager) return ErrorCode::NullPointer;

    if (!m_motionManager->MoveRelative(m_config.hexDevice, axis, distance, true)) {
        return ErrorCode::MotionAxisError;
    }
    return ErrorCode::Success;
}

ErrorCode BeamOptimizerManager::MoveHexToNamedPosition(const std::string& positionName)
{
    if (!m_motionManager) return ErrorCode::NullPointer;

    if (!m_motionManager->MoveToNamedPosition(m_config.hexDevice, positionName, true)) {
        return ErrorCode::MotionPositionNotFound;
    }
    return ErrorCode::Success;
}

ErrorCode BeamOptimizerManager::MoveGantryZ(double zPosition)
{
    if (!m_motionManager) return ErrorCode::NullPointer;

    if (!m_motionManager->MoveToPosition(m_config.gantryDevice, "Z", zPosition, true)) {
        return ErrorCode::MotionAxisError;
    }
    return ErrorCode::Success;
}

ErrorCode BeamOptimizerManager::GetHexPosition(double& x, double& y, double& z,
                                                 double& u, double& v, double& w)
{
    if (!m_motionManager) return ErrorCode::NullPointer;

    PositionStruct pos = m_motionManager->GetCurrentPosition(m_config.hexDevice);
    x = pos.x; y = pos.y; z = pos.z;
    u = pos.u; v = pos.v; w = pos.w;
    return ErrorCode::Success;
}

// --- Camera control ---

ErrorCode BeamOptimizerManager::SetExposure(double microseconds)
{
    if (!m_cameraManager) return ErrorCode::NullPointer;

    auto settings = m_cameraManager->GetExposureSettings(m_config.cameraId);
    settings.exposure_time = microseconds;
    settings.auto_exposure = false;

    if (!m_cameraManager->ApplyExposureSettings(m_config.cameraId, settings)) {
        return ErrorCode::CameraCommunicationError;
    }
    return ErrorCode::Success;
}

ErrorCode BeamOptimizerManager::SetGain(double gain)
{
    if (!m_cameraManager) return ErrorCode::NullPointer;

    auto settings = m_cameraManager->GetExposureSettings(m_config.cameraId);
    settings.gain = gain;
    settings.auto_gain = false;

    if (!m_cameraManager->ApplyExposureSettings(m_config.cameraId, settings)) {
        return ErrorCode::CameraCommunicationError;
    }
    return ErrorCode::Success;
}

ErrorCode BeamOptimizerManager::CaptureImage(const std::string& filename)
{
    if (!m_cameraManager) return ErrorCode::NullPointer;

    bool ok = false;
    if (filename.empty()) {
        ok = m_cameraManager->CaptureImage(m_config.cameraId);
    } else {
        ok = m_cameraManager->CaptureImage(m_config.cameraId, filename);
    }

    return ok ? ErrorCode::Success : ErrorCode::CameraGrabFailed;
}

// --- PSU control ---

void BeamOptimizerManager::AutoDiscoverPSU()
{
    auto names = m_psuManager->getDeviceNames();
    if (!names.empty()) {
        m_config.psuDevice = names.front();
        if (m_logManager) {
            m_logManager->Info("BeamOptimizer auto-discovered PSU: " + m_config.psuDevice, "BeamOptimizer");
        }
    }
}

ErrorCode BeamOptimizerManager::SetPSUCurrent(double milliamps)
{
    if (!m_psuManager || m_config.psuDevice.empty()) return ErrorCode::NullPointer;

    auto psu = m_psuManager->getPowerSupply(m_config.psuDevice);
    if (!psu) return ErrorCode::PSUDeviceError;

    // IPowerSupply::setCurrent takes amps
    if (!psu->setCurrent(milliamps / 1000.0, m_config.psuChannel)) {
        return ErrorCode::PSUDeviceError;
    }
    return ErrorCode::Success;
}

ErrorCode BeamOptimizerManager::SetPSUVoltage(double volts)
{
    if (!m_psuManager || m_config.psuDevice.empty()) return ErrorCode::NullPointer;

    auto psu = m_psuManager->getPowerSupply(m_config.psuDevice);
    if (!psu) return ErrorCode::PSUDeviceError;

    if (!psu->setVoltage(volts, m_config.psuChannel)) {
        return ErrorCode::PSUDeviceError;
    }
    return ErrorCode::Success;
}

ErrorCode BeamOptimizerManager::SetPSUOutput(bool enabled)
{
    if (!m_psuManager || m_config.psuDevice.empty()) return ErrorCode::NullPointer;

    auto psu = m_psuManager->getPowerSupply(m_config.psuDevice);
    if (!psu) return ErrorCode::PSUDeviceError;

    bool ok = enabled ? psu->setOutputOn(m_config.psuChannel)
                      : psu->setOutputOff(m_config.psuChannel);
    if (!ok) return ErrorCode::PSUDeviceError;
    return ErrorCode::Success;
}

ErrorCode BeamOptimizerManager::ReadPSUCurrent(double& outMilliamps)
{
    if (!m_psuManager || m_config.psuDevice.empty()) return ErrorCode::NullPointer;

    auto psu = m_psuManager->getPowerSupply(m_config.psuDevice);
    if (!psu) return ErrorCode::PSUDeviceError;

    // readCurrent returns amps, convert to milliamps
    outMilliamps = psu->readCurrent(m_config.psuChannel) * 1000.0;
    return ErrorCode::Success;
}
