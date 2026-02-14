# D4Sigma Method Comparison: Halcon (C++) vs Python

Comparison of beam parameter fitting results between the native Halcon C++ pipeline and the Python `beam_detection.py` pipeline, using the same `small_data/` z-scan images (9 slices, 1451-1531um, 10um steps).

## Background: M², D4Sigma, and Beam Propagation

### What is M² (Beam Quality Factor)?

M² (pronounced "M-squared") quantifies how close a laser beam is to an ideal Gaussian beam. It is defined by the ISO 11146 standard and is one of the most important parameters in optical system characterization.

**Physical meaning:**
- **M² = 1.0** — a perfect, diffraction-limited Gaussian TEM00 beam. This is the theoretical minimum.
- **M² > 1.0** — a real beam that diverges faster than the ideal. Higher M² means worse beam quality.
- **M² < 1.0** — physically impossible. If a measurement gives M² < 1.0, the measurement is wrong.

**Typical values:**
| Source | Typical M² |
|--------|-----------|
| Single-mode fiber output | 1.0 – 1.1 |
| Good quality laser diode | 1.1 – 1.3 |
| Multi-mode fiber, one axis | 1.2 – 2.0 |
| Heavily aberrated beam | 2.0 – 10+ |

### Why M² Matters for SIPHOG/MOE Alignment

In MOE (Micro-Optical Element) screener applications, M² tells you:

1. **Optical alignment quality** — a well-aligned fiber-to-lens system produces M² near 1.0. Higher M² indicates misalignment, aberrations, or mode mismatch.
2. **Coupling efficiency prediction** — M² directly affects how well a beam can be focused or coupled into a fiber. Higher M² means the beam focuses to a larger spot.
3. **Pass/fail criteria** — production screening uses M² thresholds to accept or reject optical assemblies.
4. **TX/RX symmetry** — comparing M² between transmit and receive beams reveals asymmetric defects in the optical train.

### What is D4Sigma?

D4Sigma (D4σ) is the ISO 11146 standard method for measuring beam width. It uses the second moment (variance) of the intensity distribution:

```
d4σ_x = 4 × √[ Σ I(x,y)·(x - x_c)² / Σ I(x,y) ]
```

where `x_c = Σ I(x,y)·x / Σ I(x,y)` is the intensity-weighted centroid.

**Why D4Sigma (not FWHM)?**
- FWHM (Full Width at Half Maximum) only looks at the 50% intensity contour — it ignores beam wings entirely
- D4Sigma uses ALL pixel intensities weighted by distance from center — it captures the full beam shape including wings
- For a perfect Gaussian beam, `D4σ = 2w` where `w` is the 1/e² beam radius
- D4Sigma is the ONLY beam width definition that correctly propagates through the Gaussian beam equation, making it the required input for M² calculation

### How M² is Measured (Z-Scan Method)

The beam diameter is measured at multiple z-positions (z-scan) along the propagation axis. The resulting D4Sigma vs z data forms a hyperbolic "V" shape:

```
d4σ(z)
  │
  │  ╲                    ╱    ← Far-field (diverging)
  │    ╲                ╱
  │      ╲            ╱
  │        ╲        ╱
  │          ╲    ╱
  │            ╲╱              ← Waist (minimum diameter = 2·w₀)
  │             z₀
  └──────────────────────── z
```

The Gaussian M² beam propagation model is then fitted:

```
d(z) = 2·w₀ · √[ 1 + ((z - z₀) / z_R)² ]

where:
  w₀  = beam waist radius (half of minimum D4Sigma)
  z₀  = waist position along z-axis
  z_R = Rayleigh range = π·w₀² / (M²·λ)
  λ   = wavelength
  M²  = beam quality factor (the fitted parameter)
```

**Fit parameters**: `z₀` (waist position), `w₀` (waist radius), `M²` (beam quality)

A steeper V-curve (more divergence relative to waist size) gives higher M². A flatter curve (less divergence) gives lower M².

### Poynting Angle

The Poynting angle describes the beam's propagation direction — the angle between the beam trajectory and the optical axis. It is measured by tracking the centroid position across z-steps:

```
centroid_x(z) = a·z + b    →    θ_x = arctan(a)
centroid_y(z) = a·z + b    →    θ_y = arctan(a)
```

A non-zero Poynting angle means the beam is "walking" laterally as it propagates. This is critical for:
- **Fiber alignment** — off-axis beams couple poorly into fibers
- **Array uniformity** — in multi-channel devices, beams should be parallel
- **TX/RX reciprocity** — transmit and receive beams should be co-aligned

## Pipeline Differences

