// SwirWorldMarkerStore.h
// Owns the SWIR window's set of world-locked V/H line markers — the ones
// the user drops at "current gantry XY" to label features in real-world
// coordinates. Provides Place / ClearAll / Snapshot + JSON persistence.
//
// The window keeps the rendering (DrawWorldMarkers) and the dialog-style
// click handlers (which call MessageBoxW); this class owns just the
// state and JSON I/O so SwirBeamWindow stops carrying the mutex and
// per-marker logic.
#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "SwirBeamWindow.h"  // for SwirWorldMarker / SwirMarkerType definitions

class SwirWorldMarkerStore {
public:
    // Reads the current gantry XY (in mm) into the supplied refs.
    // Returning false means "no motion controller available" — Place()
    // will then fail without recording anything.
    using GetGantryXYFn = std::function<bool(double& x, double& y)>;

    explicit SwirWorldMarkerStore(GetGantryXYFn getGantry);

    // Path used by SaveToFile / LoadFromFile. Empty path disables I/O.
    void SetFilePath(const std::string& path);

    // Drop a marker at the current gantry XY. The label "V<id>" / "H<id>"
    // is auto-assigned. Returns false if the gantry getter failed.
    bool Place(SwirMarkerType type);

    // Drop all markers and reset the id counter. Doesn't write to disk —
    // the caller does that explicitly via SaveToFile().
    void ClearAll();

    // Thread-safe snapshot for rendering / read-only inspection.
    std::vector<SwirWorldMarker> Snapshot() const;

    bool SaveToFile() const;
    bool LoadFromFile();

private:
    GetGantryXYFn m_getGantry;
    std::string m_filePath;
    mutable std::mutex m_mutex;
    std::vector<SwirWorldMarker> m_markers;
    int m_nextId = 1;
};
