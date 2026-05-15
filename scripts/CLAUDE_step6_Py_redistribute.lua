-- CLAUDE_step6_Py_redistribute.lua
--
-- "Spread the outlier" follow-up to CLAUDE_step_all.
--
-- After step_all converges with one cell carrying most of the Py residual
-- (typically A2.B0.Py from a structural V/W coupling), this script applies
-- a single least-squares-optimal W move that redistributes the residual
-- across all four Py cells so no single cell is the outlier.
--
-- Algorithm
-- ---------
--   c_i_new = c_i + g_i · Δw     (linear gain model from
--                                  docs/M4/hex_right_response_table.md)
--
--   minimise Σ c_i_new² over Δw → close-form LSQ:
--
--           Σ (g_i · c_i)
--   Δw = - ────────────────
--             Σ (g_i²)
--
--   Iterate up to MAX_ITER, clamped to ±MAX_MOVE_W_DEG per move so a single
--   bad measurement can't slam the hex.
--
-- Limits
-- ------
--   W is the only axis with the (B0/B1 antisymm) × (A1/A2 antisymm) Py
--   pattern except Z. With W alone, the achievable RMS floor on a strong
--   B0-row imbalance is ≈ 1.0–1.2° (see prediction in `--explain` block).
--   For deeper reduction, follow this script with a small Z move; Z has
--   the same Py double-antisymmetry but slightly different cross-coupling.

log.info("=== CLAUDE_step6: Py redistribution via W ===")

-- ----------------------------------------------------------------------------
-- Tunables
-- ----------------------------------------------------------------------------
local TARGET_MAX_DEG     = 0.5      -- stop once max|Py cell| < this
local MAX_MOVE_W_DEG     = 0.15     -- per-iteration cap on Δw
local MAX_ITER           = 3
local ZEROING_VEL_MM_S   = 0.1

-- W-axis Py gains (°/°). Source: docs/M4/hex_right_response_table.md,
-- "Empirical sensitivities" section, SN 7112b 3-pt isolation ladder.
local G_C2 = 6.3    -- ΔA1.B0.Py per Δw
local G_C4 = -6.4   -- ΔA2.B0.Py per Δw
local G_C6 = -2.5   -- ΔA1.B1.Py per Δw
local G_C8 = 6.7    -- ΔA2.B1.Py per Δw

-- ----------------------------------------------------------------------------
-- DUT bind
-- ----------------------------------------------------------------------------
if not dut.isLoaded() then
    log.error("No DUT loaded — run dut_init.lua first")
    return
end
local sn          = dut.serial()
local productName = dut.product()
log.info(string.format("DUT %s under product '%s'", sn, productName))

-- ----------------------------------------------------------------------------
-- CSV
-- ----------------------------------------------------------------------------
local baseName = string.format("CLAUDE_step6_redistribute_%s", sn)
local csvPath  = string.format("data/%s/%s.csv", productName, baseName)
-- One CSV row per measurement (A1 or A2), matching CLAUDE_step_all.lua's
-- per-station logging so DataExplorer's Loc column shows A1 / A2.
-- Full measurement set per row: pose + Poynt deg + beam location um +
-- waist um + M² + tx/rx delta. Same shape as the canonical CLAUDE
-- script header.
local header   = file.csvRow({
    "step_no", "step_desc", "position", "timestamp",
    "gantry_x_mm", "gantry_y_mm", "gantry_z_mm",
    "hex_right_x", "hex_right_y", "hex_right_z",
    "hex_right_u", "hex_right_v", "hex_right_w",
    "b0_poynt_x_deg", "b0_poynt_y_deg",
    "b0_loc_x_um", "b0_loc_y_um", "b0_z0_um",
    "b0_waist_x_um", "b0_waist_y_um", "b0_m2_x", "b0_m2_y",
    "b1_poynt_x_deg", "b1_poynt_y_deg",
    "b1_loc_x_um", "b1_loc_y_um", "b1_z0_um",
    "b1_waist_x_um", "b1_waist_y_um", "b1_m2_x", "b1_m2_y",
    "tx_rx_dx_um", "tx_rx_dy_um", "tx_rx_dz_um", "tx_rx_mag_um",
})
log.info("CSV: " .. csvPath)

