-- moe_screen_capture_all.lua
-- MOE Screening "Capture all channels": a DUT-less sibling of
-- beam_capture_all_channels_v2.lua for the MOE Screening tab (MoeScreeningWidget).
--
-- The beam server only fits TWO beams (Rx/Tx = b0/b1) at a time, so every channel
-- is measured at its OWN gantry position — one 2-beam detection per channel. The
-- run visits 14 positions: screen-A1-IR, ch1..ch12, screen-A2-IR (Y- order).
--
-- Differences from the Poynt Optimize capture (beam_capture_all_channels_v2.lua):
--   * NO DUT — identified by a MOE ID (MOE_SCREEN_ID); all metadata goes to a CSV
--     (there is no dut.insertJson); the report is CSV + PNG based.
--   * NO POWER CONTROL — MOE screening never touches the power supplies. The only
--     per-station control is the SWIR/IR camera exposure (exposure-only
--     auto-adjust for the channels; the IR endpoints use the tab's fixed exposure).
--   * POSITIONS are INTERPOLATED between the two taught endpoints screen-A1-IR and
--     screen-A2-IR ("teach these two positions first"): channel n (1..12) sits at
--     fraction n/13 of the A1-IR -> A2-IR line, so X/Z follow any tilt between the
--     endpoints and Y steps ~250um. A minimum 250um Y step is enforced so a pair of
--     endpoints taught too close together never bunches the channels.
--   * BEAM LAYOUT — one channel per position: top beam = Rx = b0, bottom = Tx = b1.
--     12 numbered channels (ch1 nearest A1-IR .. ch12 nearest A2-IR) plus the two
--     IR endpoints, each producing a single Rx/Tx row.
--
-- Globals set by the widget before launch:
--   MOE_SCREEN_ID          - operator-entered MOE ID (fallback: timestamp)
--   MOE_SCREEN_DIR         - relative output dir "data/<product>/moe-screen"
--   MOE_SCREEN_EXP_A1_US   - fixed IR exposure at screen-A1-IR (us)
--   MOE_SCREEN_EXP_A2_US   - fixed IR exposure at screen-A2-IR (us)
--   MOE_SCREEN_SLICES      - inline Z-scan slice count
--   MOE_SCREEN_STEP_UM     - inline Z-scan step (um)
--
-- Output layout (consumed by tools/make_moe_screen_report.py):
--   <MOE_SCREEN_DIR>/<MOE_ID>/<run_tag>/<station>/beam_*.png       (final centred)
--   <MOE_SCREEN_DIR>/<MOE_ID>/<run_tag>/<station>/slices/*.png     (per-Z slices)
--   <MOE_SCREEN_DIR>/<MOE_ID>/<run_tag>/capture_summary.csv        (one row/channel)

log.info("=== MOE Screening capture-all: screen-A1-IR + ch1..ch12 + screen-A2-IR ===")

-- ---------------------------------------------------------------------------
-- Identity + output dir (no DUT). Fall back to a timestamp id / generic folder
-- when run standalone.
-- ---------------------------------------------------------------------------
local moeId = (type(MOE_SCREEN_ID) == "string" and MOE_SCREEN_ID ~= "")
              and MOE_SCREEN_ID or bobcat.timestamp()
local outDir = (type(MOE_SCREEN_DIR) == "string" and MOE_SCREEN_DIR ~= "")
               and MOE_SCREEN_DIR or "data/moe-screen"

local expA1 = tonumber(MOE_SCREEN_EXP_A1_US) or 10000
local expA2 = tonumber(MOE_SCREEN_EXP_A2_US) or 10000

log.info(string.format("MOE ID: %s  |  out: %s", moeId, outDir))

-- Tunables -------------------------------------------------------------------
local TEC_DEVICE      = "tec-main"
local ANCHOR_A1       = "screen-A1-IR"
local ANCHOR_A2       = "screen-A2-IR"
local NUM_CHANNELS    = 12
local MIN_STEP_MM     = 0.250     -- minimum Y step between channels (mm)
local TRAVEL_VELOCITY = 10        -- gantry velocity for inter-station moves

-- Exposure-only auto-adjust (anti over-/under-exposure). No drive-current
-- fallback (no PSU control here). The screener's IR beams are much dimmer than
-- the Poynt SWIR beams, so the working range is scaled up (start 8000 us, ceiling
-- 30000 us) and the step tiers are coarser at high exposure so it still converges.
local SWIR_EXPOSURE_START_US = 8000  -- starting exposure at each auto station (us)
local EXPOSURE_MIN_US        = 25    -- floor (us)
local EXPOSURE_MAX_US        = 30000 -- ceiling (us)
local SAT_PK                 = 225   -- peak >= this = over-exposed (dim)
local WEAK_PK                = 120   -- peak <  this = weak (jump to max, dim down)
local EXPOSURE_SETTLE_MS     = 300   -- settle after each exposure change
local EXPOSURE_MAX_ITERS     = 80    -- safety cap

-- Inline Z-scan (mirrors moe_screen_baseline.lua; geometry from the tab globals).
-- Centred on the taught Z; a SWIR package is saved at every slice.
local SCAN_NUM_SLICES      = math.floor(tonumber(MOE_SCREEN_SLICES) or 9)
local SCAN_STEP_UM         = math.floor(tonumber(MOE_SCREEN_STEP_UM) or 5)
if SCAN_NUM_SLICES < 3 then SCAN_NUM_SLICES = 3 end
if SCAN_STEP_UM   < 1 then SCAN_STEP_UM   = 1 end
local SCAN_START_OFFSET_UM = -(SCAN_NUM_SLICES - 1) * SCAN_STEP_UM / 2   -- centred
local SCAN_VELOCITY        = 10
local DETECT_FRAMES        = 3
local UM_PER_PX            = "0.25"
local WAVELENGTH_M         = "1550e-9"
local BEAM_HOST            = "127.0.0.1"
local BEAM_PORT            = "5555"

local runTag      = bobcat.timestamp()
local captureRoot = string.format("%s/%s/%s", outDir, moeId, runTag)
local csvPath     = string.format("%s/capture_summary.csv", captureRoot)
local header = file.csvRow({
    "run_tag", "timestamp", "station", "channel_label", "kind", "status",
    "temperature_c", "swir_exposure_us",
    "target_x_mm", "target_y_mm", "target_z_mm",
    "b0_label", "b0_slot", "b0_image", "b0_cx_px", "b0_cy_px",
    "b0_poynt_x_deg", "b0_poynt_y_deg", "b0_loc_x_um", "b0_loc_y_um", "b0_z0_um",
    "b0_waist_x_um", "b0_waist_y_um", "b0_m2_x", "b0_m2_y", "b0_ee_pct", "b0_peak",
    "b1_label", "b1_slot", "b1_image", "b1_cx_px", "b1_cy_px",
    "b1_poynt_x_deg", "b1_poynt_y_deg", "b1_loc_x_um", "b1_loc_y_um", "b1_z0_um",
    "b1_waist_x_um", "b1_waist_y_um", "b1_m2_x", "b1_m2_y", "b1_ee_pct", "b1_peak",
})
log.info("CSV: " .. csvPath)

local failedStations = {}   -- stations where no Rx/Tx pair was detected

-- ---------------------------------------------------------------------------
-- Exposure auto-adjust (exposure only — no PSU current fallback).
-- Read the live SWIR peak pixel value and step exposure into [WEAK_PK, SAT_PK).
-- Over-exposed -> dim; weak/undetected -> jump to the ceiling then dim down.
-- ---------------------------------------------------------------------------
local function readMaxPeak()
    _dispatch("system", "GetSwirBeamState", {})
    local n = math.floor(ctx.getNumeric("swir.num_beams") + 0.5)
    local pkMax = 0
    for i = 0, n - 1 do
        local pk = ctx.getNumeric("swir." .. i .. ".peak_intensity")
        if pk > pkMax then pkMax = pk end
    end
    return pkMax, n
end

-- Range-based exposure step: coarse up high (fast descent from the 30000 us
-- ceiling), fine near the target. 0..300 -> 25, ..1000 -> 50, ..5000 -> 200,
-- else -> 1000 (us).
local function exposureStepUs(expUs)
    if expUs <= 300 then return 25
    elseif expUs <= 1000 then return 50
    elseif expUs <= 5000 then return 200
    else return 1000 end
end

local function autoAdjustExposure(st)
    local expUs = SWIR_EXPOSURE_START_US
    camera.setExposure("swir_camera", expUs)
    bobcat.wait(EXPOSURE_SETTLE_MS)
    local jumpedToMax = false
    for _ = 1, EXPOSURE_MAX_ITERS do
        local pkMax, n = readMaxPeak()
        if n >= 1 and pkMax >= SAT_PK then
            if expUs > EXPOSURE_MIN_US then
                local nextUs = expUs - exposureStepUs(expUs)
                if nextUs < EXPOSURE_MIN_US then nextUs = EXPOSURE_MIN_US end
                log.info(string.format("  [%s] over-exposed (peak=%d) -> exposure %d -> %d us",
                    st.pos, math.floor(pkMax + 0.5), expUs, nextUs))
                expUs = nextUs
                camera.setExposure("swir_camera", expUs)
                bobcat.wait(EXPOSURE_SETTLE_MS)
            else
                log.warn(string.format("  [%s] still over-exposed (peak=%d) at min exposure %d us",
                    st.pos, math.floor(pkMax + 0.5), EXPOSURE_MIN_US))
                return expUs
            end
        elseif n < 1 or pkMax < WEAK_PK then
            if not jumpedToMax and expUs < EXPOSURE_MAX_US then
                local why = (n < 1) and "no beam"
                    or string.format("peak=%d < %d", math.floor(pkMax + 0.5), WEAK_PK)
                log.info(string.format("  [%s] weak/undetected (%s) at %d us -> jump to max %d us, then dim",
                    st.pos, why, expUs, EXPOSURE_MAX_US))
                jumpedToMax = true
                expUs = EXPOSURE_MAX_US
                camera.setExposure("swir_camera", expUs)
                bobcat.wait(EXPOSURE_SETTLE_MS)
            elseif expUs >= EXPOSURE_MAX_US then
                log.warn(string.format("  [%s] still weak/undetected at ceiling %d us", st.pos, EXPOSURE_MAX_US))
                return expUs
            else
                local nextUs = expUs + exposureStepUs(expUs)
                if nextUs > EXPOSURE_MAX_US then nextUs = EXPOSURE_MAX_US end
                log.info(string.format("  [%s] weak beam (peak=%d < %d) -> exposure %d -> %d us",
                    st.pos, math.floor(pkMax + 0.5), WEAK_PK, expUs, nextUs))
                expUs = nextUs
                camera.setExposure("swir_camera", expUs)
                bobcat.wait(EXPOSURE_SETTLE_MS)
            end
        else
            log.info(string.format("  [%s] exposure OK: %d us (peak=%d, %d <= peak < %d)",
                st.pos, expUs, math.floor(pkMax + 0.5), WEAK_PK, SAT_PK))
            return expUs
        end
    end
    log.warn(string.format("  [%s] auto-exposure hit %d iters, leaving %d us",
        st.pos, EXPOSURE_MAX_ITERS, expUs))
    return expUs
end

-- ---------------------------------------------------------------------------
-- Beam server helpers + inline Z-scan (saves every slice before fitting).
-- ---------------------------------------------------------------------------
local function beamConnect() _dispatch("beamserver", "Connect", { host = BEAM_HOST, port = BEAM_PORT }) end
local function beamReset()   _dispatch("beamserver", "Reset", {}) end
local function beamDetectAtCurrentZ()
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

local function saveSlice(st, slicesDir, idx, offsetUm)
    local prefix = string.format("%s_slice_%02d_z%+dum", st.pos, idx, offsetUm)
    local ok = _dispatch("system", "SaveSwirPackage", { dir_path = slicesDir, prefix = prefix })
    if not ok then
        log.warn(string.format("  slice %02d (z%+dum): SaveSwirPackage failed (no live SWIR frame?)",
            idx, offsetUm))
    end
    return ok
end

-- Run the inline Z-scan: step Z, detect at each slice (feeding the beam server),
-- save each slice's images, return to the taught Z, then fit.
local function runBeamScanInline(st, slicesDir)
    beamReset()
    motion.setVelocity("gantry-main", SCAN_VELOCITY)
    motion.moveRel("gantry-main", "Z", SCAN_START_OFFSET_UM / 1000.0)
    motion.waitDone("gantry-main", 30000)
    bobcat.wait(500)

    log.info(string.format("  Z-scan: %d slices, %+dum .. %+dum step %dum",
        SCAN_NUM_SLICES, SCAN_START_OFFSET_UM,
        SCAN_START_OFFSET_UM + (SCAN_NUM_SLICES - 1) * SCAN_STEP_UM, SCAN_STEP_UM))

    for idx = 0, SCAN_NUM_SLICES - 1 do
        local offsetUm = SCAN_START_OFFSET_UM + idx * SCAN_STEP_UM
        beamDetectAtCurrentZ()
        saveSlice(st, slicesDir, idx, offsetUm)
        if idx < SCAN_NUM_SLICES - 1 then
            motion.moveRel("gantry-main", "Z", SCAN_STEP_UM / 1000.0)
            motion.waitDone("gantry-main", 30000)
            bobcat.wait(150)
        end
    end

    local lastOffsetUm = SCAN_START_OFFSET_UM + (SCAN_NUM_SLICES - 1) * SCAN_STEP_UM
    motion.moveRel("gantry-main", "Z", -lastOffsetUm / 1000.0)
    motion.waitDone("gantry-main", 30000)
    bobcat.wait(500)

    beamFit()
end

-- ---------------------------------------------------------------------------
-- Read fit beams + SWIR detector slots, and correlate them by location so each
-- beam carries both its fit results (poynt/waist) and its detector slot (for the
-- per-beam crop file beam_B<slot>.png).
-- ---------------------------------------------------------------------------
local POYNT_ZERO_EPS = 1e-6
local function isZero(v) return v ~= nil and math.abs(v) < POYNT_ZERO_EPS end

local function readFitBeams()
    local n = math.floor(ctx.getNumeric("beam.num_beams") + 0.5)
    local beams = {}
    for i = 0, n - 1 do
        local p = "beam." .. i .. "."
        beams[#beams + 1] = {
            loc_x   = ctx.getNumeric(p .. "beam_loc_x_um"),
            loc_y   = ctx.getNumeric(p .. "beam_loc_y_um"),
            poynt_x = ctx.getNumeric(p .. "poynt_x_deg"),
            poynt_y = ctx.getNumeric(p .. "poynt_y_deg"),
            waist_x = ctx.getNumeric(p .. "waist_x_um"),
            waist_y = ctx.getNumeric(p .. "waist_y_um"),
            z0      = ctx.getNumeric(p .. "centroid_z_um"),
            m2_x    = ctx.getNumeric(p .. "m2_x"),
            m2_y    = ctx.getNumeric(p .. "m2_y"),
        }
    end
    return beams, n
end

local function readSwirSlots()
    _dispatch("system", "GetSwirBeamState", {})
    local n = math.floor(ctx.getNumeric("swir.num_beams") + 0.5)
    local slots = {}
    for i = 0, n - 1 do
        local s = "swir." .. i .. "."
        slots[#slots + 1] = {
            slot  = i,
            cx_px = ctx.getNumeric(s .. "cx_px"),
            cy_px = ctx.getNumeric(s .. "cy_px"),
            cx_um = ctx.getNumeric(s .. "cx_um"),
            cy_um = ctx.getNumeric(s .. "cy_um"),
            peak  = ctx.getNumeric(s .. "peak_intensity"),
            ee    = ctx.getNumeric(s .. "ee_pct"),
        }
    end
    return slots, n
end

-- Attach the nearest detector slot (by location) to each fit beam.
local function attachSlots(beams, slots)
    for _, fb in ipairs(beams) do
        local best, bestD = nil, nil
        for _, s in ipairs(slots) do
            local dx = s.cx_um - fb.loc_x
            local dy = s.cy_um - fb.loc_y
            local d = dx * dx + dy * dy
            if bestD == nil or d < bestD then bestD = d; best = s end
        end
        if best then
            fb.slot = best.slot; fb.cx_px = best.cx_px; fb.cy_px = best.cy_px
            fb.peak = best.peak; fb.ee = best.ee
        else
            fb.slot = -1; fb.cx_px = 0; fb.cy_px = 0; fb.peak = 0; fb.ee = 0
        end
    end
end

-- One channel per station: the two strongest beams as Rx (top, smaller loc_y) /
-- Tx (bottom, larger loc_y). Returns b0(Rx), b1(Tx) or nil if fewer than 2 beams.
local function pickRxTx(beams)
    if #beams < 2 then return nil end
    table.sort(beams, function(a, b) return (a.peak or 0) > (b.peak or 0) end)
    local a, b = beams[1], beams[2]
    if a.loc_y <= b.loc_y then return a, b else return b, a end
end

-- ---------------------------------------------------------------------------
-- Per-station: move to the resolved target -> exposure -> scan -> fit -> record.
-- ---------------------------------------------------------------------------
local function measureStation(st)
    log.info(string.format("--> %s (X=%.4f Y=%.4f Z=%.4f mm)", st.pos, st.tx, st.ty, st.tz))

    motion.moveAbs("gantry-main", {
        x = tostring(st.tx), y = tostring(st.ty), z = tostring(st.tz),
        velocity = tostring(TRAVEL_VELOCITY),
    })
    motion.waitDone("gantry-main", 30000)
    bobcat.wait(500)

    local tempC = tec.readTemp(TEC_DEVICE)
    bobcat.watch(st.pos .. ".tempC", tempC)

    -- Exposure: fixed (IR endpoints) or exposure-only auto-adjust (channels).
    local stationExposureUs
    if st.expFixed then
        stationExposureUs = st.expFixed
        camera.setExposure("swir_camera", stationExposureUs)
        bobcat.wait(EXPOSURE_SETTLE_MS)
        log.info(string.format("  [%s] fixed exposure %d us", st.pos, stationExposureUs))
    else
        stationExposureUs = autoAdjustExposure(st)
    end
    bobcat.watch(st.pos .. ".exposureUs", stationExposureUs)

    local imgDir    = captureRoot .. "/" .. st.pos
    local slicesDir = imgDir .. "/slices"

    runBeamScanInline(st, slicesDir)

    -- Correlate fit beams with detector slots, then pick the Rx/Tx pair.
    local beams, nfit = readFitBeams()
    local slots       = readSwirSlots()
    attachSlots(beams, slots)
    local b0, b1 = pickRxTx(beams)

    -- Save the final centred SWIR package (raw/overlay/B0/B1/collage by slot).
    _dispatch("system", "SaveSwirPackage", { dir_path = imgDir, prefix = "beam" })

    if b0 == nil then
        log.warn(string.format("%s: only %d beam(s) fit (need 2) — recording as not-detected",
            st.pos, nfit))
        failedStations[#failedStations + 1] = st.pos
        file.appendCsv(csvPath, file.csvRow({
            runTag, bobcat.timestamp("iso"), st.pos, st.chan, st.kind, "beam_not_detected",
            tempC, stationExposureUs, st.tx, st.ty, st.tz,
            "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
            "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
        }), header)
        return
    end

    if isZero(b0.poynt_x) and isZero(b0.poynt_y) then
        log.warn(string.format("  %s: b0 Poynt (0,0) — fit may have failed", st.pos))
    end
    local b0img = string.format("beam_B%d.png", b0.slot)
    local b1img = string.format("beam_B%d.png", b1.slot)
    local b0lbl = st.chan .. ".Rx"
    local b1lbl = st.chan .. ".Tx"

    bobcat.watch(b0lbl .. ".P.X", b0.poynt_x)
    bobcat.watch(b0lbl .. ".P.Y", b0.poynt_y)
    bobcat.watch(b1lbl .. ".P.X", b1.poynt_x)
    bobcat.watch(b1lbl .. ".P.Y", b1.poynt_y)

    file.appendCsv(csvPath, file.csvRow({
        runTag, bobcat.timestamp("iso"), st.pos, st.chan, st.kind, "ok",
        tempC, stationExposureUs, st.tx, st.ty, st.tz,
        b0lbl, b0.slot, b0img, b0.cx_px, b0.cy_px,
        b0.poynt_x, b0.poynt_y, b0.loc_x, b0.loc_y, b0.z0,
        b0.waist_x, b0.waist_y, b0.m2_x, b0.m2_y, b0.ee, b0.peak,
        b1lbl, b1.slot, b1img, b1.cx_px, b1.cy_px,
        b1.poynt_x, b1.poynt_y, b1.loc_x, b1.loc_y, b1.z0,
        b1.waist_x, b1.waist_y, b1.m2_x, b1.m2_y, b1.ee, b1.peak,
    }), header)

    log.info(string.format("  [%s] %s Rx(B%d)=(%.3f,%.3f) Tx(B%d)=(%.3f,%.3f) deg | exp %d us",
        st.pos, st.chan, b0.slot, b0.poynt_x, b0.poynt_y,
        b1.slot, b1.poynt_x, b1.poynt_y, stationExposureUs))
    log.info("Saved beam images -> " .. imgDir)
end

-- ---------------------------------------------------------------------------
-- Read the two taught endpoints, build the 14-station list (interpolated
-- channels), then run. The SWIR window must be open + streaming with beam
-- detection on so the peak (auto-exposure) and per-slice crops are populated.
-- ---------------------------------------------------------------------------
beamConnect()
_dispatch("system", "SetBeamDetect", { enabled = "true" })

-- Read both taught endpoints (the interpolation anchors).
motion.moveToNamed("gantry-main", ANCHOR_A1)
motion.waitDone("gantry-main", 30000)
bobcat.wait(500)
local A1 = motion.getPosition("gantry-main")
log.info(string.format("Anchor %s: X=%.4f Y=%.4f Z=%.4f mm", ANCHOR_A1, A1.x, A1.y, A1.z))

motion.moveToNamed("gantry-main", ANCHOR_A2)
motion.waitDone("gantry-main", 30000)
bobcat.wait(500)
local A2 = motion.getPosition("gantry-main")
log.info(string.format("Anchor %s: X=%.4f Y=%.4f Z=%.4f mm", ANCHOR_A2, A2.x, A2.y, A2.z))

-- Channel n (1..NUM_CHANNELS) sits at fraction n/(NUM_CHANNELS+1) of the
-- A1 -> A2 line. X/Z are interpolated (follow any tilt); Y uses the interpolated
-- step but never less than MIN_STEP_MM so endpoints taught too close never bunch
-- the channels. A2 is measured at its own taught position.
local segs   = NUM_CHANNELS + 1
local spanY  = A1.y - A2.y                       -- > 0 when A2 is toward Y-
local stepY  = spanY / segs
if math.abs(stepY) < MIN_STEP_MM then
    stepY = (stepY < 0) and -MIN_STEP_MM or MIN_STEP_MM
    log.warn(string.format("Endpoints only %.3f mm apart in Y — clamping channel step to %.3f mm",
        spanY, stepY))
end

local STATIONS = {}
STATIONS[#STATIONS + 1] = { pos = ANCHOR_A1, chan = "A1", kind = "ir", expFixed = expA1,
                            tx = A1.x, ty = A1.y, tz = A1.z }
for n = 1, NUM_CHANNELS do
    local f = n / segs
    STATIONS[#STATIONS + 1] = {
        pos  = "ch" .. n, chan = "ch" .. n, kind = "ch",
        tx   = A1.x + f * (A2.x - A1.x),
        ty   = A1.y - n * stepY,
        tz   = A1.z + f * (A2.z - A1.z),
    }
end
STATIONS[#STATIONS + 1] = { pos = ANCHOR_A2, chan = "A2", kind = "ir", expFixed = expA2,
                            tx = A2.x, ty = A2.y, tz = A2.z }

local ok, err = pcall(function()
    for _, st in ipairs(STATIONS) do measureStation(st) end
end)

if not ok then
    log.error("Aborted: " .. tostring(err))
    bobcat.prompt("MOE capture aborted", tostring(err))
    return
end

local nDone = #STATIONS - #failedStations
log.info("Capture root: " .. captureRoot)
log.info("CSV: " .. csvPath)
if #failedStations > 0 then
    local list = table.concat(failedStations, ", ")
    log.warn(string.format("Stations with no Rx/Tx pair (%d): %s", #failedStations, list))
    bobcat.prompt("MOE capture complete (with skips)", string.format(
        "%d / %d stations captured.\nNo pair at: %s", nDone, #STATIONS, list))
else
    log.info("All stations captured successfully.")
end
log.info(string.format("=== MOE capture-all complete: %d/%d stations OK ===", nDone, #STATIONS))
