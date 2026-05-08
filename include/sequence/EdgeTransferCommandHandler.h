// EdgeTransferCommandHandler.h
// Headless Edge Transfer alignment for recipe/sequence execution
#pragma once

#include "ICommandHandler.h"
#include "EdgeTransferRetrace.h"

struct ApplicationContext;

/**
 * @brief Command handler for headless Edge Transfer alignment.
 *
 * Loads a previously taught EdgeTransferSet, recomputes deltas and
 * per-axis corrections, and (optionally) applies them to the target hex
 * device's U/V/W by moving to current + correction.
 *
 * Device type: "edgetransfer" (virtual, no real hardware).
 *
 * Supported commands:
 * - RunAlignment:
 *     Params:
 *       training_set_name  (required) - Name of the saved set
 *       apply_corrections  ("true"/"false", default "true") -
 *           if false, compute and publish results only (no motion)
 *
 *     Results written to SequenceContext (prefix "edge_transfer.result."):
 *       success            - bool
 *       training_set       - string
 *       target_device      - string (e.g. "hex-left")
 *       delta_yaw_deg      - string (degrees)
 *       delta_tilt_deg     - string (degrees)
 *       correction_u/v/w   - string (degrees)
 *       applied_u/v/w      - string (degrees, absolute) — only when
 *                            apply_corrections=true
 *
 * - UpdateEdgePoints:
 *     Replaces reference and/or target P1/P2 coordinates in a saved set
 *     and writes the file back. Used by Lua scripts to "inject" new
 *     gantry-main XYZ values that the retrace wizard will then auto-nav
 *     to on the next run.
 *     Params:
 *       training_set_name  (required) - Name of the saved set
 *       p1x, p1y, p1z      (optional) - New reference P1 in mm
 *       p2x, p2y, p2z      (optional) - New reference P2 in mm
 *       p1_named           (optional) - Pull XYZ for reference P1 from a
 *                                       named position on the set's
 *                                       measure device (default
 *                                       "gantry-main"). p1x/p1y/p1z still
 *                                       win when both are supplied.
 *       p2_named           (optional) - Same for reference P2.
 *       t1x, t1y, t1z      (optional) - New target P1 in mm
 *       t2x, t2y, t2z      (optional) - New target P2 in mm
 *       t1_named           (optional) - Named position for target P1.
 *       t2_named           (optional) - Named position for target P2.
 *     Any axis omitted is left unchanged. At least one coordinate must
 *     be provided. Each edge is marked valid=true only if any of its
 *     params were supplied; the other edge is preserved.
 */
class EdgeTransferCommandHandler : public ICommandHandler
{
public:
    EdgeTransferCommandHandler() = default;
    ~EdgeTransferCommandHandler() override = default;

    bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context) override;

    std::string GetManagerType() const override { return "edgetransfer"; }

    void SetContext(ApplicationContext* context) { m_appContext = context; }

    // Set the blocking retrace callback. Set by the host page
    // (RunProgramPage) via SequenceEngine; the handler invokes this when a
    // recipe asks for apply_corrections=true so the retrace widget can be
    // shown embedded in the execution panel instead of a modal dialog.
    void SetRetraceCallback(EdgeTransferRetraceCallback callback) {
        m_retraceCallback = std::move(callback);
    }

private:
    ApplicationContext* m_appContext = nullptr;
    EdgeTransferRetraceCallback m_retraceCallback;

    bool HandleRunAlignment(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);

    bool HandleUpdateEdgePoints(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage);
};
