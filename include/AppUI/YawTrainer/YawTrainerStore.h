// YawTrainerStore.h - JSON persistence for yaw trainer sets
#pragma once

#include "YawTrainerData.h"
#include <string>
#include <vector>
#include <optional>

/**
 * @brief Persistence layer for YawTrainer sets.
 *
 * Saves/loads sets as JSON files in <basePath>/<name>.json
 * Default basePath is "config/yaw_trainer".
 */
class YawTrainerStore
{
public:
    YawTrainerStore();

    void SetBasePath(const std::string& basePath);

    [[nodiscard]] bool SaveSet(const YawTrainerSet& set);
    [[nodiscard]] std::optional<YawTrainerSet> LoadSet(const std::string& name);
    [[nodiscard]] std::vector<std::string> ListSets() const;
    [[nodiscard]] bool DeleteSet(const std::string& name);

    std::string GetBasePath() const { return m_basePath; }

private:
    std::string m_basePath;

    std::string MakeFilePath(const std::string& name) const;
    void EnsureDirectory() const;
};
