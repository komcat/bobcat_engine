-- beam_capture_all_channels_v2.lua
-- Capture beam images + metadata across all measurement stations, labelling
-- the two detected beams per the station table. Mirrors the Poynt Optimize
-- "Single Measurement" capture (raw/overlay/B0/B1/collage PNG package) and
-- records the per-beam metadata (with the resolved label tag), the applied
-- PSU power, and the TEC temperature into the DUT JSON file and a CSV summary.
--
-- v2 ADDS PER-STATION AUTO-EXPOSURE (both directions) -------------------------
-- Different stations produce wildly different beam brightness, so a single
-- fixed SWIR exposure over-exposes some beams (peak pixel pinned at 255) and
-- leaves others too faint to detect. v2, after moving to each station (and
-- before the Z-scan), reads the live SWIR peak pixel value and adjusts the SWIR
-- exposure to land the brightest beam's peak inside the good-detection window
-- [WEAK_PK, SAT_PK) = [120, 225). The step size is RANGE-BASED (exposureStepUs):
-- 25 us up to 300, 50 us up to 1000, 100 us up to the 5000 us ceiling.
--   * while EITHER beam is over-exposed (peak >= SAT_PK = 225) it steps DOWN
--     until no beam is saturated (or the EXPOSURE_MIN_US = 25 us floor is hit);
--   * a weak / undetected beam (peak < WEAK_PK = 120, or no beam) is NOT climbed
--     up to — instead the exposure JUMPS to the EXPOSURE_MAX_US ceiling (which
--     deliberately over-exposes to force a detection) and then dims back DOWN
--     into the window, which is far more reliable for a faint beam.
-- DRIVE-CURRENT FALLBACK: when exposure alone isn't enough — at EXPOSURE_MIN_US
-- a channel beam is STILL over-exposed — the script then steps that station's
-- PSU_UDP drive current DOWN by PSU_UDP_CURRENT_STEP_MA (15 mA) from
-- PSU_UDP_DEFAULT_MA (870 mA) until the peak drops in range or the
-- PSU_UDP_CURRENT_MIN_MA (600 mA) floor is hit. The current is reset to the
-- default at the start of every station, so a reduction never carries forward.
-- The exposure left applied for the station is recorded in the CSV
-- (swir_exposure_us column) and the DUT JSON metadata; the actual (possibly
-- reduced) drive current is captured by the per-station power read-back.
--
-- The peak pixel data comes from the SWIR Beam Processing window's beam
-- detection (GetSwirBeamState -> swir.{i}.peak_intensity). The window must be
-- open and streaming; this script turns beam detection ON programmatically
-- (SetBeamDetect) so the peak/saturation readings are populated.
--
-- In addition to the single centred capture, this script also runs the Z-scan
-- INLINE (instead of delegating to the "Beam Detect Loop" recipe) so it can
-- save EVERY beam slice during the scan, before beam fitting. At each Z height
-- it saves a SWIR package into <station>/slices/ :
--   <station>_slice_<NN>_z<offset>um_raw.png      (full frame)
--   <station>_slice_<NN>_z<offset>um_overlay.png  (full frame + overlay)
--   <station>_slice_<NN>_z<offset>um_B0.png       (per-beam crop, detector slot 0)
--   <station>_slice_<NN>_z<offset>um_B1.png       (per-beam crop, detector slot 1)
--   <station>_slice_<NN>_z<offset>um_collage.png
-- NN is the slice index (00..) and <offset> is the signed Z offset in um from
-- the station's taught Z. The per-beam crops are named by DETECTOR SLOT (_B0 /
-- _B1); the slot->label mapping (e.g. _B0 = A1.RX) is recorded in the CSV/JSON
-- (b0_slot / b1_slot) — the Lua sandbox has no file-rename so crops cannot be
-- renamed to the beam label directly.
--
-- Run order : MOE-A1, MOE-A2, then ch1..ch6 (SA 03 04 .. SA 13 14).
--
-- Per-station power (set current first, output ON; all other PSU outputs OFF;
-- then settle >= 2 s BEFORE moving the gantry to that station):
--   MOE-A1 -> PSU_A1_TX 140 mA + PSU_RX 160 mA
--   MOE-A2 -> PSU_A2_TX 140 mA + PSU_RX 160 mA
--   ch1..3 -> PSU_UDP ch1 870 mA
--   ch4..6 -> PSU_UDP ch2 870 mA
--   Temperature (tec-main) -> recorded only, never driven.
--
-- Beam labelling (the tag is decided by WHERE the beam sits on the image):
--   MOE-A1 / MOE-A2  -> top beam    = B0 label (A1.RX / A2.RX)
--                       bottom beam = B1 label (A1.TX / A.TX)
--   ch1..ch6 (SA xx) -> left  beam  = B0 label (ch.2/.4/.6/.8/.10/.12)
--                       right beam  = B1 label (ch.1/.3/.5/.7/.9/.11)
-- Image Y grows downward, so "top" = smaller cy_px, "bottom" = larger cy_px;
-- "left" = smaller cx_px, "right" = larger cx_px.
--
-- Prerequisites:
--   * SWIR beam window open and streaming (SaveSwirPackage + peak readings need
--     a live frame; this script turns beam detection on for the peak readings).
--   * The named positions below are taught for gantry-main.
--   * The Python beam detection server is running (the inline scan connects to
--     it directly; the "Beam Detect Loop" recipe is no longer used).

