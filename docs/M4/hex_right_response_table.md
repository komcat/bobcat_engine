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
| U+   | 1+3+, 5−sm                       | ↑(1)      | na   | ↑(3)      | na   | | ↓sm(5)    | na   | na        | na   |
| U−   | 1−3−, 5+sm                       | ↓(1)      | na   | ↓(3)      | na   | | ↑sm(5)    | na   | na        | na   |
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
| U    | rotation | none — Py barely moves | B0-row Px ↑↑ in unison across A1/A2; B1-row Px ~na | not measured here (legacy claim from geometry) |
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

### W− (SN 7112b Phase 2) — A1 + A2, 3-point ladder

`w: 0.4633° → 0.4056°` (Δw = −0.0577° across two W− moves) from `7112b.json` `CLAUDE_full_correction` step_no `1.0` → `2.2`. U/V/X/Y/Z all frozen within ±1e−5°.

| step_no | w (°) | A1.B0.Px | A1.B0.Py | A1.B1.Px | A1.B1.Py | A2.B0.Px | A2.B0.Py | A2.B1.Px | A2.B1.Py |
|---------|------:|---------:|---------:|---------:|---------:|---------:|---------:|---------:|---------:|
| 1.0 (baseline) | 0.4633 | −2.196 | +1.497 | −0.392 | −0.269 | −2.229 | −0.113 | −0.538 | +0.350 |
| 2.1 | 0.4221 | −2.283 | +1.206 | −0.475 | −0.166 | −2.210 | +0.159 | −0.482 | +0.082 |
| 2.2 | 0.4056 | −2.299 | +1.132 | −0.495 | −0.124 | −2.303 | +0.254 | −0.551 | −0.039 |
| **Δ** (Δw=−0.0577°) | | **−0.103** | **−0.365** | **−0.103** | **+0.145** | **−0.074** | **+0.367** | **−0.013** | **−0.388** |

| # | Cell | Δ | Predicted | Match |
|---|------|---|-----------|:-----:|
| 1 | A1.B0.Px | −0.103 | ↓sm-lg | ✓ |
| 2 | A1.B0.Py | −0.365 | ↓ | ✓ |
| 3 | A2.B0.Px | −0.074 | ↓sm | ✓ |
| 4 | A2.B0.Py | +0.367 | ↑ | ✓ |
| 5 | A1.B1.Px | −0.103 | ↓sm-lg | ✓ |
| 6 | A1.B1.Py | +0.145 | ↑ | ✓ |
| 7 | A2.B1.Px | −0.013 | ↓sm | ✓ |
| 8 | A2.B1.Py | −0.388 | ↓ | ✓ |

Confirms the Py double-antisymmetry (B0/B1 oppose AND A1/A2 oppose) with both stations measured at three W positions. The A1.B1.Py gain (~2.5 °/°) is notably smaller than the other three Py cells (~6.3–6.7 °/°), so the script's choice to use the **B1-row differential** `A2.B1.Py − A1.B1.Py` as the W-balance error is asymmetric: A2.B1.Py carries most of the swing.

### V+ (step 4 iter 1) — A1 only
`v: −0.1188° → −0.0188°` (Δv = +0.100°)

| # | Cell | Before | After | Δ | Predicted | Match |
|---|------|--------|-------|----|-----------|:-----:|
| 1 | A1.B0.Px | 2.362 | 2.400 | +0.038 | na | ✓ |
| 2 | A1.B0.Py | 1.726 | 1.121 | −0.605 | ↓ | ✓ |
| 5 | A1.B1.Px | 2.794 | 2.821 | +0.027 | na | ✓ |
| 6 | A1.B1.Py | 0.047 | −0.029 | −0.076 | ↓ | ✓ |

### V+ (SN 7112b Phase 3) — A2 cross-check confirms in-station common-mode

Phase 3 V-zero only measures A1 in the loop, so A2's V response was not previously verified. Comparing the last A2 measurement before V (step `2.2.a2`) with the post-V A2 cross-check (step `4.f.a2`) in `7112b.json`:

