// DiagVerbose.h
//
// Single gate for verbose diagnostic stdout: the [CamWin/...] updateViewport
// stats (QtCameraWindow) and the [LongEvent] slow-event lines
// (LongEventDetector). Both are silenced by default — they flood the log at
// steady state and are only useful when chasing a UI-thread performance
// regression.
//
// Set the BOBCAT_DIAG_VERBOSE environment variable to "1", "true", or "TRUE"
// to re-enable them. The value is read once on first use and cached.
#pragma once

#include <cstdlib>
#include <string>

namespace diag {

// Returns true when BOBCAT_DIAG_VERBOSE is set to an enabling value.
// Evaluated once; cheap to call repeatedly afterwards.
inline bool VerboseEnabled() {
    static const bool enabled = []() {
        char* env = nullptr;
        size_t len = 0;
        bool on = false;
        if (_dupenv_s(&env, &len, "BOBCAT_DIAG_VERBOSE") == 0 && env != nullptr) {
            const std::string value(env);
            on = (value == "1" || value == "true" || value == "TRUE");
        }
        free(env);  // safe on nullptr
        return on;
    }();
    return enabled;
}

}  // namespace diag