log.info("=== Beam capture v2 (per-station auto-exposure): all stations ===")

-- Tunables -------------------------------------------------------------------
local POWER_SETTLE_MS = 2000   -- power must be on >= this long before moving
local TEC_DEVICE = "tec-main"

-- When true, ch1..ch6 + MOE-A2 are auto-derived from the taught MOE-A1 anchor
-- at the start of the run (same as the SWIR window "update ch1-6" button). When
-- false (default), the existing taught positions are used unchanged. Flip this
-- to re-enable the automatic position update later.
local AUTO_UPDATE_CHANNELS = false

-- Auto-exposure (anti over-exposure AND anti under-exposure). At each station
-- we start from SWIR_EXPOSURE_START_US and adjust by a RANGE-BASED step
-- (exposureStepUs) to land the brightest beam's peak pixel value inside
-- [WEAK_PK, SAT_PK):
--   * peak >= SAT_PK  -> over-exposed: step DOWN, never below EXPOSURE_MIN_US.
--   * peak <  WEAK_PK -> weak/undetected: jump to EXPOSURE_MAX_US, then dim DOWN.
-- A good (un-clipped) beam detection sits in peak pixel ~[120, 225]; above 225
-- the beam saturates and the fit/crop gets corrupted, so SAT_PK is 225.
--
-- DRIVE-CURRENT FALLBACK: exposure alone may not be enough — at EXPOSURE_MIN_US
-- some beams are still over-exposed. When that happens on a channel station
-- (one driven by PSU_UDP), we then step the PSU_UDP drive current DOWN by
-- PSU_UDP_CURRENT_STEP_MA (from PSU_UDP_DEFAULT_MA) until the peak drops under
-- SAT_PK or the PSU_UDP_CURRENT_MIN_MA floor is hit. The current is reset to
-- PSU_UDP_DEFAULT_MA at the start of every station (by applyStationPower) so the
-- reduction never carries over to the next position.
local SWIR_EXPOSURE_START_US = 300   -- starting exposure at every station (us)
-- Exposure step size is RANGE-BASED (see exposureStepUs): exposure <= 300 us ->
-- 25 us, <= 1000 us -> 50 us, else -> 100 us. A weak/undetected beam does NOT
-- climb up from the start; it jumps to EXPOSURE_MAX_US (deliberately
-- over-exposing) and dims back down into the window with these steps.
local EXPOSURE_MIN_US        = 25    -- floor; do not dim below this (us)
local EXPOSURE_MAX_US        = 5000  -- ceiling; do not brighten above this (us)
local SAT_PK                 = 225   -- peak >= this = over-exposed (dim / reduce current)
local WEAK_PK                = 120   -- peak <  this = weak beam (jump to max, dim down)
local EXPOSURE_SETTLE_MS     = 300   -- settle after each exposure change (~3 frames @10fps)
local EXPOSURE_MAX_ITERS     = 120   -- safety cap (jump-to-max dim-down + current steps)

-- PSU_UDP drive-current reduction (the EXPOSURE_MIN_US over-exposure fallback).
-- Only channel stations (ch1..ch6, driven by PSU_UDP) are reduced; the MOE
-- stations' currents are managed externally and left untouched.
local PSU_UDP_DEFAULT_MA     = 870   -- nominal channel drive current (mA)
local PSU_UDP_CURRENT_STEP_MA = 15   -- reduce by this per step when still over-exposed (mA)
local PSU_UDP_CURRENT_MIN_MA = 600   -- floor; do not drive a channel below this (mA)
local CURRENT_SETTLE_MS      = 400   -- settle after each current change before re-reading peak

-- Inline Z-scan (mirrors the "Beam Detect Loop" recipe). The scan starts at
-- SCAN_START_OFFSET_UM relative to the station's taught Z, takes SCAN_NUM_SLICES
-- detections stepping +SCAN_STEP_UM each time, then returns to the taught Z and
-- fits. A SWIR package (full frame + per-beam crops) is saved at every slice.
local SCAN_START_OFFSET_UM = -40   -- first slice, relative to taught Z (um)
local SCAN_STEP_UM         = 10    -- Z step between slices (um)
local SCAN_NUM_SLICES      = 9     -- number of slices (-40..+40 um by default)
local SCAN_VELOCITY        = 10    -- gantry velocity during the scan
local DETECT_FRAMES        = 3     -- frames averaged per slice detection
local UM_PER_PX            = "0.25"
local WAVELENGTH_M         = "1550e-9"
local BEAM_HOST            = "127.0.0.1"
local BEAM_PORT            = "5555"

-- ---------------------------------------------------------------------------
-- 1. DUT — ensure one is loaded; prompt for a serial number if not.
--    An empty product name makes the DUT handler fall back to the active
--    product (ProductPositionManager), so we only ask for the serial.
-- ---------------------------------------------------------------------------
if not dut.isLoaded() then
    bobcat.askInput("DUT Serial Number",
        "No DUT loaded. Enter the DUT serial number to create/load:",
        "", "_dut_sn")
    local sn = ctx.getString("_dut_sn")
    if sn == nil or sn == "" then
        log.error("No serial number entered — aborting.")
        bobcat.prompt("Aborted", "No DUT serial number entered.")
        return
    end
    dut.loadOrCreate("", sn)   -- "" -> active product
    if not dut.isLoaded() then
        log.error("Failed to create/load DUT '" .. sn .. "' — aborting.")
        bobcat.prompt("Aborted", "Failed to create/load DUT '" .. sn .. "'.")
        return
    end