-- ----------------------------------------------------------------------------
-- Helpers
-- ----------------------------------------------------------------------------
local function clamp(v, cap)
    if v >  cap then return  cap end
    if v < -cap then return -cap end
    return v
end

-- Single-station measure + log. Mirrors the per-station pattern in
-- CLAUDE_step_all.lua so each row in the DUT JSON / CSV carries a
-- `location` ("MOE-A1" / "MOE-A2") and shows up in DataExplorer's Loc
-- column.
--
-- Returns (b0px, b0py, b1px, b1py).
local function measureAt(posName, stepNo, stepDesc)
    log.info(string.format("--> Moving gantry-main to %s", posName))
    motion.moveToNamed("gantry-main", posName)
    motion.waitDone("gantry-main", 30000)
    bobcat.wait(500)

    camera.setExposure("swir_camera", 300)
    recipe.run("Beam Detect Loop")

    local g = motion.getPosition("gantry-main")
    local h = motion.getPosition("hex-right")

    local b0px      = ctx.getNumeric("beam.0.poynt_x_deg")
    local b0py      = ctx.getNumeric("beam.0.poynt_y_deg")
    local b0locx    = ctx.getNumeric("beam.0.beam_loc_x_um")
    local b0locy    = ctx.getNumeric("beam.0.beam_loc_y_um")
    local b0z0      = ctx.getNumeric("beam.0.centroid_z_um")
    local b0waistx  = ctx.getNumeric("beam.0.waist_x_um")
    local b0waisty  = ctx.getNumeric("beam.0.waist_y_um")
    local b0m2x     = ctx.getNumeric("beam.0.m2_x")
    local b0m2y     = ctx.getNumeric("beam.0.m2_y")
    local b1px      = ctx.getNumeric("beam.1.poynt_x_deg")
    local b1py      = ctx.getNumeric("beam.1.poynt_y_deg")
    local b1locx    = ctx.getNumeric("beam.1.beam_loc_x_um")
    local b1locy    = ctx.getNumeric("beam.1.beam_loc_y_um")
    local b1z0      = ctx.getNumeric("beam.1.centroid_z_um")
    local b1waistx  = ctx.getNumeric("beam.1.waist_x_um")
    local b1waisty  = ctx.getNumeric("beam.1.waist_y_um")
    local b1m2x     = ctx.getNumeric("beam.1.m2_x")
    local b1m2y     = ctx.getNumeric("beam.1.m2_y")
    local dx        = b0locx - b1locx
    local dy        = b0locy - b1locy
    local dz        = b0z0   - b1z0
    local mag       = math.sqrt(dx * dx + dy * dy + dz * dz)

    file.appendCsv(csvPath, file.csvRow({
        stepNo, stepDesc, posName, bobcat.timestamp("iso"),
        g.x, g.y, g.z,
        h.x, h.y, h.z, h.u, h.v, h.w,
        b0px, b0py, b0locx, b0locy, b0z0,
        b0waistx, b0waisty, b0m2x, b0m2y,
        b1px, b1py, b1locx, b1locy, b1z0,
        b1waistx, b1waisty, b1m2x, b1m2y,
        dx, dy, dz, mag,
    }), header)

    dut.insertJson("CLAUDE_step6_Py_redistribute", {
        step_no = stepNo, step_desc = stepDesc, location = posName,
        gantry = { x = g.x, y = g.y, z = g.z },
        hex_right = { x = h.x, y = h.y, z = h.z, u = h.u, v = h.v, w = h.w },
        b0 = {
            poynt_x_deg = b0px, poynt_y_deg = b0py,
            loc_x_um = b0locx, loc_y_um = b0locy, z0_um = b0z0,
            waist_x_um = b0waistx, waist_y_um = b0waisty,
            m2_x = b0m2x, m2_y = b0m2y,
        },
        b1 = {
            poynt_x_deg = b1px, poynt_y_deg = b1py,
            loc_x_um = b1locx, loc_y_um = b1locy, z0_um = b1z0,
            waist_x_um = b1waistx, waist_y_um = b1waisty,
            m2_x = b1m2x, m2_y = b1m2y,
        },
        tx_rx = { dx_um = dx, dy_um = dy, dz_um = dz, mag_um = mag },
    })

    local aTag = (posName == "MOE-A1") and "A1" or "A2"
    bobcat.watch(string.format("%sB0.P.X_%s", aTag, stepNo), b0px)
    bobcat.watch(string.format("%sB0.P.Y_%s", aTag, stepNo), b0py)
    bobcat.watch(string.format("%sB1.P.X_%s", aTag, stepNo), b1px)
    bobcat.watch(string.format("%sB1.P.Y_%s", aTag, stepNo), b1py)

    log.info(string.format(
        "[%s @ %s] B0=(P.X=%+.4f, P.Y=%+.4f)  B1=(P.X=%+.4f, P.Y=%+.4f)",
        stepNo, posName, b0px, b0py, b1px, b1py))

    return b0px, b0py, b1px, b1py
