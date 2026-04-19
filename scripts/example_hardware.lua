-- example_hardware.lua
-- Demonstrates hardware command patterns (requires hardware connection)

log.info("=== Hardware Script Example ===")

-- ============================================================
-- Motion: read position, move, compute correction
-- ============================================================

-- Read current position
local pos = motion.getPosition("gantry-main")
log.info("Current position: x=%.3f y=%.3f z=%.3f", pos.x, pos.y, pos.z)

-- Move to named position
motion.moveToNamed("gantry-main", "home")

-- Move to absolute coordinates
motion.moveAbs("gantry-main", {x = "10.0", y = "20.0"})

-- Read again and compute distance moved
local newPos = motion.getPosition("gantry-main")
local dx = newPos.x - pos.x
local dy = newPos.y - pos.y
local distance = math.sqrt(dx * dx + dy * dy)
log.info("Moved %.3f mm", distance)

-- ============================================================
-- Vision: capture and analyze
-- ============================================================

-- Capture an image
camera.capture("top")

-- Run a vision profile and get results
local result = vision.runProfile("circle_detect", "top")
if result.found then
    log.info("Circle found at (%.3f, %.3f) radius=%.3f",
        result.center_x, result.center_y, result.radius)

    -- Compute correction offset
    local target_x = 15.0
    local target_y = 25.0
    local offset_x = result.center_x - target_x
    local offset_y = result.center_y - target_y
    log.info("Correction: dx=%.3f dy=%.3f", offset_x, offset_y)

    -- Apply correction
    motion.moveRel("gantry-main", {x = tostring(-offset_x), y = tostring(-offset_y)})
else
    log.warn("Circle not detected, skipping correction")
end

-- ============================================================
-- Multi-point measurement loop
-- ============================================================

local measurement_positions = {
    {name = "point1", x = 10.0, y = 10.0},
    {name = "point2", x = 20.0, y = 10.0},
    {name = "point3", x = 20.0, y = 20.0},
    {name = "point4", x = 10.0, y = 20.0},
}

local results = {}
for i, mp in ipairs(measurement_positions) do
    log.info("Measuring %s (%d/%d)...", mp.name, i, #measurement_positions)

    -- Move to measurement position
    motion.moveAbs("gantry-main", {x = tostring(mp.x), y = tostring(mp.y)})

    -- Take measurement
    local reading = smu.measure("smu1", "current")
    results[mp.name] = reading
    log.info("  %s = %.6f A", mp.name, reading)

    system.wait(200)
end

-- Compute statistics
local total = 0
local min_val = math.huge
local max_val = -math.huge
for name, val in pairs(results) do
    total = total + val
    if val < min_val then min_val = val end
    if val > max_val then max_val = val end
end
local mean = total / #measurement_positions
log.info("Statistics: mean=%.6f min=%.6f max=%.6f range=%.6f",
    mean, min_val, max_val, max_val - min_val)

-- Store results in context for later use
ctx.set("measurement.mean", mean)
ctx.set("measurement.range", max_val - min_val)

-- ============================================================
-- I/O control
-- ============================================================
io.setOutput("IOBottom", "vacuum", true)
system.wait(500)
local sensor = io.getInput("IOTop", "sensor1")
log.info("Sensor state: %s", tostring(sensor))
io.setOutput("IOBottom", "vacuum", false)

log.info("=== Hardware script completed ===")
