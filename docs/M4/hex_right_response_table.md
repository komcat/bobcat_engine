# Hex-right axis → beam response rule table

Empirically derived response of the eight beam-pointing readings to a small move on each `hex-right` axis. Verified against SN 7112 plant data (`out/build/x64-debug-hardware/data/Stage main position/7112.json`) on 2026-05-01.

## Cell index

The eight outputs are laid out as a 2x4 grid (B0 row + B1 row, four columns A1.X / A1.Y / A2.X / A2.Y):

| Position | Cell |
|----------|------|
| 1 | B0 row, A1 Poynt.X |
| 2 | B0 row, A1 Poynt.Y |
| 3 | B0 row, A2 Poynt.X |
| 4 | B0 row, A2 Poynt.Y |
| 5 | B1 row, A1 Poynt.X |
| 6 | B1 row, A1 Poynt.Y |
| 7 | B1 row, A2 Poynt.X |
| 8 | B1 row, A2 Poynt.Y |

Compact notation: `2+4−, 6+8−` means cell 2 ↑, cell 4 ↓, cell 6 ↑, cell 8 ↓; unlisted cells are `na` (response below noise floor).

`sm` = small (cross-coupling, typically 5–10× smaller than the dominant cells).
`lg` = larger of the small effects (used in W where A1 Px shifts ~2× more than A2 Px).

## Master table

| Axis | Compact notation | A1.X | A1.Y | A2.X | A2.Y |   | A1.X | A1.Y | A2.X | A2.Y |
|------|------------------|------|------|------|------|---|------|------|------|------|
|      |                  | **B0 (Rx)** |||| | **B1 (Tx)** ||||
| X+   | 2+4+, 6−8−                       | na        | ↑(2) | na        | ↑(4) | | na        | ↓(6) | na        | ↓(8) |
| X−   | 2−4−, 6+8+                       | na        | ↓(2) | na        | ↓(4) | | na        | ↑(6) | na        | ↑(8) |
| Y+   | 1+3+, 5+7+                       | ↑(1)      | na   | ↑(3)      | na   | | ↑(5)      | na   | ↑(7)      | na   |
| Y−   | 1−3−, 5−7−                       | ↓(1)      | na   | ↓(3)      | na   | | ↓(5)      | na   | ↓(7)      | na   |
| Z+   | 2+4−, 6+8+                       | na        | ↑(2) | na        | ↓(4) | | na        | ↑(6) | na        | ↑(8) |
| Z−   | 2−4+, 6−8−                       | na        | ↓(2) | na        | ↑(4) | | na        | ↓(6) | na        | ↓(8) |
| U+   | 2−4−, 6−8+ ; 1−3+5−7+ (sm)       | ↓sm(1)    | ↓(2) | ↑sm(3)    | ↓(4) | | ↓sm(5)    | ↓(6) | ↑sm(7)    | ↑(8) |
| U−   | 2+4+, 6+8− ; 1+3−5+7− (sm)       | ↑sm(1)    | ↑(2) | ↓sm(3)    | ↑(4) | | ↑sm(5)    | ↑(6) | ↓sm(7)    | ↓(8) |
| V+   | 2−4−, 6−8+                       | na        | ↓(2) | na        | ↓(4) | | na        | ↓(6) | na        | ↑(8) |
| V−   | 2+4+, 6+8−                       | na        | ↑(2) | na        | ↑(4) | | na        | ↑(6) | na        | ↓(8) |
| W+   | 2+4−, 6−8+ ; 1+3+5+7+ (A1>A2)    | ↑sm-lg(1) | ↑(2) | ↑sm(3)    | ↓(4) | | ↑sm-lg(5) | ↓(6) | ↑sm(7)    | ↑(8) |
| W−   | 2−4+, 6+8− ; 1−3−5−7− (A1>A2)    | ↓sm-lg(1) | ↓(2) | ↓sm(3)    | ↑(4) | | ↓sm-lg(5) | ↑(6) | ↓sm(7)    | ↓(8) |

## Physical interpretation

