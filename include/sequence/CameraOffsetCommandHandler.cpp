// CameraOffsetCommandHandler.cpp
// Headless camera offset move for recipe/sequence execution
#include "CameraOffsetCommandHandler.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "SequenceContext.h"
#include "../AppUI/CameraOffsetTrainer/CameraOffsetData.h"
#include "../AppUI/CameraOffsetTrainer/CameraOffsetStore.h"
#include "product_position_manager.h"
#include "PneumaticManager.h"
#include "LogManager.h"
#include <iostream>
#include <filesystem>

// ============================================================================
// ExecuteCommand - Route to the appropriate handler
// ============================================================================

bool CameraOffsetCommandHandler::ExecuteCommand(
    void* /*manager*/,
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    if (methodName == "MoveToHighMag") {
        return HandleRelativeMove(true, params, errorMessage, context);
    }
    if (methodName == "MoveToLowMag") {
        return HandleRelativeMove(false, params, errorMessage, context);
    }
    if (methodName == "MoveToHighMagAbs") {
        return HandleAbsoluteMove(true, params, errorMessage, context);
    }
    if (methodName == "MoveToLowMagAbs") {
        return HandleAbsoluteMove(false, params, errorMessage, context);
    }

    errorMessage = "Unknown CameraOffset command: " + methodName;
    return false;
}

// ============================================================================
// HandleRelativeMove - Apply offset to current position
// ============================================================================

bool CameraOffsetCommandHandler::HandleRelativeMove(
    bool lowToHigh,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    // --- Load calibration set by name from product folder ---
    auto nameIt = params.find("calibration_set_name");
    if (nameIt == params.end() || nameIt->second.empty()) {
        errorMessage = "Missing required parameter: calibration_set_name";
        if (context) context->SetValue("camera_offset.result.success", false);
        return false;
    }
    const std::string& csName = nameIt->second;

    CameraOffsetStore store;
    if (m_appContext && m_appContext->productPositionManager &&
        m_appContext->productPositionManager->HasLoadedProduct()) {
        std::string productPath = m_appContext->productPositionManager->GetCurrentProductPath();
        std::filesystem::path productDir = std::filesystem::path(productPath).parent_path();
        store.SetBasePath((productDir / "PR_Camera_Offset").string());
        std::cout << "[CameraOffset] Store path: " << store.GetBasePath() << std::endl;
    }

    auto optCs = store.LoadCalibrationSet(csName);
    if (!optCs.has_value()) {
        errorMessage = "Failed to load calibration set: " + csName;
        if (context) context->SetValue("camera_offset.result.success", false);
        return false;
    }
    CameraOffsetCalibrationSet cs = std::move(optCs.value());

    if (!cs.offsetValid) {
        errorMessage = "Calibration set '" + cs.name + "' has no valid offset data";
        if (context) context->SetValue("camera_offset.result.success", false);
        return false;
    }

    if (!m_appContext || !m_appContext->motionManager) {
        errorMessage = "Motion manager not available";
        if (context) context->SetValue("camera_offset.result.success", false);
        return false;
    }

    const std::string& device = cs.deviceName;
    auto* motionMgr = m_appContext->motionManager;

    // Get current position
    auto currentPos = motionMgr->GetCurrentPosition(device);
    double fromX = currentPos.x;
    double fromY = currentPos.y;
    double fromZ = currentPos.z;

    // Compute target: add offset for low->high, subtract for high->low
    double sign = lowToHigh ? 1.0 : -1.0;
    double toX = fromX + sign * cs.offsetX;
    double toY = fromY + sign * cs.offsetY;
    double toZ = fromZ + sign * cs.offsetZ;

    // Parse optional velocity
    double velocity = 100.0;
    auto velIt = params.find("velocity");
    if (velIt != params.end() && !velIt->second.empty()) {
        velocity = std::stod(velIt->second);
    }

    std::string direction = lowToHigh ? "LowMag->HighMag" : "HighMag->LowMag";
    std::cout << "[CameraOffset] " << direction
              << " from (" << fromX << ", " << fromY << ", " << fromZ << ")"
              << " to (" << toX << ", " << toY << ", " << toZ << ")"
              << " offset=(" << cs.offsetX << ", " << cs.offsetY << ", " << cs.offsetZ << ")"
              << " vel=" << velocity << std::endl;

    // Retract pneumatic slide before any gantry motion (if configured)
    if (!cs.pneumaticSlideName.empty()) {
        if (!EnsurePneumaticRetracted(cs.pneumaticSlideName, errorMessage)) {
            if (context) context->SetValue("camera_offset.result.success", false);
            return false;
        }
    }

    bool moveOk = motionMgr->MoveToAbs(device, toX, toY, toZ, velocity, true);

    if (!moveOk) {
        errorMessage = "Motion move failed for " + direction;
        if (context) context->SetValue("camera_offset.result.success", false);
        return false;
    }

    // If moving to high-mag, extend pneumatic after arrival
    if (lowToHigh && !cs.pneumaticSlideName.empty()) {
        if (!ActivatePneumaticExtend(cs.pneumaticSlideName, errorMessage)) {
            if (context) context->SetValue("camera_offset.result.success", false);
            return false;
        }
    }

    // Store results in context
    if (context) {
        context->SetValue("camera_offset.result.success", true);
        context->SetValue("camera_offset.result.direction", direction);
        context->SetValue("camera_offset.result.device", device);
        context->SetValue("camera_offset.result.from_x", std::to_string(fromX));
        context->SetValue("camera_offset.result.from_y", std::to_string(fromY));
        context->SetValue("camera_offset.result.from_z", std::to_string(fromZ));
        context->SetValue("camera_offset.result.to_x", std::to_string(toX));
        context->SetValue("camera_offset.result.to_y", std::to_string(toY));
        context->SetValue("camera_offset.result.to_z", std::to_string(toZ));
        context->SetValue("camera_offset.result.offset_x", std::to_string(cs.offsetX));
        context->SetValue("camera_offset.result.offset_y", std::to_string(cs.offsetY));
        context->SetValue("camera_offset.result.offset_z", std::to_string(cs.offsetZ));
        if (!cs.pneumaticSlideName.empty()) {
            context->SetValue("camera_offset.result.pneumatic_slide", cs.pneumaticSlideName);
            context->SetValue("camera_offset.result.pneumatic_action", lowToHigh ? "extend" : "retract");
        }
    }

    std::cout << "[CameraOffset] " << direction << " complete." << std::endl;
    return true;
}

