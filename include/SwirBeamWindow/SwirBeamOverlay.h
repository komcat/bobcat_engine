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
/// @param micronsPerPixel Pixel-to-micron calibration for dx/dy/area labels
/// @param textScale       Overlay text size multiplier (1.0 = 100%)
void DrawBeamOverlays(Gdiplus::Graphics& gfx,
                      const std::vector<BeamDetectionResult>& results,
                      int renderX, int renderY, int renderW, int renderH,
                      uint32_t frameW, uint32_t frameH,
                      bool blobMode = false,
                      double micronsPerPixel = 1.0,
                      double textScale = 1.0);

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
                  int renderX, int renderY, int renderW, int renderH,
                  bool blobMode = false,
                  double chartScale = 1.0);

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

/// Draw a horizontal bottom-strip of close-up beam crops, two tiles max.
/// Each tile is 200x200; the cropped region (BeamDetectionResult.d4.roi) is
/// scaled to fit while preserving aspect ratio (letterboxed). Empty slots
/// render as dark placeholders so the strip layout stays stable across miss
/// frames.
///
/// @param gfx        GDI+ graphics context
/// @param results    Beam detection results (slot 0 = B0, slot 1 = B1)
/// @param frameData  BGR frame buffer (bottom-up DIB order)
/// @param frameW     Camera frame width (image pixels)
/// @param frameH     Camera frame height (image pixels)
/// @param stripRect  Bottom-strip rect in client coords; tiles are anchored
///                   to the left edge with a 10px margin
/// @param tileSize   Square tile side length in display px (default 200)
void DrawBeamCropStrip(Gdiplus::Graphics& gfx,
                       const std::vector<BeamDetectionResult>& results,
                       const uint8_t* frameData,
                       uint32_t frameW, uint32_t frameH,
                       const RECT& stripRect,
                       int tileSize = 200);

/// Render one beam tile (heatmap-coloured close-up) at (dstX, dstY) with size
/// `tileSize`. Used both by the live bottom strip and by the still-image save
/// APIs. Pass `slotIndex` only for the corner label ("B0" / "B1"); the actual
/// data comes from `beam`.
///
/// @param gfx        GDI+ graphics context
/// @param beam       Beam result whose d4.roi is the source crop
/// @param frameData  BGR frame buffer (bottom-up DIB order)
/// @param frameW     Camera frame width
/// @param frameH     Camera frame height
/// @param dstX       Tile top-left X in target coordinates
/// @param dstY       Tile top-left Y in target coordinates
/// @param tileSize   Square tile side length (e.g. 200)
/// @param slotIndex  0 -> "B0", 1 -> "B1", -1 -> no label
void DrawSingleBeamCropTile(Gdiplus::Graphics& gfx,
                            const BeamDetectionResult& beam,
                            const uint8_t* frameData,
                            uint32_t frameW, uint32_t frameH,
                            int dstX, int dstY, int tileSize,
                            int slotIndex);
