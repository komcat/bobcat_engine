-- siphog_04_pp_left_lens.lua
-- Converted from: SIPHOG888 recipe "PP Left Lens SIPHOG"

print("--- Recipe: PP Left Lens SIPHOG ---")

-- Clear left gripper
io.clearOutputName("IOBottom", "L_Gripper")

-- Set camera exposure for lens pickup
camera.setExposure("main_camera", 2000)

-- Move gantry to safe
motion.moveToNamed("gantry-main", "safe")

-- (disabled) Individual approach moves
-- motion.moveToNamed("hex-left", "approachlensgrip")
-- motion.moveToNamed("hex-left", "lensgrip")

-- Parallel Move: hex-left to lensgrip (graph), gantry to see grip position (graph)
motion.parallelMove('[' ..
    '{"device":"hex-left","type":"graphNode","target":"lensgrip","graph":"PrcessFlow"},' ..
    '{"device":"gantry-main","type":"graphNode","target":"See Grip collimate lens","graph":"ProcessFlow"}' ..
']')

-- (disabled) See Grip Lens
-- motion.moveToNamed("gantry-main", "See Grip collimate lens")

-- Confirm lens in place
bobcat.prompt("Confirmation Required", "Is lens in place, good to grip?")

-- Gripper sequence: ON > wait > OFF > wait > ON (double grip for reliability)
io.setOutputName("IOBottom", "L_Gripper")
system.wait(1500)
io.clearOutputName("IOBottom", "L_Gripper")
system.wait(1500)
io.setOutputName("IOBottom", "L_Gripper")

-- Confirm lens good after gripping
bobcat.prompt("Confirmation Required", "Lens are good after gripping?")

-- Parallel Move: hex-left to lensplace (graph), gantry to see collimate lens (graph)
motion.parallelMove('[' ..
    '{"device":"hex-left","type":"graphNode","target":"lensplace","graph":"PrcessFlow"},' ..
    '{"device":"gantry-main","type":"graphNode","target":"See Collimate Lens","graph":"ProcessFlow"}' ..
']')

-- (disabled) Individual position moves
-- motion.moveToNamed("gantry-main", "See Collimate Lens")
-- motion.moveToNamed("hex-left", "approachlensgrip")
-- motion.moveToNamed("hex-left", "approachlensplace")
-- motion.moveToNamed("hex-left", "lensplace")

-- (disabled) Manual Adjust
-- bobcat.prompt("Manual Adjust", "Adjust hex-left until power >5uA (step 10um)")

-- (disabled) XCAN Extended for left lens alignment
-- _dispatch("hex-left", "XCANExtended", {
--     axes = "XYZ",
--     step_sizes_um = "5,2,1,0.5",
--     channel = "SMU_Keithley_6482_Ch1",
--     settling_time_ms = "150",
--     exit_threshold = "0.00045",
--     capture_as = "PowerCollLens1"
-- })

print("PP Left Lens SIPHOG complete")
