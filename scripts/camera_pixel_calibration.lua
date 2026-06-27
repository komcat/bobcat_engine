-- camera_pixel_calibration.lua
-- ===========================================================================
-- Camera-to-stage scale (pixel<->mm) + orientation calibration.
--
-- Method (displacement / "known-offset dot" calibration):
--   * Start with the white dot centered under the crosshair, Z already at focus.
--   * Step the gantry over a grid (STEP_UM per step, N_STEPS_X/N_STEPS_Y each
--     side of centre) and at each cell run the CaptureZero profile to detect the
--     dot's pixel position.
--   * Least-squares fit  pixel = G * stage  (G in px/mm) and decompose G into:
--        - mm/px (and um/px) scale for X and Y
--        - camera rotation vs stage axes (deg)
--        - axis skew / orthogonality error (deg)
--        - residual RMS / max (px)  -> how trustworthy the fit is.
--
-- Outputs (under the active product folder):
--   config/{PRODUCT}/calibration report/{timestamp}/report.html   (visual report)
--   config/{PRODUCT}/calibration report/{timestamp}/calibration.json (result file)
--   data/calibration_report/{PRODUCT}/{timestamp}_grid.csv          (raw points)
-- Optionally updates config/SYSTEM/val_pixelmm.json so the vision system
-- immediately uses the new mm/px (set UPDATE_SYSTEM_CALIB = true).
--
-- Requirements: Halcon vision (CircleDetect Robust) for vision_profile_CaptureZero,
-- gantry-main motion, main_camera. Run from F9 Script Runner.
-- ===========================================================================

-- ---- Configuration --------------------------------------------------------
local PRODUCT      = dut.product() or "SIPHOG888"        -- active product
local DEVICE       = "gantry-main"
local CAMERA       = "main_camera"
local PROFILE      = string.format("config/%s/vision/vision_profile_CaptureZero.json", PRODUCT)

local STEP_UM      = 250          -- step size per cell (micrometres)
local N_STEPS_X    = 6            -- steps each side of centre, X axis
local N_STEPS_Y    = 4            -- steps each side of centre, Y axis
local VELOCITY     = 5.0          -- mm/s during calibration moves
local SETTLE_MS    = 250          -- settle time after each move before capture
local CAPTURES     = 1            -- captures averaged per cell (>=1)

local UPDATE_SYSTEM_CALIB = false -- if true, overwrite config/SYSTEM/val_pixelmm.json

-- ---- Derived --------------------------------------------------------------
local STEP_MM = STEP_UM / 1000.0
local TS      = bobcat.timestamp()                     -- YYYYMMDD_HHMMSS
local TS_ISO  = bobcat.timestamp("iso")
local REPORT_DIR = string.format("config/%s/calibration report/%s", PRODUCT, TS)
local HTML_PATH  = REPORT_DIR .. "/report.html"
local JSON_PATH  = REPORT_DIR .. "/calibration.json"
local CSV_PATH   = string.format("data/calibration_report/%s/%s_grid.csv", PRODUCT, TS)

log.info("=== Camera pixel calibration ===")
log.info("product=%s device=%s camera=%s", PRODUCT, DEVICE, CAMERA)
log.info("grid=%dx%d (XxY)  step=%d um (%.4f mm)", 2*N_STEPS_X+1, 2*N_STEPS_Y+1, STEP_UM, STEP_MM)
log.info("profile=%s", PROFILE)

-- ---- Capture the centre / origin pose -------------------------------------
local origin = motion.getPosition(DEVICE)
log.info("Origin (dot centre): X=%.5f Y=%.5f Z=%.5f mm", origin.x, origin.y, origin.z)
motion.setVelocity(DEVICE, VELOCITY)
system.wait(150)

