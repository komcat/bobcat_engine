// ZOffsetCommandHandler.h
// Headless Z offset correction for recipe/sequence execution
#pragma once

#include "ICommandHandler.h"
#include "SystemCommandHandler.h"  // For ManualAdjustCallback

struct ApplicationContext;

/**
 * @brief Command handler for headless Z offset correction
 *
 * Loads a saved training set, computes Z difference between reference and target
 * surfaces, and applies a MoveRelative Z correction to the specified hex device.
 *
 * When a ManualAdjustCallback is set, performs a live retrace:
 *   1. Prompts user to jog to Reference surface, captures Z
 *   2. Prompts user to jog to Target surface, captures Z
 *   3. Recomputes Z diff, calculates correction, applies to hex Z
 *
 * Device type: "zoffset" (virtual, no real hardware)
 *
 * Supported commands:
 * - RunZOffset: Load set, compute Z diff, apply correction
 *
 * Parameters for RunZOffset:
 *   training_set_name  - Name of the saved Z offset set
 *   target_device      - Target hex device name (e.g., "hex-right")
 *   target_z_diff_um   - Desired Z difference in microns (default: "150")
 *   reverse_sign       - "true" or "false" (default: "false")
 *
 * Results stored in SequenceContext:
 *   z_offset.result.success           - "true"/"false"
 *   z_offset.result.measured_z_diff_mm - measured Z difference
 *   z_offset.result.target_z_diff_um  - desired Z difference
 *   z_offset.result.correction_mm     - applied correction value in mm
 *   z_offset.result.target_device     - device that was corrected
 */
class ZOffsetCommandHandler : public ICommandHandler
{
public:
    ZOffsetCommandHandler() = default;
    ~ZOffsetCommandHandler() override = default;

    bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context) override;

    std::string GetManagerType() const override { return "zoffset"; }

    void SetContext(ApplicationContext* context) { m_appContext = context; }

    void SetManualAdjustCallback(ManualAdjustCallback callback) {
        m_manualAdjustCallback = callback;
    }

private:
    ApplicationContext* m_appContext = nullptr;
    ManualAdjustCallback m_manualAdjustCallback;

    bool HandleRunZOffset(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);
};
