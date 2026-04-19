-- example_basic.lua
-- Basic demonstration of bobcat scripting capabilities
-- This runs in simulation mode without hardware

log.info("=== Bobcat Script Engine Test ===")

-- ============================================================
-- 1. Variables (native Lua)
-- ============================================================
local count = 0
local name = "TestDevice"
local active = true

-- Arrays
local measurements = {1.2, 3.4, 5.6, 7.8, 9.0}
local sum = 0
for _, v in ipairs(measurements) do
    sum = sum + v
end
local avg = sum / #measurements
log.info("Average of %d measurements: %.3f", #measurements, avg)

-- Dictionaries
local offsets = {x = 0.05, y = -0.02, z = 0.0}
log.info("Offsets: x=%.3f y=%.3f z=%.3f", offsets.x, offsets.y, offsets.z)

-- Objects
local DUT = {
    serial = "",
    results = {},

    init = function(self, sn)
        self.serial = sn
        self.results = {}
        log.info("DUT initialized: %s", sn)
    end,

    addResult = function(self, name, value)
        self.results[name] = value
        ctx.set("dut." .. name, value)
    end,

    report = function(self)
        log.info("DUT %s Results:", self.serial)
        for k, v in pairs(self.results) do
            log.info("  %s = %s", k, tostring(v))
        end
    end
}

DUT:init("SN-2026-0001")
DUT:addResult("voltage", 3.31)
DUT:addResult("current", 0.125)
DUT:report()

-- ============================================================
-- 2. Context (shared variable storage)
-- ============================================================
ctx.set("test.count", 42)
ctx.setString("test.name", "bobcat")
ctx.set("test.active", true)

log.info("Context values:")
log.info("  count = %d", ctx.getNumeric("test.count"))
log.info("  name = %s", ctx.getString("test.name"))
log.info("  active = %s", tostring(ctx.getBoolean("test.active")))
log.info("  has 'test.count' = %s", tostring(ctx.has("test.count")))

-- ============================================================
-- 3. Math operations (native Lua)
-- ============================================================
local dx = 3.0
local dy = 4.0
local distance = math.sqrt(dx * dx + dy * dy)
local angle = math.atan(dy, dx) * 180 / math.pi
log.info("Distance: %.3f, Angle: %.1f degrees", distance, angle)

-- ============================================================
-- 4. Loops with calculation
-- ============================================================
local positions = {}
local radius = 10.0
local numPoints = 8

for i = 1, numPoints do
    local theta = (i - 1) * 2 * math.pi / numPoints
    positions[i] = {
        x = radius * math.cos(theta),
        y = radius * math.sin(theta)
    }
    log.info("Point %d: x=%.3f y=%.3f", i, positions[i].x, positions[i].y)
end

-- ============================================================
-- 5. Conditional logic
-- ============================================================
local threshold = 5.0
for i, pos in ipairs(positions) do
    local dist = math.sqrt(pos.x * pos.x + pos.y * pos.y)
    if dist > threshold then
        log.debug("Point %d is %.1f from origin (above threshold)", i, dist)
    end
end

-- ============================================================
-- 6. Wait demonstration
-- ============================================================
log.info("Waiting 100ms...")
system.wait(100)
log.info("Done!")

-- ============================================================
-- 7. Print all context keys
-- ============================================================
local keys = ctx.keys()
log.info("Context has %d keys:", #keys)
for _, key in ipairs(keys) do
    log.debug("  %s", key)
end

log.info("=== Script completed successfully ===")