end

local sn          = dut.serial()
local productName  = dut.product()
log.info(string.format("DUT: %s / %s", tostring(productName), tostring(sn)))

-- ---------------------------------------------------------------------------
-- 1b. Optionally auto-derive ch1..ch6 + MOE-A2 from the taught MOE-A1 anchor
--     (same as the SWIR window "update ch1-6" button) so every station below
--     moves to a freshly computed position. Gated behind AUTO_UPDATE_CHANNELS
--     (default false) — flip the flag above to re-enable. Non-fatal: if MOE-A1
--     is not taught the _dispatch raises, so we pcall it and fall back to the
--     existing taught positions rather than aborting the whole capture.
-- ---------------------------------------------------------------------------
if AUTO_UPDATE_CHANNELS then
    local okUpd, errUpd = pcall(function() return motion.updateChannelsFromMoeA1() end)
    if okUpd then
        log.info("Derived ch1-6 + MOE-A2 from MOE-A1 (positions updated).")
    else
        log.warn("Could not derive ch1-6 + MOE-A2 from MOE-A1 (" .. tostring(errUpd)
            .. ") — using existing taught positions.")
    end
else
    log.info("AUTO_UPDATE_CHANNELS disabled — using existing taught ch1-6 + MOE-A2 positions.")
end

-- ---------------------------------------------------------------------------
-- 2. Station table.  axis = "y" -> label by top/bottom; "x" -> left/right.
--    psus = the outputs to drive ON at this station {dev, ch, mA}; everything
--    else in ALL_PSU_OUTPUTS is forced OFF.
-- ---------------------------------------------------------------------------
local STATIONS = {
    { pos = "MOE-A1",   b0 = "A1.RX", b1 = "A1.TX", axis = "y",
      psus = { {dev="PSU_A1_TX", ch=1, mA=140}, {dev="PSU_RX", ch=1, mA=160} } },
    { pos = "MOE-A2",   b0 = "A2.RX", b1 = "A.TX",  axis = "y",
      psus = { {dev="PSU_A2_TX", ch=1, mA=140}, {dev="PSU_RX", ch=1, mA=160} } },
    { pos = "SA 03 04", b0 = "ch.2",  b1 = "ch.1",  axis = "x",   -- ch1
      psus = { {dev="PSU_UDP", ch=1, mA=PSU_UDP_DEFAULT_MA} } },
    { pos = "SA 05 06", b0 = "ch.4",  b1 = "ch.3",  axis = "x",   -- ch2
      psus = { {dev="PSU_UDP", ch=1, mA=PSU_UDP_DEFAULT_MA} } },
    { pos = "SA 07 08", b0 = "ch.6",  b1 = "ch.5",  axis = "x",   -- ch3
      psus = { {dev="PSU_UDP", ch=1, mA=PSU_UDP_DEFAULT_MA} } },
    { pos = "SA 09 10", b0 = "ch.8",  b1 = "ch.7",  axis = "x",   -- ch4
      psus = { {dev="PSU_UDP", ch=2, mA=PSU_UDP_DEFAULT_MA} } },
    { pos = "SA 11 12", b0 = "ch.10", b1 = "ch.9",  axis = "x",   -- ch5
      psus = { {dev="PSU_UDP", ch=2, mA=PSU_UDP_DEFAULT_MA} } },
    { pos = "SA 13 14", b0 = "ch.12", b1 = "ch.11", axis = "x",   -- ch6
      psus = { {dev="PSU_UDP", ch=2, mA=PSU_UDP_DEFAULT_MA} } },
}

-- Every PSU output the script may touch — anything not in a station's ON list
-- gets switched OFF at that station.
local ALL_PSU_OUTPUTS = {
    { dev = "PSU_RX",    ch = 1 },
    { dev = "PSU_A1_TX", ch = 1 },
    { dev = "PSU_A2_TX", ch = 1 },
    { dev = "PSU_UDP",   ch = 1 },
    { dev = "PSU_UDP",   ch = 2 },
    { dev = "PSU_UDP",   ch = 3 },
}
local PSU_DEVICES = { "PSU_RX", "PSU_A1_TX", "PSU_A2_TX", "PSU_UDP" }

local failedStations = {}   -- stations skipped because no beam was detected

local runTag      = bobcat.timestamp()
local captureRoot = string.format("data/%s/%s/%s", productName, sn, runTag)
local csvPath     = string.format("data/%s/%s_beam_capture_%s.csv",
                                  productName, sn, runTag)
