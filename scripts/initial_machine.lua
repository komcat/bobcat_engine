-- initial_machine.lua
-- Equivalent of "Initial Machine" recipe for SIPHOG888
-- Sets velocities, retracts slides, homes all devices, turns off vacuum

log.info("=== Initial Machine ===")

-- Step 1-3: Set velocities
log.info("Setting velocities...")
motion.setVelocity("gantry-main", 50)
motion.setVelocity("hex-left", 8)
motion.setVelocity("hex-right", 8)

-- Step 4: Retract UV head slide
log.info("Retracting UV Head slide...")
pneumatic.retract("UV_Head")
system.wait(300)

-- Step 5-6: Dispenser and PP tool retracts (disabled in recipe, skipped here)
-- pneumatic.retract("Dispenser_Head")
-- pneumatic.retract("Pick_Up_Tool")

-- Step 7: Retract all slides
log.info("Retracting all slides...")
_dispatch("pneumatic", "RetractAll", {
    debounce_delay_ms = "300",
    inbounce_delay_ms = "300"
})
system.wait(500)

-- Step 8-10: Individual moves (disabled in recipe, skipped)
-- motion.moveToNamed("hex-left", "home")
-- motion.moveToNamed("hex-right", "home")
-- motion.moveToNamed("gantry-main", "safe")

-- Step 11: Parallel move - all devices to home/safe
-- ParallelMove expects a JSON array string in the "moves" parameter
log.info("Moving all devices to home positions...")
local moves_json = '[' ..
    '{"device":"hex-left","type":"position","target":"home"},' ..
    '{"device":"hex-right","type":"position","target":"home"},' ..
    '{"device":"gantry-main","type":"position","target":"safe"}' ..
']'
_dispatch("parallel", "ParallelMove", { moves = moves_json })

-- Step 12: Gripper clear and see SN (disabled in recipe, skipped)

-- Step 13: Turn off vacuum
log.info("Turning off vacuum (IOBottom pin 10)...")
io.setOutput("IOBottom", "10", false)

log.info("=== Initial Machine complete ===")
