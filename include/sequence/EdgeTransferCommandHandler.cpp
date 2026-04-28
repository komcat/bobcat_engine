// EdgeTransferCommandHandler.cpp
// Edge Transfer alignment command handler. When apply_corrections=true the
// handler hands control to an EdgeTransferRetraceCallback (wired up by
// RunProgramPage) so the retrace widget can be embedded in the execution
// panel instead of popping up as a modal dialog.
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

    EdgeTransferSet taughtSet = std::move(optSet.value());
    taughtSet.Recalculate();

    if (!taughtSet.deltasValid) {
        setFail("Edge Transfer set '" + tsName + "' has invalid edges (teach both reference and target first)");
        return false;
    }

    if (context) {
        context->SetValue("edge_transfer.result.target_device", taughtSet.config.targetDevice);
    }

    // Headless mode (apply_corrections=false): publish taught deltas to
    // context without moving or showing UI. Useful for a dry-run step.
    if (!applyCorrections) {
        if (context) {
            context->SetValue("edge_transfer.result.delta_yaw_deg",  std::to_string(taughtSet.deltaYawDeg));
            context->SetValue("edge_transfer.result.delta_tilt_deg", std::to_string(taughtSet.deltaTiltDeg));
            context->SetValue("edge_transfer.result.correction_u",   std::to_string(taughtSet.correctionU));
            context->SetValue("edge_transfer.result.correction_v",   std::to_string(taughtSet.correctionV));
            context->SetValue("edge_transfer.result.correction_w",   std::to_string(taughtSet.correctionW));
            context->SetValue("edge_transfer.result.outcome",        std::string("computed"));
            context->SetValue("edge_transfer.result.success", true);
        }
        return true;
    }

    // Interactive mode: hand off to the host page via the retrace callback.
    const auto& mapping = taughtSet.config.mapping;
    if (mapping.u == EdgeAxisRole::None && mapping.v == EdgeAxisRole::None &&
        mapping.w == EdgeAxisRole::None) {
        setFail("No axis roles assigned in set '" + tsName + "' — nothing to apply");
        return false;
    }

    if (!m_retraceCallback) {
        setFail("EdgeTransfer retrace callback not configured (recipe must run from Run Program page)");
        return false;
    }

    EdgeTransferRetraceParams retraceParams;
    retraceParams.trainingSetName = tsName;

    EdgeTransferRetraceOutcome outcome = m_retraceCallback(retraceParams);

    const char* outcomeStr = "cancelled";
    switch (outcome) {
        case EdgeTransferRetraceOutcome::Applied:   outcomeStr = "applied";   break;
        case EdgeTransferRetraceOutcome::Skipped:   outcomeStr = "skipped";   break;
        case EdgeTransferRetraceOutcome::Cancelled: outcomeStr = "cancelled"; break;
    }
    std::cout << "[EdgeTransfer] Retrace outcome: " << outcomeStr << std::endl;
    if (context) context->SetValue("edge_transfer.result.outcome",
                                   std::string(outcomeStr));

    if (outcome == EdgeTransferRetraceOutcome::Cancelled) {
        setFail("Edge Transfer retrace cancelled by user");
        return false;
    }

    // Applied → the host widget already ran the moves; query post-move UVW.
    // Skipped → no motion was performed; leave applied_* absent.
    if (outcome == EdgeTransferRetraceOutcome::Applied &&
        context && m_appContext && m_appContext->motionManager) {
        PositionStruct afterPos = m_appContext->motionManager
            ->GetCurrentPosition(taughtSet.config.targetDevice);
        context->SetValue("edge_transfer.result.applied_u", std::to_string(afterPos.u));
        context->SetValue("edge_transfer.result.applied_v", std::to_string(afterPos.v));
        context->SetValue("edge_transfer.result.applied_w", std::to_string(afterPos.w));
    }

    if (context) context->SetValue("edge_transfer.result.success", true);
    return true;
}
