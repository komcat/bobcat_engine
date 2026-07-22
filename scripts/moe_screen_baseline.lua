-- moe_screen_baseline.lua
-- MOE Screening baseline IR read. Mirrors CLAUDE_step1_baseline.lua's
-- "Single measurement" flow but WITHOUT a DUT: it screens a bare MOE
-- identified by a MOE ID and measures at the IR screening positions
-- screen-A1-IR / screen-A2-IR (instead of MOE-A1 / MOE-A2).
--
-- Driven by the "MOE Screening" tab (MoeScreeningWidget). The widget sets
-- these Lua globals before launch:
--   MOE_SCREEN_ID       - the operator-entered MOE ID (fallback: timestamp)
--   MOE_SCREEN_DIR      - relative output dir "data/<product>/moe-screen"
--   MOE_SCREEN_REPEATS  - how many times to repeat the A1+A2 measurement
--                         (statistics run; default 1 = single measurement)
-- One CSV row per location PER RUN is appended to <MOE_SCREEN_DIR>/<MOE_ID>.csv
-- (tagged with a 1-based run_index column); the widget's read-back panel polls
-- that folder and shows the newest file.

log.info("=== MOE Screening: baseline beam fit at screen-A1-IR + screen-A2-IR ===")

-- Identity + output dir come from the widget (no DUT). Fall back to a
-- timestamp id and the generic data/moe-screen folder if run standalone.
local moeId = (type(MOE_SCREEN_ID) == "string" and MOE_SCREEN_ID ~= "")
              and MOE_SCREEN_ID or bobcat.timestamp()
local outDir = (type(MOE_SCREEN_DIR) == "string" and MOE_SCREEN_DIR ~= "")
               and MOE_SCREEN_DIR or "data/moe-screen"

-- Per-position IR camera exposure (us), from the MOE Screening tab fields.
-- Default 10000 us when run standalone / unset.
local expA1 = tonumber(MOE_SCREEN_EXP_A1_US) or 10000
local expA2 = tonumber(MOE_SCREEN_EXP_A2_US) or 10000

-- Number of times the whole A1+A2 measurement is repeated (statistics run).
-- Default 1 (single measurement) when unset. Each run appends its own rows,
-- tagged with a 1-based run_index column, so stddev/mean can be computed later.
local repeats = math.floor(tonumber(MOE_SCREEN_REPEATS) or 1)
if repeats < 1 then repeats = 1 end

local csvPath = string.format("%s/%s.csv", outDir, moeId)
-- run_index is placed LAST so appending to a pre-existing CSV (whose header is
-- not rewritten) keeps every earlier named column aligned; a single-measurement
-- run just writes run_index = 1.
local header  = file.csvRow({
    "moe_id", "timestamp", "location", "ir_exposure_us",
    "gantry_x_mm", "gantry_y_mm", "gantry_z_mm",
    "b0_poynt_x_deg", "b0_poynt_y_deg",
    "b0_loc_x_um", "b0_loc_y_um", "b0_z0_um",
    "b0_waist_x_um", "b0_waist_y_um", "b0_m2_x", "b0_m2_y",
    "b1_poynt_x_deg", "b1_poynt_y_deg",
    "b1_loc_x_um", "b1_loc_y_um", "b1_z0_um",
    "b1_waist_x_um", "b1_waist_y_um", "b1_m2_x", "b1_m2_y",
    "tx_rx_dx_um", "tx_rx_dy_um", "tx_rx_dz_um", "tx_rx_mag_um",
    "run_index",
})
log.info(string.format("MOE ID: %s  |  CSV: %s", moeId, csvPath))

-- ----------------------------------------------------------------------------
-- Beam detect with single retry on convergence failure. Poynt (0,0) on either
-- beam means the detector failed to converge; re-run once, else abort.
-- (Identical to CLAUDE_step1_baseline.lua.)
-- ----------------------------------------------------------------------------
local POYNT_ZERO_EPS = 1e-6
local function poyntFailed()
    local function isZero(v) return v ~= nil and math.abs(v) < POYNT_ZERO_EPS end
    local b0px = ctx.getNumeric("beam.0.poynt_x_deg")
    local b0py = ctx.getNumeric("beam.0.poynt_y_deg")
    local b1px = ctx.getNumeric("beam.1.poynt_x_deg")
    local b1py = ctx.getNumeric("beam.1.poynt_y_deg")
    return (isZero(b0px) and isZero(b0py))
        or (isZero(b1px) and isZero(b1py)),
        b0px, b0py, b1px, b1py
