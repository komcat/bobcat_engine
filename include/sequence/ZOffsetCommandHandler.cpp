// ZOffsetCommandHandler.cpp
// Headless Z offset correction for recipe/sequence execution
#include "ZOffsetCommandHandler.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "SequenceContext.h"
#include "../AppUI/ZOffsetTrainer/ZOffsetData.h"
#include "../AppUI/ZOffsetTrainer/ZOffsetStore.h"
#include "product_position_manager.h"
#include "TransformationMatrixLoader.h"
#include "CoordinateTransformer.h"
#include <iostream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <cmath>

// ============================================================================
// ExecuteCommand - Route to the appropriate handler
// ============================================================================

bool ZOffsetCommandHandler::ExecuteCommand(
    void* /*manager*/,
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    if (methodName == "RunZOffset") {
        return HandleRunZOffset(params, errorMessage, context);
    }

    errorMessage = "Unknown ZOffset command: " + methodName;
    return false;
}

// ============================================================================
// HandleRunZOffset
// ============================================================================

bool ZOffsetCommandHandler::HandleRunZOffset(
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

    double targetZDiffUm = 150.0;
    auto tzIt = params.find("target_z_diff_um");
    if (tzIt != params.end() && !tzIt->second.empty()) {
        try {
            targetZDiffUm = std::stod(tzIt->second);
        } catch (...) {
            errorMessage = "Invalid target_z_diff_um value: " + tzIt->second;
            return false;
        }
    }

    bool reverseSign = false;
    auto rsIt = params.find("reverse_sign");
    if (rsIt != params.end() && rsIt->second == "true") {
        reverseSign = true;
    }

    std::cout << "[ZOffset] RunZOffset: set=" << tsName
              << " target=" << targetDevice
              << " targetZDiff=" << targetZDiffUm << "um"
              << " reverse=" << (reverseSign ? "true" : "false") << std::endl;

    // --- Load training set ---
    ZOffsetStore store;
    if (m_appContext && m_appContext->productPositionManager &&
        m_appContext->productPositionManager->HasLoadedProduct()) {
        std::string productPath = m_appContext->productPositionManager->GetCurrentProductPath();
        std::filesystem::path productDir = std::filesystem::path(productPath).parent_path();
        store.SetBasePath((productDir / "z_offset").string());
        std::cout << "[ZOffset] Store path: " << store.GetBasePath() << std::endl;
    }
    auto optSet = store.LoadSet(tsName);
    if (!optSet.has_value()) {
        errorMessage = "Failed to load training set: " + tsName;
        if (context) context->SetValue("z_offset.result.success", false);
        return false;
    }
    ZOffsetSet set = std::move(optSet.value());

    // --- Validate ---
    if (!set.reference.valid || !set.target.valid) {
        errorMessage = "Training set '" + tsName + "' has incomplete points";
        if (context) context->SetValue("z_offset.result.success", false);
        return false;
    }

    if (!m_appContext || !m_appContext->motionManager) {
        errorMessage = "Motion manager not available";
        if (context) context->SetValue("z_offset.result.success", false);
        return false;
    }

    // --- Live retrace using ManualAdjust callback ---
    bool retracePerformed = false;
    if (m_manualAdjustCallback) {
        // --- Step 1: Navigate to saved Reference surface, then prompt user ---
        std::cout << "[ZOffset] Navigating to saved Reference: X=" << set.reference.x
                  << " Y=" << set.reference.y << " Z=" << set.reference.z << std::endl;

        bool navRefOk = m_appContext->motionManager->MoveToAbs(
            set.deviceName, set.reference.x, set.reference.y, set.reference.z, 100.0, true);
        if (!navRefOk) {
            std::cout << "[ZOffset] WARNING: Failed to navigate to Reference position, continuing with manual adjust" << std::endl;
        }

        std::ostringstream refMsg;
        refMsg << std::fixed << std::setprecision(4);
        refMsg << "Z Offset Retrace - Confirm Reference Surface\n\n"
               << "Saved Reference: Z=" << set.reference.z
               << "  (X=" << set.reference.x << ", Y=" << set.reference.y << ")"
               << "\n\nJog to Reference surface, then click OK.";

        bool refOk = m_manualAdjustCallback(refMsg.str(), set.deviceName, 10.0, true, true, true, {});
        if (!refOk) {
            errorMessage = "User cancelled retrace at Reference surface";
            if (context) context->SetValue("z_offset.result.success", false);
            return false;
        }

        // Capture current position as new reference
        auto refPos = m_appContext->motionManager->GetCurrentPosition(set.deviceName);
        set.reference.x = refPos.x;
        set.reference.y = refPos.y;
        set.reference.z = refPos.z;
        set.reference.valid = true;

        std::cout << "[ZOffset] Reference confirmed: Z=" << refPos.z << std::endl;

        // --- Step 2: Navigate to saved Target surface, then prompt user ---
        std::cout << "[ZOffset] Navigating to saved Target: X=" << set.target.x
                  << " Y=" << set.target.y << " Z=" << set.target.z << std::endl;

        bool navTgtOk = m_appContext->motionManager->MoveToAbs(
            set.deviceName, set.target.x, set.target.y, set.target.z, 100.0, true);
        if (!navTgtOk) {
            std::cout << "[ZOffset] WARNING: Failed to navigate to Target position, continuing with manual adjust" << std::endl;
        }

        std::ostringstream tgtMsg;
        tgtMsg << std::fixed << std::setprecision(4);
        tgtMsg << "Z Offset Retrace - Confirm Target Surface\n\n"
               << "Saved Target: Z=" << set.target.z
               << "  (X=" << set.target.x << ", Y=" << set.target.y << ")"
               << "\n\nJog to Target surface, then click OK.";

        bool tgtOk = m_manualAdjustCallback(tgtMsg.str(), set.deviceName, 10.0, true, true, true, {});
        if (!tgtOk) {
            errorMessage = "User cancelled retrace at Target surface";
            if (context) context->SetValue("z_offset.result.success", false);
            return false;
        }

        // Capture current position as new target
        auto tgtPos = m_appContext->motionManager->GetCurrentPosition(set.deviceName);
        set.target.x = tgtPos.x;
        set.target.y = tgtPos.y;
        set.target.z = tgtPos.z;
        set.target.valid = true;

        std::cout << "[ZOffset] Target confirmed: Z=" << tgtPos.z << std::endl;

        retracePerformed = true;
    }

    // --- Compute Z difference ---
    set.RecalculateZDiff();

    std::cout << "[ZOffset] Measured Z diff: " << set.measuredZDiffMm << " mm ("
              << (set.measuredZDiffMm * 1000.0) << " um)" << std::endl;

    if (!set.zDiffValid) {
        errorMessage = "Z difference could not be computed (points not valid)";
        if (context) context->SetValue("z_offset.result.success", false);
        return false;
    }

    // --- Calculate correction ---
    double correctionMm = ZOffsetMath::ComputeCorrection(set.measuredZDiffMm, targetZDiffUm, reverseSign);

    std::cout << "[ZOffset] Correction: " << correctionMm << " mm -> "
              << targetDevice << " Z" << std::endl;

    // --- Apply correction (global Z -> transform to local hex axes) ---
    bool moveOk = true;
    if (std::abs(correctionMm) >= 0.0001) {
        // Load transformation matrix for hex device
        TransformationMatrixLoader matrixLoader;
        bool matrixLoaded = matrixLoader.LoadFromFile("config/transformation_matrix.json");
        if (!matrixLoaded) {
            errorMessage = "Could not load transformation_matrix.json for coordinate transform";
            if (context) context->SetValue("z_offset.result.success", false);
            return false;
        }

        const auto* matrix = matrixLoader.GetMatrix(targetDevice);
        if (!matrix) {
            errorMessage = "No transformation matrix found for device: " + targetDevice;
            if (context) context->SetValue("z_offset.result.success", false);
            return false;
        }

        // Transform global Z correction to local hex axes
        CoordinateTransformer::Vector6D zGlobalDelta(0.0, 0.0, correctionMm, 0.0, 0.0, 0.0);
        auto zLocalDelta = CoordinateTransformer::TransformGlobalToLocal(zGlobalDelta, *matrix);

        std::cout << "[ZOffset] Global Z correction: " << correctionMm << " mm" << std::endl;
        std::cout << "[ZOffset] Local delta: dX=" << zLocalDelta.x
                  << " dY=" << zLocalDelta.y << " dZ=" << zLocalDelta.z << std::endl;

        // Apply via MoveToAbs6 (adds local deltas to current position)
        auto curPos = m_appContext->motionManager->GetCurrentPosition(targetDevice);
        PositionStruct targetPos;
        targetPos.x = curPos.x + zLocalDelta.x;
        targetPos.y = curPos.y + zLocalDelta.y;
        targetPos.z = curPos.z + zLocalDelta.z;
        targetPos.u = curPos.u;
        targetPos.v = curPos.v;
        targetPos.w = curPos.w;

        std::cout << "[ZOffset] Hex target: X=" << targetPos.x
                  << " Y=" << targetPos.y << " Z=" << targetPos.z << std::endl;

        moveOk = m_appContext->motionManager->MoveToAbs6(targetDevice, targetPos, true);

        if (!moveOk) {
            errorMessage = "MoveToAbs6 failed for " + targetDevice;
            if (context) context->SetValue("z_offset.result.success", false);
            return false;
        }
    } else {
        std::cout << "[ZOffset] Correction is ~0 mm, skipping move" << std::endl;
    }

    // --- Store results ---
    if (context) {
        context->SetValue("z_offset.result.success", true);
        context->SetValue("z_offset.result.training_set", tsName);
        context->SetValue("z_offset.result.measured_z_diff_mm",
                          std::to_string(set.measuredZDiffMm));
        context->SetValue("z_offset.result.target_z_diff_um",
                          std::to_string(targetZDiffUm));
        context->SetValue("z_offset.result.correction_mm",
                          std::to_string(correctionMm));
        context->SetValue("z_offset.result.target_device", targetDevice);
        context->SetValue("z_offset.result.reverse_sign",
                          reverseSign ? "true" : "false");
    }

    // --- Save updated training set (if retrace was performed) ---
    if (retracePerformed) {
        set.correction.applied = true;
        set.correction.appliedValueMm = correctionMm;
        set.correction.targetDevice = targetDevice;
        set.correction.targetZDiffUm = targetZDiffUm;
        set.correction.reverseSign = reverseSign;
        if (store.SaveSet(set)) {
            std::cout << "[ZOffset] Updated training set saved: " << tsName << std::endl;
        } else {
            std::cout << "[ZOffset] WARNING: Failed to save updated training set" << std::endl;
        }
    }

    std::cout << "[ZOffset] Correction applied successfully: "
              << correctionMm << " mm to " << targetDevice << " Z" << std::endl;
    return true;
}
