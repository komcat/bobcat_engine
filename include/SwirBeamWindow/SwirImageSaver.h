// SwirImageSaver.h
// Lifts the five SWIR-window save APIs (raw / raw+overlay / per-beam crop /
// collage / one-call package) into a self-contained class. The window keeps
// thin public forwards so SwirBeamWindowManager and the SaveSwirPackage
// system command keep working unchanged.
//
// The saver doesn't own any frame or beam state — it pulls a fresh snapshot
// each call via the callbacks supplied at construction time. That means it's
// safe to invoke from any thread (script, recipe, dispatch handler) without
// blocking detection or display.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "SwirBeamDetector.h"  // BeamDetectionResult

class SwirImageSaver {
public:
    struct Deps {
        // Copy out the current BGR bottom-up frame buffer + dimensions.
        // Returns false if no frame is available (window hidden, no
        // camera, etc.).
        std::function<bool(std::vector<uint8_t>& outFrame,
                           uint32_t& outW, uint32_t& outH)> snapshotFrame;

        // Copy out the live beam-display results (size 0..2).
        std::function<std::vector<BeamDetectionResult>()> snapshotBeams;

        // Active calibration (um/px) for the current detection mode.
        std::function<double()> getMicronsPerPixel;

        // Whether the active detection mode is "blob" — DrawBeamOverlays
        // toggles a couple of visual choices on this flag.
        std::function<bool()>   getUseBlobMode;
    };

    explicit SwirImageSaver(Deps deps);

    bool SaveFrameAsPng(const std::wstring& fullPath);
    bool SaveFrameWithOverlayAsPng(const std::wstring& fullPath);
    bool SaveBeamCropAsPng(int slotIndex, const std::wstring& fullPath);
    bool SaveCollageAsPng(const std::wstring& fullPath);

    // Writes <prefix>_raw.png (required) + _overlay.png + _B0.png + _B1.png +
    // _collage.png (best-effort) into dirPath. outFiles, if non-null,
    // receives the absolute paths written.
    bool SaveImagePackage(const std::wstring& dirPath,
                          const std::wstring& namePrefix,
                          std::vector<std::wstring>* outFiles = nullptr);

private:
    Deps m_deps;
};
