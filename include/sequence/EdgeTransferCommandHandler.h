// EdgeTransferCommandHandler.h
// Headless Edge Transfer alignment for recipe/sequence execution
#pragma once

#include "ICommandHandler.h"

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

private:
    ApplicationContext* m_appContext = nullptr;

    bool HandleRunAlignment(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);
};