local header = file.csvRow({
    "timestamp", "station", "status", "image_dir", "psu_summary", "temperature_c",
    "swir_exposure_us",
    "b0_label", "b0_slot", "b0_image", "b0_cx_px", "b0_cy_px",
    "b0_poynt_x_deg", "b0_poynt_y_deg", "b0_loc_x_um", "b0_loc_y_um",
    "b0_d4_x_um", "b0_d4_y_um", "b0_ee_pct", "b0_peak",
    "b1_label", "b1_slot", "b1_image", "b1_cx_px", "b1_cy_px",
    "b1_poynt_x_deg", "b1_poynt_y_deg", "b1_loc_x_um", "b1_loc_y_um",
    "b1_d4_x_um", "b1_d4_y_um", "b1_ee_pct", "b1_peak",
})
log.info("CSV: " .. csvPath)

-- ---------------------------------------------------------------------------
-- 3. PSU helpers (channel-aware via _dispatch; the psu.* wrappers can't pass
--    a channel, which PSU_UDP needs).  SetCurrent takes mA.
-- ---------------------------------------------------------------------------
local function psuSetCurrent(dev, ch, mA)
    _dispatch(dev, "SetCurrent", { current = tostring(mA), channel = tostring(ch) })
end
local function psuOn(dev, ch)
    _dispatch(dev, "OutputOn", { channel = tostring(ch) })
end
local function psuOff(dev, ch)
    _dispatch(dev, "OutputOff", { channel = tostring(ch) })
end
local function psuReadCurrentmA(dev, ch)
    _dispatch(dev, "ReadCurrent", { channel = tostring(ch), captureAs = "_prc" })
    return ctx.getNumeric("_prc") * 1000.0   -- ReadCurrent returns amps
end
local function keyOf(dev, ch) return dev .. "#" .. tostring(ch) end

-- Devices whose current is left at whatever it is already set to: the script
-- only toggles their output ON/OFF and does NOT issue SetCurrent. (MOE TX/RX
-- currents are managed externally; PSU_UDP still gets its SetCurrent.)
local SETCURRENT_SKIP = {
    PSU_A1_TX = true,
    PSU_A2_TX = true,
    PSU_RX    = true,
}

local function connectPSUs()
    for _, d in ipairs(PSU_DEVICES) do
        _dispatch(d, "Connect", {})
    end
end

local function allPsusOff()
    for _, out in ipairs(ALL_PSU_OUTPUTS) do psuOff(out.dev, out.ch) end
end

-- Drive the station's outputs ON (current first), force every other output
-- OFF, then settle for POWER_SETTLE_MS before the caller moves the gantry.
local function applyStationPower(st)
    local onSet = {}
    for _, p in ipairs(st.psus) do onSet[keyOf(p.dev, p.ch)] = p end
    for _, out in ipairs(ALL_PSU_OUTPUTS) do
        local on = onSet[keyOf(out.dev, out.ch)]
        if on then
            -- MOE PSUs (PSU_A1_TX/PSU_A2_TX/PSU_RX): output ON/OFF only, current
            -- left as-is. Others (PSU_UDP): set current first, then ON.
            if SETCURRENT_SKIP[on.dev] then
                psuOn(on.dev, on.ch)
                log.info(string.format("PSU ON  %s ch%d (current unchanged)", on.dev, on.ch))
            else
                psuSetCurrent(on.dev, on.ch, on.mA)
                psuOn(on.dev, on.ch)
                log.info(string.format("PSU ON  %s ch%d @ %d mA", on.dev, on.ch, on.mA))
            end
        else
            psuOff(out.dev, out.ch)
        end
    end
    log.info(string.format("Power settling %d ms before move...", POWER_SETTLE_MS))
    bobcat.wait(POWER_SETTLE_MS)
end

-- ---------------------------------------------------------------------------
-- 3b. Auto-exposure (anti over-exposure AND anti under-exposure) with a
--    drive-current fallback.
--    Read the live SWIR peak pixel value (from the beam window's detection) and
--    step the exposure DOWN while either beam is over-exposed (peak >= SAT_PK),
--    or UP while the brightest beam is too weak to detect (peak < WEAK_PK).
--    When exposure alone can't tame a hot beam — at EXPOSURE_MIN_US the peak is
--    still >= SAT_PK — step the station's PSU_UDP drive current DOWN (channel
--    stations only) until the beam drops into range or the current floor is hit.
--    Called once per station, at the taught Z (the brightest point, before the
--    Z-scan), so the whole scan is captured usable. Returns the exposure (us)
--    left applied for the station, which is recorded in the CSV/JSON metadata.
-- ---------------------------------------------------------------------------
local function readMaxPeak()
    -- Snapshot the SWIR window's beam state into context, then take the max
    -- peak pixel value across all detected beams. Returns peak, numBeams.
    _dispatch("system", "GetSwirBeamState", {})
    local n = math.floor(ctx.getNumeric("swir.num_beams") + 0.5)
    local pkMax = 0
    for i = 0, n - 1 do
        local pk = ctx.getNumeric("swir." .. i .. ".peak_intensity")
        if pk > pkMax then pkMax = pk end
    end
    return pkMax, n
end

-- The reducible PSU_UDP output for a station (ch1..ch6), or nil for the MOE
-- stations whose currents are managed externally and must not be touched.
local function udpOutputOf(st)
    for _, p in ipairs(st.psus) do
        if p.dev == "PSU_UDP" then return p end
    end
    return nil
end

