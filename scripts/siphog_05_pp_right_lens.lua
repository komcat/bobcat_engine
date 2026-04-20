-- siphog_05_pp_right_lens.lua
-- Converted from: SIPHOG888 recipe "PP Right Lens SIPHOG"

print("--- Recipe: PP Right Lens SIPHOG ---")

-- Move gantry to safe
motion.moveToNamed("gantry-main", "safe")

-- Clear right gripper
io.clearOutputName("IOBottom", "R_Gripper")

-- (disabled) Individual approach moves
-- motion.moveToNamed("hex-right", "approachlensgrip")
-- motion.moveToNamed("hex-right", "lensgrip")
-- motion.moveToNamed("gantry-main", "See Grip Focus Lens")

-- Parallel Move: hex-right to lensgrip (graph), gantry to see grip focus lens (graph)
motion.parallelMove('[' ..
    '{"device":"hex-right","type":"graphNode","target":"lensgrip","graph":"ProcessFlow"},' ..
    '{"device":"gantry-main","type":"graphNode","target":"See Grip Focus Lens","graph":"ProcessFlow"}' ..
']')

-- Confirm lens in place
bobcat.prompt("Confirmation Required", "Is lens in place, good to grip?")

-- Gripper sequence: ON > wait > OFF > wait > ON (double grip for reliability)
io.setOutputName("IOBottom", "R_Gripper")
system.wait(1500)
io.clearOutputName("IOBottom", "R_Gripper")
system.wait(1500)
io.setOutputName("IOBottom", "R_Gripper")

-- Confirm lens good after gripping
bobcat.prompt("Confirmation Required", "Lens are good after gripping?")

-- (disabled) See lens place and approach positions
-- motion.moveToNamed("gantry-main", "See Focus Lens")
-- motion.moveToNamed("hex-right", "approachlensgrip")
-- motion.moveToNamed("hex-right", "approachlensplace")
-- motion.moveToNamed("hex-right", "lensplace")

-- Parallel Move: hex-right to lensplace (graph), gantry to see focus lens (graph)
motion.parallelMove('[' ..
    '{"device":"hex-right","type":"graphNode","target":"lensplace","graph":"ProcessFlow"},' ..
    '{"device":"gantry-main","type":"graphNode","target":"See Focus Lens","graph":"ProcessFlow"}' ..
']')

-- (disabled) Fast Align XY
-- _dispatch("hex-right", "FastAlignXY", {
--     analog_channel = "5", axis1 = "X", axis2 = "Y",
--     length1_um = "80", length2_um = "80",
--     line_distance_um = "10", threshold = "0.01"
-- })

-- (disabled) Manual Adjust
-- bobcat.prompt("Manual Adjust", "Adjust right lens power > 80uA (step 10um)")

-- (disabled) XCAN Extended for right lens alignment
-- _dispatch("hex-left", "XCANExtended", {
--     axes = "XYZ",
--     step_sizes_um = "5,2,1,0.5",
--     channel = "SMU_Keithley_6482_Ch1",
--     settling_time_ms = "200",
--     exit_threshold = "0.0034",
--     capture_as = "PowerBothLens2"
-- })

print("PP Right Lens SIPHOG complete")
