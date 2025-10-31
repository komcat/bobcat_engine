"""Configuration for alignment application (pA to mA range)."""

# Server Configuration
SERVER_CH1_HOST = "127.0.0.44"
SERVER_CH1_PORT = 55555
SERVER_CH2_HOST = "127.0.0.45"
SERVER_CH2_PORT = 55556
SERVER_LISTEN_BACKLOG = 5

# Multimeter Configuration
MULTIMETER_ADDRESS = 'GPIB0::1::INSTR'
MEASUREMENT_FUNCTION = 'CURR'
ENABLED_CHANNELS = [1, 2]

# Alignment Modes
class AlignmentMode:
    SEARCH = "SEARCH"      # Looking for signal (high sensitivity, slower)
    COARSE = "COARSE"      # Initial alignment (medium speed)
    FINE = "FINE"          # Fine alignment (balanced)
    MONITOR = "MONITOR"    # Production monitoring (fastest)

# Mode-specific settings
MODE_SETTINGS = {
    AlignmentMode.SEARCH: {
        "initial_range": "LOW",     # Start with high sensitivity
        "nplc": 1.0,                 # High accuracy
        "auto_zero": True,
        "averaging": 5,
        "update_rate": 0.2           # 5 Hz update
    },
    AlignmentMode.COARSE: {
        "initial_range": "MID",
        "nplc": 0.1,
        "auto_zero": True,
        "averaging": 3,
        "update_rate": 0.1           # 10 Hz update
    },
    AlignmentMode.FINE: {
        "initial_range": "AUTO",
        "nplc": 0.1,
        "auto_zero": True,
        "averaging": 2,
        "update_rate": 0.05          # 20 Hz update
    },
    AlignmentMode.MONITOR: {
        "initial_range": "HIGH",     # Fixed at 20mA range
        "nplc": 0.01,
        "auto_zero": False,
        "averaging": 0,              # No averaging
        "update_rate": 0.01          # 100 Hz update
    }
}

# Current alignment mode
CURRENT_MODE = AlignmentMode.COARSE

# Expected current ranges for your application
MIN_EXPECTED_CURRENT = 1e-12    # 1 pA minimum
MAX_EXPECTED_CURRENT = 6e-3     # 6 mA maximum
TYPICAL_CURRENT = 1e-3           # 1 mA typical after alignment

# Performance Settings
BATCH_SIZE = 10
UI_UPDATE_INTERVAL = 0.1
HISTOGRAM_UPDATE_INTERVAL = 10

# UI Configuration
MAX_DATA_POINTS = 500
PLOT_LINE_COLOR_CH1 = 'yellow'
PLOT_LINE_COLOR_CH2 = 'cyan'
PLOT_LINE_WIDTH = 2
HISTOGRAM_COLOR = 'r'

# Application Settings
APP_NAME = "Alignment System - Keithley 6482 (pA to mA)"
SHOW_RANGE_INFO = True  # Show current range in UI
ENABLE_AUTO_MODE_SWITCH = True  # Automatically switch modes based on signal