-- Range-based exposure step: coarse up high (fast descent from the ceiling),
-- fine near the target. 0..300 us -> 25 us, 300..1000 us -> 50 us,
-- 1000..5000 us -> 100 us.
local function exposureStepUs(expUs)
    if expUs <= 300 then
        return 25
    elseif expUs <= 1000 then
        return 50
    else
        return 100
    end
end

local function autoAdjustExposure(st)
    local udp   = udpOutputOf(st)                 -- reducible drive current, or nil
    local curMa = udp and PSU_UDP_DEFAULT_MA or nil  -- tracked PSU_UDP current (mA)
    local expUs = SWIR_EXPOSURE_START_US
    camera.setExposure("swir_camera", expUs)
    bobcat.wait(EXPOSURE_SETTLE_MS)

    -- A faint / undetected beam at the start exposure is unreliable to climb up
    -- to (the detector may never lock on), so the FIRST time we see one we jump
    -- straight to EXPOSURE_MAX_US — deliberately over-exposing to force a
    -- detection — and then dim back down into the window. At most one jump per
    -- station; afterwards we only fine-tune.
    local jumpedToMax = false

    for _ = 1, EXPOSURE_MAX_ITERS do
        local pkMax, n = readMaxPeak()
        if n >= 1 and pkMax >= SAT_PK then
            -- Over-exposed. Dim by the range step; once at EXPOSURE_MIN_US,
            -- reduce the PSU_UDP drive current (channel stations only).
            if expUs > EXPOSURE_MIN_US then
                local nextUs = expUs - exposureStepUs(expUs)
                if nextUs < EXPOSURE_MIN_US then nextUs = EXPOSURE_MIN_US end
                log.info(string.format("  [%s] over-exposed (peak=%d >= %d) -> exposure %d -> %d us",
                    st.pos, math.floor(pkMax + 0.5), SAT_PK, expUs, nextUs))
                expUs = nextUs
                camera.setExposure("swir_camera", expUs)
                bobcat.wait(EXPOSURE_SETTLE_MS)
            elseif udp ~= nil and (curMa - PSU_UDP_CURRENT_STEP_MA) >= PSU_UDP_CURRENT_MIN_MA then
                local nextMa = curMa - PSU_UDP_CURRENT_STEP_MA
                log.info(string.format("  [%s] over-exposed at min exposure (peak=%d) -> %s ch%d current %d -> %d mA",
                    st.pos, math.floor(pkMax + 0.5), udp.dev, udp.ch, curMa, nextMa))
                curMa = nextMa
                psuSetCurrent(udp.dev, udp.ch, curMa)
                bobcat.wait(CURRENT_SETTLE_MS)
            else
                if udp ~= nil then
                    log.warn(string.format("  [%s] still over-exposed (peak=%d) at min exposure %d us and min current %d mA",
                        st.pos, math.floor(pkMax + 0.5), EXPOSURE_MIN_US, curMa))
                else
                    log.warn(string.format("  [%s] still over-exposed (peak=%d) at min exposure %d us (no PSU_UDP to reduce)",
                        st.pos, math.floor(pkMax + 0.5), EXPOSURE_MIN_US))
                end
                return expUs
            end
        elseif n < 1 or pkMax < WEAK_PK then
            -- Under-exposed / undetected.
            if not jumpedToMax and expUs < EXPOSURE_MAX_US then
                local why = (n < 1) and "no beam"
                    or string.format("peak=%d < %d", math.floor(pkMax + 0.5), WEAK_PK)
                log.info(string.format("  [%s] weak/undetected (%s) at %d us -> jump to max %d us, then dim down",
                    st.pos, why, expUs, EXPOSURE_MAX_US))
                jumpedToMax = true
                expUs = EXPOSURE_MAX_US
                camera.setExposure("swir_camera", expUs)
                bobcat.wait(EXPOSURE_SETTLE_MS)
            elseif expUs >= EXPOSURE_MAX_US then
                log.warn(string.format("  [%s] still weak/undetected at ceiling %d us",
                    st.pos, EXPOSURE_MAX_US))
                return expUs
            else
                -- Dimming overshot just below the window; nudge back UP by the
                -- range step. (Drive current is only ever reduced, never raised.)
                local nextUs = expUs + exposureStepUs(expUs)
                if nextUs > EXPOSURE_MAX_US then nextUs = EXPOSURE_MAX_US end
                log.info(string.format("  [%s] weak beam (peak=%d < %d) -> exposure %d -> %d us",
                    st.pos, math.floor(pkMax + 0.5), WEAK_PK, expUs, nextUs))
                expUs = nextUs
                camera.setExposure("swir_camera", expUs)
                bobcat.wait(EXPOSURE_SETTLE_MS)
            end
        else
            -- In the good window [WEAK_PK, SAT_PK).
            if curMa ~= nil and curMa < PSU_UDP_DEFAULT_MA then
                log.info(string.format("  [%s] exposure OK: %d us (peak=%d, %d <= peak < %d) @ %s ch%d %d mA",
                    st.pos, expUs, math.floor(pkMax + 0.5), WEAK_PK, SAT_PK, udp.dev, udp.ch, curMa))
            else
                log.info(string.format("  [%s] exposure OK: %d us (peak=%d, %d <= peak < %d)",
                    st.pos, expUs, math.floor(pkMax + 0.5), WEAK_PK, SAT_PK))
            end
            return expUs
        end
    end
    log.warn(string.format("  [%s] auto-exposure hit %d iters, leaving %d us",
        st.pos, EXPOSURE_MAX_ITERS, expUs))
    return expUs
