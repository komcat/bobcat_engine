// EdgeTransferCommandHandler.cpp
// Headless Edge Transfer alignment for recipe/sequence execution
#include "EdgeTransferCommandHandler.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "SequenceContext.h"
#include "../AppUI/EdgeTransferTrainer/EdgeTransferData.h"
#include "../AppUI/EdgeTransferTrainer/EdgeTransferStore.h"
#include "product_position_manager.h"
#include <filesystem>
#include <iostream>
#include <string>

bool EdgeTransferCommandHandler::ExecuteCommand(
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

    errorMessage = "Unknown EdgeTransfer command: " + methodName;
    return false;
}

bool EdgeTransferCommandHandler::HandleRunAlignment(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    auto setFail = [&](const std::string& msg) {
        errorMessage = msg;
        if (context) context->SetValue("edge_transfer.result.success", false);
    };

    auto nameIt = params.find("training_set_name");
    if (nameIt == params.end() || nameIt->second.empty()) {
        setFail("Missing required parameter: training_set_name");
        return false;
    }
    const std::string& tsName = nameIt->second;

    bool applyCorrections = true;
    auto applyIt = params.find("apply_corrections");
    if (applyIt != params.end() && applyIt->second == "false") {
        applyCorrections = false;
    }

    if (context) context->SetValue("edge_transfer.result.training_set", tsName);

    std::cout << "[EdgeTransfer] RunAlignment: set=" << tsName
              << " apply=" << (applyCorrections ? "true" : "false") << std::endl;

    EdgeTransferStore store;
    if (m_appContext && m_appContext->productPositionManager &&
        m_appContext->productPositionManager->HasLoadedProduct()) {
        std::string productPath = m_appContext->productPositionManager->GetCurrentProductPath();
        std::filesystem::path productDir = std::filesystem::path(productPath).parent_path();
        store.SetBasePath((productDir / "edge_transfer").string());
        std::cout << "[EdgeTransfer] Store path: " << store.GetBasePath() << std::endl;
    }

    auto optSet = store.LoadSet(tsName);
    if (!optSet.has_value()) {
        setFail("Failed to load Edge Transfer set: " + tsName);
        return false;
    }

    EdgeTransferSet set = std::move(optSet.value());
    set.Recalculate();

    if (!set.deltasValid) {
        setFail("Edge Transfer set '" + tsName + "' has invalid edges (teach both reference and target first)");
        return false;
    }

    if (context) {
        context->SetValue("edge_transfer.result.target_device", set.config.targetDevice);
        context->SetValue("edge_transfer.result.delta_yaw_deg",  std::to_string(set.deltaYawDeg));
        context->SetValue("edge_transfer.result.delta_tilt_deg", std::to_string(set.deltaTiltDeg));
        context->SetValue("edge_transfer.result.correction_u",   std::to_string(set.correctionU));
        context->SetValue("edge_transfer.result.correction_v",   std::to_string(set.correctionV));
        context->SetValue("edge_transfer.result.correction_w",   std::to_string(set.correctionW));
    }

    std::cout << "[EdgeTransfer] device=" << set.config.targetDevice
              << " dYaw=" << set.deltaYawDeg << " dTilt=" << set.deltaTiltDeg
              << " corrU=" << set.correctionU
              << " corrV=" << set.correctionV
              << " corrW=" << set.correctionW << std::endl;

    if (!applyCorrections) {
        if (context) context->SetValue("edge_transfer.result.success", true);
        return true;
    }

    if (!m_appContext || !m_appContext->motionManager) {
        setFail("Motion manager not available");
        return false;
    }

    const auto& mapping = set.config.mapping;
    if (mapping.u == EdgeAxisRole::None && mapping.v == EdgeAxisRole::None &&
        mapping.w == EdgeAxisRole::None) {
        setFail("No axis roles assigned in set '" + tsName + "' — nothing to apply");
        return false;
    }

    auto* motionMgr = m_appContext->motionManager;
    PositionStruct cur = motionMgr->GetCurrentPosition(set.config.targetDevice);
    PositionStruct target = cur;

    if (mapping.u != EdgeAxisRole::None && mapping.applyU) {
        target.u = cur.u + set.correctionU;
    }
    if (mapping.v != EdgeAxisRole::None && mapping.applyV) {
        target.v = cur.v + set.correctionV;
    }
    if (mapping.w != EdgeAxisRole::None && mapping.applyW) {
        target.w = cur.w + set.correctionW;
    }

    std::cout << "[EdgeTransfer] Applying to " << set.config.targetDevice
              << ": U " << cur.u << " -> " << target.u
              << ", V " << cur.v << " -> " << target.v
              << ", W " << cur.w << " -> " << target.w << std::endl;

    if (!motionMgr->MoveToAbs6(set.config.targetDevice, target, true)) {
        setFail("MoveToAbs6 failed on device: " + set.config.targetDevice);
        return false;
    }

    if (context) {
        context->SetValue("edge_transfer.result.applied_u", std::to_string(target.u));
        context->SetValue("edge_transfer.result.applied_v", std::to_string(target.v));
        context->SetValue("edge_transfer.result.applied_w", std::to_string(target.w));
        context->SetValue("edge_transfer.result.success", true);
    }
    return true;
}
