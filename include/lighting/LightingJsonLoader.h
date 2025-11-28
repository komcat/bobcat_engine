#pragma once
#include "LightingManager.h"
#include <string>

namespace Lighting {

class LightingJsonLoader {
public:
    // Load configuration from file and populate manager
    // Returns number of controllers loaded, or 0 on failure
    static int LoadFromFile(LightingManager& manager, const std::string& filepath);

    // Save current configuration to file
    static bool SaveToFile(const LightingManager& manager, const std::string& filepath);
};

} // namespace Lighting
