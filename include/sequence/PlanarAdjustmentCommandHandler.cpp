// PlanarAdjustmentCommandHandler.cpp
// Headless planar adjustment for recipe/sequence execution
#include "PlanarAdjustmentCommandHandler.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "SequenceContext.h"
#include "../AppUI/PlanarAdjustmentTrainer/PlanarAdjustmentData.h"
#include "../AppUI/PlanarAdjustmentTrainer/PlanarAdjustmentStore.h"
#include "product_position_manager.h"
#include <iostream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <cmath>

// ============================================================================
// ExecuteCommand - Route to the appropriate handler
// ============================================================================

bool PlanarAdjustmentCommandHandler::ExecuteCommand(
    void* /*manager*/,
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    if (methodName == "RunPlanarAdjustment") {
        return HandleRunPlanarAdjustment(params, errorMessage, context);
    }

    errorMessage = "Unknown PlanarAdjustment command: " + methodName;
    return false;
}

// ============================================================================
// HandleRunPlanarAdjustment
// ============================================================================

bool PlanarAdjustmentCommandHandler::HandleRunPlanarAdjustment(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    // --- Parse parameters ---
    auto nameIt = params.find("training_set_name");
    if (nameIt == params.end() || nameIt->second.empty()) {
        errorMessage = "Missing required parameter: training_set_name";
        return false;
    }
    const std::string& tsName = nameIt->second;

    std::string sourceAngle = "angle_x";
    auto saIt = params.find("source_angle");
    if (saIt != params.end() && !saIt->second.empty()) {
        sourceAngle = saIt->second;
    }

    auto tdIt = params.find("target_device");
    if (tdIt == params.end() || tdIt->second.empty()) {
        errorMessage = "Missing required parameter: target_device";
        return false;
    }
    const std::string& targetDevice = tdIt->second;

    std::string targetAxis = "U";
    auto taIt = params.find("target_axis");
    if (taIt != params.end() && !taIt->second.empty()) {
        targetAxis = taIt->second;
    }

    bool reverseSign = false;
    auto rsIt = params.find("reverse_sign");
    if (rsIt != params.end() && rsIt->second == "true") {
        reverseSign = true;
    }

    std::cout << "[PlanarAdjustment] RunPlanarAdjustment: set=" << tsName
              << " source=" << sourceAngle
              << " target=" << targetDevice << "/" << targetAxis
              << " reverse=" << (reverseSign ? "true" : "false") << std::endl;

    // --- Load training set ---
    PlanarAdjustmentStore store;
    if (m_appContext && m_appContext->productPositionManager &&
        m_appContext->productPositionManager->HasLoadedProduct()) {
        std::string productPath = m_appContext->productPositionManager->GetCurrentProductPath();
        std::filesystem::path productDir = std::filesystem::path(productPath).parent_path();
        store.SetBasePath((productDir / "planar_adjustment").string());
        std::cout << "[PlanarAdjustment] Store path: " << store.GetBasePath() << std::endl;
    }
    auto optSet = store.LoadSet(tsName);
    if (!optSet.has_value()) {
        errorMessage = "Failed to load training set: " + tsName;
        if (context) context->SetValue("planar_adjustment.result.success", false);
        return false;
    }
    PlanarAdjustmentSet set = std::move(optSet.value());

    // --- Validate ---
    if (!set.point1.valid || !set.point2.valid) {
        errorMessage = "Training set '" + tsName + "' has incomplete points";
        if (context) context->SetValue("planar_adjustment.result.success", false);
        return false;
    }

    if (!m_appContext || !m_appContext->motionManager) {
        errorMessage = "Motion manager not available";
        if (context) context->SetValue("planar_adjustment.result.success", false);
        return false;
    }

    // --- Live retrace using ManualAdjust callback ---
    bool retracePerformed = false;
    if (m_manualAdjustCallback) {
        // Save taught P2 absolute position BEFORE any overwrite
        double taughtP2X = set.point2.x;
        double taughtP2Y = set.point2.y;
        double taughtP2Z = set.point2.z;

        std::cout << "[PlanarAdjustment] Live retrace: taught P1=("
                  << set.point1.x << "," << set.point1.y << "," << set.point1.z
                  << ") P2=(" << taughtP2X << "," << taughtP2Y << "," << taughtP2Z << ")" << std::endl;

        // --- Step 1: Auto-navigate to P1, then prompt user ---
        {
            std::cout << "[PlanarAdjustment] Auto-navigating to P1: X="
                      << set.point1.x << " Y=" << set.point1.y
                      << " Z=" << set.point1.z << std::endl;
            bool navOk = m_appContext->motionManager->MoveToAbs(
                set.deviceName,
                set.point1.x, set.point1.y, set.point1.z,
                100.0, true);
            if (!navOk) {
                std::cout << "[PlanarAdjustment] WARNING: Auto-navigate to P1 failed" << std::endl;
            }
        }

        std::ostringstream p1Msg;
        p1Msg << std::fixed << std::setprecision(4);
        p1Msg << "Planar Retrace - Confirm Point 1\n\n"
              << "Saved P1: X=" << set.point1.x
              << "  Y=" << set.point1.y
              << "  Z=" << set.point1.z
              << "\n\nVerify position, then click OK.";

        bool p1Ok = m_manualAdjustCallback(p1Msg.str(), set.deviceName, 10.0, true, true, true, {});
        if (!p1Ok) {
            errorMessage = "User cancelled retrace at Point 1";
            if (context) context->SetValue("planar_adjustment.result.success", false);
            return false;
        }

        // Capture current position as new P1
        auto p1Pos = m_appContext->motionManager->GetCurrentPosition(set.deviceName);
        set.point1.x = p1Pos.x;
        set.point1.y = p1Pos.y;
        set.point1.z = p1Pos.z;
        set.point1.valid = true;

        std::cout << "[PlanarAdjustment] P1 confirmed: X=" << p1Pos.x
                  << " Y=" << p1Pos.y << " Z=" << p1Pos.z << std::endl;

        // --- Step 2: Auto-navigate to P2, then prompt user ---
        {
            std::cout << "[PlanarAdjustment] Auto-navigating to P2: X="
                      << taughtP2X << " Y=" << taughtP2Y
                      << " Z=" << taughtP2Z << std::endl;
            bool navOk = m_appContext->motionManager->MoveToAbs(
                set.deviceName,
                taughtP2X, taughtP2Y, taughtP2Z,
                100.0, true);
            if (!navOk) {
                std::cout << "[PlanarAdjustment] WARNING: Auto-navigate to P2 failed" << std::endl;
            }
        }

        std::ostringstream p2Msg;
        p2Msg << std::fixed << std::setprecision(4);
        p2Msg << "Planar Retrace - Confirm Point 2\n\n"
              << "Saved P2: X=" << taughtP2X
              << "  Y=" << taughtP2Y
              << "  Z=" << taughtP2Z
              << "\n\nVerify position, then click OK.";

        bool p2Ok = m_manualAdjustCallback(p2Msg.str(), set.deviceName, 10.0, true, true, true, {});
        if (!p2Ok) {
            errorMessage = "User cancelled retrace at Point 2";
            if (context) context->SetValue("planar_adjustment.result.success", false);
            return false;
        }

        // Capture current position as new P2
        auto p2Pos = m_appContext->motionManager->GetCurrentPosition(set.deviceName);
        set.point2.x = p2Pos.x;
        set.point2.y = p2Pos.y;
        set.point2.z = p2Pos.z;
        set.point2.valid = true;

        std::cout << "[PlanarAdjustment] P2 confirmed: X=" << p2Pos.x
                  << " Y=" << p2Pos.y << " Z=" << p2Pos.z << std::endl;

        retracePerformed = true;
    }

    // --- Compute tilt angles ---
    set.RecalculateAngles();

    std::cout << "[PlanarAdjustment] Angle X: " << set.angleX_deg << " deg (valid="
              << (set.angleXValid ? "true" : "false") << ")" << std::endl;
    std::cout << "[PlanarAdjustment] Angle Y: " << set.angleY_deg << " deg (valid="
              << (set.angleYValid ? "true" : "false") << ")" << std::endl;

    // --- Get source angle ---
    double angleDeg = 0.0;
    if (sourceAngle == "angle_x") {
        if (!set.angleXValid) {
            errorMessage = "Angle X is not valid (insufficient Y separation)";
            if (context) context->SetValue("planar_adjustment.result.success", false);
            return false;
        }
        angleDeg = set.angleX_deg;
    } else if (sourceAngle == "angle_y") {
        if (!set.angleYValid) {
            errorMessage = "Angle Y is not valid (insufficient X separation)";
            if (context) context->SetValue("planar_adjustment.result.success", false);
            return false;
        }
        angleDeg = set.angleY_deg;
    } else {
        errorMessage = "Invalid source_angle: " + sourceAngle + " (expected angle_x or angle_y)";
        if (context) context->SetValue("planar_adjustment.result.success", false);
        return false;
    }

    // --- Calculate correction ---
    double correctionDeg = PlanarAdjustmentMath::GetCorrectionValue(angleDeg, reverseSign);

    std::cout << "[PlanarAdjustment] Correction: " << correctionDeg << " deg -> "
              << targetDevice << " " << targetAxis << std::endl;

    // --- Apply correction ---
    // Skip move if correction is essentially zero (< 0.0001 deg)
    bool moveOk = true;
    if (std::abs(correctionDeg) >= 0.0001) {
        moveOk = m_appContext->motionManager->MoveRelative(
            targetDevice, targetAxis, correctionDeg, true);

        if (!moveOk) {
            errorMessage = "MoveRelative failed for " + targetDevice + " " + targetAxis;
            if (context) context->SetValue("planar_adjustment.result.success", false);
            return false;
        }
    } else {
        std::cout << "[PlanarAdjustment] Correction is ~0 deg, skipping move" << std::endl;
    }

    // --- Store results ---
    if (context) {
        context->SetValue("planar_adjustment.result.success", true);
        context->SetValue("planar_adjustment.result.training_set", tsName);
        context->SetValue("planar_adjustment.result.angle_x_deg",
                          std::to_string(set.angleX_deg));
        context->SetValue("planar_adjustment.result.angle_y_deg",
                          std::to_string(set.angleY_deg));
        context->SetValue("planar_adjustment.result.correction_deg",
                          std::to_string(correctionDeg));
        context->SetValue("planar_adjustment.result.target_device", targetDevice);
        context->SetValue("planar_adjustment.result.target_axis", targetAxis);
        context->SetValue("planar_adjustment.result.reverse_sign",
                          reverseSign ? "true" : "false");
    }

    // --- Save updated training set (if retrace was performed) ---
    if (retracePerformed) {
        set.correction.applied = true;
        set.correction.appliedValue = correctionDeg;
        if (store.SaveSet(set)) {
            std::cout << "[PlanarAdjustment] Updated training set saved: " << tsName << std::endl;
        } else {
            std::cout << "[PlanarAdjustment] WARNING: Failed to save updated training set" << std::endl;
        }
    }

    std::cout << "[PlanarAdjustment] Correction applied successfully: "
              << correctionDeg << " deg to " << targetDevice << " " << targetAxis << std::endl;
    return true;
}
