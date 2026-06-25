// MoeChannelLayout.h
// Single source of truth for deriving the SWIR channel / MOE named positions
// (ch1..ch6 + MOE-A2) on "gantry-main" from the taught MOE-A1 anchor.
//
// The layout is anchored on MOE-A1: X/Z are copied verbatim and Y is shifted in
// the Y- direction by a cumulative per-channel offset (375um to ch1, +500um per
// channel, then a final -375um past ch6 to MOE-A2). This logic is shared by the
// SWIR window "update ch1-6" button and the headless "UpdateChannelsFromMoeA1"
// motion command (Lua / recipe accessible), so both stay in lock-step.
#pragma once

#include <string>
#include <vector>

class UnifiedMotionManager;

namespace bobcat {

// One derived position: a named position on "gantry-main" at a cumulative Y-
// offset (mm) from the taught MOE-A1 anchor.
struct MoeChannelDef {
    const char* position;   // named-position key in the product file
    const char* label;      // human label (ch1..ch6, MOE-A2)
    double      offsetMm;   // Y- offset from MOE-A1 (positive => toward Y-)
};

// The ch1..ch6 + MOE-A2 layout. 375um from MOE-A1 to ch1, +500um per channel,
// then -375um past ch6 to MOE-A2 (cumulative 3.250mm).
const std::vector<MoeChannelDef>& MoeChannelLayout();

// Result of deriving + saving the positions from MOE-A1.
struct MoeChannelDeriveResult {
    bool anchorFound = false;        // false if MOE-A1 is not taught
    int  saved = 0;                  // positions written successfully
    int  total = 0;                  // positions attempted
    std::vector<std::string> failed; // labels that failed to save
    bool ok() const { return anchorFound && saved == total; }
};

// Derive ch1..ch6 + MOE-A2 from the saved "gantry-main" MOE-A1 named position
// and persist each to the product file (waypoints preserved). MOE-A1 must
// already be taught. Returns counts; performs no UI.
MoeChannelDeriveResult DeriveAndSaveMoeChannelsFromMoeA1(UnifiedMotionManager* mm);

}  // namespace bobcat