end

-- ----------------------------------------------------------------------------
-- Inline Z-scan for MOE screening: mirrors the "Beam Detect Loop" recipe
-- step-for-step, but with the scan geometry local to this script so it does NOT
-- affect the shared recipe (Poynt Optimize). Defaults 9 slices x 5 um; the MOE
-- Screening tab's "Fit scan..." dialog overrides via the globals below. The scan
-- is always centred on the taught Z.
-- ----------------------------------------------------------------------------
local BEAM_HOST            = "127.0.0.1"
local BEAM_PORT            = "5555"
local SCAN_NUM_SLICES      = math.floor(tonumber(MOE_SCREEN_SLICES) or 9)   -- number of Z steps
local SCAN_STEP_UM         = math.floor(tonumber(MOE_SCREEN_STEP_UM) or 5)  -- step size (um)
if SCAN_NUM_SLICES < 3 then SCAN_NUM_SLICES = 3 end
if SCAN_STEP_UM   < 1 then SCAN_STEP_UM   = 1 end
local SCAN_START_OFFSET_UM = -(SCAN_NUM_SLICES - 1) * SCAN_STEP_UM / 2      -- centred on taught Z
local DETECT_FRAMES        = 3      -- frames averaged per slice (recipe uses 3)
local SCAN_VELOCITY        = 10
local UM_PER_PX            = "0.25"
local WAVELENGTH_M         = "1550e-9"

local function beamReset()
    _dispatch("beamserver", "Reset", {})
end
local function beamDetectAtCurrentZ()
    -- Mirrors the recipe's "Beam: Detect N": read Z live, average DETECT_FRAMES,
    -- one slice registration per call (server accumulates for the fit).
    _dispatch("beamserver", "DetectN", {
        camera_device      = "swir_camera",
        captureAs          = "beam",
        use_device_z       = "true",
        z_device           = "gantry-main",
        z_axis             = "Z",
        z_um               = "",
        num_frames         = tostring(DETECT_FRAMES),
        repeat_count       = "1",
        delay_between_ms   = "100",
        inbounce_delay_ms  = "100",
        outbounce_delay_ms = "100",
        rotate_180         = "false",
        um_per_px_x        = UM_PER_PX,
        um_per_px_y        = UM_PER_PX,
    })
end
local function beamFit()
    _dispatch("beamserver", "Fit", {
        captureAs    = "beam",
        um_per_px_x  = UM_PER_PX,
        um_per_px_y  = UM_PER_PX,
        wavelength_m = WAVELENGTH_M,
    })
end

-- One inline Z-scan at the current gantry XY: reset, step through the slices
-- detecting at each, return to the taught Z, then fit.
local function runBeamScan()
    beamReset()
    motion.setVelocity("gantry-main", SCAN_VELOCITY)
    motion.moveRel("gantry-main", "Z", SCAN_START_OFFSET_UM / 1000.0)
    motion.waitDone("gantry-main", 30000)
    bobcat.wait(500)
    for idx = 0, SCAN_NUM_SLICES - 1 do
        beamDetectAtCurrentZ()
        if idx < SCAN_NUM_SLICES - 1 then
            motion.moveRel("gantry-main", "Z", SCAN_STEP_UM / 1000.0)
            motion.waitDone("gantry-main", 30000)
            bobcat.wait(150)
        end
    end
    -- Undo the net scan offset to return to the taught Z.
    local lastOffsetUm = SCAN_START_OFFSET_UM + (SCAN_NUM_SLICES - 1) * SCAN_STEP_UM
    motion.moveRel("gantry-main", "Z", -lastOffsetUm / 1000.0)
    motion.waitDone("gantry-main", 30000)
    bobcat.wait(500)
    beamFit()
end

