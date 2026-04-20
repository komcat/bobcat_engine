-- build_siphog_rev1.lua
-- Converted from: Build SIPHOG Rev1 program (Production slot)
-- Recipes: Init SMU and TEC, Initial Machine, Probe SLED and PIC,
--          PP Left Lens SIPHOG, PP Right Lens SIPHOG, UV

print("========================================")
print("  Build SIPHOG Rev1 - Production")
print("========================================")

-- ============================================================================
-- Recipe 1: Init SMU and TEC
-- ============================================================================
local function initSmuAndTec()
    print("\n--- Recipe: Init SMU and TEC ---")

    -- TEC Connect
    tec.connect("CLD101x_Direct")

    -- SMU Connect
    smu.connect("Keithley_6482")

    -- SMU Set Voltage 1V
    smu.setVoltage("Keithley_6482", 1.0)

    -- SMU Output ON
    smu.enable("Keithley_6482")

    -- Wait 300ms
    system.wait(300)

    -- SMU Start Polling (ULTRA_FAST mode)
    smu.startPolling("Keithley_6482", "ULTRA_FAST", 100, 1)

    -- Wait 1000ms
    system.wait(1000)

    -- SMU Stop Polling
    smu.stopPolling("Keithley_6482")

    -- Wait 500ms
    system.wait(500)

    -- SMU Start Polling (FAST mode)
    smu.startPolling("Keithley_6482", "FAST", 100, 1)

    print("Init SMU and TEC complete")
end

-- ============================================================================
-- Recipe 2: Initial Machine
-- ============================================================================
local function initialMachine()
    print("\n--- Recipe: Initial Machine ---")

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
end

-- ============================================================================
-- Recipe 3: Probe SLED and PIC
-- ============================================================================
local function probeSledAndPic()
    print("\n--- Recipe: Probe SLED and PIC ---")

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
end

-- ============================================================================
-- Recipe 4: PP Left Lens SIPHOG (Pick & Place Left Lens)
-- ============================================================================
local function ppLeftLens()
    print("\n--- Recipe: PP Left Lens SIPHOG ---")

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
end

-- ============================================================================
-- Recipe 5: PP Right Lens SIPHOG (Pick & Place Right Lens)
-- ============================================================================
local function ppRightLens()
    print("\n--- Recipe: PP Right Lens SIPHOG ---")

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

    -- Gripper sequence: ON > wait > OFF > wait > ON
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
end

-- ============================================================================
-- Recipe 6: UV Curing
-- ============================================================================
local function uvCuring()
    print("\n--- Recipe: UV Curing ---")

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
end

-- ============================================================================
-- MAIN EXECUTION FLOW (Production slot with dependencies)
-- ============================================================================

print("\n>>> Starting Build SIPHOG Rev1 Production <<<\n")

-- Step 1: Init SMU and TEC (no dependency)
initSmuAndTec()

-- Step 2: Initial Machine (no dependency)
initialMachine()

-- Step 3: Probe SLED and PIC (no dependency)
probeSledAndPic()

-- Step 4: PP Left Lens (depends on: DarkCurrent > 0)
local darkCurrent = ctx.getNumeric("DarkCurrent", 0)
if darkCurrent > 0 then
    print(string.format("\nDarkCurrent = %.6f > 0 => proceeding with Left Lens", darkCurrent))
    ppLeftLens()
else
    print(string.format("\nDarkCurrent = %.6f <= 0 => SKIPPING Left Lens", darkCurrent))
end

-- Step 5: PP Right Lens (depends on: PowerCollLens1 > 0.0004)
local powerCollLens1 = ctx.getNumeric("PowerCollLens1", 0)
if powerCollLens1 > 0.0004 then
    print(string.format("\nPowerCollLens1 = %.6f > 0.0004 => proceeding with Right Lens", powerCollLens1))
    ppRightLens()
else
    print(string.format("\nPowerCollLens1 = %.6f <= 0.0004 => SKIPPING Right Lens", powerCollLens1))
end

-- Step 6: UV (depends on: PowerBothLens2 > 0.003)
local powerBothLens2 = ctx.getNumeric("PowerBothLens2", 0)
if powerBothLens2 > 0.003 then
    print(string.format("\nPowerBothLens2 = %.6f > 0.003 => proceeding with UV", powerBothLens2))
    uvCuring()
else
    print(string.format("\nPowerBothLens2 = %.6f <= 0.003 => SKIPPING UV", powerBothLens2))
end

print("\n========================================")
print("  Build SIPHOG Rev1 - DONE")
print("========================================")
