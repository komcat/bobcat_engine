-- hex_left_x_repeatability_3sigma.lua
-- Jog hex-left X axis ±100 µm, 100 cycles, at 1 mm/s.
-- Capture before/expected/after position for each move and run a 3-sigma
-- statistical summary on positional error vs target.

local DEVICE   = "hex-left"
local AXIS     = "X"
local STEP_MM  = 0.100   -- 100 µm
local VELOCITY = 1.0     -- mm/s
local CYCLES   = 100
local CSV_PATH = "data/hex_left_x_repeatability.csv"

log.info("=== Repeatability test: %s.%s  cycles=%d  step=%+.0f um  vel=%.1f mm/s ===",
    DEVICE, AXIS, CYCLES, STEP_MM * 1000.0, VELOCITY)

-- Set safe velocity once
motion.setVelocity(DEVICE, VELOCITY)
system.wait(200)

local errFwd, errBwd = {}, {}

-- CSV header (appended; delete the file beforehand for a fresh run).
-- file.appendCsv expects a STRING row built via file.csvRow(table).
file.appendCsv(CSV_PATH, file.csvRow(
    {"idx", "dir", "before_mm", "expected_mm", "after_mm", "error_um"}))

local function recordMove(idx, dir, before, expected, after, bucket)
    local err_um = (after - expected) * 1000.0
    table.insert(bucket, err_um)
    file.appendCsv(CSV_PATH, file.csvRow({
        idx, dir,
        string.format("%.6f", before),
        string.format("%.6f", expected),
        string.format("%.6f", after),
        string.format("%+.4f", err_um),
    }))
    log.info("  [%03d %s] before=%.6f expected=%.6f after=%.6f err=%+.4f um",
        idx, dir, before, expected, after, err_um)
end

-- Main loop ----------------------------------------------------------------
for i = 1, CYCLES do
    -- Forward +STEP
    local p0 = motion.getPosition(DEVICE)
    local before   = p0.x
    local expected = before + STEP_MM
    motion.moveRel(DEVICE, AXIS, STEP_MM)
    motion.waitDone(DEVICE, 5000)
    local p1 = motion.getPosition(DEVICE)
    recordMove(i, "fwd", before, expected, p1.x, errFwd)

    -- Backward -STEP
    local before2   = p1.x
    local expected2 = before2 - STEP_MM
    motion.moveRel(DEVICE, AXIS, -STEP_MM)
    motion.waitDone(DEVICE, 5000)
    local p2 = motion.getPosition(DEVICE)
    recordMove(i, "bwd", before2, expected2, p2.x, errBwd)
end

-- Statistics ---------------------------------------------------------------
local function stats(arr)
    local n = #arr
    if n == 0 then return 0, 0, 0, 0, 0 end
    local sum = 0
    for _, v in ipairs(arr) do sum = sum + v end
    local mean = sum / n
    local sq = 0
    for _, v in ipairs(arr) do
        local d = v - mean
        sq = sq + d * d
    end
    -- Sample standard deviation (n-1 denominator)
    local std = (n > 1) and math.sqrt(sq / (n - 1)) or 0.0
    local minV, maxV = math.huge, -math.huge
    for _, v in ipairs(arr) do
        if v < minV then minV = v end
        if v > maxV then maxV = v end
    end
    return mean, std, minV, maxV, n
end

local function report(label, arr)
    local mean, std, minV, maxV, n = stats(arr)
    local sigma3 = 3.0 * std
    local outliers = 0
    for _, v in ipairs(arr) do
        if math.abs(v - mean) > sigma3 then outliers = outliers + 1 end
    end
    log.info("--- %s  (n=%d) ---", label, n)
    log.info("  mean error  : %+.4f um",  mean)
    log.info("  std (1 sig) : %.4f um",   std)
    log.info("  3 sigma band: +/-%.4f um", sigma3)
    log.info("  range       : %+.4f .. %+.4f um  (span=%.4f)",
        minV, maxV, maxV - minV)
    log.info("  outliers >3s: %d / %d (%.1f%%)",
        outliers, n, (n > 0) and (100.0 * outliers / n) or 0.0)
    return mean, std, sigma3
end

log.info("======== Statistical Summary (positional error vs target) ========")
local fwdMean, fwdStd, fwd3 = report("Forward (+100 um)",  errFwd)
local bwdMean, bwdStd, bwd3 = report("Backward (-100 um)", errBwd)

-- Combined
local all = {}
for _, v in ipairs(errFwd) do table.insert(all, v) end
for _, v in ipairs(errBwd) do table.insert(all, v) end
local allMean, allStd, all3 = report("Combined", all)

-- One-line summary for quick at-a-glance
log.info("==== SUMMARY: combined mean=%+.3f um  sigma=%.3f um  3sigma=+/-%.3f um ====",
    allMean, allStd, all3)
log.info("CSV: %s", CSV_PATH)
log.info("=== Repeatability test complete ===")