local function runBeamDetect()
    log.info(string.format(
        "Inline Z-scan: %d slices, step %d um (%+.1f..%+.1f um around taught Z)",
        SCAN_NUM_SLICES, SCAN_STEP_UM, SCAN_START_OFFSET_UM,
        SCAN_START_OFFSET_UM + (SCAN_NUM_SLICES - 1) * SCAN_STEP_UM))
    runBeamScan()
    local failed, b0px, b0py, b1px, b1py = poyntFailed()
    if failed then
        log.warn(string.format(
            "Beam convergence failed (B0=(%+.4f,%+.4f) B1=(%+.4f,%+.4f)) — " ..
            "retrying inline scan once",
            b0px or 0, b0py or 0, b1px or 0, b1py or 0))
        runBeamScan()
        failed, b0px, b0py, b1px, b1py = poyntFailed()
        if failed then
            local msg = string.format(
                "Beam not detected — Poynt still (0,0) after retry " ..
                "(B0=(%+.4f,%+.4f) B1=(%+.4f,%+.4f)). Aborting script.",
                b0px or 0, b0py or 0, b1px or 0, b1py or 0)
            log.error(msg)
            bobcat.prompt("Beam not detected", msg)
            error(msg)
        end
    end
end

local function measureAt(posName, aTag, expUs, runIndex)
    log.info(string.format("--> Moving gantry-main to %s (exposure %d us)",
                           posName, expUs))
    motion.moveToNamed("gantry-main", posName)
    motion.waitDone("gantry-main", 30000)
    bobcat.wait(500)

    camera.setExposure("swir_camera", expUs)

    runBeamDetect()

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

    bobcat.watch(string.format("%sB0.P.Y", aTag), b0py)
    bobcat.watch(string.format("%sB0.P.X", aTag), b0px)
    bobcat.watch(string.format("%sB1.P.Y", aTag), b1py)
    bobcat.watch(string.format("%sB1.P.X", aTag), b1px)

    local g = motion.getPosition("gantry-main")

    file.appendCsv(csvPath, file.csvRow({
        moeId, bobcat.timestamp("iso"), posName, expUs,
        g.x, g.y, g.z,
        b0px, b0py, b0locx, b0locy, b0z0,
        b0waistx, b0waisty, b0m2x, b0m2y,
        b1px, b1py, b1locx, b1locy, b1z0,
        b1waistx, b1waisty, b1m2x, b1m2y,
        dx, dy, dz, mag,
        runIndex,
    }), header)

    log.info(string.format(
        "[%s @ %s]\n" ..
        "| Beam | P.X (deg)  | P.Y (deg)  |\n" ..
        "|  B0  |  %+.4f  |  %+.4f  |\n" ..
        "|  B1  |  %+.4f  |  %+.4f  |",
        aTag, posName, b0px, b0py, b1px, b1py))

    return b0px, b0py, b1px, b1py
end

-- Connect the beam server once before measuring (idempotent; the inline scan
-- assumes an open connection, which the shared recipe used to open itself).
_dispatch("beamserver", "Connect", { host = BEAM_HOST, port = BEAM_PORT })

if repeats > 1 then
    log.info(string.format("Statistics run: %d repeats of A1 + A2", repeats))
end

for run = 1, repeats do
    if repeats > 1 then
        log.info(string.format("=== Run %d / %d ===", run, repeats))
    end

    local a1B0x, a1B0y, a1B1x, a1B1y = measureAt("screen-A1-IR", "A1", expA1, run)
    local a2B0x, a2B0y, a2B1x, a2B1y = measureAt("screen-A2-IR", "A2", expA2, run)

    log.info(string.format(
        "--- MOE screen %s (run %d/%d) ---\n" ..
        "|        |    A1      |    A2      |   A2 - A1   |\n" ..
        "| B0.P.X |  %+.4f  |  %+.4f  |   %+.4f   |\n" ..
        "| B0.P.Y |  %+.4f  |  %+.4f  |   %+.4f   |\n" ..
        "| B1.P.X |  %+.4f  |  %+.4f  |   %+.4f   |\n" ..
        "| B1.P.Y |  %+.4f  |  %+.4f  |   %+.4f   |",
        moeId, run, repeats,
        a1B0x, a2B0x, a2B0x - a1B0x,
        a1B0y, a2B0y, a2B0y - a1B0y,
        a1B1x, a2B1x, a2B1x - a1B1x,
        a1B1y, a2B1y, a2B1y - a1B1y))
end

log.info("CSV: " .. csvPath)
log.info(string.format("=== MOE Screening complete (%d run(s)) ===", repeats))
