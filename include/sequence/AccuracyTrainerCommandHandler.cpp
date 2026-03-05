// AccuracyTrainerCommandHandler.cpp
// Headless accuracy trainer alignment for recipe/sequence execution
#include "AccuracyTrainerCommandHandler.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "SequenceContext.h"
#include "../AppUI/AccuracyTrainer/AccuracyTrainerData.h"
#include "../AppUI/AccuracyTrainer/AccuracyTrainerStore.h"
#include "../motions/TransformationMatrixLoader.h"
#include "../motions/CoordinateTransformer.h"
#include "product_position_manager.h"
#include "LogManager.h"
#include <iostream>
#include <cmath>
#include <filesystem>

#ifdef HAS_OPENCV
#include "VisionCommandHandler.h"
#include <nlohmann/json.hpp>
#include <fstream>
#endif

// ============================================================================
// ExecuteCommand - Route to the appropriate handler
// ============================================================================

bool AccuracyTrainerCommandHandler::ExecuteCommand(
    void* /*manager*/,
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    if (methodName == "RunAlignment") {
        return HandleRunAlignment(params, errorMessage, context);
    }

    errorMessage = "Unknown AccuracyTrainer command: " + methodName;
    return false;
}

// ============================================================================
// HandleRunAlignment - Full alignment workflow
// ============================================================================

