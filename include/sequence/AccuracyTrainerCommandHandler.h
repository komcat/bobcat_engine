// AccuracyTrainerCommandHandler.h
// Headless accuracy trainer alignment for recipe/sequence execution
#pragma once

#include "ICommandHandler.h"
#include "SystemCommandHandler.h"  // For ManualAlignPointCallback
#include "AccuracyTrainerLogger.h"
#include <functional>

// Forward declarations
struct ApplicationContext;

/**
 * @brief Command handler for headless accuracy trainer alignment
 *
 * Runs the same alignment flow as RunAlignmentWidget but without any Qt widget
 * dependency. Designed for production recipe execution with vision auto-detect
 * and manual fallback via ManualAlignPointCallback.
 *
 * Supported commands:
 * - RunAlignment: Load training set, align references (vision+manual fallback),
 *                 compute correction transform, optionally register hex devices,
 *                 optionally move to corrected bonding positions.
 *
 * Device type: "accuracytrainer" (virtual, no real hardware)
 *
 * Parameters for RunAlignment:
 *   training_set_name   - Name of the training set to load
 *   register_hex_left   - "true"/"false" - register hex-left device
 *   register_hex_right  - "true"/"false" - register hex-right device
 *   move_to_bonding     - "true"/"false" - move to corrected bonding positions
 *
 * Results stored in SequenceContext:
 *   accuracy_trainer.result.success           - "true"/"false"
 *   accuracy_trainer.result.ref_count         - number of references aligned
 *   accuracy_trainer.result.bond_count        - number of bonding points computed
 *   accuracy_trainer.result.bond_N.name       - bonding point name
 *   accuracy_trainer.result.bond_N.corrected_x - corrected world X
 *   accuracy_trainer.result.bond_N.corrected_y - corrected world Y
 *   accuracy_trainer.result.bond_N.corrected_z - corrected Z (original)
 *   accuracy_trainer.result.bond_N.delta_x    - correction delta X
 *   accuracy_trainer.result.bond_N.delta_y    - correction delta Y
 */
class AccuracyTrainerCommandHandler : public ICommandHandler
{
public:
    AccuracyTrainerCommandHandler() = default;
    ~AccuracyTrainerCommandHandler() override = default;

    bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context) override;

    std::string GetManagerType() const override { return "accuracytrainer"; }

    void SetContext(ApplicationContext* context) { m_appContext = context; }

    void SetManualAlignPointCallback(ManualAlignPointCallback callback) {
        m_manualAlignCallback = callback;
    }

private:
    ApplicationContext* m_appContext = nullptr;
    ManualAlignPointCallback m_manualAlignCallback;
    AccuracyTrainerLogger m_logger;

    /**
     * @brief Execute the full alignment workflow
     */
    bool HandleRunAlignment(
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Run vision detect-and-center at current position
     * @return true if vision succeeded and gantry moved to detected center
     */
    bool RunVisionDetectCenter(
        const std::string& profilePath,
        const std::string& device,
        std::string& errorMessage);
};
