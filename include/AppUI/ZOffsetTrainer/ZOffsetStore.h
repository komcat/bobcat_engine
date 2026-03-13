// ZOffsetStore.h - JSON persistence for Z offset training sets
#pragma once

#include "ZOffsetData.h"
#include <string>
#include <vector>
#include <optional>

/**
 * @brief Persistence layer for ZOffsetTrainer sets.
 *
 * Saves/loads sets as JSON files in <basePath>/<name>.json
 * Default basePath is "config/z_offset".
 */
class ZOffsetStore
{
public:
    ZOffsetStore();

    void SetBasePath(const std::string& basePath);

    [[nodiscard]] bool SaveSet(const ZOffsetSet& set);
    [[nodiscard]] std::optional<ZOffsetSet> LoadSet(const std::string& name);
    [[nodiscard]] std::vector<std::string> ListSets() const;
    [[nodiscard]] bool DeleteSet(const std::string& name);

    std::string GetBasePath() const { return m_basePath; }

private:
    std::string m_basePath;

    std::string MakeFilePath(const std::string& name) const;
    void EnsureDirectory() const;
};
