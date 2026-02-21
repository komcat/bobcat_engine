// AccuracyTrainerStore.h - JSON persistence for training sets
#pragma once

#include "AccuracyTrainerData.h"
#include <string>
#include <vector>
#include <optional>

/**
 * @brief Persistence layer for AccuracyTrainer training sets.
 *
 * Saves/loads training sets as JSON files in config/accuracy_trainer/<name>.json
 */
class AccuracyTrainerStore
{
public:
    AccuracyTrainerStore();

    /**
     * @brief Set the base directory for training set files
     * @param basePath Directory path (default: "config/accuracy_trainer")
     */
    void SetBasePath(const std::string& basePath);

    /**
     * @brief Save a training set to disk
     * @return true on success
     */
    [[nodiscard]] bool SaveTrainingSet(const TrainingSet& ts);

    /**
     * @brief Load a training set by name
     * @return The training set, or empty optional on failure
     */
    [[nodiscard]] std::optional<TrainingSet> LoadTrainingSet(const std::string& name);

    /**
     * @brief List all saved training set names
     */
    [[nodiscard]] std::vector<std::string> ListTrainingSets() const;

    /**
     * @brief Delete a training set by name
     * @return true if deleted
     */
    [[nodiscard]] bool DeleteTrainingSet(const std::string& name);

    /**
     * @brief Get the base directory path
     */
    std::string GetBasePath() const { return m_basePath; }

private:
    std::string m_basePath;

    std::string MakeFilePath(const std::string& name) const;
    void EnsureDirectory() const;
};
