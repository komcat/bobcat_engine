-- siphog_03_probe_sled_pic.lua
-- Converted from: SIPHOG888 recipe "Probe SLED and PIC"

print("--- Recipe: Probe SLED and PIC ---")

-- Camera settings
camera.setAutoGain("main_camera", "off")
camera.setGain("main_camera", 0)
camera.setExposure("main_camera", 500)

-- (disabled) Individual slide retracts
-- system.wait(100); _dispatch("UV_Head", "Retract", {timeout_ms = "10000"}); system.wait(100)
-- system.wait(100); _dispatch("Dispenser_Head", "Retract", {timeout_ms = "10000"}); system.wait(100)
-- system.wait(100); _dispatch("Pick_Up_Tool", "Retract", {timeout_ms = "10000"}); system.wait(100)

-- Slide Retract All
system.wait(300)
pneumatic.retractAll()
system.wait(300)

-- Set velocities
motion.setVelocity("gantry-main", 35)
motion.setVelocity("hex-left", 5)
motion.setVelocity("hex-right", 5)

-- Parallel Move: grippers to clear_vision, gantry to seesled
motion.parallelMove('[' ..
    '{"device":"hex-left","type":"position","target":"clear_vision"},' ..
    '{"device":"hex-right","type":"position","target":"clear_vision"},' ..
    '{"device":"gantry-main","type":"position","target":"seesled"}' ..
']')

-- (disabled) Individual moves
-- motion.moveToNamed("gantry-main", "safe")
-- motion.moveToNamed("hex-left", "clear_vision")
-- motion.moveToNamed("hex-right", "clear_vision")

-- Vacuum Base ON
io.setOutputName("IOBottom", "Vacuum_Base")

-- Move gantry to see SLED
motion.moveToNamed("gantry-main", "seesled")

-- Probe SLED confirmation
bobcat.prompt("Probe SLED?", "Please confirm to continue")

-- Camera exposure for probing
camera.setExposure("main_camera", 300)

-- TEC ON at 25C
tec.setTemp("CLD101x_Direct", 25.0)
tec.enable("CLD101x_Direct")

-- Laser ON at 250mA
tec.setLaserCurrent("CLD101x_Direct", 250)
tec.laserOn("CLD101x_Direct")

-- Move to see PIC
motion.moveToNamed("gantry-main", "seepic")

-- Probe PIC confirmation
bobcat.prompt("Probe PIC", "Please confirm to continue")

-- Read and log data channel
log.info("======== Read from Data Channel")
_dispatch("datastore", "ReadAndLog", {
    channelName = "SMU_Keithley_6482_Ch1",
    logLevel = "Warning"
})
log.info("=========== READ DONE")

-- Wait 500ms
system.wait(500)

-- Read dark current and store in context
local darkCurrent = smu.readCurrent("Keithley_6482", 1)
ctx.set("DarkCurrent", darkCurrent)
print(string.format("Dark Current = %.6f A", darkCurrent))

-- Move to see serial number
motion.moveToNamed("gantry-main", "SN view")

-- (disabled) Ask User to record data
-- bobcat.prompt("Record data and ok to end process.", "Please confirm to continue")

-- (disabled) Laser OFF
-- tec.setLaserCurrent("CLD101x_Direct", 150)
-- tec.laserOff("CLD101x_Direct")

-- (disabled) TEC OFF
-- tec.setTemp("CLD101x_Direct", 25.0)
-- tec.disable("CLD101x_Direct")

-- (disabled) Clear Vacuum_Base
-- io.clearOutputName("IOBottom", "Vacuum_Base")

-- DUT Create
dut.create("")

-- DUT Insert Status
dut.insertString("$Status", "started")

-- (disabled) DUT Load
-- dut.load("", "")

print("Probe SLED and PIC complete")