| Aspect | Halcon C++ (Native) | Python (`beam_detection.py`) |
|--------|---------------------|------------------------------|
| **Image bit depth** | 8-bit (byte) via `GenImage1("byte")` | Float64 via `PIL.Image.convert("L")` |
| **Background subtraction** | Corner-average on byte, then `ScaleImage(1.0, -bgMean)` to float | Corner-average on float, clip negatives to 0 |
| **Image rotation** | `cv::flip(image, -1)` when `rotate_180=true` | `np.rot90(img, k=2)` when `rotate_180=True` |
| **Beam detection** | Threshold at `bgMean + minSignalAboveBg`, connected regions, area filter | Peak finding on X/Y projections (`scipy.signal.find_peaks`) |
| **D4Sigma region** | Expanded bounding box of detected region, thresholded at `d4sigmaThresh * peakSignal` | Tight crop around beam (2.5x d4sigma from center), no threshold |
| **D4Sigma input** | Intensity `I(x,y)` directly | `\|E\|^2` where `E = sqrt(I)`, effectively same as `I` |
| **D4Sigma formula** | `4 * sqrt(sum(I*(x-xc)^2) / sum(I))` (Halcon region-based) | `4 * sqrt(sum(I*(x-xc)^2) / sum(I))` (numpy array-based) |
| **Fitting algorithm** | Levenberg-Marquardt (custom C++ header-only) | `scipy.optimize.curve_fit` (Trust Region Reflective) |
| **Beam model** | `d(z) = 2*w0 * sqrt(1 + ((z-z0)/zR)^2)` fits diameter | `w(z) = w0 * sqrt(1 + ((z-z0)/zR)^2)` fits radius (= d4sigma/2) |
| **Waist output** | Diameter (`2*w0`) | Radius (`w0`) |
| **Poynting Y sign** | Negated (`-atan(dy/dz)`) to match image convention | Negated (`-ny`) per `beam_detection.py` line 270 |

### Key Difference: D4Sigma Integration Region

The primary source of systematic offset is how each method defines the integration region for the second-moment computation:

- **Python**: Crops a tight rectangle (2.5x the d4sigma width from beam center) from the background-subtracted float image. All pixels in the crop contribute. No further thresholding.

- **Halcon**: Finds beam via threshold detection, expands the bounding box by `roiExpandFactor`, then applies a second threshold (`d4sigmaThresh * peakSignal`) within the expanded ROI. Only pixels above this threshold contribute.

This means:
- Higher `d4sigmaThresh` clips beam wings -> underestimates d4sigma -> M^2 too low
- Lower `d4sigmaThresh` with large ROI includes background noise -> overestimates d4sigma -> M^2 too high
- The sweet spot depends on beam brightness, size, and background noise level

## D4Sigma Detection Parameters Reference

### Processing Pipeline

The Halcon D4Sigma plugin processes each image through these steps:

```
Input Image (8-bit grayscale)
    │
    ▼
Step 1: Background Estimation ◄── BG Box Size
    │   Mean intensity of top-left + bottom-right corner boxes
    │
    ▼
Step 2: Signal Image = Original - Background Mean
    │   Converted to float for math
    │
    ▼
Step 3: Beam Detection ◄── Min Signal Above BG, Min/Max Area
    │   Threshold at (bgMean + minSignalAboveBg)
    │   Connected regions → area filter → beam candidates
    │
    ▼
Step 4: For each beam candidate (up to Max Beams):
    │
    ├─ 4a: Peak signal in beam region
    │
    ├─ 4b: Expand bounding box ◄── ROI Expand Factor
    │       expandedROI = bbox ± (bboxSize × roiExpandFactor)
    │
    ├─ 4c: D4Sigma integration region ◄── D4Sigma Threshold
    │       if thresh > 0: keep pixels ≥ thresh × peakSignal
    │       if thresh = 0: keep all pixels with signal > 0 (ISO mode)
    │
    ├─ 4d: Compute centroid (intensity-weighted center of mass)
    │       xc = Σ(I·x) / Σ(I)
    │       yc = Σ(I·y) / Σ(I)
    │
    ├─ 4e: Compute D4Sigma (4× standard deviation of intensity)
    │       d4σ_x = 4 × √[ Σ(I·(x-xc)²) / Σ(I) ]
    │       d4σ_y = 4 × √[ Σ(I·(y-yc)²) / Σ(I) ]
    │
    └─ 4f: Check saturation ◄── Saturation Threshold
            Flag beam if max pixel ≥ threshold
```

### Parameter Definitions