-- ---- One detection at the current pose ------------------------------------
-- Returns ok, px, py, imgW, imgH. Averages CAPTURES detections.
local function detectDot()
    local sumX, sumY, n = 0, 0, 0
    local imgW, imgH = 0, 0
    for c = 1, CAPTURES do
        local ok, r = pcall(vision.runProfile, PROFILE, CAMERA)
        if ok and r and r.success and r.center_x ~= nil and r.center_y ~= nil then
            sumX = sumX + r.center_x
            sumY = sumY + r.center_y
            n = n + 1
            imgW = r.image_width  or imgW
            imgH = r.image_height or imgH
        end
        if CAPTURES > 1 then system.wait(40) end
    end
    if n == 0 then return false, 0, 0, imgW, imgH end
    return true, sumX / n, sumY / n, imgW, imgH
end

-- ---- Snake traversal of the grid ------------------------------------------
-- Build (ix, iy) order so adjacent cells differ by a single axis step.
local order = {}
for iy = -N_STEPS_Y, N_STEPS_Y do
    if (iy + N_STEPS_Y) % 2 == 0 then
        for ix = -N_STEPS_X, N_STEPS_X do order[#order+1] = {ix, iy} end
    else
        for ix = N_STEPS_X, -N_STEPS_X, -1 do order[#order+1] = {ix, iy} end
    end
end

local samples = {}      -- successful cells: {ix,iy,sx,sy,px,py}
local failed  = 0
local imgW, imgH = 0, 0

-- raw CSV (always allowed under data/)
file.appendCsv(CSV_PATH, file.csvRow(
    {"ix","iy","cmd_dx_mm","cmd_dy_mm","stage_x_mm","stage_y_mm","px","py","status"}))

for idx, cell in ipairs(order) do
    local ix, iy = cell[1], cell[2]
    local tx = origin.x + ix * STEP_MM
    local ty = origin.y + iy * STEP_MM

    motion.moveAbs(DEVICE, {x = tx, y = ty, z = origin.z, velocity = VELOCITY})
    motion.waitDone(DEVICE, 8000)
    system.wait(SETTLE_MS)

    local pos = motion.getPosition(DEVICE)            -- actual encoder readback
    local ok, px, py, w, h = detectDot()
    if w > 0 then imgW = w end
    if h > 0 then imgH = h end

    if ok then
        samples[#samples+1] = {ix=ix, iy=iy, sx=pos.x, sy=pos.y, px=px, py=py}
    else
        failed = failed + 1
    end

    file.appendCsv(CSV_PATH, file.csvRow({
        ix, iy,
        string.format("%+.4f", ix*STEP_MM), string.format("%+.4f", iy*STEP_MM),
        string.format("%.5f", pos.x), string.format("%.5f", pos.y),
        ok and string.format("%.2f", px) or "",
        ok and string.format("%.2f", py) or "",
        ok and "ok" or "no_detect",
    }))

    log.info("[%2d/%2d] cell(%+d,%+d) stage=(%.4f,%.4f) %s",
        idx, #order, ix, iy, pos.x, pos.y,
        ok and string.format("px=(%.1f,%.1f)", px, py) or "NO DETECTION")

    bobcat.watch("calib.progress", string.format("%d/%d  fails=%d", idx, #order, failed))
end

-- ---- Return to origin -----------------------------------------------------
motion.moveAbs(DEVICE, {x = origin.x, y = origin.y, z = origin.z, velocity = VELOCITY})
motion.waitDone(DEVICE, 8000)
log.info("Returned to origin. Detected %d / %d cells (%d failed).",
    #samples, #order, failed)

if #samples < 6 then
    log.error("Not enough detections (%d) to fit a calibration. Aborting report.", #samples)
    return
end

-- ---- Least-squares fit:  pixel = G * stage  (centred) ---------------------
local n = #samples
local mSx, mSy, mPx, mPy = 0, 0, 0, 0
for _, s in ipairs(samples) do
    mSx = mSx + s.sx; mSy = mSy + s.sy; mPx = mPx + s.px; mPy = mPy + s.py
end
mSx, mSy, mPx, mPy = mSx/n, mSy/n, mPx/n, mPy/n

local Sxx, Sxy, Syy = 0, 0, 0
local bx1, by1, bx2, by2 = 0, 0, 0, 0   -- RHS for Px (1) and Py (2)
for _, s in ipairs(samples) do
    local dsx, dsy = s.sx - mSx, s.sy - mSy
    local dpx, dpy = s.px - mPx, s.py - mPy
    Sxx = Sxx + dsx*dsx
    Sxy = Sxy + dsx*dsy
    Syy = Syy + dsy*dsy
    bx1 = bx1 + dsx*dpx;  by1 = by1 + dsy*dpx
    bx2 = bx2 + dsx*dpy;  by2 = by2 + dsy*dpy
end

local det = Sxx*Syy - Sxy*Sxy
if math.abs(det) < 1e-12 then
    log.error("Degenerate stage geometry (det~0); cannot fit. Aborting.")
    return
end
-- G row 1: px = g11*sx + g12*sy ; row 2: py = g21*sx + g22*sy
local g11 = ( bx1*Syy - by1*Sxy) / det
local g12 = ( Sxx*by1 - Sxy*bx1) / det
local g21 = ( bx2*Syy - by2*Sxy) / det
local g22 = ( Sxx*by2 - Sxy*bx2) / det

-- ---- Decompose ------------------------------------------------------------
local function deg(r) return r * 180.0 / math.pi end
local mag_x = math.sqrt(g11*g11 + g21*g21)     -- px per mm of stage-X
local mag_y = math.sqrt(g12*g12 + g22*g22)     -- px per mm of stage-Y
local mmpx_x = 1.0 / mag_x                      -- mm per px (X)
local mmpx_y = 1.0 / mag_y                      -- mm per px (Y)
local rot_x  = math.atan(g21, g11)              -- stage-X dir in image (Lua 5.4)
local rot_y  = math.atan(g22, g12)              -- stage-Y dir in image
local rotation_deg = deg(rot_x)
local between = deg(rot_y - rot_x)
while between > 180 do between = between - 360 end
while between < -180 do between = between + 360 end
local skew_deg = between - 90.0                 -- 0 = perfectly orthogonal
local aspect = mag_x / mag_y

-- residuals
local sumSq, maxErr = 0, 0
for _, s in ipairs(samples) do
    local dsx, dsy = s.sx - mSx, s.sy - mSy
    local phx = mPx + g11*dsx + g12*dsy
    local phy = mPy + g21*dsx + g22*dsy
    s.resx = s.px - phx
    s.resy = s.py - phy
    local e2 = s.resx*s.resx + s.resy*s.resy
    sumSq = sumSq + e2
    local e = math.sqrt(e2)
    if e > maxErr then maxErr = e end
end
local rms_px = math.sqrt(sumSq / n)
local rms_um = rms_px * ((mmpx_x + mmpx_y) / 2.0) * 1000.0

log.info("---- CALIBRATION RESULT ----")
log.info("  mm/px  X = %.6f  (%.4f um/px)", mmpx_x, mmpx_x*1000.0)
log.info("  mm/px  Y = %.6f  (%.4f um/px)", mmpx_y, mmpx_y*1000.0)
log.info("  rotation = %+.4f deg   skew = %+.4f deg   aspect = %.5f",
    rotation_deg, skew_deg, aspect)
log.info("  residual RMS = %.3f px (~%.3f um), max = %.3f px  (n=%d/%d)",
    rms_px, rms_um, maxErr, n, #order)
bobcat.watch("calib.mmpx_x", mmpx_x)
bobcat.watch("calib.mmpx_y", mmpx_y)
bobcat.watch("calib.rotation_deg", rotation_deg)
bobcat.watch("calib.rms_px", rms_px)

-- ---- Build SVG scatter of detected pixels ---------------------------------
local function buildSvg()
    local W, H, pad = 460, 460, 30
    local minx, maxx, miny, maxy = math.huge, -math.huge, math.huge, -math.huge
    for _, s in ipairs(samples) do
        if s.px < minx then minx = s.px end
        if s.px > maxx then maxx = s.px end
        if s.py < miny then miny = s.py end
        if s.py > maxy then maxy = s.py end
    end
    local spanx = math.max(maxx - minx, 1)
    local spany = math.max(maxy - miny, 1)
    local span  = math.max(spanx, spany)
    local function mapx(px) return pad + (px - minx) / span * (W - 2*pad) end
    local function mapy(py) return pad + (py - miny) / span * (H - 2*pad) end
    local parts = {}
    parts[#parts+1] = string.format(
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '..
        'style="background:#0d1117;border:1px solid #30363d">', W, H)
    -- image centre marker
    if imgW > 0 and imgH > 0 then
        local cx, cy = mapx(imgW/2), mapy(imgH/2)
        parts[#parts+1] = string.format(
            '<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#39d353" stroke-width="1"/>',
            cx-8, cy, cx+8, cy)
        parts[#parts+1] = string.format(
            '<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#39d353" stroke-width="1"/>',
            cx, cy-8, cx, cy+8)
    end
    for _, s in ipairs(samples) do
        local x, y = mapx(s.px), mapy(s.py)
        parts[#parts+1] = string.format(
            '<circle cx="%.1f" cy="%.1f" r="3" fill="#58a6ff"/>', x, y)
    end
    parts[#parts+1] = string.format(
        '<text x="%d" y="%d" fill="#8b949e" font-size="11" font-family="monospace">'..
        'detected dot positions (px), green=image centre</text>', pad, H-8)
    parts[#parts+1] = '</svg>'
    return table.concat(parts)
end

-- ---- Build HTML report ----------------------------------------------------
local function esc(s) return (tostring(s):gsub("&","&amp;"):gsub("<","&lt;"):gsub(">","&gt;")) end
local rows = {}
for _, s in ipairs(samples) do
    rows[#rows+1] = string.format(
        "<tr><td>%+d,%+d</td><td>%.4f</td><td>%.4f</td><td>%.1f</td><td>%.1f</td>"..
        "<td>%+.2f</td><td>%+.2f</td></tr>",
        s.ix, s.iy, s.sx, s.sy, s.px, s.py, s.resx, s.resy)
end

local html = string.format([[<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Pixel Calibration %s</title>
<style>
 body{font-family:Segoe UI,Roboto,sans-serif;background:#0d1117;color:#c9d1d9;margin:24px}
 h1{font-size:20px}h2{font-size:15px;color:#8b949e;margin-top:24px}
 .grid{display:flex;gap:32px;flex-wrap:wrap}
 table{border-collapse:collapse;font-family:monospace;font-size:12px}
 td,th{border:1px solid #30363d;padding:3px 8px;text-align:right}
 th{background:#161b22;color:#8b949e}
 .kpi{display:inline-block;background:#161b22;border:1px solid #30363d;border-radius:8px;
      padding:12px 18px;margin:6px 10px 6px 0;min-width:140px}
 .kpi .v{font-size:22px;font-weight:600;color:#58a6ff}
 .kpi .l{font-size:11px;color:#8b949e;text-transform:uppercase;letter-spacing:.5px}
 .meta td{text-align:left;border:none;padding:2px 12px 2px 0}
 .warn{color:#f0883e}
</style></head><body>
<h1>Camera Pixel Calibration &mdash; %s</h1>
<table class="meta">
 <tr><td>Timestamp</td><td>%s</td></tr>
 <tr><td>Device / Camera</td><td>%s / %s</td></tr>
 <tr><td>Grid</td><td>%dx%d @ %d um/step</td></tr>
 <tr><td>Cells detected</td><td>%d / %d  (%d failed)</td></tr>
</table>

<h2>Result</h2>
<div>
 <div class="kpi"><div class="v">%.4f</div><div class="l">um/px X</div></div>
 <div class="kpi"><div class="v">%.4f</div><div class="l">um/px Y</div></div>
 <div class="kpi"><div class="v">%+.3f&deg;</div><div class="l">rotation</div></div>
 <div class="kpi"><div class="v">%+.3f&deg;</div><div class="l">skew</div></div>
 <div class="kpi"><div class="v">%.4f</div><div class="l">aspect X/Y</div></div>
 <div class="kpi"><div class="v">%.2f px</div><div class="l">residual RMS</div></div>
 <div class="kpi"><div class="v">%.2f px</div><div class="l">residual max</div></div>
</div>
<p>mm/px X = <b>%.6f</b>&nbsp;&nbsp; mm/px Y = <b>%.6f</b>&nbsp;&nbsp; (RMS &asymp; %.2f um)</p>
%s

<div class="grid">
 <div>
  <h2>Detected positions</h2>
  %s
 </div>
 <div>
  <h2>Per-cell data</h2>
  <table><tr><th>cell</th><th>stageX</th><th>stageY</th><th>px</th><th>py</th>
   <th>resX</th><th>resY</th></tr>
   %s
  </table>
 </div>
</div>
</body></html>]],
    esc(PRODUCT), esc(PRODUCT), TS_ISO, esc(DEVICE), esc(CAMERA),
    2*N_STEPS_X+1, 2*N_STEPS_Y+1, STEP_UM,
    n, #order, failed,
    mmpx_x*1000.0, mmpx_y*1000.0, rotation_deg, skew_deg, aspect, rms_px, maxErr,
    mmpx_x, mmpx_y, rms_um,
    (rms_px > 2.0)
        and '<p class="warn">&#9888; Residual RMS &gt; 2 px &mdash; check focus, backlash, '..
            'detection stability, or whether the dot left the FOV at grid extremes.</p>'
        or  '',
    buildSvg(),
    table.concat(rows, "\n   "))

file.writeText(HTML_PATH, html)
log.info("HTML report: %s", HTML_PATH)

-- ---- Build calibration.json result file -----------------------------------
local json = string.format([==[{
  "type": "camera_pixel_calibration",
  "product": "%s",
  "timestamp": "%s",
  "device": "%s",
  "camera": "%s",
  "profile": "%s",
  "grid": { "steps_each_side_x": %d, "steps_each_side_y": %d, "step_um": %d, "cells_detected": %d, "cells_total": %d },
  "calibration": {
    "pixel_to_mm_x": %.8f,
    "pixel_to_mm_y": %.8f,
    "um_per_px_x": %.6f,
    "um_per_px_y": %.6f,
    "rotation_deg": %.6f,
    "skew_deg": %.6f,
    "aspect_x_over_y": %.6f
  },
  "fit": {
    "G_px_per_mm": [[%.8f, %.8f], [%.8f, %.8f]],
    "residual_rms_px": %.6f,
    "residual_max_px": %.6f,
    "origin_mm": { "x": %.6f, "y": %.6f, "z": %.6f }
  }
}
]==],
    PRODUCT, TS_ISO, DEVICE, CAMERA, PROFILE,
    N_STEPS_X, N_STEPS_Y, STEP_UM, n, #order,
    mmpx_x, mmpx_y, mmpx_x*1000.0, mmpx_y*1000.0, rotation_deg, skew_deg, aspect,
    g11, g12, g21, g22, rms_px, maxErr, origin.x, origin.y, origin.z)

file.writeText(JSON_PATH, json)
log.info("Calibration file: %s", JSON_PATH)

-- ---- Optionally push into the live vision calibration ---------------------
if UPDATE_SYSTEM_CALIB then
    local sys = string.format([[{
  "calibration": {
    "pixel_to_mm_x": %.8f,
    "pixel_to_mm_y": %.8f,
    "source": "camera_pixel_calibration",
    "timestamp": "%s"
  }
}
]], mmpx_x, mmpx_y, TS_ISO)
    file.writeText("config/SYSTEM/val_pixelmm.json", sys)
    log.warn("Updated config/SYSTEM/val_pixelmm.json with new mm/px values.")
end

log.info("=== Calibration complete: open %s ===", HTML_PATH)