| Axis | Type | Dominant Py pattern | Px / cross-coupling | Z differential |
|------|------|---------------------|---------------------|----------------|
| X    | translation | B0/B1 oppose within station; **A1/A2 in unison** within B-row | none | none |
| Y    | translation | none | all four cells common-mode | none |
| Z    | translation | B0/B1 oppose at A1; B0 == B1 at A2 | none | none |
| U    | rotation | B0 == B1 within station; A1/A2 oppose | small Px, A1 and A2 oppose | yes — A1.z ↑ when A2.z ↓ |
| V    | rotation | B0 == B1 within station; A1/A2 same | none | none |
| W    | rotation | B0/B1 oppose AND A1/A2 oppose | Px common-mode, A1 ≈ 2× A2 | none |

## Verification — SN 7112

Walkthrough confirmed four of the six axes against real plant data. Each move below isolates a single hex-right axis (other axes change by < 1e−4):

### W− (step 3 iter 1) — both A1 and A2 measured
`w: 0.5848° → 0.4877°` (Δw = −0.097°)

| # | Cell | Before | After | Δ | Predicted | Match |
|---|------|--------|-------|----|-----------|:-----:|
| 1 | A1.B0.Px | 2.576 | 2.473 | −0.103 | ↓sm-lg | ✓ |
| 2 | A1.B0.Py | 1.591 | 1.025 | −0.566 | ↓ | ✓ |
| 3 | A2.B0.Px | 1.978 | 1.922 | −0.055 | ↓sm | ✓ |
| 4 | A2.B0.Py | −2.034 | −1.387 | +0.647 | ↑ | ✓ |
| 5 | A1.B1.Px | 2.961 | 2.836 | −0.125 | ↓sm-lg | ✓ |
| 6 | A1.B1.Py | 0.120 | 0.710 | +0.590 | ↑ | ✓ |
| 7 | A2.B1.Px | 2.624 | 2.582 | −0.042 | ↓sm | ✓ |
| 8 | A2.B1.Py | 1.577 | 1.336 | −0.241 | ↓ | ✓ |

### V+ (step 4 iter 1) — A1 only
`v: −0.1188° → −0.0188°` (Δv = +0.100°)

| # | Cell | Before | After | Δ | Predicted | Match |
|---|------|--------|-------|----|-----------|:-----:|
| 1 | A1.B0.Px | 2.362 | 2.400 | +0.038 | na | ✓ |
| 2 | A1.B0.Py | 1.726 | 1.121 | −0.605 | ↓ | ✓ |
| 5 | A1.B1.Px | 2.794 | 2.821 | +0.027 | na | ✓ |
| 6 | A1.B1.Py | 0.047 | −0.029 | −0.076 | ↓ | ✓ |

### Y− (step 5 iter 1) — A1 only
`y: −0.8689 → −0.8739` (Δy = −5 µm)

| # | Cell | Before | After | Δ | Predicted | Match |
|---|------|--------|-------|----|-----------|:-----:|
| 1 | A1.B0.Px | 2.753 | 1.799 | −0.954 | ↓ | ✓ |
| 2 | A1.B0.Py | 0.313 | 0.340 | +0.027 | na | ✓ |
| 5 | A1.B1.Px | 3.145 | 2.210 | −0.935 | ↓ | ✓ |
| 6 | A1.B1.Py | −0.019 | 0.010 | +0.029 | na | ✓ |

### X− (step 2 iter 1) — A1 only
`x: −3.2989 → −3.3039` (Δx = −5 µm)

| # | Cell | Before | After | Δ | Predicted | Match |
|---|------|--------|-------|----|-----------|:-----:|
| 1 | A1.B0.Px | 2.837 | 2.857 | +0.020 | na | ✓ |
| 2 | A1.B0.Py | 6.491 | 5.418 | −1.073 | ↓ | ✓ |
| 5 | A1.B1.Px | 3.189 | 3.198 | +0.009 | na | ✓ |
| 6 | A1.B1.Py | −3.803 | −2.926 | +0.877 | ↑ | ✓ |

### X+ (step 3 X-zero phase) — A1 + A2 measured

`x: −3.3214 → −3.3164` (Δx = +5 µm). Hex pose otherwise frozen between 3.3.a1/a2 and 3.f.a1/a2.

