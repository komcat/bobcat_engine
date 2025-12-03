# Bobcat Engine User Manual

Industrial motion control and automation application.

## Quick Start

1. Launch `bobcat_engine.exe`
2. Connect hardware devices
3. Load or create a recipe
4. Run program sequence

## Manual Contents

### Pages
- [Main Menu](pages/main_menu/) - Home screen and product management
- [Motion Page](pages/motion_page/) - Position management and graph editing
- [Hardware Instrument](pages/hardware_instrument/) - Hardware device configuration
- [Auxiliary Display](pages/auxiliary_display/) - Camera view and live data
- [Jog Window](pages/jog_window/) - Manual motion control
- [Recipe Page](pages/recipe_page/) - Recipe editing and process configuration
- [Run Program Page](pages/run_program_page/) - Program execution and monitoring

### Systems
- [Live Chart View](systems/live_chart_view/) - Real-time data with target comparison
- [Motion Control](systems/motion_control/) - Motion devices and commands
- [Graph Navigation](systems/graph_navigation/) - Path planning with motion graphs

### Configuration
- [Config Files](config/) - JSON configuration file reference

### Programs
- [Build SiPhOG Rev1](programs/build_siphog_rev1.md) - SiPhOG assembly program with recipe details

## Common Workflows

### Manual Alignment
1. Open Jog Window
2. Select motion device
3. Use jog buttons to position
4. Save position as named node

### Running a Recipe
1. Go to Recipe Page
2. Select or create recipe
3. Enable/disable process steps
4. Click Run

### Monitoring Process Data
1. Open Auxiliary Display
2. Select data channel in Live Chart
3. Set target value for comparison
4. Monitor PASS/MRGN/NWRK status

## Folder Structure

```
user_manual/
├── README.md (this file)
├── pages/
│   ├── main_menu/
│   ├── motion_page/
│   ├── hardware_instrument/
│   ├── auxiliary_display/
│   ├── jog_window/
│   ├── recipe_page/
│   └── run_program_page/
├── systems/
│   ├── live_chart_view/
│   ├── motion_control/
│   └── graph_navigation/
├── config/
│   └── README.md (config file reference)
└── programs/
    └── build_siphog_rev1.md (program documentation)
```
