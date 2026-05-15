// YawTrainerCommandHandler.cpp
// Headless yaw correction for recipe/sequence execution
#include "YawTrainerCommandHandler.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "SequenceContext.h"
#include "../AppUI/YawTrainer/YawTrainerData.h"
#include "../AppUI/YawTrainer/YawTrainerStore.h"
#include "product_position_manager.h"
#include <iostream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <cmath>

// ============================================================================
// ExecuteCommand - Route to the appropriate handler
// ============================================================================

bool YawTrainerCommandHandler::ExecuteCommand(
    void* /*manager*/,
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    if (methodName == "RunYawCorrection") {
        return HandleRunYawCorrection(params, errorMessage, context);
    }

    errorMessage = "Unknown YawTrainer command: " + methodName;
    return false;
}

// ============================================================================
// HandleRunYawCorrection
// ============================================================================

bool YawTrainerCommandHandler::HandleRunYawCorrection(
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

    auto tdIt = params.find("target_device");
    if (tdIt == params.end() || tdIt->second.empty()) {
        errorMessage = "Missing required parameter: target_device";
        return false;
    }
    const std::string& targetDevice = tdIt->second;

    std::string targetAxis = "W";
    auto taIt = params.find("target_axis");
    if (taIt != params.end() && !taIt->second.empty()) {
        targetAxis = taIt->second;
    }

    bool reverseSign = false;
    auto rsIt = params.find("reverse_sign");
    if (rsIt != params.end() && rsIt->second == "true") {
        reverseSign = true;
    }

    std::cout << "[YawTrainer] RunYawCorrection: set=" << tsName
              << " target=" << targetDevice << "/" << targetAxis
              << " reverse=" << (reverseSign ? "true" : "false") << std::endl;

    // --- Load training set ---
    YawTrainerStore store;
    if (m_appContext && m_appContext->productPositionManager &&
        m_appContext->productPositionManager->HasLoadedProduct()) {
        std::string productPath = m_appContext->productPositionManager->GetCurrentProductPath();
        std::filesystem::path productDir = std::filesystem::path(productPath).parent_path();
        store.SetBasePath((productDir / "yaw_trainer").string());
        std::cout << "[YawTrainer] Store path: " << store.GetBasePath() << std::endl;
    }
    auto optSet = store.LoadSet(tsName);
    if (!optSet.has_value()) {
        errorMessage = "Failed to load training set: " + tsName;
        if (context) context->SetValue("yaw_trainer.result.success", false);
        return false;
    }
    YawTrainerSet set = std::move(optSet.value());

    // --- Validate ---
    if (!set.referenceLine.valid || !set.targetLine.valid) {
        errorMessage = "Training set '" + tsName + "' has incomplete lines";
        if (context) context->SetValue("yaw_trainer.result.success", false);
        return false;
    }

    if (!m_appContext || !m_appContext->motionManager) {
        errorMessage = "Motion manager not available";
        if (context) context->SetValue("yaw_trainer.result.success", false);
        return false;
    }

    // --- Live retrace using ManualAdjust callback (4 points) ---
    bool retracePerformed = false;
    if (m_manualAdjustCallback) {
        // Save taught deltas BEFORE any overwrite
        double refDeltaX = set.referenceLine.p2x - set.referenceLine.p1x;
        double refDeltaY = set.referenceLine.p2y - set.referenceLine.p1y;
        double refDeltaZ = set.referenceLine.p2z - set.referenceLine.p1z;
        double tgtDeltaX = set.targetLine.p2x - set.targetLine.p1x;
        double tgtDeltaY = set.targetLine.p2y - set.targetLine.p1y;
        double tgtDeltaZ = set.targetLine.p2z - set.targetLine.p1z;

        std::cout << "[YawTrainer] Live retrace: ref delta dX=" << refDeltaX
                  << " dY=" << refDeltaY << " | tgt delta dX=" << tgtDeltaX
                  << " dY=" << tgtDeltaY << std::endl;

        // --- Step 1: Ref P1 --- Auto-navigate to taught position first
        {
            std::cout << "[YawTrainer] Auto-navigating to Ref P1: X="
                      << set.referenceLine.p1x << " Y=" << set.referenceLine.p1y
                      << " Z=" << set.referenceLine.p1z << std::endl;
            bool navOk = m_appContext->motionManager->MoveToAbs(
                set.deviceName,
                set.referenceLine.p1x, set.referenceLine.p1y, set.referenceLine.p1z,
                100.0, true);
            if (!navOk) {
                std::cout << "[YawTrainer] WARNING: Auto-navigate to Ref P1 failed" << std::endl;
            }
        }

        std::ostringstream refP1Msg;
        refP1Msg << std::fixed << std::setprecision(4);
        refP1Msg << "Yaw Retrace - Reference Line Point 1\n\n"
                 << "Saved Ref P1: X=" << set.referenceLine.p1x
                 << "  Y=" << set.referenceLine.p1y
                 << "\n\nJog to Reference P1, then click OK.";

        bool p1Ok = m_manualAdjustCallback(refP1Msg.str(), set.deviceName, 10.0, true, true, true, {});
        if (!p1Ok) {
            errorMessage = "User cancelled retrace at Reference P1";
            if (context) context->SetValue("yaw_trainer.result.success", false);
            return false;
        }

        auto refP1Pos = m_appContext->motionManager->GetCurrentPosition(set.deviceName);
        set.referenceLine.p1x = refP1Pos.x;
        set.referenceLine.p1y = refP1Pos.y;
        set.referenceLine.p1z = refP1Pos.z;

        // --- Auto-move to Ref P2 ---
        bool moveOk = true;
        if (std::abs(refDeltaX) > 1e-6)
            moveOk = m_appContext->motionManager->MoveRelative(set.deviceName, "X", refDeltaX, true);
        if (moveOk && std::abs(refDeltaY) > 1e-6)
            moveOk = m_appContext->motionManager->MoveRelative(set.deviceName, "Y", refDeltaY, true);
        if (moveOk && std::abs(refDeltaZ) > 1e-6)
            moveOk = m_appContext->motionManager->MoveRelative(set.deviceName, "Z", refDeltaZ, true);

        if (!moveOk) {
            errorMessage = "Failed to move to Ref P2 area";
            if (context) context->SetValue("yaw_trainer.result.success", false);
            return false;
        }

        // --- Step 2: Ref P2 ---
        std::ostringstream refP2Msg;
        refP2Msg << "Yaw Retrace - Reference Line Point 2\n\n"
                 << "System moved by taught delta to Ref P2 area.\n"
                 << "Adjust if needed, then click OK.";

        bool p2Ok = m_manualAdjustCallback(refP2Msg.str(), set.deviceName, 10.0, true, true, true, {});
        if (!p2Ok) {
            errorMessage = "User cancelled retrace at Reference P2";
            if (context) context->SetValue("yaw_trainer.result.success", false);
            return false;
        }

        auto refP2Pos = m_appContext->motionManager->GetCurrentPosition(set.deviceName);
        set.referenceLine.p2x = refP2Pos.x;
        set.referenceLine.p2y = refP2Pos.y;
        set.referenceLine.p2z = refP2Pos.z;
        set.referenceLine.valid = true;

        // --- Step 3: Tgt P1 --- Auto-navigate to taught position first
        {
            std::cout << "[YawTrainer] Auto-navigating to Tgt P1: X="
                      << set.targetLine.p1x << " Y=" << set.targetLine.p1y
                      << " Z=" << set.targetLine.p1z << std::endl;
            bool navOk = m_appContext->motionManager->MoveToAbs(
                set.deviceName,
                set.targetLine.p1x, set.targetLine.p1y, set.targetLine.p1z,
                100.0, true);
            if (!navOk) {
                std::cout << "[YawTrainer] WARNING: Auto-navigate to Tgt P1 failed" << std::endl;
            }
        }

        std::ostringstream tgtP1Msg;
        tgtP1Msg << std::fixed << std::setprecision(4);
        tgtP1Msg << "Yaw Retrace - Target Line Point 1\n\n"
                 << "Saved Tgt P1: X=" << set.targetLine.p1x
                 << "  Y=" << set.targetLine.p1y
                 << "\n\nJog to Target P1, then click OK.";

        bool t1Ok = m_manualAdjustCallback(tgtP1Msg.str(), set.deviceName, 10.0, true, true, true, {});
        if (!t1Ok) {
            errorMessage = "User cancelled retrace at Target P1";
            if (context) context->SetValue("yaw_trainer.result.success", false);
            return false;
        }

        auto tgtP1Pos = m_appContext->motionManager->GetCurrentPosition(set.deviceName);
        set.targetLine.p1x = tgtP1Pos.x;
        set.targetLine.p1y = tgtP1Pos.y;
        set.targetLine.p1z = tgtP1Pos.z;

        // --- Auto-move to Tgt P2 ---
        moveOk = true;
        if (std::abs(tgtDeltaX) > 1e-6)
            moveOk = m_appContext->motionManager->MoveRelative(set.deviceName, "X", tgtDeltaX, true);
        if (moveOk && std::abs(tgtDeltaY) > 1e-6)
            moveOk = m_appContext->motionManager->MoveRelative(set.deviceName, "Y", tgtDeltaY, true);
        if (moveOk && std::abs(tgtDeltaZ) > 1e-6)
            moveOk = m_appContext->motionManager->MoveRelative(set.deviceName, "Z", tgtDeltaZ, true);

        if (!moveOk) {
            errorMessage = "Failed to move to Tgt P2 area";
            if (context) context->SetValue("yaw_trainer.result.success", false);
            return false;
        }

        // --- Step 4: Tgt P2 ---
        std::ostringstream tgtP2Msg;
        tgtP2Msg << "Yaw Retrace - Target Line Point 2\n\n"
                 << "System moved by taught delta to Tgt P2 area.\n"
                 << "Adjust if needed, then click OK.";

        bool t2Ok = m_manualAdjustCallback(tgtP2Msg.str(), set.deviceName, 10.0, true, true, true, {});
        if (!t2Ok) {
            errorMessage = "User cancelled retrace at Target P2";
            if (context) context->SetValue("yaw_trainer.result.success", false);
            return false;
        }

        auto tgtP2Pos = m_appContext->motionManager->GetCurrentPosition(set.deviceName);
        set.targetLine.p2x = tgtP2Pos.x;
        set.targetLine.p2y = tgtP2Pos.y;
        set.targetLine.p2z = tgtP2Pos.z;
        set.targetLine.valid = true;

        retracePerformed = true;
    }

    // --- Compute delta angle ---
    set.RecalculateDelta();

    std::cout << "[YawTrainer] Ref angle: " << set.referenceLine.angleDeg
              << " deg, Tgt angle: " << set.targetLine.angleDeg
              << " deg, Delta: " << set.deltaAngleDeg << " deg" << std::endl;

    if (!set.deltaValid) {
        errorMessage = "Delta angle is not valid";
        if (context) context->SetValue("yaw_trainer.result.success", false);
        return false;
    }

    // --- Calculate correction ---
    double correctionDeg = YawTrainerMath::GetCorrectionValue(set.deltaAngleDeg, reverseSign);

    std::cout << "[YawTrainer] Correction: " << correctionDeg << " deg -> "
              << targetDevice << " " << targetAxis << std::endl;

    // --- Apply correction ---
    bool moveOk = true;
    if (std::abs(correctionDeg) >= 0.0001) {
        moveOk = m_appContext->motionManager->MoveRelative(
            targetDevice, targetAxis, correctionDeg, true);

        if (!moveOk) {
            errorMessage = "MoveRelative failed for " + targetDevice + " " + targetAxis;
            if (context) context->SetValue("yaw_trainer.result.success", false);
            return false;
        }
    } else {
        std::cout << "[YawTrainer] Correction is ~0 deg, skipping move" << std::endl;
    }

    // --- Store results ---
    if (context) {
        context->SetValue("yaw_trainer.result.success", true);
        context->SetValue("yaw_trainer.result.training_set", tsName);
        context->SetValue("yaw_trainer.result.ref_angle_deg",
                          std::to_string(set.referenceLine.angleDeg));
        context->SetValue("yaw_trainer.result.tgt_angle_deg",
                          std::to_string(set.targetLine.angleDeg));
        context->SetValue("yaw_trainer.result.delta_deg",
                          std::to_string(set.deltaAngleDeg));
        context->SetValue("yaw_trainer.result.correction_deg",
                          std::to_string(correctionDeg));
        context->SetValue("yaw_trainer.result.target_device", targetDevice);
        context->SetValue("yaw_trainer.result.target_axis", targetAxis);
        context->SetValue("yaw_trainer.result.reverse_sign",
                          reverseSign ? "true" : "false");
    }

    // --- Save updated training set (if retrace was performed) ---
    if (retracePerformed) {
        set.correction.applied = true;
        set.correction.appliedValue = correctionDeg;
        if (store.SaveSet(set)) {
            std::cout << "[YawTrainer] Updated training set saved: " << tsName << std::endl;
        } else {
            std::cout << "[YawTrainer] WARNING: Failed to save updated training set" << std::endl;
        }
    }

    std::cout << "[YawTrainer] Correction applied successfully: "
              << correctionDeg << " deg to " << targetDevice << " " << targetAxis << std::endl;
    return true;
}
