// CameraOffsetStore.h - JSON persistence for camera offset calibration sets
#pragma once

#include "CameraOffsetData.h"
#include <string>
#include <vector>
#include <optional>

/**
 * @brief Persistence layer for camera offset calibration sets.
 *
 * Saves/loads calibration sets as JSON files in <basePath>/<name>.json
 * Default basePath is "config/SYSTEM/PR_Camera_Offset".
 */
class CameraOffsetStore
{
public:
    CameraOffsetStore();

    /**
     * @brief Set the base directory for calibration set files
     * @param basePath Directory path (should be product-relative when product loaded)
     */
    void SetBasePath(const std::string& basePath);

    /**
     * @brief Save a calibration set to disk
     * @return true on success
     */
    [[nodiscard]] bool SaveCalibrationSet(const CameraOffsetCalibrationSet& cs);

    /**
     * @brief Load a calibration set by name
     * @return The calibration set, or empty optional on failure
     */
    [[nodiscard]] std::optional<CameraOffsetCalibrationSet> LoadCalibrationSet(const std::string& name);

    /**
     * @brief List all saved calibration set names
     */
    [[nodiscard]] std::vector<std::string> ListCalibrationSets() const;

    /**
     * @brief Delete a calibration set by name
     * @return true if deleted
     */
    [[nodiscard]] bool DeleteCalibrationSet(const std::string& name);

    /**
     * @brief Get the base directory path
     */
    std::string GetBasePath() const { return m_basePath; }

private:
    std::string m_basePath;

    std::string MakeFilePath(const std::string& name) const;
    void EnsureDirectory() const;
};