#### BG Box Size (px)
- **Default**: 50
- **Range**: 5 – 500
- **What it does**: Defines the size of square corner regions (top-left and bottom-right) used to estimate the background intensity level. The mean intensity across both corners becomes the background value that is subtracted from the entire image.
- **Effect on D4Sigma**:
  - Too small → noisy background estimate, especially if corners have hot pixels
  - Too large → may overlap with beam signal near image edges, overestimating background → artificially reduces beam signal → smaller d4sigma
  - **Recommendation**: 50px works well for 1280×1024 images where beams are centered

#### Min Signal Above BG
- **Default**: 30
- **Range**: 1 – 255
- **What it does**: Sets the detection threshold as `bgMean + minSignalAboveBg` (gray levels). Only pixels above this threshold are considered when finding beam candidate regions. This is the DETECTION threshold, separate from the D4Sigma threshold.
- **Effect on D4Sigma**:
  - Too low → noise clusters detected as false beams; detected beam bounding box may be larger (includes dim wings), which means the expanded ROI is also larger
  - Too high → misses dim beams entirely; detected bounding box is smaller (only bright core), so expanded ROI captures less of the wings
  - **Recommendation**: 30 works for typical NIR beams on dark background. Reduce for very dim beams.
- **Indirect effect**: The detected bounding box size determines the base for ROI expansion. A smaller detected region × roiExpandFactor = smaller D4Sigma integration area.

#### Min Area (px^2)
- **Default**: 20
- **Range**: 1 – 1,000,000
- **What it does**: Minimum area (in pixels) for a connected region to qualify as a beam candidate. Regions smaller than this are rejected as noise.
- **Effect on D4Sigma**: No direct effect on the D4Sigma computation. Only affects whether a beam is detected at all.
  - Too high → small or tightly focused beams are rejected
  - Too low → noise clusters pass through as false beams

#### Max Area (px^2)
- **Default**: 50000
- **Range**: 1 – 1,000,000
- **What it does**: Maximum area for a beam candidate. Regions larger than this are rejected (e.g., large stray light patches).
- **Effect on D4Sigma**: No direct effect. Only affects beam detection filtering.
  - Too low → large defocused beams or overlapping beams are rejected
  - Too high → stray light regions may be detected as beams

#### Max Beams
- **Default**: 2 (for SIPHOG dual-beam TX/RX)
- **Range**: 1 – 100
- **What it does**: Limits the number of beams processed per image. Beams are sorted by the Halcon region index (spatial position dependent).
- **Effect on D4Sigma**: No effect on the computation. Only limits how many beams are returned.

#### D4Sigma Threshold ⭐ (most critical for M² accuracy)
- **Default**: 0.10 → **Baseline**: 0.03
- **Range**: 0.00 – 0.99
- **What it does**: Within the expanded ROI, only pixels with signal intensity ≥ `d4sigmaThresh × peakSignal` contribute to the D4Sigma second-moment computation. This clips the dim wings of the beam profile.
- **Effect on D4Sigma**:

  A beam intensity cross-section looks like a Gaussian bell curve. The "wings" are the dim tails on either side. The threshold controls how much of the wings are included in the D4Sigma computation:

  ```
  Beam intensity cross-section (1D slice through center):

  I(x)
  100%│          ▄███▄
     │        ▄██   ██▄
     │      ▄██       ██▄
     │    ▄██           ██▄
     │  ▄██    bright     ██▄
  10%│▄██─ ─ ─ core ─ ─ ─ ─██▄─ ─ ─ ─ ─ thresh=0.10
     │██                     ██
   3%│█─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ █─ ─ ─ ─ ─ thresh=0.03
   1%│█        wings          █
     │█▄                     ▄█
   0%│▄▄▄▄▄▄ noise floor ▄▄▄▄▄▄─ ─ ─ ─ thresh=0.00
     └───────────────────────────── x
          ◄─── d4sigma ────►

  thresh=0.10: only measures the bright core (──── short d4sigma)
  thresh=0.03: measures core + wings    (──────── medium d4sigma)
  thresh=0.00: measures everything      (──────────── large d4sigma, includes noise)
  ```

  The key insight is that the wings matter MORE for defocused beams than focused beams:

  ```
  At the WAIST (focused):             At FAR-FIELD (defocused):

  I(x)                                I(x)
  200│    ▄███▄     bright peak       80│
     │  ▄██   ██▄                      │      ▄████████▄      dim, spread out
     │▄██       ██▄                    │   ▄██          ██▄
  20│██           ██  ← 10% thresh    8│▄██              ██▄ ← 10% thresh
     │█             █                   │██     wings      ██
     │█   small     █                   │█  (more area!)    █
     └───────────────── x               └───────────────────── x
        ◄──d4σ──►                          ◄────d4σ────►

  Threshold removes FEW wing pixels    Threshold removes MANY wing pixels
  d4sigma barely affected              d4sigma UNDERESTIMATED
  ```

  This asymmetric clipping makes the d4sigma(z) V-curve appear flatter → lower M²:

  ```
  d4σ(z)
    │
    │ ╲                      ╱   True curve (all wings included)
    │   ╲                  ╱
    │  x  ╲              ╱  x    Measured (wings clipped at far-field)
    │       ╲  x    x  ╱
    │    x    ╲      ╱    x      x = measured points (threshold clips wings
    │           ╲  ╱               more at far-field than at waist)
    │            ╲╱
    └────────────────────── z     Result: flatter curve → smaller M²
  ```

  | Threshold | Beam Wings | d4sigma | M² Effect | Fit Quality |
  |-----------|-----------|---------|-----------|-------------|
  | 0.10 | Heavily clipped | Underestimated | M² < 1.0 (unphysical) | Good |
  | 0.05 | Moderately clipped | Slightly under | M² ≈ 1.0 | Good |
  | **0.03** | **Lightly clipped** | **Balanced** | **M² > 1.0 (valid)** | **Best** |
  | 0.01 | Barely clipped | Slightly over | M² somewhat high | Poor for small beams |
  | 0.00 | No clipping | Overestimated (noise) | M² too high | Variable |