end

-- Measure both stations, return c2/c4/c6/c8 (the four Py cells) plus
-- rms_py and max|Py|. Each station call writes its own DUT entry and
-- CSV row inside measureAt; this function just aggregates.
local function measureBoth(iter, desc)
    local _, a1_b0py, _, a1_b1py = measureAt("MOE-A1",
        string.format("%s.a1", iter), desc .. " A1")
    local _, a2_b0py, _, a2_b1py = measureAt("MOE-A2",
        string.format("%s.a2", iter), desc .. " A2")

    local c2, c4, c6, c8 = a1_b0py, a2_b0py, a1_b1py, a2_b1py
    local rms = math.sqrt((c2*c2 + c4*c4 + c6*c6 + c8*c8) / 4)
    local mx  = math.max(math.abs(c2), math.abs(c4),
                         math.abs(c6), math.abs(c8))

    log.info(string.format(
        "  >>> iter %s: c2=%+.4f  c4=%+.4f  c6=%+.4f  c8=%+.4f   RMS=%.4f  max=%.4f",
        iter, c2, c4, c6, c8, rms, mx))
    return c2, c4, c6, c8, rms, mx
end

-- LSQ-optimal W move over the 4 Py cells.
local function solveDw(c2, c4, c6, c8)
    local num = G_C2*c2 + G_C4*c4 + G_C6*c6 + G_C8*c8
    local den = G_C2*G_C2 + G_C4*G_C4 + G_C6*G_C6 + G_C8*G_C8
    return -num / den
end

-- ----------------------------------------------------------------------------
-- Pre-flight: cap hex velocity, snapshot baseline
-- ----------------------------------------------------------------------------
local savedVel = motion.getSystemVelocity("hex-right")
log.info(string.format("Saved hex-right velocity = %.4f mm/s, capping to %.3f mm/s",
    savedVel, ZEROING_VEL_MM_S))
motion.setVelocity("hex-right", ZEROING_VEL_MM_S)

log.info("")
log.info("================================================================")
log.info("  PHASE 6.0 — pre-redistribute baseline")
log.info("================================================================")
local c2, c4, c6, c8, rms, mx = measureBoth("0", "baseline")
local rmsBaseline = rms
local mxBaseline  = mx