// ============================================================================
// HandleAbsoluteMove - Move to calibrated absolute position
// ============================================================================

bool CameraOffsetCommandHandler::HandleAbsoluteMove(
    bool toHighMag,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    // --- Load calibration set by name from product folder ---
    auto nameIt = params.find("calibration_set_name");
    if (nameIt == params.end() || nameIt->second.empty()) {
        errorMessage = "Missing required parameter: calibration_set_name";
        if (context) context->SetValue("camera_offset.result.success", false);
        return false;
    }
    const std::string& csName = nameIt->second;

    CameraOffsetStore store;
    if (m_appContext && m_appContext->productPositionManager &&
        m_appContext->productPositionManager->HasLoadedProduct()) {
        std::string productPath = m_appContext->productPositionManager->GetCurrentProductPath();
        std::filesystem::path productDir = std::filesystem::path(productPath).parent_path();
        store.SetBasePath((productDir / "PR_Camera_Offset").string());
    }

    auto optCs = store.LoadCalibrationSet(csName);
    if (!optCs.has_value()) {
        errorMessage = "Failed to load calibration set: " + csName;
        if (context) context->SetValue("camera_offset.result.success", false);
        return false;
    }
    CameraOffsetCalibrationSet cs = std::move(optCs.value());

    if (!cs.offsetValid) {
        errorMessage = "Calibration set '" + cs.name + "' has no valid offset data";
        if (context) context->SetValue("camera_offset.result.success", false);
        return false;
    }

    if (!m_appContext || !m_appContext->motionManager) {
        errorMessage = "Motion manager not available";
        if (context) context->SetValue("camera_offset.result.success", false);
        return false;
    }

    const std::string& device = cs.deviceName;
    auto* motionMgr = m_appContext->motionManager;

    // Select target position
    const CameraPosition& target = toHighMag ? cs.highMagPosition : cs.lowMagPosition;
    if (!target.valid) {
        std::string camName = toHighMag ? "high-mag" : "low-mag";
        errorMessage = "Calibration set has no valid " + camName + " absolute position";
        if (context) context->SetValue("camera_offset.result.success", false);
        return false;
    }

    // Get current position for logging
    auto currentPos = motionMgr->GetCurrentPosition(device);

    // Parse optional velocity
    double velocity = 100.0;
    auto velIt = params.find("velocity");
    if (velIt != params.end() && !velIt->second.empty()) {
        velocity = std::stod(velIt->second);
    }

    std::string camLabel = toHighMag ? "HighMag(abs)" : "LowMag(abs)";
    std::cout << "[CameraOffset] " << camLabel
              << " from (" << currentPos.x << ", " << currentPos.y << ", " << currentPos.z << ")"
              << " to (" << target.x << ", " << target.y << ", " << target.z << ")"
              << " vel=" << velocity << std::endl;

    // Always retract pneumatic before any gantry motion (if configured)
    if (!cs.pneumaticSlideName.empty()) {
        if (!EnsurePneumaticRetracted(cs.pneumaticSlideName, errorMessage)) {
            if (context) context->SetValue("camera_offset.result.success", false);
            return false;
        }
    }

    bool moveOk = motionMgr->MoveToAbs(device, target.x, target.y, target.z, velocity, true);

    if (!moveOk) {
        errorMessage = "Motion move failed for " + camLabel;
        if (context) context->SetValue("camera_offset.result.success", false);
        return false;
    }

    // If moving to high-mag, extend pneumatic after arrival
    if (toHighMag && !cs.pneumaticSlideName.empty()) {
        if (!ActivatePneumaticExtend(cs.pneumaticSlideName, errorMessage)) {
            if (context) context->SetValue("camera_offset.result.success", false);
            return false;
        }
    }

    // Store results in context
    if (context) {
        context->SetValue("camera_offset.result.success", true);
        context->SetValue("camera_offset.result.direction", camLabel);
        context->SetValue("camera_offset.result.device", device);
        context->SetValue("camera_offset.result.from_x", std::to_string(currentPos.x));
        context->SetValue("camera_offset.result.from_y", std::to_string(currentPos.y));
        context->SetValue("camera_offset.result.from_z", std::to_string(currentPos.z));
        context->SetValue("camera_offset.result.to_x", std::to_string(target.x));
        context->SetValue("camera_offset.result.to_y", std::to_string(target.y));
        context->SetValue("camera_offset.result.to_z", std::to_string(target.z));
        context->SetValue("camera_offset.result.offset_x", std::to_string(cs.offsetX));
        context->SetValue("camera_offset.result.offset_y", std::to_string(cs.offsetY));
        context->SetValue("camera_offset.result.offset_z", std::to_string(cs.offsetZ));
        if (!cs.pneumaticSlideName.empty()) {
            context->SetValue("camera_offset.result.pneumatic_slide", cs.pneumaticSlideName);
            context->SetValue("camera_offset.result.pneumatic_action", toHighMag ? "extend" : "retract");
        }
    }

    std::cout << "[CameraOffset] " << camLabel << " complete." << std::endl;
    return true;
}

