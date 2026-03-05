// PlanarAdjustmentCommandHandler.h
// Headless planar adjustment for recipe/sequence execution
#pragma once

#include "ICommandHandler.h"
#include "SystemCommandHandler.h"  // For ManualAdjustCallback

struct ApplicationContext;

/**
 * @brief Command handler for headless planar adjustment correction
 *
 * Loads a saved training set, computes tilt angles from the two taught points,
 * and applies a MoveRelative correction to the specified hex device/axis.
 *
 * When a ManualAdjustCallback is set, performs a live retrace:
 *   1. Prompts user to jog to P1 (ManualAdjust), captures position
 *   2. Auto-moves by taught delta toward P2
 *   3. Prompts user to adjust P2 (ManualAdjust), captures position
 *   4. Recomputes angles from fresh P1/P2, applies correction, saves set
 *
 * Device type: "planaradjustment" (virtual, no real hardware)
 *
 * Supported commands:
 * - RunPlanarAdjustment: Load set, compute angles, apply correction
 *
 * Parameters for RunPlanarAdjustment:
 *   training_set_name  - Name of the saved planar adjustment set
 *   source_angle       - "angle_x" or "angle_y" (default: "angle_x")
 *   target_device      - Target hex device name (e.g., "hex-right")
 *   target_axis        - "U", "V", or "W" (default: "U")
 *   reverse_sign       - "true" or "false" (default: "false")
 *
 * Results stored in SequenceContext:
 *   planar_adjustment.result.success        - "true"/"false"
 *   planar_adjustment.result.angle_x_deg    - computed angle X
 *   planar_adjustment.result.angle_y_deg    - computed angle Y
 *   planar_adjustment.result.correction_deg - applied correction value
 *   planar_adjustment.result.target_device  - device that was corrected
 *   planar_adjustment.result.target_axis    - axis that was corrected
 */
class PlanarAdjustmentCommandHandler : public ICommandHandler
{
public:
    PlanarAdjustmentCommandHandler() = default;
    ~PlanarAdjustmentCommandHandler() override = default;

    bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context) override;

    std::string GetManagerType() const override { return "planaradjustment"; }

    void SetContext(ApplicationContext* context) { m_appContext = context; }

    // Set the ManualAdjust callback for live retrace (reuses existing UI widget)
    void SetManualAdjustCallback(ManualAdjustCallback callback) {
        m_manualAdjustCallback = callback;
    }

private:
    ApplicationContext* m_appContext = nullptr;
    ManualAdjustCallback m_manualAdjustCallback;

    bool HandleRunPlanarAdjustment(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);
};
