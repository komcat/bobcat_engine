// EdgeTransferStore.h - JSON persistence for Edge Transfer trainer sets
#pragma once

#include "EdgeTransferData.h"
#include <string>
#include <vector>
#include <optional>

/**
 * @brief Persistence layer for EdgeTransfer sets.
 *
 * Saves/loads sets as JSON files in <basePath>/<name>.json
 * Default basePath is "config/edge_transfer"; EdgeTransferPage
 * redirects to the currently-loaded product directory when available.
 */
class EdgeTransferStore
{
public:
    EdgeTransferStore();

    void SetBasePath(const std::string& basePath);

    [[nodiscard]] bool SaveSet(const EdgeTransferSet& set);
    [[nodiscard]] std::optional<EdgeTransferSet> LoadSet(const std::string& name);
    [[nodiscard]] std::vector<std::string> ListSets() const;
    [[nodiscard]] bool DeleteSet(const std::string& name);

    std::string GetBasePath() const { return m_basePath; }

private:
    std::string m_basePath;

    std::string MakeFilePath(const std::string& name) const;
    void EnsureDirectory() const;
};