- **Recommendation**: 0.03 gives physically valid M² with good fit quality for typical fiber-coupled NIR beams on 8-bit cameras.

#### ROI Expand Factor ⭐ (second most critical)
- **Default**: 1.5 → **Baseline**: 2.5
- **Range**: 0.0 – 10.0
- **What it does**: After detecting a beam's bounding box, the box is expanded by this factor in all four directions: `expanded = bbox ± (bboxSize × factor)`. The D4Sigma computation only uses pixels within this expanded region.
- **Effect on D4Sigma**:

  ```
  Factor=1.0:  [    ████bbox████    ]  Barely larger than detected beam
  Factor=1.5:  [      ████bbox████      ]  Some wing capture
  Factor=2.5:  [          ████bbox████          ]  Good wing capture
  Factor=5.0:  [                ████bbox████                ]  May include neighboring beam
  ```

  - Too small → beam wings are cut off at the ROI boundary even if D4Sigma threshold allows them → d4sigma underestimated
  - Too large → more background area included → noise pixels contribute (especially with low d4sigmaThresh) → d4sigma overestimated
  - Too large also risks overlapping with adjacent beams in multi-beam images
- **Interaction with D4Sigma Threshold**: These two parameters work together:
  - Large ROI + high threshold → threshold clips wings before ROI boundary matters
  - Large ROI + low threshold → ROI boundary matters, noise contamination risk
  - Small ROI + low threshold → ROI boundary clips wings even without threshold
  - **Best combo**: ROI=2.5, threshold=0.03 — ROI is large enough to capture wings, threshold rejects noise
- **Recommendation**: 2.5 captures sufficient beam wings for typical beams. Match Python's 2.5× crop factor.

#### Saturation Threshold
- **Default**: 250
- **Range**: 1 – 255
- **What it does**: If any pixel within the beam region has a gray value ≥ this threshold, the beam is flagged as `isSaturated = true`. The D4Sigma computation still proceeds, but results may be unreliable.
- **Effect on D4Sigma**: No direct effect on the computation. Only sets a warning flag.
  - Saturated beams have clipped intensity peaks, which distorts the intensity-weighted centroid and second-moment computation
  - A saturated beam's d4sigma will be UNDERESTIMATED because the flat-top peak reduces the apparent variance
  - **Recommendation**: 250 for 8-bit cameras. Reduce camera exposure until no saturation warning.

### Parameter Interaction Summary

```
                    ┌─────────────────────┐
                    │   Background Est.   │
                    │   (BG Box Size)     │
                    └─────────┬───────────┘
                              │ bgMean
                              ▼
                    ┌─────────────────────┐
                    │  Beam Detection     │
                    │  (Min Signal Above  │──── Controls detected bbox SIZE
                    │   BG, Min/Max Area) │     which is the base for ROI expansion
                    └─────────┬───────────┘
                              │ bbox
                              ▼
                    ┌─────────────────────┐
                    │  ROI Expansion      │
                    │  (ROI Expand Factor)│──── Controls how much AREA beyond
                    └─────────┬───────────┘     the detected beam is included
                              │ expandedROI
                              ▼
                    ┌─────────────────────┐
                    │  D4Sigma Region     │
                    │  (D4Sigma Threshold)│──── Controls which PIXELS within
                    └─────────┬───────────┘     the ROI actually contribute
                              │ d4Region
                              ▼
                    ┌─────────────────────┐
                    │  D4Sigma Compute    │──── Centroid + 4×√variance
                    │  (ISO 11146)        │     on selected pixels only
                    └─────────────────────┘
```

