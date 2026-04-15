// PlacementRetraceLogger.h — JSONL append-only audit log for retrace runs.
//
// One line per retrace attempt in {productDir}/placement_correction/
// {setName}.retrace_log.jsonl. File rotates at 10 MB to *.jsonl.1 (single
// backup, mirrors SafetyEventLog pattern).
//
// Call sites: PlacementRetraceWidget on outcome (saved/skipped/ended/failed).
// The viewer widget (RetraceLogViewer) reads the same file back for display.
#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "PlacementCorrectionData.h"

// Per-point metadata captured during a retrace run. Populated by the widget
// as each of the 4 steps completes (manually or via auto-detect).
struct RetracePointMeta {
    bool autoDetected = false;
    int  visionIters = 0;         // 0 if manual; 1..N if auto-detect iterated
    double finalResidualMm = 0.0; // residual from the last vision iteration
    std::string reason;           // "manual" | "auto" | "auto-failed-manual"
};

// Snapshot of a single retrace outcome. Built by the widget, serialized to
// JSONL by the logger.
struct RetraceLogEntry {
    std::string timestamp;
    std::string setName;
    std::string setUid;
    std::string mode;              // "manual" | "autoRunAll" | "recipe"
    std::string outcome;           // "saved" | "skipped" | "ended" | "failed"

    std::string measureDevice;
    std::string targetDevice;
    std::string masterPositionName;
    std::string targetPositionName;

    // Retraced points (XYZ per corner)
    double retracedRefP1x = 0.0, retracedRefP1y = 0.0, retracedRefP1z = 0.0;
    double retracedRefP2x = 0.0, retracedRefP2y = 0.0, retracedRefP2z = 0.0;
    double retracedNrmP1x = 0.0, retracedNrmP1y = 0.0, retracedNrmP1z = 0.0;
    double retracedNrmP2x = 0.0, retracedNrmP2y = 0.0, retracedNrmP2z = 0.0;
    double retracedRefAngleDeg = 0.0;
    double retracedNrmAngleDeg = 0.0;

    // Frame math
    double taughtRefAngleDeg = 0.0;
    double frameDeltaThetaDeg = 0.0;

    // Design offset + rotated offset
    double designOffsetX = 0.0, designOffsetY = 0.0;
    double rotatedOffsetX = 0.0, rotatedOffsetY = 0.0;

    // Ideal target + correction
    double idealTargetX = 0.0, idealTargetY = 0.0;
    double correctionGlobalX = 0.0, correctionGlobalY = 0.0;
    double correctionLocalX = 0.0, correctionLocalY = 0.0, correctionLocalZ = 0.0;

    // Hex state
    double liveHexX = 0.0, liveHexY = 0.0, liveHexZ = 0.0;
    double liveHexU = 0.0, liveHexV = 0.0, liveHexW = 0.0;

    // Tune offsets (operator bias)
    double tuneOffsetX = 0.0, tuneOffsetY = 0.0, tuneOffsetZ = 0.0;
    double tuneOffsetU = 0.0, tuneOffsetV = 0.0, tuneOffsetW = 0.0;
    bool applyUCorrection = true;

    // Corrected pose written to targetPositionName
    double correctedX = 0.0, correctedY = 0.0, correctedZ = 0.0;
    double correctedU = 0.0, correctedV = 0.0, correctedW = 0.0;

    // Per-step meta (order: refP1, refP2, nrmP1, nrmP2)
    RetracePointMeta steps[4];

    // Optional failure context
    std::string failureStep;    // "refP1" | "refP2" | "nrmP1" | "nrmP2" | ""
    std::string failureDetail;  // short description of why
};

class PlacementRetraceLogger {
public:
    // Resolve the log file path for the given set under the given product
    // directory. If productDir is empty, falls back to "config/placement_correction".
    [[nodiscard]] static std::string MakeLogPath(const std::string& productDir,
                                                  const std::string& setNameOrSourceFile);

    // Append a single JSONL entry. Returns true on write success. Creates
    // parent directory if missing. Rotates the file at 10 MB to *.jsonl.1.
    [[nodiscard]] static bool Append(const std::string& logPath,
                                      const RetraceLogEntry& entry);

    // Read all entries from the file (oldest → newest). Returns empty on
    // missing/empty file. Malformed lines are skipped with a warning.
    [[nodiscard]] static std::vector<nlohmann::json> ReadAll(const std::string& logPath);

    // Same as MakeLogPath but resolves productDir automatically via the set's
    // sourceFilename plus ApplicationContext.productPositionManager. Returns
    // "" if context/product is unavailable.
    [[nodiscard]] static std::string ResolvePathForSet(
        const PlacementCorrectionSet& set,
        void* productPositionMgr);   // opaque to avoid extra includes
};