| # | Cell | Before | After | Δ | Predicted | Match |
|---|------|--------|-------|----|-----------|:-----:|
| 2 | A1.B0.Py | 0.601 | 1.784 | **+1.183** | ↑ | ✓ |
| 4 | A2.B0.Py | −1.136 | 0.083 | **+1.219** | ↑ | ✓ (corrected from ↓) |
| 6 | A1.B1.Py | 0.923 | 0.084 | **−0.839** | ↓ | ✓ |
| 8 | A2.B1.Py | 0.921 | 0.108 | **−0.813** | ↓ | ✓ (corrected from ↑) |

This isolates X+ with both stations measured. The original X row was wrong on cells 4 and 8 (no A2 X-only data was available when first derived). Real X+ moves the **B0 row up and B1 row down in unison across A1/A2** — i.e. it tilts B0 vs B1 oppositely but doesn't differentiate A1 vs A2. **Implication for the pipeline:** X cannot balance A1 vs A2 — they move together. Only **W** can balance the A1↔A2 differential.

Cross-check from step 2 (X-zero, A1 only inside loop, A2 cross-checked at end):

| | A1.B1.Py (cell 6) | A2.B1.Py (cell 8) |
|---|---:|---:|
| Step 1 baseline | −3.803 | −2.387 |
| After step 2 (5× X−, total ≈ −22.5 µm) | ≈ 0 | **+1.684** |

A1 reached its target (0); A2 picked up the same +Δ ≈ +4° and overshot zero. This is the diagnostic the user noticed — and it's expected behavior given the corrected X rule.

U and Z were not isolated in this dataset and remain derived from geometry plus the legacy Z−1 µm screenshot. Mark for verification when a clean single-axis step is captured.

## Use in correction solving

The plant condition number for SN 7112 was ≈ 80 with axes X, Y, U, V, W (Z dropped — no span). The signatures above are the structural reason:

- **Y is fully orthogonal** to every other axis — no other move touches Px common-mode the same way.
- **V and U are nearly degenerate on Py** — disambiguated only by U's A2-Px nudge and the A1/A2 Z differential.
- **W is uniquely identifiable** by the Py double-antisymmetry (B0/B1 oppose AND A1/A2 oppose).
- **X and Z share the Py B0/B1-antisymmetry within station** but differ across A1/A2: X keeps A1/A2 in unison (common-mode across stations), while Z keeps the antisymmetry there too.

Solver implication: project the residual onto each signature in this order — Y first (cleanest), then W, then U/V (resolved by A2.Px or Z differential), then X/Z (resolved by A1/A2 sign pattern).

## Pipeline strategy — rotations balance, translations zero

Each axis can only fix the differentials it spans. Knowing which axis is "balancer" vs "common-mode-zeroer" gives a clean execution order:

| Axis | A1↔A2 | B0↔B1 | Role |
|------|:-----:|:-----:|------|
| **W** (rot) | antisymm | antisymm | balancer for A2.Py − A1.Py differential |
| **V** (rot) | symm  | symm  | in-station Py common-mode (zeros B0, B1 follows) |
| **U** (rot) | antisymm | symm  | tilt — Z differential, weak Py |
| **X** (trans) | symm  | antisymm | B0/B1 Py differential — but cannot balance A1/A2 |
| **Y** (trans) | symm  | symm  | global Px common-mode |
| **Z** (trans) | mixed | antisymm at A1, symm at A2 | not currently used |

**Recommended pipeline order — rotations first to balance, translations second to zero:**

1. **W (step 3)** — balance `A2.B1.Py − A1.B1.Py → 0`. Both stations meet at a shared (non-zero) value determined by the asymmetric A1/A2 W-gain (see "Empirical sensitivities" below).
2. **V (step 4)** — zero `A1.B0.Py → 0`. V is in-station common-mode, so B1 drifts the same direction; this is fine because step 3 already balanced A1/A2, so the drift is symmetric.
3. **X (step 2)** — zero `A1.B1.Py → 0`. X is A1/A2 common-mode, so A2.B1.Py picks up the same Δ; since A1 and A2 were equal after step 3, they remain equal after this and both land near 0.
4. **Y (step 5)** — zero `avg(A1 Px) → 0`. Y is fully common-mode, so all four Px cells shift together.

This matches the existing script structure once you read step 3 as "W-balance + tail X-zero" rather than "W only" — that tail X is exactly the "translation zeros after rotation balances" idea.

If you want to make it explicit, the cleanest renaming:

