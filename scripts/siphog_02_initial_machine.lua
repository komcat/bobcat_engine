-- siphog_02_initial_machine.lua
-- Converted from: SIPHOG888 recipe "Initial Machine"

print("--- Recipe: Initial Machine ---")

-- Set velocities
motion.setVelocity("gantry-main", 50)
motion.setVelocity("hex-left", 8)
motion.setVelocity("hex-right", 8)

-- Slide Retract UV_Head (inbounce 100ms, timeout 10s, debounce 100ms)
system.wait(100)
_dispatch("UV_Head", "Retract", {timeout_ms = "10000"})
system.wait(100)

-- (disabled) Slide Retract Dispenser_Head
-- system.wait(100)
-- _dispatch("Dispenser_Head", "Retract", {timeout_ms = "10000"})
-- system.wait(100)

-- (disabled) Slide Retract Pick_Up_Tool
-- system.wait(100)
-- _dispatch("Pick_Up_Tool", "Retract", {timeout_ms = "10000"})
-- system.wait(100)

-- Slide Retract All (inbounce 300ms, debounce 300ms)
system.wait(300)
pneumatic.retractAll()
system.wait(300)

-- (disabled) Individual moves
-- motion.moveToNamed("hex-left", "home")
-- motion.moveToNamed("hex-right", "home")
-- motion.moveToNamed("gantry-main", "safe")

-- Parallel Move: all devices to home/safe
motion.parallelMove('[' ..
    '{"device":"hex-left","type":"position","target":"home"},' ..
    '{"device":"hex-right","type":"position","target":"home"},' ..
    '{"device":"gantry-main","type":"position","target":"safe"}' ..
']')

-- (disabled) Parallel Move with graph nodes
-- motion.parallelMove('[' ..
--     '{"device":"hex-left","type":"graphNode","target":"clear_vision","graph":"PrcessFlow"},' ..
--     '{"device":"hex-right","type":"graphNode","target":"clear_vision","graph":"ProcessFlow"},' ..
--     '{"device":"gantry-main","type":"graphNode","target":"seepic","graph":"ProcessFlow"}' ..
-- ']')

-- Vacuum off (pin 10 = false)
io.setOutput("IOBottom", "10", false)

print("Initial Machine complete")