| | v (°) | x (mm) | A1.B0.Py | A2.B0.Py | A1.B1.Py | A2.B1.Py |
|---|---:|---:|---:|---:|---:|---:|
| Pre-V (2.2.a2 / 3.0) | −0.0147 | −3.3140 | +1.304 (3.0) | +0.254 | — | — |
| Post-V (4.1.a1 / 4.f.a2) | +0.1591 | −3.3151 | +0.072 | −1.230 | — | +0.040 |
| **Δ** (Δv=+0.174°, Δx=−1µm) | | | **−1.232** | **−1.484** | | |

The Δx=−1µm contributes ≈ +0.21° to A2.B0.Py per the X− rule, so V+ alone drives A2.B0.Py by **≈ −1.69°** over Δv=+0.174° (gain ≈ 9.7 °/°). This confirms A1/A2 move in the **same direction** under V — the existing in-station common-mode rule holds at A2. **V− is therefore A1.B0.Py ↑ AND A2.B0.Py ↑ in unison**, not split.

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

### U+ (SN 7112b) — A1 + A2 measured, 4-step ladder

`u: 0.176° → 0.336°` over four `step1_beam_fit` runs (step_tags `20260505_210430` → `20260505_210717` in `data/Stage main position/7112b.json`). All other axes frozen within ±0.001°.

| step_tag | u (°) | A1.B0.Px | A1.B0.Py | A1.B1.Px | A1.B1.Py | A2.B0.Px | A2.B0.Py | A2.B1.Px | A2.B1.Py |
|----------|------:|---------:|---------:|---------:|---------:|---------:|---------:|---------:|---------:|
| 20260505_210430 | 0.176 | −0.6932 | +0.0745 | +0.0595 | +0.0087 | −0.9439 | −1.1892 | +0.0394 | −0.0971 |
| 20260505_210507 | 0.266 | −0.5726 | +0.0974 | −0.0649 | −0.0419 | −0.6199 | −1.1426 | +0.1416 | −0.1114 |
| 20260505_210543 | 0.306 | −0.3091 | +0.1196 | −0.0154 | −0.0023 | −0.4664 | −1.1677 | +0.1201 | −0.1724 |
| 20260505_210717 | 0.336 | −0.1777 | +0.0689 | −0.0493 | −0.0125 | −0.4251 | −1.1673 | +0.0135 | −0.1473 |
| **Δ** (Δu=+0.160°) |  | **+0.516** | −0.006 | −0.109 | −0.021 | **+0.519** | +0.022 | −0.026 | −0.050 |

| # | Cell | Δ | Predicted (corrected) | Match |
|---|------|---|-----------|:-----:|
| 1 | A1.B0.Px | +0.516 | ↑ | ✓ |
| 2 | A1.B0.Py | −0.006 | na | ✓ (corrected from ↓) |
| 3 | A2.B0.Px | +0.519 | ↑ | ✓ (corrected from ↑sm) |
| 4 | A2.B0.Py | +0.022 | na | ✓ (corrected from ↓) |
| 5 | A1.B1.Px | −0.109 | ↓sm | ✓ |
| 6 | A1.B1.Py | −0.021 | na | ✓ (corrected from ↓) |
| 7 | A2.B1.Px | −0.026 | na | ✓ (corrected from ↑sm) |
| 8 | A2.B1.Py | −0.050 | na | ✓ (corrected from ↑) |

The B0-row Px gain is **~3.2 °/°** at both A1 and A2 — i.e. U+ is **A1/A2 common-mode on B0 Px**, not the antisymmetric tilt the geometric derivation predicted. Py is essentially flat (|Δ| < 0.06° at all four cells). The original U row was derived from geometry without isolated-axis data; this measurement supersedes it.

**Implication:** U is the only axis that drives B0-row Px without touching B1-row Px. It is **orthogonal to Y on Px** (Y is full common-mode across both rows; U is B0-only). If a residual leaves B0 Px off-zero after Y zeroes the global Px average, U is the only single-axis lever. The current `CLAUDE_step_all.lua` does not move U and so cannot fix this residual.

Z was not isolated in this dataset and remains derived from geometry plus the legacy Z−1 µm screenshot. Mark for verification when a clean single-axis step is captured.

## Use in correction solving

