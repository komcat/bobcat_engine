# SWIR Real-Time Beam Detection

Real-time beam detection and characterization overlay for the SWIR Beam Processing window, ported from the `beam_analyzer_ui_cpp` project.

## Overview

The system detects up to 2 beams in the live SWIR camera feed, computes d4sigma beam diameters and encircled energy (EE), and renders overlays including bounding boxes, centroid markers, EE circles, Gaussian profile mini-charts, and saturation warnings.

## Architecture

```
OnNewFrame (camera thread)
    |
    +--> BGR frame buffer (for display)
    +--> Grayscale buffer --> condition_variable notify
                                    |
                        Detection Thread (below-normal priority)
                                    |
                            RunBeamDetection()
                              1. uint8 -> CV_32F
                              2. Background subtract (corner patches)
                              3. find_roi() (X/Y projection peaks)
                              4. d4sigma_fast() per ROI
                              5. Filter fake beams
                              6. Compute encircled energy (EE)
                              7. Sort/label B0/B1, assign GA1/GA2
                              8. Saturation check
                              9. Extract 1D profiles
                                    |
                            m_beamResults --> DrawOverlays (paint thread)
                                                |
                                        DrawBeamOverlays()
                                          - Bounding boxes
                                          - Centroid crosshairs
                                          - EE circles (green)
                                          - Grid-snapped multi-row labels (with EE%)
                                          - X/Y profile mini-charts
```

## Files

| File | Purpose |
|------|---------|
| `include/SwirBeamWindow/SwirPeakFinder.h/.cpp` | 1D peak detection (scipy.signal.find_peaks equivalent) |
| `include/SwirBeamWindow/SwirBeamDetector.h/.cpp` | ROI finding, d4sigma calculation, beam labeling, saturation check |
| `include/SwirBeamWindow/SwirBeamOverlay.h/.cpp` | GDI+ rendering of beam overlays (boxes, labels, profile charts) |
| `include/SwirBeamWindow/SwirBeamWindow.h/.cpp` | Integration: toggle button, frame extraction, detection thread, overlay call |

## Detection Pipeline

### 1. Peak Finding (`SwirPeakFinder`)

Direct port of `peak_finder.h/.cpp`. Finds peaks in a 1D signal with:
- **Height filtering** - minimum peak amplitude
- **Distance filtering** - minimum spacing between peaks (keeps tallest)
- **Width filtering** - minimum width at half-prominence

### 2. ROI Detection (`find_roi`)

Projects the image onto X and Y axes (column/row sums), finds peaks in each projection, and creates rectangular ROIs at peak intersections.

```
Image --> X projection (sum columns) --> find_peaks --> X bands
      --> Y projection per X band     --> find_peaks --> Y bands
      --> Combine into ROI rectangles
```

Adjacent ROIs closer than `combineXThresh`/`combineYThresh` are merged.

### 3. D4Sigma Calculation (`d4sigma_fast`)

Single-pass beam diameter using second-moment method:

```
cx = sum(I * x) / sum(I)           centroid X
cy = sum(I * y) / sum(I)           centroid Y
dx = 4 * sqrt(var_x)               d4sigma diameter X
dy = 4 * sqrt(var_y)               d4sigma diameter Y

where I = E^2 (intensity), thresholded at 5% of max
```

Uses `E[X^2] - E[X]^2` variance formula for single-pass efficiency.

### 4. Beam Filtering

Beams are rejected if:
- d4sigma diameter is zero (no signal)
- Diameter > 50% of image dimension (not a localized beam)
- Diameter > 1.5x the ROI size (diffuse noise)

### 5. Encircled Energy (EE)

After d4sigma computation and filtering, Encircled Energy is computed for each beam candidate via `ComputeEncircledEnergy()`. EE measures the fraction of total beam energy concentrated within the d4sigma circle:

```
radius = max(dx, dy) / 2       (larger d4sigma half-width)
totalEnergy  = sum of all positive pixels in ROI
circleEnergy = sum of pixels within radius of (cx, cy)
EE = circleEnergy / totalEnergy     (0.0 to 1.0)
```