bool AccuracyTrainerCommandHandler::HandleRunAlignment(
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

    bool registerHexLeft = false;
    auto hexLeftIt = params.find("register_hex_left");
    if (hexLeftIt != params.end() && hexLeftIt->second == "true") {
        registerHexLeft = true;
    }

    bool registerHexRight = false;
    auto hexRightIt = params.find("register_hex_right");
    if (hexRightIt != params.end() && hexRightIt->second == "true") {
        registerHexRight = true;
    }

    bool moveToBonding = true;
    auto bondIt = params.find("move_to_bonding");
    if (bondIt != params.end() && bondIt->second == "false") {
        moveToBonding = false;
    }

    std::cout << "[AccuracyTrainer] RunAlignment: set=" << tsName
              << " hexL=" << (registerHexLeft ? "true" : "false")
              << " hexR=" << (registerHexRight ? "true" : "false")
              << " bond=" << (moveToBonding ? "true" : "false") << std::endl;

    // --- Load training set from active product folder ---
    AccuracyTrainerStore store;
    if (m_appContext && m_appContext->productPositionManager &&
        m_appContext->productPositionManager->HasLoadedProduct()) {
        std::string productPath = m_appContext->productPositionManager->GetCurrentProductPath();
        std::filesystem::path productDir = std::filesystem::path(productPath).parent_path();
        store.SetBasePath((productDir / "accuracy_trainer").string());
        std::cout << "[AccuracyTrainer] Store path: " << store.GetBasePath() << std::endl;
    }
    auto optTs = store.LoadTrainingSet(tsName);
    if (!optTs.has_value()) {
        errorMessage = "Failed to load training set: " + tsName;
        if (context) context->SetValue("accuracy_trainer.result.success", false);
        return false;
    }
    TrainingSet ts = std::move(optTs.value());

    // --- Validate ---
    if (!ts.valid) {
        errorMessage = "Training set '" + tsName + "' is not valid (frame not computed)";
        if (context) context->SetValue("accuracy_trainer.result.success", false);
        return false;
    }
    if (ts.referencePoints.size() < 2) {
        errorMessage = "Training set '" + tsName + "' needs at least 2 reference points (has "
                       + std::to_string(ts.referencePoints.size()) + ")";
        if (context) context->SetValue("accuracy_trainer.result.success", false);
        return false;
    }
    if (ts.bondingPoints.empty()) {
        errorMessage = "Training set '" + tsName + "' has no bonding points";
        if (context) context->SetValue("accuracy_trainer.result.success", false);
        return false;
    }

    if (!m_appContext || !m_appContext->motionManager) {
        errorMessage = "Motion manager not available";
        if (context) context->SetValue("accuracy_trainer.result.success", false);
        return false;
    }

    const std::string& device = ts.deviceName;
    auto* motionMgr = m_appContext->motionManager;

    // ========================================================================
    // PHASE 1: Reference alignment
    // ========================================================================
    std::vector<ReferencePoint> actualRefs;

    for (size_t i = 0; i < ts.referencePoints.size(); ++i) {
        const auto& ref = ts.referencePoints[i];
        std::cout << "[AccuracyTrainer] Ref " << (i + 1) << "/" << ts.referencePoints.size()
                  << ": " << ref.name << " -> (" << ref.globalX << ", " << ref.globalY
                  << ", " << ref.globalZ << ")" << std::endl;

        // Move gantry to taught ref position
        bool moveOk = motionMgr->MoveToAbs(device, ref.globalX, ref.globalY, ref.globalZ, 100.0, true);
        if (!moveOk) {
            std::cout << "[AccuracyTrainer] Move to ref failed, will try manual fallback" << std::endl;
        }

        // Check stop between operations
        if (context && context->IsStopRequestedAny()) {
            errorMessage = "Alignment stopped by user";
            context->SetValue("accuracy_trainer.result.success", false);
            return false;
        }

        bool positionCaptured = false;

        // Try vision auto-detect if profile exists
        if (!ref.visionProfilePath.empty()) {
            std::cout << "[AccuracyTrainer] Running vision profile for " << ref.name << std::endl;
            std::string visionError;
            bool visionOk = RunVisionDetectCenter(ref.visionProfilePath, device, visionError);

            if (visionOk) {
                std::cout << "[AccuracyTrainer] Vision succeeded for " << ref.name << std::endl;
                positionCaptured = true;
            } else {
                std::cout << "[AccuracyTrainer] Vision failed for " << ref.name
                          << ": " << visionError << " - falling back to manual" << std::endl;
            }
        }

        // Manual fallback if vision failed or no profile
        if (!positionCaptured) {
            if (m_manualAlignCallback) {
                std::string msg = "Align to reference point " + ref.name
                    + " (" + std::to_string(i + 1) + " of "
                    + std::to_string(ts.referencePoints.size()) + ")\n"
                    + "Jog gantry to center on the reference feature.";
                bool userOk = m_manualAlignCallback(ref.name, msg, true, true, true);
                if (!userOk) {
                    errorMessage = "User cancelled alignment at reference " + ref.name;
                    if (context) context->SetValue("accuracy_trainer.result.success", false);
                    return false;
                }
                positionCaptured = true;
            } else {
                // No manual callback and no vision - cannot proceed
                errorMessage = "No vision profile and no manual callback for ref " + ref.name;
                if (context) context->SetValue("accuracy_trainer.result.success", false);
                return false;
            }
        }

        // Capture current position
        auto pos = motionMgr->GetCurrentPosition(device);
        ReferencePoint actual;
        actual.name = ref.name;
        actual.globalX = pos.x;
        actual.globalY = pos.y;
        actual.globalZ = pos.z;
        actual.valid = true;
        actualRefs.push_back(actual);

        double dx = pos.x - ref.globalX;
        double dy = pos.y - ref.globalY;
        std::cout << "[AccuracyTrainer] Captured " << ref.name
                  << ": X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z
                  << " (delta: dX=" << dx << " dY=" << dy << ")" << std::endl;
    }

    // ========================================================================
    // PHASE 2: Compute correction transform
    // ========================================================================
    std::cout << "[AccuracyTrainer] Computing correction transform..." << std::endl;

    double liveOriginX = 0.0, liveOriginY = 0.0;
    double liveXAxisX = 1.0, liveXAxisY = 0.0;
    double liveYAxisX = 0.0, liveYAxisY = 1.0;

    AccuracyTrainerMath::ComputeCoordinateAxes(
        actualRefs[0].globalX, actualRefs[0].globalY,
        actualRefs[1].globalX, actualRefs[1].globalY,
        liveOriginX, liveOriginY,
        liveXAxisX, liveXAxisY,
        liveYAxisX, liveYAxisY);

    std::cout << "[AccuracyTrainer] Live origin: (" << liveOriginX << ", " << liveOriginY << ")"
              << " X-axis: (" << liveXAxisX << ", " << liveXAxisY << ")" << std::endl;

    // Optional scale correction if 3+ refs
    double scale = 1.0;
    if (actualRefs.size() >= 3 && ts.referencePoints.size() >= 3) {
        double taughtDist = AccuracyTrainerMath::Distance(
            ts.referencePoints[0].globalX, ts.referencePoints[0].globalY,
            ts.referencePoints[1].globalX, ts.referencePoints[1].globalY);
        double liveDist = AccuracyTrainerMath::Distance(
            actualRefs[0].globalX, actualRefs[0].globalY,
            actualRefs[1].globalX, actualRefs[1].globalY);

        if (taughtDist > 1e-6) {
            scale = liveDist / taughtDist;
            std::cout << "[AccuracyTrainer] Scale factor: " << scale
                      << " (taught=" << taughtDist << "mm, live=" << liveDist << "mm)" << std::endl;
        }
    }

    // ========================================================================
    // PHASE 3: Compute corrected bonding positions
    // ========================================================================
    struct CorrectedBond {
        std::string name;
        double correctedX = 0.0;
        double correctedY = 0.0;
        double correctedZ = 0.0;
        double deltaX = 0.0;
        double deltaY = 0.0;
        std::string assignedDevice;
    };

    std::vector<CorrectedBond> correctedBonds;

    for (const auto& bp : ts.bondingPoints) {
        double scaledLocalX = bp.localX * scale;
        double scaledLocalY = bp.localY * scale;

        double corrX = 0.0, corrY = 0.0;
        AccuracyTrainerMath::LocalToWorld(
            scaledLocalX, scaledLocalY,
            liveOriginX, liveOriginY,
            liveXAxisX, liveXAxisY,
            liveYAxisX, liveYAxisY,
            corrX, corrY);

        CorrectedBond cb;
        cb.name = bp.name;
        cb.correctedX = corrX;
        cb.correctedY = corrY;
        cb.correctedZ = bp.globalZ;  // Keep original Z
        cb.deltaX = corrX - bp.globalX;
        cb.deltaY = corrY - bp.globalY;

        // Check device assignment
        auto assignIt = ts.bondingDeviceAssignments.find(bp.name);
        cb.assignedDevice = (assignIt != ts.bondingDeviceAssignments.end())
            ? assignIt->second : device;

        correctedBonds.push_back(cb);

        std::cout << "[AccuracyTrainer] " << bp.name
                  << ": corrected(" << corrX << ", " << corrY << ")"
                  << " delta(" << cb.deltaX << ", " << cb.deltaY << ")"
                  << " device=" << cb.assignedDevice << std::endl;
    }

    // ========================================================================
    // PHASE 4: Hex device registration (per-device enable)
    // ========================================================================
    // Map of registered hex device name -> gantry center + hex 6-axis position
    struct RegisteredHexPos {
        double centerGantryX = 0.0, centerGantryY = 0.0, centerGantryZ = 0.0;  // Gantry coords at hex center
        double x = 0.0, y = 0.0, z = 0.0, u = 0.0, v = 0.0, w = 0.0;         // Hex device 6-axis position
    };
    std::map<std::string, RegisteredHexPos> registeredHexPositions;

    if ((registerHexLeft || registerHexRight) && !ts.hexDevicePositions.empty()) {
        std::cout << "[AccuracyTrainer] Hex device registration:"
                  << (registerHexLeft ? " hex-left" : "")
                  << (registerHexRight ? " hex-right" : "") << std::endl;

        for (size_t i = 0; i < ts.hexDevicePositions.size(); ++i) {
            const auto& hexDev = ts.hexDevicePositions[i];
            if (!hexDev.valid) continue;

            // Skip devices not enabled by the user
            bool isLeft = (hexDev.deviceName.find("left") != std::string::npos);
            bool isRight = (hexDev.deviceName.find("right") != std::string::npos);
            if (isLeft && !registerHexLeft) {
                std::cout << "[AccuracyTrainer] Skipping " << hexDev.deviceName << " (not enabled)" << std::endl;
                continue;
            }
            if (isRight && !registerHexRight) {
                std::cout << "[AccuracyTrainer] Skipping " << hexDev.deviceName << " (not enabled)" << std::endl;
                continue;
            }

            std::cout << "[AccuracyTrainer] Hex device " << (i + 1) << ": "
                      << hexDev.deviceName << std::endl;

            if (context && context->IsStopRequestedAny()) {
                errorMessage = "Alignment stopped by user during hex registration";
                context->SetValue("accuracy_trainer.result.success", false);
                return false;
            }

            // Move gantry to viewing position
            bool gantryOk = motionMgr->MoveToAbs(
                device, hexDev.gantryViewX, hexDev.gantryViewY, hexDev.gantryViewZ, 100.0, true);
            if (!gantryOk) {
                std::cout << "[AccuracyTrainer] WARNING: Gantry move failed for hex " << hexDev.deviceName << std::endl;
            }

            // Move hex device to taught 6-axis position (single multi-axis command)
            PositionStruct hexTarget;
            hexTarget.x = hexDev.hexX; hexTarget.y = hexDev.hexY; hexTarget.z = hexDev.hexZ;
            hexTarget.u = hexDev.hexU; hexTarget.v = hexDev.hexV; hexTarget.w = hexDev.hexW;
            bool hexOk = motionMgr->MoveToAbs6(hexDev.deviceName, hexTarget, true);
            if (!hexOk) {
                std::cout << "[AccuracyTrainer] WARNING: Hex move failed for " << hexDev.deviceName << std::endl;
            }

            // Try vision for hex if profile available
            bool hexCaptured = false;
            if (!hexDev.visionProfilePath.empty()) {
                std::string visionError;
                hexCaptured = RunVisionDetectCenter(hexDev.visionProfilePath, device, visionError);
                if (!hexCaptured) {
                    std::cout << "[AccuracyTrainer] Hex vision failed: " << visionError << std::endl;
                }
            }

            // Manual fallback for hex: 2-3 corner capture (same pattern as UI widget)
            if (!hexCaptured && m_manualAlignCallback) {
                std::cout << "[AccuracyTrainer] Starting 2-3 corner capture for " << hexDev.deviceName << std::endl;

                // Corner 1: Top-Left
                std::string msg1 = "Corner 1/3 (Top-Left): " + hexDev.deviceName
                    + "\nJog gantry camera crosshair to the TOP-LEFT corner of the end-effector tip.";
                bool userOk = m_manualAlignCallback(hexDev.deviceName, msg1, true, true, true);
                if (!userOk) {
                    std::cout << "[AccuracyTrainer] User cancelled hex corner 1 for " << hexDev.deviceName << std::endl;
                    continue;
                }
                auto cornerTL = motionMgr->GetCurrentPosition(device);
                std::cout << "[AccuracyTrainer] Corner 1 (TL): X=" << cornerTL.x
                          << " Y=" << cornerTL.y << " Z=" << cornerTL.z << std::endl;

                if (context && context->IsStopRequestedAny()) {
                    errorMessage = "Alignment stopped during hex corner capture";
                    context->SetValue("accuracy_trainer.result.success", false);
                    return false;
                }

                // Corner 2: Bottom-Right
                std::string msg2 = "Corner 2/3 (Bottom-Right): " + hexDev.deviceName
                    + "\nJog gantry camera crosshair to the BOTTOM-RIGHT corner of the end-effector tip.";
                userOk = m_manualAlignCallback(hexDev.deviceName, msg2, true, true, true);
                if (!userOk) {
                    std::cout << "[AccuracyTrainer] User cancelled hex corner 2 for " << hexDev.deviceName << std::endl;
                    continue;
                }
                auto cornerBR = motionMgr->GetCurrentPosition(device);
                std::cout << "[AccuracyTrainer] Corner 2 (BR): X=" << cornerBR.x
                          << " Y=" << cornerBR.y << std::endl;

                // Corner 3: Top-Right (optional)
                std::string msg3 = "Corner 3/3 (Top-Right, Optional): " + hexDev.deviceName
                    + "\nJog gantry camera crosshair to the TOP-RIGHT corner.\n"
                      "Click OK to capture, or Cancel to use 2-corner midpoint.";
                bool hasThirdCorner = m_manualAlignCallback(hexDev.deviceName, msg3, true, true, true);
                double cornerTR_X = 0.0, cornerTR_Y = 0.0;
                if (hasThirdCorner) {
                    auto cornerTR = motionMgr->GetCurrentPosition(device);
                    cornerTR_X = cornerTR.x;
                    cornerTR_Y = cornerTR.y;
                    std::cout << "[AccuracyTrainer] Corner 3 (TR): X=" << cornerTR_X
                              << " Y=" << cornerTR_Y << std::endl;
                } else {
                    std::cout << "[AccuracyTrainer] Skipped corner 3 (using 2-corner midpoint)" << std::endl;
                }

                // Compute center from corners (same math as UI widgets)
                double centerX = 0.0, centerY = 0.0;
                if (hasThirdCorner) {
                    double blX = cornerTL.x + cornerBR.x - cornerTR_X;
                    double blY = cornerTL.y + cornerBR.y - cornerTR_Y;
                    centerX = (cornerTL.x + cornerBR.x + cornerTR_X + blX) / 4.0;
                    centerY = (cornerTL.y + cornerBR.y + cornerTR_Y + blY) / 4.0;
                } else {
                    centerX = (cornerTL.x + cornerBR.x) / 2.0;
                    centerY = (cornerTL.y + cornerBR.y) / 2.0;
                }

                double dist = AccuracyTrainerMath::Distance(
                    cornerTL.x, cornerTL.y, cornerBR.x, cornerBR.y);
                std::cout << "[AccuracyTrainer] Computed center: X=" << centerX
                          << " Y=" << centerY << " diagonal=" << dist << "mm"
                          << (hasThirdCorner ? " (3-corner)" : " (2-corner)") << std::endl;

                // Move gantry to computed center
                bool moveOk = motionMgr->MoveToAbs(
                    device, centerX, centerY, cornerTL.z, 100.0, true);
                if (!moveOk) {
                    std::cout << "[AccuracyTrainer] WARNING: Move to corner center failed" << std::endl;
                }
                hexCaptured = true;
            }

            if (!hexCaptured) {
                std::cout << "[AccuracyTrainer] No vision and no manual callback for hex "
                          << hexDev.deviceName << " - skipping" << std::endl;
                continue;
            }

            // Capture gantry position (where gantry is centered on hex) and hex device position
            auto gantryPos = motionMgr->GetCurrentPosition(device);
            auto hexPos = motionMgr->GetCurrentPosition(hexDev.deviceName);
            RegisteredHexPos regPos;
            regPos.centerGantryX = gantryPos.x;
            regPos.centerGantryY = gantryPos.y;
            regPos.centerGantryZ = gantryPos.z;
            regPos.x = hexPos.x; regPos.y = hexPos.y; regPos.z = hexPos.z;
            regPos.u = hexPos.u; regPos.v = hexPos.v; regPos.w = hexPos.w;
            registeredHexPositions[hexDev.deviceName] = regPos;

            std::cout << "[AccuracyTrainer] Registered " << hexDev.deviceName
                      << ": gantryCenter=(" << gantryPos.x << ", " << gantryPos.y << ", " << gantryPos.z << ")"
                      << " hexPos=(" << hexPos.x << ", " << hexPos.y << ", " << hexPos.z << ")" << std::endl;

            // Store hex registration result in context
            if (context) {
                std::string prefix = "accuracy_trainer.result.hex_" + std::to_string(i);
                context->SetValue(prefix + ".device", hexDev.deviceName);
                context->SetValue(prefix + ".x", std::to_string(hexPos.x));
                context->SetValue(prefix + ".y", std::to_string(hexPos.y));
                context->SetValue(prefix + ".z", std::to_string(hexPos.z));
            }
        }
    }

    // ========================================================================
    // PHASE 5: Move to corrected bonding positions (optional)
    // ========================================================================
    if (moveToBonding && !correctedBonds.empty()) {
        std::cout << "[AccuracyTrainer] Moving to " << correctedBonds.size()
                  << " corrected bonding positions..." << std::endl;

        // Load transformation matrices for hex coordinate conversion
        TransformationMatrixLoader matrixLoader;
        if (!registeredHexPositions.empty()) {
            bool matrixLoaded = matrixLoader.LoadFromFile("config/transformation_matrix.json");
            if (!matrixLoaded) {
                std::cout << "[AccuracyTrainer] WARNING: Could not load transformation_matrix.json"
                          << " - hex moves will be skipped" << std::endl;
            }
        }

        for (size_t i = 0; i < correctedBonds.size(); ++i) {
            const auto& cb = correctedBonds[i];

            if (context && context->IsStopRequestedAny()) {
                errorMessage = "Alignment stopped by user during bonding moves";
                context->SetValue("accuracy_trainer.result.success", false);
                return false;
            }

            // Check if this bonding point is assigned to a hex device
            bool assignedToHex = (cb.assignedDevice != device);

            // If assigned to hex, only move if that hex was registered
            if (assignedToHex) {
                auto hexIt = registeredHexPositions.find(cb.assignedDevice);
                if (hexIt == registeredHexPositions.end()) {
                    std::cout << "[AccuracyTrainer] Skipping " << cb.name
                              << " (assigned to " << cb.assignedDevice << ", not registered)" << std::endl;
                    continue;
                }
            }

            std::cout << "[AccuracyTrainer] Moving gantry to " << cb.name
                      << ": (" << cb.correctedX << ", " << cb.correctedY
                      << ", " << cb.correctedZ << ")" << std::endl;

            // Move gantry to corrected bonding position
            bool gantryOk = motionMgr->MoveToAbs(
                device, cb.correctedX, cb.correctedY, cb.correctedZ, 100.0, true);
            if (!gantryOk) {
                std::cout << "[AccuracyTrainer] WARNING: Gantry move to " << cb.name << " failed" << std::endl;
            }

            // If assigned to a registered hex device, compute corrected hex position
            // using transformation matrix (same math as MultiDeviceRunAlignmentWidget)
            if (assignedToHex) {
                auto hexIt = registeredHexPositions.find(cb.assignedDevice);
                const auto& regHex = hexIt->second;
                const auto* matrix = matrixLoader.GetMatrix(cb.assignedDevice);

                if (!matrix) {
                    std::cout << "[AccuracyTrainer] WARNING: No transformation matrix for "
                              << cb.assignedDevice << " - skipping hex move" << std::endl;
                    continue;
                }

                // Delta in gantry-main XY plane (corrected bond vs hex registration center)
                double gantryDeltaX = cb.correctedX - regHex.centerGantryX;
                double gantryDeltaY = cb.correctedY - regHex.centerGantryY;

                std::cout << "[AccuracyTrainer] " << cb.assignedDevice
                          << " gantry delta: dX=" << gantryDeltaX << " dY=" << gantryDeltaY << std::endl;

                // Transform gantry delta to hex local coordinates via 3x3 matrix
                CoordinateTransformer::Vector6D globalDelta(gantryDeltaX, gantryDeltaY, 0.0, 0.0, 0.0, 0.0);
                auto localDelta = CoordinateTransformer::TransformGlobalToLocal(globalDelta, *matrix);

                // Get current hex position and add the local delta
                auto curHexPos = motionMgr->GetCurrentPosition(cb.assignedDevice);
                PositionStruct targetPos;
                targetPos.x = curHexPos.x + localDelta.x;
                targetPos.y = curHexPos.y + localDelta.y;
                targetPos.z = curHexPos.z + localDelta.z;
                targetPos.u = curHexPos.u + localDelta.u;
                targetPos.v = curHexPos.v + localDelta.v;
                targetPos.w = curHexPos.w + localDelta.w;

                std::cout << "[AccuracyTrainer] " << cb.assignedDevice
                          << " target: X=" << targetPos.x << " Y=" << targetPos.y
                          << " Z=" << targetPos.z << " U=" << targetPos.u
                          << " V=" << targetPos.v << " W=" << targetPos.w << std::endl;

                bool hexOk = motionMgr->MoveToAbs6(cb.assignedDevice, targetPos, true);
                if (!hexOk) {
                    std::cout << "[AccuracyTrainer] WARNING: " << cb.assignedDevice
                              << " move failed for " << cb.name << std::endl;
                }
            }
        }
    }

    // ========================================================================
    // PHASE 6: Store results in context
    // ========================================================================
    if (context) {
        context->SetValue("accuracy_trainer.result.success", true);
        context->SetValue("accuracy_trainer.result.training_set", tsName);
        context->SetValue("accuracy_trainer.result.ref_count",
                          std::to_string(actualRefs.size()));
        context->SetValue("accuracy_trainer.result.bond_count",
                          std::to_string(correctedBonds.size()));
        context->SetValue("accuracy_trainer.result.scale",
                          std::to_string(scale));

        // Live frame data
        context->SetValue("accuracy_trainer.result.live_origin_x",
                          std::to_string(liveOriginX));
        context->SetValue("accuracy_trainer.result.live_origin_y",
                          std::to_string(liveOriginY));

        // Per-bond results
        for (size_t i = 0; i < correctedBonds.size(); ++i) {
            const auto& cb = correctedBonds[i];
            std::string prefix = "accuracy_trainer.result.bond_" + std::to_string(i);
            context->SetValue(prefix + ".name", cb.name);
            context->SetValue(prefix + ".corrected_x", std::to_string(cb.correctedX));
            context->SetValue(prefix + ".corrected_y", std::to_string(cb.correctedY));
            context->SetValue(prefix + ".corrected_z", std::to_string(cb.correctedZ));
            context->SetValue(prefix + ".delta_x", std::to_string(cb.deltaX));
            context->SetValue(prefix + ".delta_y", std::to_string(cb.deltaY));
            context->SetValue(prefix + ".device", cb.assignedDevice);
        }
    }

    std::cout << "[AccuracyTrainer] Alignment complete. "
              << actualRefs.size() << " refs, "
              << correctedBonds.size() << " bonds corrected." << std::endl;
    return true;
}

