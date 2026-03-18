// SwirBeamOverlay.h
// GDI+ rendering of beam detection results (boxes, labels, profile charts)
#pragma once

#include <winsock2.h>
#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include "SwirBeamDetector.h"
#include <vector>
#include <array>
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

/// Draw real-time CE (circle energy) charts for B0 and B1.
/// Charts are placed at the top-right of the viewport, stacked vertically.
///
/// @param gfx          GDI+ graphics context
/// @param ceHistory    Array of 2 CE history ring buffers (B0, B1)
/// @param ceIndex      Array of 2 write indices into the ring buffers
/// @param ceCount      Array of 2 filled counts
/// @param historySize  Max entries per ring buffer
/// @param renderX      Viewport left edge (display pixels)
/// @param renderY      Viewport top edge (display pixels)
/// @param renderW      Viewport width (display pixels)
/// @param renderH      Viewport height (display pixels)
void DrawCECharts(Gdiplus::Graphics& gfx,
                  const std::array<std::vector<double>, 2>& ceHistory,
                  const std::array<std::vector<double>, 2>& eeHistory,
                  const std::array<int, 2>& ceIndex,
                  const std::array<int, 2>& ceCount,
                  int historySize,
                  int renderX, int renderY, int renderW, int renderH);

/// Draw magnified beam thumbnails at fixed positions in the viewport.
/// B0 thumbnail at top-right, B1 thumbnail below it.
///
/// @param gfx          GDI+ graphics context
/// @param results      Beam detection results
/// @param frameData    BGR frame buffer (bottom-up DIB order)
/// @param frameW       Camera frame width (image pixels)
/// @param frameH       Camera frame height (image pixels)
/// @param renderX      Viewport left edge (display pixels)
/// @param renderY      Viewport top edge (display pixels)
/// @param renderW      Viewport width (display pixels)
/// @param renderH      Viewport height (display pixels)
void DrawBeamMagnifiers(Gdiplus::Graphics& gfx,
                        const std::vector<BeamDetectionResult>& results,
                        const uint8_t* frameData, uint32_t frameW, uint32_t frameH,
                        int renderX, int renderY, int renderW, int renderH);