// ============================================================================
// Pneumatic helpers
// ============================================================================

bool CameraOffsetCommandHandler::EnsurePneumaticRetracted(
    const std::string& slideName, std::string& errorMessage)
{
    if (!m_appContext || !m_appContext->pneumaticManager) {
        // No pneumatic manager available — skip silently
        std::cout << "[CameraOffset] No pneumatic manager, skipping retract for: " << slideName << std::endl;
        return true;
    }

    if (!m_appContext->pneumaticManager->HasSlide(slideName)) {
        errorMessage = "Pneumatic slide not found: " + slideName;
        return false;
    }

    auto pos = m_appContext->pneumaticManager->GetPosition(slideName);
    if (pos == PneumaticPosition::RETRACTED) {
        std::cout << "[CameraOffset] Pneumatic '" << slideName << "' already retracted" << std::endl;
        return true;
    }

    std::cout << "[CameraOffset] Retracting pneumatic '" << slideName << "'..." << std::endl;
    auto err = m_appContext->pneumaticManager->RetractAndWait(slideName);
    if (err != PneumaticError::SUCCESS && err != PneumaticError::ALREADY_AT_POSITION) {
        errorMessage = "Failed to retract pneumatic '" + slideName + "': " + GetPneumaticErrorString(err);
        return false;
    }

    std::cout << "[CameraOffset] Pneumatic '" << slideName << "' retracted" << std::endl;
    return true;
}

bool CameraOffsetCommandHandler::ActivatePneumaticExtend(
    const std::string& slideName, std::string& errorMessage)
{
    if (!m_appContext || !m_appContext->pneumaticManager) {
        std::cout << "[CameraOffset] No pneumatic manager, skipping extend for: " << slideName << std::endl;
        return true;
    }

    if (!m_appContext->pneumaticManager->HasSlide(slideName)) {
        errorMessage = "Pneumatic slide not found: " + slideName;
        return false;
    }

    std::cout << "[CameraOffset] Extending pneumatic '" << slideName << "'..." << std::endl;
    auto err = m_appContext->pneumaticManager->ExtendAndWait(slideName);
    if (err != PneumaticError::SUCCESS && err != PneumaticError::ALREADY_AT_POSITION) {
        errorMessage = "Failed to extend pneumatic '" + slideName + "': " + GetPneumaticErrorString(err);
        return false;
    }

    std::cout << "[CameraOffset] Pneumatic '" << slideName << "' extended" << std::endl;
    return true;
}
