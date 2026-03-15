// SwirBeamOverlay.h
// GDI+ rendering of beam detection results (boxes, labels, profile charts)
#pragma once

#include <winsock2.h>
#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include "SwirBeamDetector.h"
#include <vector>
#include <cstdint>

/// Draw beam detection overlays onto the camera viewport.
/// Renders bounding boxes, centroid crosshairs, labels, and profile charts.
///
/// @param gfx       GDI+ graphics context
/// @param results   Beam detection results to draw
/// @param renderX   Viewport left edge (display pixels)
/// @param renderY   Viewport top edge (display pixels)
/// @param renderW   Viewport width (display pixels)
/// @param renderH   Viewport height (display pixels)
/// @param frameW    Camera frame width (image pixels)
/// @param frameH    Camera frame height (image pixels)
void DrawBeamOverlays(Gdiplus::Graphics& gfx,
                      const std::vector<BeamDetectionResult>& results,
                      int renderX, int renderY, int renderW, int renderH,
                      uint32_t frameW, uint32_t frameH);