EE is computed on the background-subtracted float image (`floatMat`), not the amplitude field. Only positive pixels contribute. The single-pass algorithm iterates over the ROI and tests each pixel's distance from the centroid against the radius.

**Interpretation:**
- Well-focused Gaussian beam: EE > 80%
- Defocused or misaligned beam: EE drops
- EE is independent of absolute intensity — it's a shape metric

EE is used as a real-time feedback metric during manual hexapod alignment. The user adjusts the hexapod while watching EE% change in the overlay.

### 6. Labeling

```
1. Sort detected beams by intensity (sum of pixel values), keep top 2
2. Sort remaining by cy ascending:
   - B0 = smaller cy (higher on image)
   - B1 = larger cy (lower on image)
3. GA assignment (only if beams are in opposite halves):
   - B0 in top half  --> GA2
   - B1 in bottom half --> GA1
   - Both in same half --> no GA label
```

### 7. Saturation Check

Scans original uint8 pixels within each beam's ROI. If any pixel exceeds the saturation threshold (default 200), the beam is flagged as saturated.

## Overlay Rendering

### Bounding Box
- Drawn around d4sigma extent with 1.25x padding
- Cyan for normal beams, red for saturated

### Centroid Crosshair
- Yellow 10px cross at (cx, cy)

### EE Circle
- Green semi-transparent ellipse centered on (cx, cy)
- Radius = `max(dx, dy) / 2` (the d4sigma half-width used for EE computation)
- Visually shows the encircled energy integration boundary

### Text Label (Grid-Snapped)
- Position snapped to a 20px display grid to prevent jitter
- Multi-row layout:
  ```
  B0 (GA2)
  cx:    8.8
  cy:    4.2
  dx:   20.6
  dy:    7.0
  EE:  85.3%
  ```
- Semi-transparent black background
- Title row turns red if saturated (appends "SAT!")

### Profile Charts
- Square charts sized at 2.5x beam diameter (image pixels)
- **X profile** (cyan): horizontal cross-section at centroid row, positioned above beam
- **Y profile** (magenta): vertical cross-section at centroid column, positioned right of beam
- Yellow tick marks show d4sigma extent within each chart
- Semi-transparent dark background

### FPS Counter
- Lower-left corner of viewport
- Updated every 1 second
- Light green text on dark background

## Configuration

Default `BeamDetectionConfig`:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `xpeaks.height` | -1 | X projection peak minimum height (-1 = no filter) |
| `xpeaks.distance` | -1 | X projection minimum peak spacing |
| `xpeaks.width` | -1 | X projection minimum peak width |
| `ypeaks.*` | -1 | Same filters for Y projection |
| `combineXThresh` | 20.0 | Merge X ROIs closer than this (pixels) |
| `combineYThresh` | 20.0 | Merge Y ROIs closer than this (pixels) |
| `d4sigmaThreshold` | 0.05 | Intensity threshold as fraction of max (P < thresh * maxP zeroed) |
| `maxBeams` | 2 | Maximum number of beams to report |
| `saturationValue` | 200 | uint8 pixel value above which saturation is flagged |
| `roiWidthFactor` | 2.5 | ROI extends peak_width * this factor from peak center |

## Threading Model

- **Camera thread** (`OnNewFrame`): copies grayscale into detection buffer, signals condition variable
- **Detection thread** (below-normal priority): waits on CV, runs `RunBeamDetection()`, publishes results
- **Paint thread** (`OnPaint` / `DrawOverlays`): reads results under mutex, calls `DrawBeamOverlays()`
- Frame skipping is natural: if detection is slower than camera, the CV picks up the latest frame

## Usage

1. Open the SWIR Beam Processing window
2. Click **BEAM DETECT** in the TOOLS panel to toggle detection on/off
3. Beam overlays appear on the live camera view when beams are detected
4. Detection runs continuously at background priority with no impact on frame rate

## Dependencies

- **OpenCV** (`HAS_OPENCV`): Required for `SwirBeamDetector` (cv::Mat operations)
- **GDI+**: Used by `SwirBeamOverlay` for all rendering (already available in the window)
- `SwirPeakFinder` has no external dependencies (pure C++ STL)