The two starred parameters (D4Sigma Threshold and ROI Expand Factor) are the primary knobs for tuning M² accuracy. The others mainly affect beam detection reliability.

## Baseline Halcon D4Sigma Parameters

These parameters were tuned against the Python reference using the `small_data/` z-scan images:

| Parameter | Value | Description |
|-----------|-------|-------------|
| **D4sigma Threshold** | **0.03** | 3% of peak signal. Balances wing capture vs noise rejection |
| **ROI Expand Factor** | **2.5** | Bounding box expansion for wing capture |
| Min Signal Above BG | 30 | Detection threshold (gray levels above background) |
| Min Area (px^2) | 20 | Minimum beam region area |
| Max Area (px^2) | 50000 | Maximum beam region area |
| Max Beams | 2 | Maximum beams to detect per image |
| BG Box Size (px) | 50 | Corner box size for background estimation |
| Saturation Threshold | 250 | Gray level for saturation warning |

### Threshold Tuning History

| d4sigmaThresh | ROI Expand | Beam 0 M2_x | Beam 1 M2_x | Fit Error | Notes |
|---------------|------------|-------------|-------------|-----------|-------|
| 0.10 (default) | 1.5 | 0.89 | 0.94 | 0.65-1.70 | M^2 < 1.0, wing clipping too aggressive |
| 0.00 (ISO) | 1.5 | 1.98 | 1.87 | 0.65-0.86 | Noise contamination inflates d4sigma |
| 0.01 | 2.5 | 2.16 | 1.39 | 8.35-13.5 | Poor fit quality, beam 0 noise-dominated |
| **0.03** | **2.5** | **1.12** | **1.01** | **0.26-1.23** | **Best fit quality, M^2 > 1.0** |

## Comparison Results

Test data: `small_data/` (9 z-scan images, PE-7045_5S, 1451-1531um, 2 beams)

### Beam 0 (TX - smaller beam, top of rotated image)

| Metric | Halcon C++ | Python Reference | Difference |
|--------|-----------|-----------------|------------|
| Waist X | 9.05 um (diam) | 6.89 um (radius) = 13.78 um (diam) | -34% |
| Waist Y | 7.48 um (diam) | 5.72 um (radius) = 11.43 um (diam) | -35% |
| M^2 X | 1.12 | 1.32 | -15% |
| M^2 Y | 1.13 | 1.50 | -25% |
| Waist Z | 1495.7 um | 1498.9 um | -3.2 um |
| Poynting X | +0.262 deg | +0.222 deg | +0.04 deg |
| Poynting Y | -0.342 deg | -0.280 deg | -0.06 deg |
| Fit Error X | 0.27 | 5.56e-15 | - |
| Fit Converged X | false | true | - |

### Beam 1 (RX - larger beam, bottom of rotated image)

| Metric | Halcon C++ | Python Reference | Difference |
|--------|-----------|-----------------|------------|
| Waist X | 9.63 um (diam) | 6.06 um (radius) = 12.11 um (diam) | -20% |
| Waist Y | 10.06 um (diam) | 6.06 um (radius) = 12.13 um (diam) | -17% |
| M^2 X | 1.01 | 1.21 | -17% |
| M^2 Y | 1.18 | 1.16 | +2% |
| Waist Z | 1494.5 um | 1492.0 um | +2.5 um |
| Poynting X | +1.007 deg | +0.949 deg | +0.06 deg |
| Poynting Y | -0.949 deg | -0.919 deg | -0.03 deg |
| Fit Error X | 0.26 | 5.65e-15 | - |
| Fit Converged X | true | true | - |

### Beam Location & TX→RX Vector

Beam centroid positions and the TX→RX separation vector show excellent agreement (sub-micrometer) between the two pipelines:

| Metric | Halcon C++ | Python Reference | Difference |
|--------|-----------|-----------------|------------|
| Beam 0 (TX) loc X | 285.206 um | 285.266 um | -0.06 um |
| Beam 0 (TX) loc Y | 126.662 um | 126.344 um | +0.32 um |
| Beam 1 (RX) loc X | 283.622 um | 283.592 um | +0.03 um |
| Beam 1 (RX) loc Y | 176.272 um | 176.127 um | +0.14 um |
| TX→RX vector X | -1.584 um | -1.674 um | +0.09 um |
| TX→RX vector Y | 49.610 um | 49.783 um | -0.17 um |
| TX→RX distance | 49.636 um | — | — |

**Why beam location matches but waist/M² differs**: Both Halcon and Python use the same centroid formula — the intensity-weighted center of mass (first moment):