| Existing | Suggested | Role |
|----------|-----------|------|
| step3 (W) + step3 tail (X) | step3a (W balance), step3b (X zero) | rotation balance, then translation zero of Py at B1 |
| step4 (V) + recommended re-run of step2 | step4a (V), step4b (X) | rotation common-mode, then translation re-zero |
| step5 (Y) | step5 | translation common-mode of Px |

The point is: **never use translation to balance, never use rotation to zero common-mode unless that's the only available axis**. The corrected X row (and confirmed Y row) make this explicit in the table.

## Cross-check — existing step scripts

All step scripts under `config/Stage main position/scripts/` were cross-checked against this rule table on 2026-05-01. Every polarity convention matches.

| Step | Axis | Drives toward 0 | Cell(s) targeted | Polarity match |
|------|------|-----------------|------------------|:--------------:|
| 2 / 2.1 (`step2_poyntY_zeroing.lua`) | hex-right.X | A1.B1.Py | cell 6 | ✓ |
| 3 / 3.1 (`step3_poyntA1A2Y_balancing.lua`) | hex-right.W | A2.B1.Py − A1.B1.Py | cells 6 vs 8 | ✓ |
| 4 / 4.1 / 4.2 (`step4_poyntA1B0B1Y_balancing.lua`) | hex-right.V | A1.B0.Py | cell 2 (cell 6 drifts → re-run step 2) | ✓ |
| 5 / 5.1 (`step5_poyntB0B1X_zeroing.lua`) | hex-right.Y | avg(A1.B0.Px, A1.B1.Px) | cells 1 + 5 | ✓ |

### Verification details

**Step 2** — comments declare `Poynt.Y > 0 → X+`, `< 0 → X−`. Rule at cell 6: X+ → ↓(6), X− → ↑(6). Match.

**Step 3** — `idealW = −(A2−A1) / sens`. When err > 0, idealW < 0 → W−. Rule at cells 6, 8 under W−: cell 6 ↑, cell 8 ↓ ⇒ err = (cell 8 − cell 6) decreases. Match.

**Step 4** — `idealV = HALF_GAIN * B0 / sens` (controls on B0 only). B0 > 0 → V+, drives cell 2 ↓. Match. Script explicitly acknowledges V drags cell 6 the same direction (V is in-station common-mode by the rule) and trailer instructs re-run of step 2 to re-zero B1 — consistent with rule.

**Step 5** — `idealUm = −err / sens` with err = avg(A1 B0.Px, A1 B1.Px). err > 0 → Y−, drives cells 1 and 5 ↓. Match. Rule predicts A2 cells (3, 7) drag along the same direction; step 5 is A1-only so A2 stays uncorrected — matches the residual seen in SN 7112 walkthrough.

### Empirical sensitivities vs spec

The scripts assume **0.15 °/µm** for translation→Poynt sensitivity. SN 7112 walkthrough shows actual gains run higher:

| Move | Spec (°/µm) | Empirical (°/µm) | Notes |
|------|------------:|-----------------:|-------|
| X− → A1.B0.Py | 0.15 | 0.21 | step 2 iter 1 |
| X− → A1.B1.Py | 0.15 | 0.18 | step 2 iter 1 |
| Y− → A1.B0.Px | 0.15 | 0.36 | post-step-5 1 µm test |
| Y− → A1.B1.Px | 0.15 | 0.29 | post-step-5 1 µm test |
| Y− → A2.B0.Px | 0.15 | 0.24 | post-step-5 1 µm test |
| Y− → A2.B1.Px | 0.15 | 0.26 | post-step-5 1 µm test |

**Y gain is asymmetric across stations:** A1 ≈ 0.33 °/µm, A2 ≈ 0.25 °/µm (A1 is ~30 % higher). Step 5 controls on A1 only, so this doesn't affect convergence at A1, but it explains why A2 always sits with a larger Px residual after step 5 completes.

The conservative spec is *safe* — it just costs 4–5 iterations where 1–2 would suffice. If iteration count becomes a bottleneck, candidates for a tuning pass:

1. Bump `SENSITIVITY_DEG_PER_UM` from 0.15 to ~0.25 (X) and ~0.30 (Y at A1).
2. Optionally split Y sensitivity per station if a future controller measures A2 inside the loop.
