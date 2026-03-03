// CameraOffsetCommandHandler.h
// Headless camera offset move for recipe/sequence execution
#pragma once

#include "ICommandHandler.h"

struct ApplicationContext;

/**
 * @brief Command handler for moving gantry between low-mag and high-mag camera positions
 *        using a previously calibrated camera offset.
 *
 * Loads a CameraOffsetCalibrationSet JSON file, reads the calibrated XYZ offset,
 * and applies it to the current gantry position to switch camera views.
 *
 * Device type: "cameraoffset" (virtual, no real hardware)
 *
 * Supported commands:
 *
 * - MoveToHighMag: Apply +offset from current position (low-mag -> high-mag)
 *     Parameters:
 *       calibration_set_name  - Name of the calibration set (resolved from product folder)
 *       velocity              - (optional) Move velocity, default 100.0
 *
 * - MoveToLowMag: Apply -offset from current position (high-mag -> low-mag)
 *     Parameters:
 *       calibration_set_name  - Name of the calibration set (resolved from product folder)
 *       velocity              - (optional) Move velocity, default 100.0
 *
 * - MoveToHighMagAbs: Move to the absolute calibrated high-mag position
 *     Parameters:
 *       calibration_set_name  - Name of the calibration set (resolved from product folder)
 *       velocity              - (optional) Move velocity, default 100.0
 *
 * - MoveToLowMagAbs: Move to the absolute calibrated low-mag position
 *     Parameters:
 *       calibration_set_name  - Name of the calibration set (resolved from product folder)
 *       velocity              - (optional) Move velocity, default 100.0
 *
 * Results stored in SequenceContext:
 *   camera_offset.result.success        - "true"/"false"
 *   camera_offset.result.from_x/y/z     - position before move
 *   camera_offset.result.to_x/y/z       - position after move
 *   camera_offset.result.offset_x/y/z   - applied offset
 */
class CameraOffsetCommandHandler : public ICommandHandler
{
public:
    CameraOffsetCommandHandler() = default;
    ~CameraOffsetCommandHandler() override = default;

    bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context) override;

    std::string GetManagerType() const override { return "cameraoffset"; }

    void SetContext(ApplicationContext* context) { m_appContext = context; }

private:
    ApplicationContext* m_appContext = nullptr;

    /**
     * @brief Move gantry by applying the camera offset (relative move)
     * @param lowToHigh true = add offset (low->high), false = subtract offset (high->low)
     */
    bool HandleRelativeMove(
        bool lowToHigh,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Move gantry to the absolute calibrated position
     * @param toHighMag true = move to high-mag position, false = move to low-mag position
     */
    bool HandleAbsoluteMove(
        bool toHighMag,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context);

    /**
     * @brief Retract the pneumatic slide (blocking) if configured
     * @return true on success or if no slide is configured
     */
    bool EnsurePneumaticRetracted(const std::string& slideName, std::string& errorMessage);

    /**
     * @brief Extend the pneumatic slide (blocking) if configured
     * @return true on success or if no slide is configured
     */
    bool ActivatePneumaticExtend(const std::string& slideName, std::string& errorMessage);
};