```
Halcon (HalconD4SigmaBeamDetectionPlugin.cpp):
    centroid_x = Σ(I · x) / Σ(I)
    centroid_y = Σ(I · y) / Σ(I)

Python (beam_detection.py):
    xc = Σ(I · x) / Σ(I)       # Sz = |E|² = I
    yc = Σ(I · y) / Σ(I)
```

The only difference is which pixels are included:
- **Python**: All pixels in a tight crop around the beam (no threshold)
- **Halcon**: Pixels above `d4sigmaThresh × peakSignal` in the expanded ROI

This barely affects the centroid because it is a **first moment (mean)**. A dim wing pixel at 1% intensity and 50px from center shifts the centroid by ~0.5px — negligible.

In contrast, D4Sigma is a **second moment (variance)**, where the same pixel contributes via `I · (x - x_c)²`. The `(x - x_c)² = 2500` term amplifies the effect of distant dim pixels, making d4sigma highly sensitive to which pixels are included or excluded by the threshold.

```
                    Centroid (1st moment)          D4Sigma (2nd moment)
Pixel weight:       I                              I · (x - xc)²
Dim pixel at
50px from center:   0.01 × 50 = 0.5               0.01 × 50² = 25.0
                    (negligible shift)             (significant contribution)
```

This is why beam locations agree to < 0.3 um while waist sizes differ by 20-35%.

### Summary of Systematic Offsets

| Parameter | Typical Offset | Direction | Cause |
|-----------|---------------|-----------|-------|
| **Beam location** | < 0.3 um | varies | Sub-micrometer; centroid is threshold-insensitive |
| **TX→RX vector** | < 0.2 um | varies | Sub-micrometer; derived from centroids |
| **Poynting angle** | < 0.06 deg | varies | Different centroid computation (threshold vs crop) |
| **Waist Z position** | < 3 um | varies | Different d4sigma shape across z |
| **M^2** | 10-25% lower | C++ < Python | Smaller d4sigma values reduce apparent M^2 |
| **Waist size** | 20-35% smaller | C++ < Python | Halcon d4sigma threshold clips beam wings |

### What Matches Well

- **Beam locations**: Within 0.3 um of Python reference (sub-pixel accuracy)
- **TX→RX vector**: Within 0.2 um of Python reference
- **Poynting angles**: Within 0.06 deg of Python reference (both X and Y)
- **Waist Z position**: Within 3 um of Python reference
- **Beam ordering**: Consistent after 180-degree rotation
- **Fit quality**: Low residuals with baseline parameters (0.26-1.23)
- **M^2 Y for Beam 1**: 1.18 vs 1.16 (within 2%)

### Known Limitations

1. **Waist size systematic offset**: Halcon d4sigma with threshold always gives smaller values than Python's full-crop d4sigma. The 20-35% offset in waist is consistent and could be calibrated out.

2. **8-bit quantization**: Halcon processes byte images, losing dynamic range in beam tails. This contributes to the d4sigma underestimate, especially for dim/defocused beams.

3. **fit_converged=false**: The LM solver sometimes hits the iteration limit (800) without reaching the convergence tolerance (1e-8). Results are still usable if fit_error is low.

## Recipe Configuration

### Live Mode (with hardware)
| Step | Process | Key Parameters |
|------|---------|---------------|
| 1 | D4Sigma Z-Scan | profile=d4sigma_slice.json, slices=N, z_step=0.010mm |
| 2 | Beam: Fit Slices | prefix=splice, num_slices=auto, wavelength=1550e-9 |

### Offline Test Mode (from folder)
| Step | Process | Key Parameters |
|------|---------|---------------|
| 1 | D4Sigma Z-Scan (from Folder) | profile=d4sigma_slice.json, folder=small_data, rotate_180=true |
| 2 | Beam: Fit Slices | prefix=splice, num_slices=auto, wavelength=1550e-9 |

## Fixed-Crop Second Pass (v6-v8, February 2026)

### Problem: Threshold-Dependent D4Sigma Underestimation

The per-image Halcon detection (first pass) uses connected-component thresholding to define the integration region for d4sigma. At defocused z-positions, beams are dimmer and larger, so more wing pixels fall below the detection threshold. This causes **8-18% d4sigma underestimation at far-field z-positions**, producing a flatter d4sigma(z) curve and systematically low waist and M2 values.

```
d4sigma(z)
  |
  | \                      /   True curve (consistent integration region)
  |   \                  /
  |  x  \              /  x    Measured (per-image threshold clips more wings at far-field)
  |       \  x    x  /
  |    x    \      /    x
  |           \  /
  |            \/
  +------------------------ z

  At focus: peak=200, threshold clips at 20 => 90% of beam captured
  At far-field: peak=65, threshold clips at 20 => 70% of beam captured
```