The plant condition number for SN 7112 was ≈ 80 with axes X, Y, U, V, W (Z dropped — no span). The signatures above are the structural reason:

- **Y is full Px common-mode** (B0 + B1, A1 + A2 all together), but **U is B0-only Px common-mode** (B0 ↑↑, B1 ~na). They are **orthogonal in Px space**, not degenerate.
- **V is the in-station Py axis** — drives B0/B1 the same direction at one station; not degenerate with U because U barely touches Py at all.
- **W is uniquely identifiable** by the Py double-antisymmetry (B0/B1 oppose AND A1/A2 oppose).
- **X and Z share the Py B0/B1-antisymmetry within station** but differ across A1/A2: X keeps A1/A2 in unison (common-mode across stations), while Z keeps the antisymmetry there too.

Solver implication: project the residual onto each signature in this order — W first (only axis that balances A1/A2), then V (in-station Py common-mode), then X (Py B0/B1 differential), then Y (full Px common-mode), then U (B0-only Px residual). Z is redundant given the other five.

## Pipeline strategy — rotations balance, translations zero

Each axis can only fix the differentials it spans. Knowing which axis is "balancer" vs "common-mode-zeroer" gives a clean execution order:

| Axis | A1↔A2 | B0↔B1 | Role |
|------|:-----:|:-----:|------|
| **W** (rot) | antisymm | antisymm | balancer for A2.Py − A1.Py differential |
| **V** (rot) | symm  | symm  | in-station Py common-mode (zeros B0, B1 follows) |
| **U** (rot) | symm  | antisymm on Px | B0-row Px common-mode — orthogonal to Y; only lever for B0-only Px residuals |
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

Rotation gains from the SN 7112b isolation ladders:

| Move | Empirical (°/°) | Notes |
|------|----------------:|-------|
| U+ → A1.B0.Px | ~3.2 | 4-step ladder, Δu=+0.16° |
| U+ → A2.B0.Px | ~3.2 | 4-step ladder, Δu=+0.16° |
| U+ → A1.B1.Px | ~−0.7 | small cross-coupling |
| U+ → A2.B1.Px | ~−0.2 | near noise floor |
| W− → A1.B0.Py | +6.3 | Phase 2 3-pt ladder, Δw=−0.058° |
| W− → A2.B0.Py | −6.4 | opposite sign — A1/A2 antisymm |
| W− → A1.B1.Py | −2.5 | smaller — see note below |
| W− → A2.B1.Py | +6.7 | dominant cell at B1 |
| V+ → A1.B0.Py | ~−6.1 | step4 iter1 (Δv=+0.10°) |
| V+ → A2.B0.Py | ~−9.7 | Phase 3 cross-check (Δv=+0.17°, X-corrected) |
| V+ → A1.B1.Py | ~−0.8 | small in-station coupling |

**W asymmetry note:** A1.B1.Py (~2.5 °/°) carries about 40% of the swing the other three Py cells get (~6.3–6.7 °/°). The script uses `A2.B1.Py − A1.B1.Py` as the W-balance error → effective differential gain ≈ 9.2 °/°, vs. the script's conservative `W_SENSITIVITY_DEG_PER_DEG = 15.0`. A more accurate constant would converge in fewer iterations; current value is safe but slow.

**V A1/A2 asymmetry:** A2.B0.Py gain (~9.7 °/°) is ~60% larger than A1.B0.Py (~6.1 °/°). The script controls on A1 only, so this just means A2 over-corrects — fine because Phase 2 (W) already balanced A1↔A2, and V is then applied symmetrically.

**Y gain is asymmetric across stations:** A1 ≈ 0.33 °/µm, A2 ≈ 0.25 °/µm (A1 is ~30 % higher). Step 5 controls on A1 only, so this doesn't affect convergence at A1, but it explains why A2 always sits with a larger Px residual after step 5 completes.

The conservative spec is *safe* — it just costs 4–5 iterations where 1–2 would suffice. If iteration count becomes a bottleneck, candidates for a tuning pass:

1. Bump `SENSITIVITY_DEG_PER_UM` from 0.15 to ~0.25 (X) and ~0.30 (Y at A1).
2. Optionally split Y sensitivity per station if a future controller measures A2 inside the loop.
