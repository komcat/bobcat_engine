-- siphog_01_init_smu_tec.lua
-- Converted from: SIPHOG888 recipe "Init SMU and TEC"

print("--- Recipe: Init SMU and TEC ---")

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

-- SMU Start Polling (ULTRA_FAST mode, channel 1, 100ms interval)
smu.startPolling("Keithley_6482", "ULTRA_FAST", 100, 1)

-- Wait 1000ms
system.wait(1000)

-- SMU Stop Polling
smu.stopPolling("Keithley_6482")

-- Wait 500ms
system.wait(500)

-- SMU Start Polling (FAST mode, channel 1, 100ms interval)
smu.startPolling("Keithley_6482", "FAST", 100, 1)

print("Init SMU and TEC complete")
