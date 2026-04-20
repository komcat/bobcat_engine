-- siphog_06_uv_curing.lua
-- Converted from: SIPHOG888 recipe "UV"

print("--- Recipe: UV Curing ---")

-- Laser ON at 150mA
tec.setLaserCurrent("CLD101x_Direct", 150)
tec.laserOn("CLD101x_Direct")

-- Set velocities
motion.setVelocity("hex-left", 5)
motion.setVelocity("hex-right", 5)

-- Read current to check power level
local checkPower = smu.readCurrent("Keithley_6482", 1)
ctx.set("$check_low_power", checkPower)

-- Conditional: warn if power < 900 uA
if checkPower < 0.0009 then
    bobcat.prompt("Power <900 uA", "Does not suggest to continue UV")
end

-- Wait 1 sec
system.wait(1000)

-- Retract UV Head
system.wait(300)
_dispatch("UV_Head", "Retract", {timeout_ms = "10000"})
system.wait(300)

-- Move gantry to safe then UV position
motion.moveToNamed("gantry-main", "safe")
motion.moveToNamed("gantry-main", "UV")

-- Extend UV Head
system.wait(300)
_dispatch("UV_Head", "Extend", {timeout_ms = "10000"})
system.wait(300)

-- (disabled) Ask user for fine alignment
-- bobcat.prompt("Fine Alignement both lens?", "Please confirm to continue")

-- (disabled) XCAN fine alignment
-- _dispatch("hex-left", "XCAN", {
--     channel = "SMU_Keithley_6482_Ch1", mode = "fine", settling_time_ms = "300"
-- })
-- _dispatch("hex-right", "XCAN", {
--     channel = "SMU_Keithley_6482_Ch1", mode = "fine", settling_time_ms = "300"
-- })

-- XCAN Extended: fine alignment for left lens
_dispatch("hex-left", "XCANExtended", {
    axes = "XYZ",
    step_sizes_um = "1,0.5",
    channel = "SMU_Keithley_6482_Ch1",
    settling_time_ms = "150",
    exit_threshold = "",
    capture_as = "$Xcan_Left_Peak"
})

-- XCAN Extended: fine alignment for right lens
_dispatch("hex-right", "XCANExtended", {
    axes = "XYZ",
    step_sizes_um = "1,0.5",
    channel = "SMU_Keithley_6482_Ch1",
    settling_time_ms = "150",
    exit_threshold = "",
    capture_as = "$Xcan_Right_Peak"
})

-- (disabled) Left gripper move relative
-- motion.moveRel("hex-left", "X", 0)

-- Read dry value (pre-UV power)
local dryValue = smu.readCurrent("Keithley_6482", 1)
ctx.set("$DRY_VALUE", dryValue)
print(string.format("Pre-UV dry value = %.6f A", dryValue))

-- (disabled) Ready to UV confirmation
-- bobcat.prompt("UV ready?", "Ready to UV, Yes to continue?")

-- UV PLC trigger sequence (down edge then up edge)
io.setOutput("IOBottom", "UV_PLC1", false)      -- down leg
io.clearOutputName("IOBottom", "UV_PLC1")
system.wait(100)
io.setOutputName("IOBottom", "UV_PLC1")          -- up leg
io.setOutputName("IOBottom", "UV_PLC1")          -- trigger

-- UV dose wait (90 seconds)
-- (disabled) Wait 210 seconds
-- system.wait(210000)
print("UV curing in progress... (90 seconds)")
system.wait(90000)

-- Retract UV Head after curing
system.wait(300)
_dispatch("UV_Head", "Retract", {timeout_ms = "10000"})
system.wait(300)

-- Move gantry to safe via graph
motion.moveToGraph("gantry-main", "ProcessFlow", "safe")

-- Read post-UV values
local uvValue1 = smu.readCurrent("Keithley_6482", 1)
ctx.set("$UV_VALUE1", uvValue1)
local uvValue = smu.readCurrent("Keithley_6482", 1)
ctx.set("$UV_VALUE", uvValue)
print(string.format("Post-UV value = %.6f A", uvValue1))

-- Release both grippers
io.clearOutputName("IOBottom", "L_Gripper")
io.clearOutputName("IOBottom", "R_Gripper")

-- Wait 3 seconds for grippers to fully release
system.wait(3000)

-- Move both grippers up 4mm (clear lens)
motion.parallelMoveRel('[' ..
    '{"device":"hex-left","axis":"X","distance":4.0},' ..
    '{"device":"hex-right","axis":"X","distance":4.0}' ..
']')

-- Move both grippers to home
motion.parallelMove('[' ..
    '{"device":"hex-left","type":"position","target":"home"},' ..
    '{"device":"hex-right","type":"position","target":"home"}' ..
']')

-- (disabled) Parallel Move grippers to clear_vision via graph
-- motion.parallelMove('[' ..
--     '{"device":"hex-left","type":"graphNode","target":"clear_vision","graph":"PrcessFlow"},' ..
--     '{"device":"hex-right","type":"graphNode","target":"clear_vision","graph":"ProcessFlow"}' ..
-- ']')

-- Vacuum off
io.clearOutputName("IOBottom", "Vacuum_Base")

-- (disabled) Individual home moves
-- motion.moveToNamed("hex-left", "home")
-- motion.moveToGraph("hex-left", "PrcessFlow", "clear_vision")
-- motion.moveToNamed("hex-right", "home")
-- motion.moveToGraph("hex-right", "ProcessFlow", "clear_vision")

-- DUT: save pre/post UV values
dut.insertFromContext("pre_uv_value", "$DRY_VALUE", "auto")
dut.insertFromContext("post_uv_value", "$UV_VALUE1", "auto")

-- DUT Close (finalize record)
dut.close()

-- Laser OFF
tec.setLaserCurrent("CLD101x_Direct", 150)
tec.laserOff("CLD101x_Direct")

-- TEC OFF
tec.setTemp("CLD101x_Direct", 25.0)
tec.disable("CLD101x_Direct")

-- (disabled) UV Curing complete confirmation
-- bobcat.prompt("UV Curing complete", "Please confirm to continue")

-- Move to see serial number via graph
motion.moveToGraph("gantry-main", "ProcessFlow", "SN view")

print("UV Curing complete")