end

-- ---------------------------------------------------------------------------
-- 4. Inline Z-scan + slice capture (replaces the "Beam Detect Loop" recipe so
--    we can save every slice before fitting). Poynt (0,0) on either beam means
--    the detector left the context at its default — treated as a failed station.
-- ---------------------------------------------------------------------------
local POYNT_ZERO_EPS = 1e-6
local function poyntFailed()
    local function isZero(v) return v ~= nil and math.abs(v) < POYNT_ZERO_EPS end
    local b0px = ctx.getNumeric("beam.0.poynt_x_deg")
    local b0py = ctx.getNumeric("beam.0.poynt_y_deg")
    local b1px = ctx.getNumeric("beam.1.poynt_x_deg")
    local b1py = ctx.getNumeric("beam.1.poynt_y_deg")
    return (isZero(b0px) and isZero(b0py)) or (isZero(b1px) and isZero(b1py))
end

-- Beam server dispatch helpers (device name = "beamserver").
local function beamConnect()
    _dispatch("beamserver", "Connect", { host = BEAM_HOST, port = BEAM_PORT })
end
local function beamReset()
    _dispatch("beamserver", "Reset", {})
end
local function beamDetectAtCurrentZ()
    -- Mirrors the recipe's "Beam: Detect N": read Z live, average DETECT_FRAMES,
    -- one registration per slice. Server accumulates the slice for the fit.
    _dispatch("beamserver", "DetectN", {
        camera_device     = "swir_camera",
        captureAs         = "beam",
        use_device_z      = "true",
        z_device          = "gantry-main",
        z_axis            = "Z",
        z_um              = "",
        num_frames        = tostring(DETECT_FRAMES),
        repeat_count      = "1",
        delay_between_ms  = "100",
        inbounce_delay_ms = "100",
        outbounce_delay_ms = "100",
        rotate_180        = "false",
        um_per_px_x       = UM_PER_PX,
        um_per_px_y       = UM_PER_PX,
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

-- Save one slice's SWIR package (full frame + per-beam crops) into slicesDir.
-- Best-effort: _raw always saves if a live frame exists; per-beam crops only
-- save when the SWIR window has detected the beams at this Z.
local function saveSlice(st, slicesDir, idx, offsetUm)
    local prefix = string.format("%s_slice_%02d_z%+dum", st.pos, idx, offsetUm)
    local ok = _dispatch("system", "SaveSwirPackage",
                         { dir_path = slicesDir, prefix = prefix })
    if not ok then
        log.warn(string.format("  slice %02d (z%+dum): SaveSwirPackage failed "
            .. "(no live SWIR frame?)", idx, offsetUm))
    end
    return ok
end

-- Run the inline Z-scan for a station: step Z, detect at each slice (feeding the
-- beam server), save each slice's images, recentre, then fit. Returns true if
-- both beams produced a non-(0,0) Poynt, false otherwise (caller skips it).
local function runBeamScanInline(st, slicesDir)
    beamReset()
    motion.setVelocity("gantry-main", SCAN_VELOCITY)

    -- Move to the first slice Z, then settle.
    motion.moveRel("gantry-main", "Z", SCAN_START_OFFSET_UM / 1000.0)
    motion.waitDone("gantry-main", 30000)
    bobcat.wait(500)

    log.info(string.format("  Z-scan: %d slices, %+dum .. %+dum step %dum",
        SCAN_NUM_SLICES, SCAN_START_OFFSET_UM,
        SCAN_START_OFFSET_UM + (SCAN_NUM_SLICES - 1) * SCAN_STEP_UM, SCAN_STEP_UM))

    for idx = 0, SCAN_NUM_SLICES - 1 do
        local offsetUm = SCAN_START_OFFSET_UM + idx * SCAN_STEP_UM
        beamDetectAtCurrentZ()      -- accumulate this slice on the server
        saveSlice(st, slicesDir, idx, offsetUm)  -- save full frame + crops
        if idx < SCAN_NUM_SLICES - 1 then
            motion.moveRel("gantry-main", "Z", SCAN_STEP_UM / 1000.0)
            motion.waitDone("gantry-main", 30000)
            bobcat.wait(150)
        end
    end

    -- Return to the taught Z (undo the net scan offset), then settle.
    local lastOffsetUm = SCAN_START_OFFSET_UM + (SCAN_NUM_SLICES - 1) * SCAN_STEP_UM
    motion.moveRel("gantry-main", "Z", -lastOffsetUm / 1000.0)
    motion.waitDone("gantry-main", 30000)
    bobcat.wait(500)

    beamFit()
    if poyntFailed() then
        log.warn("Beam not detected at " .. st.pos ..
                 " (Poynt 0,0) — skipping this station, continuing.")
        return false
    end
    return true
end

-- Read one detector slot's full data (snapshot pixels + beam-fit results).
local function readSlot(i)
    local p = "beam." .. i .. "."
    local s = "swir." .. i .. "."
    return {
        slot       = i,
        cx_px      = ctx.getNumeric(s .. "cx_px"),
        cy_px      = ctx.getNumeric(s .. "cy_px"),
        d4_x_um    = ctx.getNumeric(s .. "d4_x_um"),
        d4_y_um    = ctx.getNumeric(s .. "d4_y_um"),
        ee_pct     = ctx.getNumeric(s .. "ee_pct"),
        peak       = ctx.getNumeric(s .. "peak_intensity"),
        poynt_x    = ctx.getNumeric(p .. "poynt_x_deg"),
        poynt_y    = ctx.getNumeric(p .. "poynt_y_deg"),
        loc_x_um   = ctx.getNumeric(p .. "beam_loc_x_um"),
        loc_y_um   = ctx.getNumeric(p .. "beam_loc_y_um"),
        z0_um      = ctx.getNumeric(p .. "centroid_z_um"),
        waist_x_um = ctx.getNumeric(p .. "waist_x_um"),
        waist_y_um = ctx.getNumeric(p .. "waist_y_um"),
        m2_x       = ctx.getNumeric(p .. "m2_x"),
        m2_y       = ctx.getNumeric(p .. "m2_y"),
    }
end

local function beamMeta(lbl, b, img)
    return {
        label = lbl, slot = b.slot, image = img,
        cx_px = b.cx_px, cy_px = b.cy_px,
        poynt_x_deg = b.poynt_x, poynt_y_deg = b.poynt_y,
        loc_x_um = b.loc_x_um, loc_y_um = b.loc_y_um, z0_um = b.z0_um,
        d4_x_um = b.d4_x_um, d4_y_um = b.d4_y_um,
        waist_x_um = b.waist_x_um, waist_y_um = b.waist_y_um,
        m2_x = b.m2_x, m2_y = b.m2_y,
        ee_pct = b.ee_pct, peak_intensity = b.peak,
    }
end

-- ---------------------------------------------------------------------------
-- 5. Per-station: power -> settle -> move -> auto-exposure -> detect -> capture
--    -> record.
-- ---------------------------------------------------------------------------
local function measureStation(st)
    log.info(string.format("--> %s   (B0=%s / B1=%s)", st.pos, st.b0, st.b1))

    -- Power ON required outputs, OFF the rest, settle >= 2 s BEFORE moving.
    applyStationPower(st)

    motion.moveToNamed("gantry-main", st.pos)
    motion.waitDone("gantry-main", 30000)
    bobcat.wait(500)

    -- Power readback (best effort) + temperature — recorded for pass OR fail.
    local powerRec = {}
    local psuParts = {}
    for _, p in ipairs(st.psus) do
        local measmA = psuReadCurrentmA(p.dev, p.ch)
        powerRec[#powerRec + 1] = {
            device = p.dev, channel = p.ch,
            setpoint_mA = p.mA, measured_mA = measmA,
        }
        psuParts[#psuParts + 1] = string.format("%s/ch%d=%dmA", p.dev, p.ch, p.mA)
    end
    local psuSummary = table.concat(psuParts, "; ")
    local tempC = tec.readTemp(TEC_DEVICE)
    bobcat.watch(st.pos .. ".tempC", tempC)

    -- v2: auto-adjust SWIR exposure so neither beam is over-exposed AND a weak
    -- beam is bright enough to detect at this station. Done at the taught Z
    -- (brightest point) before the Z-scan, so the whole scan stays usable.
    -- Recorded in the CSV/JSON metadata.
    local stationExposureUs = autoAdjustExposure(st)
    bobcat.watch(st.pos .. ".exposureUs", stationExposureUs)

    -- Image output dirs: final centred package at <station>/, slices under
    -- <station>/slices/.
    local imgDir    = captureRoot .. "/" .. st.pos
    local slicesDir = imgDir .. "/slices"

    -- Inline Z-scan: save every slice (full frame + per-beam crops) while
    -- feeding the beam server, then fit. On failure: record + skip, keep going.
    if not runBeamScanInline(st, slicesDir) then
        failedStations[#failedStations + 1] = st.pos
        file.appendCsv(csvPath, file.csvRow({
            bobcat.timestamp("iso"), st.pos, "beam_not_detected", slicesDir, psuSummary, tempC,
            stationExposureUs,
            st.b0, "", "", "", "", "", "", "", "", "", "", "", "",
            st.b1, "", "", "", "", "", "", "", "", "", "", "", "",
        }), header)
        dut.insertJson("beam_capture_all_channels", {
            run_tag          = runTag,
            station          = st.pos,
            axis             = st.axis,
            status           = "beam_not_detected",
            swir_exposure_us = stationExposureUs,
            power            = powerRec,
            temperature_c    = tempC,
            slices           = {
                dir        = slicesDir,
                count      = SCAN_NUM_SLICES,
                z_start_um = SCAN_START_OFFSET_UM,
                z_step_um  = SCAN_STEP_UM,
                naming     = "<station>_slice_<NN>_z<offset>um_{raw|overlay|B0|B1|collage}.png",
            },
        })
        return
    end

    -- Snapshot pixel positions so we can label by location on the image.
    _dispatch("system", "GetSwirBeamState", {})
    local n = math.floor(ctx.getNumeric("swir.num_beams") + 0.5)
    if n < 2 then
        log.warn(string.format("%s: only %d beam(s) detected — labels may be unreliable",
                 st.pos, n))
    end

    local s0 = readSlot(0)
    local s1 = readSlot(1)

    -- Which detector slot is B0 (top/left) vs B1 (bottom/right)?
    local b0, b1
    if st.axis == "y" then
        if s0.cy_px <= s1.cy_px then b0, b1 = s0, s1 else b0, b1 = s1, s0 end
    else
        if s0.cx_px <= s1.cx_px then b0, b1 = s0, s1 else b0, b1 = s1, s0 end
    end

    -- Save this station's final centred SWIR image package (raw/overlay/B0/B1/
    -- collage). beam_B0.png / beam_B1.png are by DETECTOR slot, so map each
    -- label to the file for its slot. (imgDir was computed before the scan.)
    _dispatch("system", "SaveSwirPackage", { dir_path = imgDir, prefix = "beam" })
    local b0img = string.format("beam_B%d.png", b0.slot)
    local b1img = string.format("beam_B%d.png", b1.slot)
    log.info("Saved beam images -> " .. imgDir)

    -- Live variables panel.
    bobcat.watch(st.b0 .. ".P.X", b0.poynt_x)
    bobcat.watch(st.b0 .. ".P.Y", b0.poynt_y)
    bobcat.watch(st.b1 .. ".P.X", b1.poynt_x)
    bobcat.watch(st.b1 .. ".P.Y", b1.poynt_y)

    -- CSV summary row.
    file.appendCsv(csvPath, file.csvRow({
        bobcat.timestamp("iso"), st.pos, "ok", imgDir, psuSummary, tempC,
        stationExposureUs,
        st.b0, b0.slot, b0img, b0.cx_px, b0.cy_px,
        b0.poynt_x, b0.poynt_y, b0.loc_x_um, b0.loc_y_um,
        b0.d4_x_um, b0.d4_y_um, b0.ee_pct, b0.peak,
        st.b1, b1.slot, b1img, b1.cx_px, b1.cy_px,
        b1.poynt_x, b1.poynt_y, b1.loc_x_um, b1.loc_y_um,
        b1.d4_x_um, b1.d4_y_um, b1.ee_pct, b1.peak,
    }), header)

    -- Structured metadata into the DUT JSON file (auto-timestamped entry).
    dut.insertJson("beam_capture_all_channels", {
        run_tag          = runTag,
        station          = st.pos,
        axis             = st.axis,
        status           = "ok",
        image_dir        = imgDir,
        swir_exposure_us = stationExposureUs,
        power            = powerRec,
        temperature_c    = tempC,
        b0 = beamMeta(st.b0, b0, b0img),
        b1 = beamMeta(st.b1, b1, b1img),
        slices = {
            dir        = slicesDir,
            count      = SCAN_NUM_SLICES,
            z_start_um = SCAN_START_OFFSET_UM,
            z_step_um  = SCAN_STEP_UM,
            naming     = "<station>_slice_<NN>_z<offset>um_{raw|overlay|B0|B1|collage}.png",
            -- per-beam crops are by detector slot; map slot -> beam label:
            b0_crop    = string.format("_B%d.png = %s", b0.slot, st.b0),
            b1_crop    = string.format("_B%d.png = %s", b1.slot, st.b1),
        },
    })

    log.info(string.format("[%s] %s -> beam_B%d (%.1f,%.1f)px | %s -> beam_B%d (%.1f,%.1f)px | %.2f C | exp %d us",
        st.pos, st.b0, b0.slot, b0.cx_px, b0.cy_px,
        st.b1, b1.slot, b1.cx_px, b1.cy_px, tempC, stationExposureUs))
end

-- ---------------------------------------------------------------------------
-- 6. Run all stations. Always switch every PSU output OFF on exit (incl. abort).
-- ---------------------------------------------------------------------------
connectPSUs()
allPsusOff()                 -- clean known state before the first station
log.info("All PSU outputs OFF (start state).")

-- Connect the beam server once, and make sure the SWIR window is detecting so
-- the live peak readings (auto-exposure) and the per-slice per-beam crops
-- (_B0/_B1) are populated during the scan.
beamConnect()
_dispatch("system", "SetBeamDetect", { enabled = "true" })

local ok, err = pcall(function()
    for _, st in ipairs(STATIONS) do measureStation(st) end
end)
allPsusOff()
log.info("All PSU outputs OFF.")

if not ok then
    -- Unexpected error (not a beam-detect skip — those are handled per station).
    log.error("Aborted: " .. tostring(err))
    bobcat.prompt("Beam capture aborted", tostring(err))
    return
end

local nDone = #STATIONS - #failedStations
log.info("Capture root: " .. captureRoot)
log.info("CSV: " .. csvPath)
if #failedStations > 0 then
    local list = table.concat(failedStations, ", ")
    log.warn(string.format("Stations with NO beam detected (%d): %s",
             #failedStations, list))
    bobcat.prompt("Beam capture complete (with skips)", string.format(
        "%d / %d stations captured.\nNo beam at: %s",
        nDone, #STATIONS, list))
else
    log.info("All stations captured successfully.")
end
log.info(string.format("=== Beam capture v2 complete: %d/%d stations OK ===",
         nDone, #STATIONS))