// ============================================================================
// RunVisionDetectCenter - Vision auto-detect and move to center
// ============================================================================

bool AccuracyTrainerCommandHandler::RunVisionDetectCenter(
    const std::string& profilePath,
    const std::string& device,
    std::string& errorMessage)
{
#ifdef HAS_OPENCV
    if (!m_appContext) {
        errorMessage = "No application context";
        return false;
    }

    // Read camera name from profile JSON
    std::string cameraName;
    {
        std::ifstream file(profilePath);
        if (!file.is_open()) {
            errorMessage = "Cannot open vision profile: " + profilePath;
            return false;
        }
        nlohmann::json j;
        file >> j;
        file.close();
        if (j.contains("camera") && j["camera"].contains("cameraId")) {
            cameraName = j["camera"].value("cameraId", "");
        }
    }
    if (cameraName.empty()) {
        errorMessage = "Vision profile has no camera ID";
        return false;
    }

    // Execute vision using VisionCommandHandler (same pattern as RunAlignmentWidget)
    VisionCommandHandler visionHandler(m_appContext);
    SequenceContext visionCtx;
    std::string visionError;

    std::map<std::string, std::string> visionParams;
    visionParams["profile_path"] = profilePath;
    visionParams["camera"] = cameraName;
    visionParams["motion_device"] = device;
    visionParams["calculate_world_position"] = "true";

    bool ok = visionHandler.ExecuteCommand(
        nullptr, "vision", "execute_profile", visionParams, visionError, &visionCtx);

    if (!ok) {
        errorMessage = "Vision execution error: " + visionError;
        return false;
    }

    bool visionSuccess = visionCtx.GetBoolean("vision.result.success", false);
    if (!visionSuccess) {
        errorMessage = "Vision detection failed: " + visionCtx.GetString("vision.result.error", "Unknown");
        return false;
    }

    if (!visionCtx.HasValue("vision.detected.world_x_mm") ||
        !visionCtx.HasValue("vision.detected.world_y_mm")) {
        errorMessage = "No feature detected (no world position)";
        return false;
    }

    double worldX = static_cast<double>(visionCtx.GetNumeric("vision.detected.world_x_mm", 0.0f));
    double worldY = static_cast<double>(visionCtx.GetNumeric("vision.detected.world_y_mm", 0.0f));

    std::cout << "[AccuracyTrainer] Vision detected feature at world: X=" << worldX
              << " Y=" << worldY << std::endl;

    // Move gantry to detected center (keep current Z)
    auto currentPos = m_appContext->motionManager->GetCurrentPosition(device);
    bool moveOk = m_appContext->motionManager->MoveToAbs(
        device, worldX, worldY, currentPos.z, 100.0, true);

    if (!moveOk) {
        errorMessage = "Move to detected position failed";
        return false;
    }

    return true;
#else
    (void)profilePath;
    (void)device;
    errorMessage = "Vision not available (OpenCV not built)";
    return false;
#endif
}
