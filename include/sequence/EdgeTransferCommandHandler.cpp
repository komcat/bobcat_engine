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

#include <cstdlib>
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
    if (methodName == "UpdateEdgePoints") {
        return HandleUpdateEdgePoints(params, errorMessage);
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

namespace {
// Exception-free strtod (CLAUDE.md: codebase has been purged of throws).
// Returns true and writes the parsed value when the string parses cleanly;
// returns false otherwise (caller leaves the field unchanged).
bool TryParseDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return false;
    out = v;
    return true;
}
}  // namespace

bool EdgeTransferCommandHandler::HandleUpdateEdgePoints(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage)
{
    auto nameIt = params.find("training_set_name");
    if (nameIt == params.end() || nameIt->second.empty()) {
        errorMessage = "Missing required parameter: training_set_name";
        return false;
    }
    const std::string& tsName = nameIt->second;

    EdgeTransferStore store;
    if (m_appContext && m_appContext->productPositionManager &&
        m_appContext->productPositionManager->HasLoadedProduct()) {
        std::string productPath = m_appContext->productPositionManager->GetCurrentProductPath();
        std::filesystem::path productDir = std::filesystem::path(productPath).parent_path();
        store.SetBasePath((productDir / "edge_transfer").string());
    }

    auto optSet = store.LoadSet(tsName);
    if (!optSet.has_value()) {
        errorMessage = "Failed to load Edge Transfer set: " + tsName;
        return false;
    }
    EdgeTransferSet set = std::move(optSet.value());

    bool refTouched = false;
    bool tgtTouched = false;

    // Named-position lookup (resolved against the set's measure device,
    // e.g. "gantry-main"). Per-axis numeric overrides below still win.
    auto applyNamed = [&](const std::string& key,
                          double& fx, double& fy, double& fz,
                          bool& touchedFlag) {
        auto it = params.find(key);
        if (it == params.end() || it->second.empty()) return;
        if (!m_appContext || !m_appContext->motionManager) {
            errorMessage = key + ": motionManager not available";
            return;
        }
        PositionStruct named;
        if (!m_appContext->motionManager->GetNamedPosition(
                set.measureDevice, it->second, named)) {
            errorMessage = key + ": named position '" + it->second +
                           "' not found on '" + set.measureDevice + "'";
            return;
        }
        fx = named.x;
        fy = named.y;
        fz = named.z;
        touchedFlag = true;
    };

    applyNamed("p1_named", set.referenceEdge.p1x,
               set.referenceEdge.p1y, set.referenceEdge.p1z, refTouched);
    applyNamed("p2_named", set.referenceEdge.p2x,
               set.referenceEdge.p2y, set.referenceEdge.p2z, refTouched);
    applyNamed("t1_named", set.targetEdge.p1x,
               set.targetEdge.p1y, set.targetEdge.p1z, tgtTouched);
    applyNamed("t2_named", set.targetEdge.p2x,
               set.targetEdge.p2y, set.targetEdge.p2z, tgtTouched);
    if (!errorMessage.empty()) {
        return false;
    }

    auto applyParam = [&](const std::string& key, double& field, bool& touchedFlag) {
        auto it = params.find(key);
        if (it == params.end()) return;
        double v = 0.0;
        if (TryParseDouble(it->second, v)) {
            field = v;
            touchedFlag = true;
        }
    };

    applyParam("p1x", set.referenceEdge.p1x, refTouched);
    applyParam("p1y", set.referenceEdge.p1y, refTouched);
    applyParam("p1z", set.referenceEdge.p1z, refTouched);
    applyParam("p2x", set.referenceEdge.p2x, refTouched);
    applyParam("p2y", set.referenceEdge.p2y, refTouched);
    applyParam("p2z", set.referenceEdge.p2z, refTouched);

    applyParam("t1x", set.targetEdge.p1x, tgtTouched);
    applyParam("t1y", set.targetEdge.p1y, tgtTouched);
    applyParam("t1z", set.targetEdge.p1z, tgtTouched);
    applyParam("t2x", set.targetEdge.p2x, tgtTouched);
    applyParam("t2y", set.targetEdge.p2y, tgtTouched);
    applyParam("t2z", set.targetEdge.p2z, tgtTouched);

    if (!refTouched && !tgtTouched) {
        errorMessage = "No edge coordinates provided "
                       "(need p1_named/p2_named/t1_named/t2_named or p?x/p?y/p?z/t?x/t?y/t?z)";
        return false;
    }

    if (refTouched) set.referenceEdge.valid = true;
    if (tgtTouched) set.targetEdge.valid = true;
    set.Recalculate();

    if (!store.SaveSet(set)) {
        errorMessage = "Failed to save Edge Transfer set: " + tsName;
        return false;
    }

    std::cout << "[EdgeTransfer] UpdateEdgePoints: " << tsName;
    if (refTouched) {
        std::cout << " refP1=(" << set.referenceEdge.p1x << ", "
                  << set.referenceEdge.p1y << ", " << set.referenceEdge.p1z << ")"
                  << " refP2=(" << set.referenceEdge.p2x << ", "
                  << set.referenceEdge.p2y << ", " << set.referenceEdge.p2z << ")";
    }
    if (tgtTouched) {
        std::cout << " tgtP1=(" << set.targetEdge.p1x << ", "
                  << set.targetEdge.p1y << ", " << set.targetEdge.p1z << ")"
                  << " tgtP2=(" << set.targetEdge.p2x << ", "
                  << set.targetEdge.p2y << ", " << set.targetEdge.p2z << ")";
    }
    std::cout << std::endl;
    return true;
}
