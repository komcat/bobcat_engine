// AccuracyTrainerCommandHandler.cpp
// Headless accuracy trainer alignment for recipe/sequence execution
#include "AccuracyTrainerCommandHandler.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "SequenceContext.h"
#include "../AppUI/AccuracyTrainer/AccuracyTrainerData.h"
#include "../AppUI/AccuracyTrainer/AccuracyTrainerStore.h"
#include "../dut/DeviceUnderTest.h"
#include "../motions/TransformationMatrixLoader.h"
#include "../motions/CoordinateTransformer.h"
#include "product_position_manager.h"
#include "LogManager.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <cmath>
#include <filesystem>

#ifdef HAS_OPENCV
#include "VisionCommandHandler.h"
#endif

// Helper: Write taught position to the ManualAlignPointWidget navigation file
// so the widget auto-navigates to the correct taught position (not stale saved data)
static void WriteNavigationData(
    const std::string& productName,
    const std::string& deviceName,
    const std::string& pointName,
    double x, double y, double z)
{
    std::filesystem::path navDir = std::filesystem::path("data") / productName / "referenceimages" / "navigation";
    std::filesystem::create_directories(navDir);

    std::filesystem::path navFile = navDir / (pointName + ".json");
    nlohmann::json j;
    j["point_name"] = pointName;
    j["device"] = deviceName;
    j["position"]["x"] = x;
    j["position"]["y"] = y;
    j["position"]["z"] = z;

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
    localtime_s(&tm_now, &time_t_now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_now);
    j["last_saved"] = std::string(buf);

    std::ofstream file(navFile);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
        std::cout << "[AccuracyTrainer] Updated navigation data: " << navFile.string()
                  << " (X:" << x << ", Y:" << y << ", Z:" << z << ")" << std::endl;
    }
}

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

    // Auto-detect hex registration from bondingDeviceAssignments:
    // If a bonding point is assigned to a hex device, that hex must be registered
    // so the bonding move phase can compute the corrected hex position.
    for (const auto& [bondName, assignedDevice] : ts.bondingDeviceAssignments) {
        if (assignedDevice != device) {
            // Find matching hex device in training set
            for (const auto& hexDev : ts.hexDevicePositions) {
                if (hexDev.deviceName == assignedDevice && hexDev.valid) {
                    if (assignedDevice.find("left") != std::string::npos && !registerHexLeft) {
                        registerHexLeft = true;
                        std::cout << "[AccuracyTrainer] Auto-enabled hex-left registration "
                                  << "(assigned to " << bondName << ")" << std::endl;
                    }
                    if (assignedDevice.find("right") != std::string::npos && !registerHexRight) {
                        registerHexRight = true;
                        std::cout << "[AccuracyTrainer] Auto-enabled hex-right registration "
                                  << "(assigned to " << bondName << ")" << std::endl;
                    }
                    break;
                }
            }
        }
    }

    // Get product name for navigation file updates
    std::string productName;
    if (m_appContext->productPositionManager &&
        m_appContext->productPositionManager->HasLoadedProduct()) {
        productName = m_appContext->productPositionManager->GetCurrentProductName();
    }

    // Get DUT serial number if available
    std::string serialNumber;
    if (m_appContext->currentDUT) {
        serialNumber = m_appContext->currentDUT->getSerialNumber();
    }

    // Initialize SQLite logger for corner confirmations
    m_logger.Initialize();

    // ========================================================================
    // PHASE 1: Reference alignment
    // ========================================================================
    std::vector<ReferencePoint> actualRefs;

    for (size_t i = 0; i < ts.referencePoints.size(); ++i) {
        const auto& ref = ts.referencePoints[i];
        std::cout << "[AccuracyTrainer] Ref " << (i + 1) << "/" << ts.referencePoints.size()
                  << ": " << ref.name << " -> (" << ref.globalX << ", " << ref.globalY
                  << ", " << ref.globalZ << ")" << std::endl;

        // Build unique ID for this reference point
        std::string refUid = "ref_" + ref.name;

        // Write taught ref position to navigation file BEFORE calling the manual
        // callback, so ManualAlignPointWidget auto-navigates to the taught position
        // (not stale saved data from a previous run)
        if (!productName.empty()) {
            WriteNavigationData(productName, device, refUid,
                                ref.globalX, ref.globalY, ref.globalZ);
        }

        // Check stop between operations
        if (context && context->IsStopRequestedAny()) {
            errorMessage = "Alignment stopped by user";
            context->SetValue("accuracy_trainer.result.success", false);
            return false;
        }

        // Navigate to taught reference position
        std::cout << "[AccuracyTrainer] Moving to taught position for " << ref.name << std::endl;
        bool moveOk = motionMgr->MoveToAbs(device, ref.globalX, ref.globalY, ref.globalZ, 100.0, true);
        if (!moveOk) {
            std::cout << "[AccuracyTrainer] WARNING: Move to taught position failed for " << ref.name << std::endl;
        }

        // Try vision first if profile was saved during teaching
        bool visionAligned = false;
        if (!ref.visionProfilePath.empty()) {
            std::cout << "[AccuracyTrainer] Running vision profile for " << ref.name << std::endl;
            std::string visionError;
            visionAligned = RunVisionDetectCenter(ref.visionProfilePath, device, visionError);
            if (visionAligned) {
                std::cout << "[AccuracyTrainer] Vision succeeded for " << ref.name
                          << " - auto-capturing position" << std::endl;
            } else {
                std::cout << "[AccuracyTrainer] Vision failed for " << ref.name
                          << ": " << visionError << " - falling back to manual" << std::endl;
            }
        }

        // If vision did not align, fall back to manual confirmation
        if (!visionAligned) {
            if (m_manualAlignCallback) {
                std::string msg = "Align to reference point " + ref.name
                    + " (" + std::to_string(i + 1) + " of "
                    + std::to_string(ts.referencePoints.size()) + ")\n";
                if (!ref.visionProfilePath.empty()) {
                    msg += "Vision detection failed. ";
                }
                msg += "Verify or adjust gantry position on the reference feature, then click OK.";
                bool userOk = m_manualAlignCallback(refUid, msg, true, true, true);
                if (!userOk) {
                    errorMessage = "User cancelled alignment at reference " + ref.name;
                    if (context) context->SetValue("accuracy_trainer.result.success", false);
                    return false;
                }
            } else {
                errorMessage = "Vision failed and no manual callback for ref " + ref.name;
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
                      << hexDev.deviceName
                      << " (teachMode=" << hexDev.gantryTeachMode << ")" << std::endl;

            if (context && context->IsStopRequestedAny()) {
                errorMessage = "Alignment stopped by user during hex registration";
                context->SetValue("accuracy_trainer.result.success", false);
                return false;
            }

            // Move hex device to taught 6-axis position
            PositionStruct hexTarget;
            hexTarget.x = hexDev.hexX; hexTarget.y = hexDev.hexY; hexTarget.z = hexDev.hexZ;
            hexTarget.u = hexDev.hexU; hexTarget.v = hexDev.hexV; hexTarget.w = hexDev.hexW;
            bool hexOk = motionMgr->MoveToAbs6(hexDev.deviceName, hexTarget, true);
            if (!hexOk) {
                std::cout << "[AccuracyTrainer] WARNING: Hex move failed for " << hexDev.deviceName << std::endl;
            }

            // Move gantry to viewing position
            bool gantryOk = motionMgr->MoveToAbs(
                device, hexDev.gantryViewX, hexDev.gantryViewY, hexDev.gantryViewZ, 100.0, true);
            if (!gantryOk) {
                std::cout << "[AccuracyTrainer] WARNING: Gantry move failed for hex " << hexDev.deviceName << std::endl;
            }

            // Try vision for hex if profile available (pre-positions gantry)
            if (!hexDev.visionProfilePath.empty() && gantryOk) {
                std::cout << "[AccuracyTrainer] Running vision profile for " << hexDev.deviceName << std::endl;
                std::string visionError;
                bool visionOk = RunVisionDetectCenter(hexDev.visionProfilePath, device, visionError);
                if (visionOk) {
                    std::cout << "[AccuracyTrainer] Vision pre-positioned for " << hexDev.deviceName << std::endl;
                } else {
                    std::cout << "[AccuracyTrainer] Hex vision failed: " << visionError
                              << " - user must jog manually" << std::endl;
                }
            }

            // Registration based on teach mode
            if (hexDev.gantryTeachMode == 0) {
                // Single-point mode: confirm current gantry position as hex center
                std::cout << "[AccuracyTrainer] Single-point mode for " << hexDev.deviceName << std::endl;

                // Always require manual confirmation for hex registration
                if (m_manualAlignCallback) {
                    // Write viewing position so ManualAlignPointWidget navigates correctly
                    std::string hexUid = "hex_" + hexDev.deviceName + "_CENTER";
                    if (!productName.empty()) {
                        WriteNavigationData(productName, device, hexUid,
                                            hexDev.gantryViewX, hexDev.gantryViewY, hexDev.gantryViewZ);
                    }
                    std::string msg = "Confirm hex registration: " + hexDev.deviceName
                        + "\nVerify gantry is centered on the hex end-effector tip, then click OK.";
                    bool userOk = m_manualAlignCallback(hexUid, msg, true, true, true);
                    if (!userOk) {
                        std::cout << "[AccuracyTrainer] User cancelled hex registration for "
                                  << hexDev.deviceName << std::endl;
                        continue;
                    }
                }

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
                          << " (single-point): gantryCenter=(" << gantryPos.x << ", " << gantryPos.y
                          << ", " << gantryPos.z << ")" << std::endl;

                if (context) {
                    std::string prefix = "accuracy_trainer.result.hex_" + std::to_string(i);
                    context->SetValue(prefix + ".device", hexDev.deviceName);
                    context->SetValue(prefix + ".x", std::to_string(hexPos.x));
                    context->SetValue(prefix + ".y", std::to_string(hexPos.y));
                    context->SetValue(prefix + ".z", std::to_string(hexPos.z));
                    context->SetValue(prefix + ".z_adjustment", std::to_string(hexDev.hexZAdjustment));
                }
                continue;  // Skip to next hex device
            }

            // TeachMode 1 or 2: manual 2-3 corner capture
            if (!m_manualAlignCallback) {
                std::cout << "[AccuracyTrainer] No manual callback for hex "
                          << hexDev.deviceName << " - skipping" << std::endl;
                continue;
            }

            {
                std::cout << "[AccuracyTrainer] Starting corner capture for " << hexDev.deviceName
                          << " (teachMode=" << hexDev.gantryTeachMode << ")" << std::endl;

                // Auto-navigate to taught TL corner position if available
                bool hasTaughtTL = (hexDev.gantryTeachMode >= 1 &&
                    (std::abs(hexDev.cornerTL_X) > 1e-6 || std::abs(hexDev.cornerTL_Y) > 1e-6));
                if (hasTaughtTL) {
                    std::cout << "[AccuracyTrainer] Auto-navigating to taught TL corner: X="
                              << hexDev.cornerTL_X << " Y=" << hexDev.cornerTL_Y
                              << " Z=" << hexDev.cornerTL_Z << std::endl;
                    bool navOk = motionMgr->MoveToAbs(device,
                        hexDev.cornerTL_X, hexDev.cornerTL_Y, hexDev.cornerTL_Z, 100.0, true);
                    if (!navOk) {
                        std::cout << "[AccuracyTrainer] WARNING: Auto-navigate to TL corner failed" << std::endl;
                    }
                }

                // Write corner position to navigation file so ManualAlignPointWidget
                // stays at the corner (not the viewing position) when it opens
                std::string tlUid = "hex_" + hexDev.deviceName + "_TL";
                if (!productName.empty()) {
                    if (hasTaughtTL) {
                        WriteNavigationData(productName, device, tlUid,
                                            hexDev.cornerTL_X, hexDev.cornerTL_Y, hexDev.cornerTL_Z);
                    } else {
                        WriteNavigationData(productName, device, tlUid,
                                            hexDev.gantryViewX, hexDev.gantryViewY, hexDev.gantryViewZ);
                    }
                }

                // Corner 1: Top-Left
                std::string msg1 = "Corner 1/3 (Top-Left): " + hexDev.deviceName
                    + "\nJog gantry camera crosshair to the TOP-LEFT corner of the end-effector tip.";
                bool userOk = m_manualAlignCallback(tlUid, msg1, true, true, true);
                if (!userOk) {
                    std::cout << "[AccuracyTrainer] User cancelled hex corner 1 for " << hexDev.deviceName << std::endl;
                    continue;
                }
                auto cornerTL = motionMgr->GetCurrentPosition(device);
                std::cout << "[AccuracyTrainer] Corner 1 (TL): X=" << cornerTL.x
                          << " Y=" << cornerTL.y << " Z=" << cornerTL.z << std::endl;

                m_logger.LogCorner(productName, tsName, serialNumber, hexDev.deviceName,
                    hexDev.gantryTeachMode, "TL",
                    hexDev.cornerTL_X, hexDev.cornerTL_Y, hexDev.cornerTL_Z,
                    cornerTL.x, cornerTL.y, cornerTL.z);

                if (context && context->IsStopRequestedAny()) {
                    errorMessage = "Alignment stopped during hex corner capture";
                    context->SetValue("accuracy_trainer.result.success", false);
                    return false;
                }

                // Auto-navigate to taught BR corner position if available
                bool hasTaughtBR = (hexDev.gantryTeachMode >= 1 &&
                    (std::abs(hexDev.cornerBR_X) > 1e-6 || std::abs(hexDev.cornerBR_Y) > 1e-6));
                if (hasTaughtBR) {
                    std::cout << "[AccuracyTrainer] Auto-navigating to taught BR corner: X="
                              << hexDev.cornerBR_X << " Y=" << hexDev.cornerBR_Y << std::endl;
                    double curZ = motionMgr->GetCurrentPosition(device).z;
                    bool navOk = motionMgr->MoveToAbs(device,
                        hexDev.cornerBR_X, hexDev.cornerBR_Y, curZ, 100.0, true);
                    if (!navOk) {
                        std::cout << "[AccuracyTrainer] WARNING: Auto-navigate to BR corner failed" << std::endl;
                    }
                }

                // Update navigation file with BR corner so widget stays here
                std::string brUid = "hex_" + hexDev.deviceName + "_BR";
                if (!productName.empty() && hasTaughtBR) {
                    double curZ = motionMgr->GetCurrentPosition(device).z;
                    WriteNavigationData(productName, device, brUid,
                                        hexDev.cornerBR_X, hexDev.cornerBR_Y, curZ);
                }

                // Corner 2: Bottom-Right
                std::string msg2 = "Corner 2/3 (Bottom-Right): " + hexDev.deviceName
                    + "\nJog gantry camera crosshair to the BOTTOM-RIGHT corner of the end-effector tip.";
                userOk = m_manualAlignCallback(brUid, msg2, true, true, true);
                if (!userOk) {
                    std::cout << "[AccuracyTrainer] User cancelled hex corner 2 for " << hexDev.deviceName << std::endl;
                    continue;
                }
                auto cornerBR = motionMgr->GetCurrentPosition(device);
                std::cout << "[AccuracyTrainer] Corner 2 (BR): X=" << cornerBR.x
                          << " Y=" << cornerBR.y << std::endl;

                m_logger.LogCorner(productName, tsName, serialNumber, hexDev.deviceName,
                    hexDev.gantryTeachMode, "BR",
                    hexDev.cornerBR_X, hexDev.cornerBR_Y, 0.0,
                    cornerBR.x, cornerBR.y, cornerBR.z);

                // Corner 3: Top-Right (only for 3-corner mode, optional for 2-corner)
                bool hasThirdCorner = false;
                double cornerTR_X = 0.0, cornerTR_Y = 0.0;
                if (hexDev.gantryTeachMode == 2) {
                    // Auto-navigate to taught TR corner position if available
                    bool hasTaughtTR = (hexDev.hasThirdCorner &&
                        (std::abs(hexDev.cornerTR_X) > 1e-6 || std::abs(hexDev.cornerTR_Y) > 1e-6));
                    if (hasTaughtTR) {
                        std::cout << "[AccuracyTrainer] Auto-navigating to taught TR corner: X="
                                  << hexDev.cornerTR_X << " Y=" << hexDev.cornerTR_Y << std::endl;
                        double curZ = motionMgr->GetCurrentPosition(device).z;
                        bool navOk = motionMgr->MoveToAbs(device,
                            hexDev.cornerTR_X, hexDev.cornerTR_Y, curZ, 100.0, true);
                        if (!navOk) {
                            std::cout << "[AccuracyTrainer] WARNING: Auto-navigate to TR corner failed" << std::endl;
                        }
                    }

                    // Update navigation file with TR corner so widget stays here
                    std::string trUid = "hex_" + hexDev.deviceName + "_TR";
                    if (!productName.empty() && hasTaughtTR) {
                        double curZ = motionMgr->GetCurrentPosition(device).z;
                        WriteNavigationData(productName, device, trUid,
                                            hexDev.cornerTR_X, hexDev.cornerTR_Y, curZ);
                    }

                    std::string msg3 = "Corner 3/3 (Top-Right): " + hexDev.deviceName
                        + "\nJog gantry camera crosshair to the TOP-RIGHT corner.\n"
                          "Click OK to capture, or Cancel to use 2-corner midpoint.";
                    hasThirdCorner = m_manualAlignCallback(trUid, msg3, true, true, true);
                    if (hasThirdCorner) {
                        auto cornerTR = motionMgr->GetCurrentPosition(device);
                        cornerTR_X = cornerTR.x;
                        cornerTR_Y = cornerTR.y;
                        std::cout << "[AccuracyTrainer] Corner 3 (TR): X=" << cornerTR_X
                                  << " Y=" << cornerTR_Y << std::endl;

                        m_logger.LogCorner(productName, tsName, serialNumber, hexDev.deviceName,
                            hexDev.gantryTeachMode, "TR",
                            hexDev.cornerTR_X, hexDev.cornerTR_Y, 0.0,
                            cornerTR_X, cornerTR_Y, 0.0);
                    } else {
                        std::cout << "[AccuracyTrainer] Skipped corner 3 (using 2-corner midpoint)" << std::endl;
                    }
                }

                // Compute center from corners (same math as MultiDeviceRunAlignmentWidget)
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

                m_logger.LogCorner(productName, tsName, serialNumber, hexDev.deviceName,
                    hexDev.gantryTeachMode, "CENTER",
                    0.0, 0.0, 0.0,
                    centerX, centerY, cornerTL.z);

                // Move gantry to computed center
                bool moveOk = motionMgr->MoveToAbs(
                    device, centerX, centerY, cornerTL.z, 100.0, true);
                if (!moveOk) {
                    std::cout << "[AccuracyTrainer] WARNING: Move to corner center failed" << std::endl;
                }
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
                context->SetValue(prefix + ".z_adjustment", std::to_string(hexDev.hexZAdjustment));
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
            // Two-stage move: XY lateral first, then Z height adjustment (avoids crashes)
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

                // Look up hexZAdjustment from training set teach data
                double hexZAdjustment = 0.0;
                for (const auto& hd : ts.hexDevicePositions) {
                    if (hd.deviceName == cb.assignedDevice && hd.valid) {
                        hexZAdjustment = hd.hexZAdjustment;
                        break;
                    }
                }

                std::cout << "[AccuracyTrainer] " << cb.assignedDevice
                          << " gantry delta: dX=" << gantryDeltaX << " dY=" << gantryDeltaY
                          << " Zadj=" << hexZAdjustment << std::endl;

                // Step 1: Lateral repositioning - transform gantry XY delta only
                CoordinateTransformer::Vector6D xyGlobalDelta(gantryDeltaX, gantryDeltaY, 0.0, 0.0, 0.0, 0.0);
                auto xyLocalDelta = CoordinateTransformer::TransformGlobalToLocal(xyGlobalDelta, *matrix);

                std::cout << "[AccuracyTrainer]   Lateral local delta: dX=" << xyLocalDelta.x
                          << " dY=" << xyLocalDelta.y << " dZ=" << xyLocalDelta.z << std::endl;

                // Step 2: Height adjustment - transform Z adjustment only
                CoordinateTransformer::Vector6D zGlobalDelta(0.0, 0.0, hexZAdjustment, 0.0, 0.0, 0.0);
                auto zLocalDelta = CoordinateTransformer::TransformGlobalToLocal(zGlobalDelta, *matrix);

                bool hasZAdjustment = (std::abs(zLocalDelta.x) > 1e-6 ||
                                       std::abs(zLocalDelta.y) > 1e-6 ||
                                       std::abs(zLocalDelta.z) > 1e-6);

                if (hasZAdjustment) {
                    std::cout << "[AccuracyTrainer]   Height local delta:  dX=" << zLocalDelta.x
                              << " dY=" << zLocalDelta.y << " dZ=" << zLocalDelta.z << std::endl;
                }

                // Execute Step 1: lateral move
                auto curHexPos = motionMgr->GetCurrentPosition(cb.assignedDevice);
                PositionStruct xyTargetPos;
                xyTargetPos.x = curHexPos.x + xyLocalDelta.x;
                xyTargetPos.y = curHexPos.y + xyLocalDelta.y;
                xyTargetPos.z = curHexPos.z + xyLocalDelta.z;
                xyTargetPos.u = curHexPos.u + xyLocalDelta.u;
                xyTargetPos.v = curHexPos.v + xyLocalDelta.v;
                xyTargetPos.w = curHexPos.w + xyLocalDelta.w;

                std::cout << "[AccuracyTrainer]   Step 1 - Hex lateral target: X=" << xyTargetPos.x
                          << " Y=" << xyTargetPos.y << " Z=" << xyTargetPos.z << std::endl;

                bool hexOk = motionMgr->MoveToAbs6(cb.assignedDevice, xyTargetPos, true);
                if (!hexOk) {
                    std::cout << "[AccuracyTrainer] WARNING: " << cb.assignedDevice
                              << " lateral move failed for " << cb.name << std::endl;
                }

                // Execute Step 2: height adjustment (separate move to avoid crash)
                if (hexOk && hasZAdjustment) {
                    auto afterXYPos = motionMgr->GetCurrentPosition(cb.assignedDevice);
                    PositionStruct zTargetPos;
                    zTargetPos.x = afterXYPos.x + zLocalDelta.x;
                    zTargetPos.y = afterXYPos.y + zLocalDelta.y;
                    zTargetPos.z = afterXYPos.z + zLocalDelta.z;
                    zTargetPos.u = afterXYPos.u;
                    zTargetPos.v = afterXYPos.v;
                    zTargetPos.w = afterXYPos.w;

                    std::cout << "[AccuracyTrainer]   Step 2 - Hex height target: X=" << zTargetPos.x
                              << " Y=" << zTargetPos.y << " Z=" << zTargetPos.z << std::endl;

                    bool zOk = motionMgr->MoveToAbs6(cb.assignedDevice, zTargetPos, true);
                    if (!zOk) {
                        std::cout << "[AccuracyTrainer] WARNING: " << cb.assignedDevice
                                  << " height move failed for " << cb.name << std::endl;
                    }
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
