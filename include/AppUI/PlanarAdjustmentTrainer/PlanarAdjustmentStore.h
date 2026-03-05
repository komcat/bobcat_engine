// PlanarAdjustmentStore.h - JSON persistence for planar adjustment sets
#pragma once

#include "PlanarAdjustmentData.h"
#include <string>
#include <vector>
#include <optional>

/**
 * @brief Persistence layer for PlanarAdjustmentTrainer sets.
 *
 * Saves/loads sets as JSON files in <basePath>/<name>.json
 * Default basePath is "config/planar_adjustment".
 */
class PlanarAdjustmentStore
{
public:
    PlanarAdjustmentStore();

    void SetBasePath(const std::string& basePath);

    [[nodiscard]] bool SaveSet(const PlanarAdjustmentSet& set);
    [[nodiscard]] std::optional<PlanarAdjustmentSet> LoadSet(const std::string& name);
    [[nodiscard]] std::vector<std::string> ListSets() const;
    [[nodiscard]] bool DeleteSet(const std::string& name);

    std::string GetBasePath() const { return m_basePath; }

private:
    std::string m_basePath;

    std::string MakeFilePath(const std::string& name) const;
    void EnsureDirectory() const;
};