### Solution: Fixed-Crop Second Pass

The Python reference algorithm (`poynt.py`) solves this by:
1. Detecting beams at the **middle z-step** (best SNR, near focus)
2. Computing a **fixed crop rectangle** from the middle step's centroid and d4sigma
3. Applying that **same crop** to all z-steps for d4sigma computation

This ensures consistent integration regions across the entire z-scan, eliminating the threshold-dependent wing clipping.

### Implementation in `CustomServerCommandHandler.cpp`

The second pass was added to `HandleD4SigmaZScanFolder`, executing after the existing Halcon detection loop:

```
Pass 1 (existing):  per-image -> Halcon plugin -> connected component d4sigma -> context
Pass 2 (new):       middle slice reference -> fixed crop -> OpenCV d4sigma -> overwrite context
HandleFitSlices:    reads context (same keys) -> fits Gaussian beam model
```

#### Algorithm Flow

```
1. Collect successful slice indices from Pass 1
2. Pick middle slice (midIdx = successCount / 2)
3. Read middle slice's per-beam centroid and d4sigma from context
4. Compute fixed crop per beam:
     crop_w = d4sigma_x * crop_width_factor (default 2.5)
     crop_h = d4sigma_y * crop_width_factor
     r0 = max(0, centroid_y - crop_h)
     c0 = max(0, centroid_x - crop_w)
     r1 = min(rows, centroid_y + crop_h)
     c1 = min(cols, centroid_x + crop_w)
5. For each z-slice:
     a. Load image (cv::imread UNCHANGED), apply rotate_180 if needed
     b. Convert to float
     c. Background subtraction: 2-corner mean (TL + BR, bgBoxSize from profile)
     d. For each beam: d4sigma_crop(bgSubtracted, r0, c0, r1, c1, noiseFloor=0.0)
     e. Overwrite context keys: beam_N_d4sigma_x_px, beam_N_d4sigma_y_px,
        beam_N_centroid_x_px, beam_N_centroid_y_px
```

#### D4Sigma Crop Helper (`d4sigma_crop`)

```cpp
struct D4CropResult { double cx, cy, d4x, d4y; };

static D4CropResult d4sigma_crop(const cv::Mat& img, int r0, int c0, int r1, int c1,
                                  double noiseFloor = 0.0) {
    // Two-pass: intensity-weighted centroid, then variance
    // Pixels below noiseFloor (after bg subtraction) are zeroed.
    // Pass 1: centroid (first moment)
    // Pass 2: variance (second moment)
    // Returns d4sigma = 4 * sqrt(variance) for both axes
}
```

#### New Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `fixed_crop_enabled` | `"true"` | Enable/disable fixed-crop second pass |
| `crop_width_factor` | `"2.5"` | Crop size = centroid +/- d4sigma x factor |

### Trial-and-Error History

#### v6: Fixed Crop, No Noise Floor (noiseFloor=0.0)

First implementation of the fixed-crop second pass. Used `noiseFloor=0.0` (only clip negatives after background subtraction).

**Results vs Python reference:**

| Metric | v6 Beam 0 | Python B1 (TX) | Error |
|--------|-----------|----------------|-------|
| waist_x | 6.906 um | 7.391 um | -6.6% |
| M2_x | 1.319 | 1.399 | -5.7% |
| poynt_x | 0.223 deg | 0.222 deg | +0.5% |

| Metric | v6 Beam 1 | Python B2 (RX) | Error |
|--------|-----------|----------------|-------|
| waist_x | 6.083 um | 6.355 um | -4.3% |
| M2_x | 1.217 | 1.264 | -3.7% |
| poynt_x | 0.948 deg | 0.947 deg | +0.1% |

**Assessment:** Good results, all M2 > 1.0, fits converge. But initial comparison with Python's `d4sigma_results_bg20.csv` showed C++ d4sigma values were 16-104% larger than the CSV values.

**Misdiagnosis:** The CSV values were from Python's `extract_info()` which uses `threshold=0.1` (10% of peak) — these are thresholded values used only to determine crop sizes. Python's actual fitting in `calc_beam_params()` uses `threshold=0` (no thresholding). The comparison was apples-to-oranges.

#### v7: Fixed Crop with Noise Floor (noiseFloor=20) -- FAILED

Attempted to match d4sigma values to the Python CSV by adding `noiseFloor = minSignalAboveBg = 20` to `d4sigma_crop`. This zeroed all pixels below 20 DN after background subtraction.

**Results vs Python reference:**

| Metric | v7 Beam 0 | Python B1 (TX) | Error |
|--------|-----------|----------------|-------|
| waist_x | 3.900 um | 7.391 um | **-47%** |
| M2_x | 0.594 | 1.399 | **-58%** |
| converged_x | **false** | - | - |

