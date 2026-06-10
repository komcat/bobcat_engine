#pragma once

#include "PicAdjustData.h"
#include <string>

class PicAdjustStore {
public:
    // Default file lives next to the running exe so it is machine-local and
    // gitignored just like other *.local.json files. Caller may override.
    static std::string DefaultFilePath();

    static bool Save(const PicAdjustSet& set,
                     const PicAdjustSettings& settings,
                     const std::string& filePath = DefaultFilePath());
    static bool Load(PicAdjustSet& outSet,
                     PicAdjustSettings& outSettings,
                     const std::string& filePath = DefaultFilePath());
};
