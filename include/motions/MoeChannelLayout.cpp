// MoeChannelLayout.cpp
// Implementation of the ch1..ch6 + MOE-A2 derivation. See MoeChannelLayout.h.
#include "motions/MoeChannelLayout.h"

#include "motions/unified_motion_manager.h"

#include <iostream>

namespace bobcat {

const std::vector<MoeChannelDef>& MoeChannelLayout() {
    // 375um from MOE-A1 to ch1, +500um per subsequent channel, then a final
    // -375um past ch6 to MOE-A2 (2.875 + 0.375 = 3.250mm).
    static const std::vector<MoeChannelDef> kChannels = {
        {"SA 03 04", "ch1",    0.375},
        {"SA 05 06", "ch2",    0.875},
        {"SA 07 08", "ch3",    1.375},
        {"SA 09 10", "ch4",    1.875},
        {"SA 11 12", "ch5",    2.375},
        {"SA 13 14", "ch6",    2.875},
        {"MOE-A2",   "MOE-A2", 3.250},
    };
    return kChannels;
}

MoeChannelDeriveResult DeriveAndSaveMoeChannelsFromMoeA1(UnifiedMotionManager* mm) {
    MoeChannelDeriveResult result;
    if (!mm) {
        return result;  // anchorFound == false
    }

    PositionStruct anchor;
    if (!mm->GetNamedPosition("gantry-main", "MOE-A1", anchor)) {
        return result;  // MOE-A1 not taught
    }
    result.anchorFound = true;

    const std::vector<MoeChannelDef>& channels = MoeChannelLayout();
    result.total = static_cast<int>(channels.size());

    for (const MoeChannelDef& ch : channels) {
        PositionStruct target = anchor;               // copy X/Z (and U/V/W)
        target.y = anchor.y - ch.offsetMm;            // Y- direction
        if (mm->SaveNamedPositionToFile("gantry-main", ch.position, target,
                                        /*preserveWaypoints=*/true)) {
            ++result.saved;
        } else {
            result.failed.emplace_back(ch.label);
            std::cerr << "[MoeChannelLayout] failed to save '" << ch.position
                      << "' (" << ch.label << ")" << std::endl;
        }
    }

    std::cout << "[MoeChannelLayout] derived " << result.saved << "/"
              << result.total << " positions from MOE-A1" << std::endl;
    return result;
}

}  // namespace bobcat