| Metric | v7 Beam 1 | Python B2 (RX) | Error |
|--------|-----------|----------------|-------|
| waist_x | 4.115 um | 6.355 um | **-35%** |
| M2_x | 0.777 | 1.264 | **-39%** |
| converged_x | **false** | - | - |

**Assessment:** Catastrophic failure. M2 < 1.0 (physically impossible), fits did not converge.

**Root cause:** `noiseFloor=20` clipped too much beam wing signal. For a beam with peak=200, 20 DN = 10% threshold (reasonable). But for a dim defocused beam with peak=65, 20 DN = 31% threshold (far too aggressive). The absolute threshold amplified the exact problem we were trying to fix — differential clipping across z-positions.

**Key lesson from poynt.py review:**
- `extract_info()` uses `threshold=0.1` (relative) for beam DETECTION and crop sizing only
- `calc_beam_params()` uses `threshold=0` (none) for the d4sigma values that feed the Gaussian M2 fit
- The fixed crop from the middle slice is tight enough (2.5x d4sigma) that background noise within the crop is minimal — no additional thresholding needed

#### v8: Reverted to No Noise Floor (noiseFloor=0.0) -- FINAL

Reverted to `noiseFloor=0.0`, same as v6. This matches Python's `calc_beam_params(threshold=0)` approach.

**Final results vs Python reference:**

| Metric | v8 Beam 0 | Python B1 (TX) | Error |
|--------|-----------|----------------|-------|
| waist_x | 6.906 um | 7.391 um | -6.6% |
| waist_y | 5.716 um | 6.038 um | -5.3% |
| M2_x | 1.319 | 1.399 | -5.7% |
| M2_y | 1.499 | 1.636 | -8.4% |
| poynt_x | 0.223 deg | 0.222 deg | +0.5% |
| poynt_y | -0.280 deg | -0.276 deg | +1.4% |
| converged | x:yes y:yes | - | - |

| Metric | v8 Beam 1 | Python B2 (RX) | Error |
|--------|-----------|----------------|-------|
| waist_x | 6.083 um | 6.355 um | -4.3% |
| waist_y | 6.090 um | 6.354 um | -4.2% |
| M2_x | 1.217 | 1.264 | -3.7% |
| M2_y | 1.168 | 1.209 | -3.4% |
| poynt_x | 0.948 deg | 0.947 deg | +0.1% |
| poynt_y | -0.918 deg | -0.917 deg | +0.1% |
| converged | x:yes y:no | - | - |

### Remaining Systematic Offset (~5%)

The v8 results show a consistent -4 to -8% offset in waist and M2 relative to Python. Likely causes:

1. **Background subtraction difference**: C++ uses 2-corner bg with `bgBoxSize=100` (from vision profile); Python uses 2-corner with `bgBox=50`. Larger bg box may include faint beam signal near corners, slightly overestimating background, which reduces beam signal and shrinks d4sigma.

2. **Image loading**: C++ loads via `cv::imread(IMREAD_UNCHANGED)` as 8-bit then converts to float; Python loads via PIL and operates in float64 throughout. Minor quantization differences in bg subtraction.

3. **Crop sizing**: C++ crops are based on Halcon's first-pass d4sigma (thresholded), while Python crops are based on its own `extract_info` d4sigma (also thresholded but with different threshold). Different crop sizes lead to slightly different integration regions.

These offsets are consistent and small enough for production use. Poynting angles match within 1.5%.

### Key Lessons

1. **Never apply absolute noise thresholds to d4sigma computation for M2 fitting.** An absolute threshold (e.g., 20 DN) becomes a different relative threshold at each z-position, creating exactly the differential clipping that biases M2.

2. **Fixed crops eliminate threshold sensitivity.** By using the same crop for all z-steps, the integration region is consistent regardless of beam brightness. The tight crop (2.5x d4sigma) naturally limits background noise without needing a threshold.

3. **Distinguish detection thresholds from measurement thresholds.** The Python pipeline uses threshold=0.1 for DETECTION (finding beams, sizing crops) but threshold=0 for MEASUREMENT (d4sigma values fed to the fit). These serve different purposes and should not be conflated.

4. **Compare fit results, not intermediate d4sigma values.** Raw d4sigma values from different methods (thresholded vs unthresholded, different crop sizes) are not directly comparable. The fit outputs (waist, M2, Poynting) are the meaningful comparison points.

## Python Reference Command

```bash
python fit_halcon_results.py --csv small_data/d4sigma_results.csv --folder small_data
```

Produces: `TEST_*.json` with per-beam waist, M^2, Poynting, beam location.
