// YawTrainerCommandHandler.h
// Headless yaw correction for recipe/sequence execution
#pragma once

#include "ICommandHandler.h"
#include "SystemCommandHandler.h"  // For ManualAdjustCallback

struct ApplicationContext;

/**
 * @brief Command handler for headless yaw correction
 *
 * Loads a saved training set, computes line angles from the taught reference and
 * target lines, and applies a MoveRelative yaw correction to the specified hex device/axis.
 *
 * When a ManualAdjustCallback is set, performs a live retrace:
 *   1. Prompts user to jog to Ref P1 (ManualAdjust), captures position
 *   2. Auto-moves by taught ref delta toward Ref P2, prompts user, captures
 *   3. Prompts user to jog to Tgt P1 (ManualAdjust), captures position
 *   4. Auto-moves by taught tgt delta toward Tgt P2, prompts user, captures
 *   5. Recomputes delta angle, applies correction, saves set
 *
 * Device type: "yawtrainer" (virtual, no real hardware)
 *
 * Supported commands:
 * - RunYawCorrection: Load set, compute delta angle, apply correction
 *
 * Parameters for RunYawCorrection:
 *   training_set_name  - Name of the saved yaw trainer set
 *   target_device      - Target hex device name (e.g., "hex-right")
 *   target_axis        - "U", "V", or "W" (default: "W")
 *   reverse_sign       - "true" or "false" (default: "false")
 *
 * Results stored in SequenceContext:
 *   yaw_trainer.result.success        - "true"/"false"
 *   yaw_trainer.result.ref_angle_deg  - reference line angle
 *   yaw_trainer.result.tgt_angle_deg  - target line angle
 *   yaw_trainer.result.delta_deg      - delta angle
 *   yaw_trainer.result.correction_deg - applied correction value
 *   yaw_trainer.result.target_device  - device that was corrected
 *   yaw_trainer.result.target_axis    - axis that was corrected
 */
class YawTrainerCommandHandler : public ICommandHandler
{
public:
    YawTrainerCommandHandler() = default;
    ~YawTrainerCommandHandler() override = default;

    bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context) override;

    std::string GetManagerType() const override { return "yawtrainer"; }

    void SetContext(ApplicationContext* context) { m_appContext = context; }

    void SetManualAdjustCallback(ManualAdjustCallback callback) {
        m_manualAdjustCallback = callback;
    }

private:
    ApplicationContext* m_appContext = nullptr;
    ManualAdjustCallback m_manualAdjustCallback;

    bool HandleRunYawCorrection(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);
};
