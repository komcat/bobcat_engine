// PlacementCorrectionStore.h - JSON persistence for placement correction trainer sets
#pragma once

#include "PlacementCorrectionData.h"
#include <string>
#include <vector>
#include <optional>

/**
 * @brief Persistence layer for PlacementCorrection sets.
 *
 * Saves/loads sets as JSON files in <basePath>/<name>.json
 * Default basePath is "config/placement_correction".
 */
class PlacementCorrectionStore
{
public:
    PlacementCorrectionStore();

    void SetBasePath(const std::string& basePath);

    [[nodiscard]] bool SaveSet(const PlacementCorrectionSet& set);
    [[nodiscard]] std::optional<PlacementCorrectionSet> LoadSet(const std::string& name);
    [[nodiscard]] std::vector<std::string> ListSets() const;
    [[nodiscard]] bool DeleteSet(const std::string& name);

    std::string GetBasePath() const { return m_basePath; }

private:
    std::string m_basePath;

    std::string MakeFilePath(const std::string& name) const;
    void EnsureDirectory() const;
};