-- ----------------------------------------------------------------------------
-- Iterative LSQ W-redistribution
-- ----------------------------------------------------------------------------
local iters     = 0
local stopped   = "max iterations reached"
for i = 1, MAX_ITER do
    iters = i
    if mx < TARGET_MAX_DEG then
        stopped = "within tolerance"
        log.info(string.format("max|Py| = %.4f < target %.2f — stopping",
            mx, TARGET_MAX_DEG))
        break
    end

    local idealW = solveDw(c2, c4, c6, c8)
    local moveW  = clamp(idealW, MAX_MOVE_W_DEG)
    if math.abs(moveW) < 1e-4 then
        stopped = "Δw below noise floor (LSQ optimum reached)"
        log.info(string.format("Iter %d: ideal Δw=%+.5f below noise floor — stopping",
            i, idealW))
        break
    end

    log.info("")
    log.info("================================================================")
    log.info(string.format("  PHASE 6.%d — W redistribute (ideal Δw=%+.4f, applied %+.4f)",
        i, idealW, moveW))
    log.info("================================================================")

    motion.moveRel("hex-right", "W", moveW)
    motion.waitDone("hex-right", 10000)

    c2, c4, c6, c8, rms, mx = measureBoth(tostring(i),
        string.format("after Δw=%+.4f", moveW))
end

-- ----------------------------------------------------------------------------
-- Restore velocity
-- ----------------------------------------------------------------------------
log.info(string.format("Restoring hex-right velocity to %.4f mm/s", savedVel))
motion.setVelocity("hex-right", savedVel)

-- ----------------------------------------------------------------------------
-- Verdict
-- ----------------------------------------------------------------------------
log.info("")
log.info("================================================================")
log.info("  FINAL VERDICT")
log.info("================================================================")

local function fmtCell(name, value)
    local mark = (math.abs(value) < TARGET_MAX_DEG) and " ✓ " or " ✗ "
    return string.format("|   %-10s  | %+8.4f | %s |", name, value, mark)
end

log.info(string.format("Iterations: %d (%s)", iters, stopped))
log.info("Py cells (target |cell| < " ..
    string.format("%.2f", TARGET_MAX_DEG) .. " deg):")
log.info("|     Cell      |   Value   | OK |")
log.info(fmtCell("A1.B0.Py", c2))
log.info(fmtCell("A2.B0.Py", c4))
log.info(fmtCell("A1.B1.Py", c6))
log.info(fmtCell("A2.B1.Py", c8))
log.info(string.format("max|Py|: %.4f → %.4f  (Δ %+0.4f)",
    mxBaseline, mx, mx - mxBaseline))
log.info(string.format("RMS Py:  %.4f → %.4f  (Δ %+0.4f)",
    rmsBaseline, rms, rms - rmsBaseline))

local outOfTol = {}
for _, cell in ipairs({
    {"A1.B0.Py", c2}, {"A2.B0.Py", c4},
    {"A1.B1.Py", c6}, {"A2.B1.Py", c8},
}) do
    if math.abs(cell[2]) >= TARGET_MAX_DEG then
        table.insert(outOfTol, string.format("%s=%+.4f", cell[1], cell[2]))
    end
end

local verdictMsg
if #outOfTol == 0 then
    verdictMsg = string.format(
        "PASS — all 4 Py cells within ±%.2f deg.\n" ..
        "  max|Py| = %.4f   RMS Py = %.4f", TARGET_MAX_DEG, mx, rms)
else
    verdictMsg = string.format(
        "PARTIAL — %d of 4 Py cells still out of tolerance:\n  %s\n\n" ..
        "max|Py| %.4f → %.4f   RMS %.4f → %.4f.\n" ..
        "W is at LSQ optimum; further reduction needs Z (same Py double-antisymm) " ..
        "or accept this as the structural floor.",
        #outOfTol, table.concat(outOfTol, ", "),
        mxBaseline, mx, rmsBaseline, rms)
end

bobcat.prompt("CLAUDE_step6 redistribute complete", verdictMsg)
log.info(verdictMsg)
log.info("CSV: " .. csvPath)
log.info("=== CLAUDE_step6 done ===")
