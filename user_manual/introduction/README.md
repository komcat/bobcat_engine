# Introduction

## Overview

Bobcat Engine is an industrial motion control and automation application built with C++ and Qt6. It serves as a comprehensive platform for automated assembly, testing, and measurement operations in manufacturing environments.

The application provides a unified control interface for complex electromechanical systems, integrating:

- **Multi-axis motion control** - Coordinated movement across gantry systems and hexapod positioners
- **Machine vision** - Automated image capture, processing, and measurement
- **Process automation** - Recipe-based execution of manufacturing sequences
- **Hardware integration** - Unified access to cameras, I/O devices, power supplies, and instruments
- **Real-time monitoring** - Live data visualization with target comparison and status tracking

Bobcat Engine is designed for precision applications in semiconductor manufacturing, electronics assembly, and optical testing where coordinated control of multiple subsystems is required.

## Purpose

Bobcat Engine addresses key challenges in industrial automation:

### Unified Hardware Control
Consolidates control of diverse industrial equipment (motion controllers, cameras, I/O modules, power supplies, temperature controllers) into a single application, eliminating the need for separate software tools.

### Repeatable Process Execution
Enables operators to define, save, and execute complex manufacturing sequences as recipes. Processes can be reliably reproduced across production runs with consistent parameters.

### Precision Positioning
Provides sub-micron positioning capability through integration with high-precision motion controllers (ACS gantries, PI hexapods) with coordinate transformation and waypoint-based path planning.

### Vision-Guided Operations
Supports automated alignment and measurement using integrated camera systems with extensible vision processing plugins for pattern matching, edge detection, and dimensional measurement.

### Real-Time Process Monitoring
Displays live measurement data during process execution with configurable targets and pass/fail status indication, enabling immediate quality assessment.

### Flexible Configuration
Uses JSON-based configuration files for all hardware definitions, allowing system setup changes without recompilation. New devices can be added through configuration updates.

## Key Features

### Motion Control System
- Multi-controller support (ACS gantry systems, PI hexapods)
- Named position management with save/recall functionality
- Waypoint-based safe path routing between positions
- Coordinate transformation between reference frames
- Calculated positions with offset notation (e.g., "home+X5-Y2")
- Graph-based visual path planning editor

### Vision System
- Multi-camera support (Basler Pylon, IDS uEye)
- Live camera view with exposure/gain control
- Extensible vision processing plugins:
  - Pattern matching and fiducial detection
  - Edge and line detection
  - Circle and corner detection
- Click-to-move camera integration
- Configurable regions of interest (ROI)

### Recipe and Program Management
- JSON-based recipe definitions with hierarchical process steps
- Visual recipe editor with drag-and-drop configuration
- Process categories: Motion, I/O, Camera, Vision, TEC, Power Supply, SMU, System, Data
- Conditional execution (if/else logic, loops)
- Program manager for multi-recipe sequences with dependency tracking
- Real-time execution monitoring with progress and logging

### Hardware Integration
- **Digital I/O** - EziIO module control with configurable pin mapping
- **Pneumatics** - Pneumatic slide control with sensor-based state tracking
- **Temperature Control** - TEC device management (CLD101x, TED4015)
- **Power Supplies** - Multi-channel Keysight power supply control
- **Source Measure Units** - Keithley SMU integration for electrical measurement
- **Lighting** - LED and CCSPD3 color controller support

### Scanning and Optimization
- Adaptive scan controller with coarse-to-fine search strategies
- Automatic optimal position detection based on measurement feedback
- Dynamic data collection during scan operations

### Data Monitoring and Visualization
- Live chart view with real-time measurement display
- SI unit prefix formatting (nA, uA, mA, etc.)
- Target value comparison with PASS/MARGIN/WARNING status
- Multi-channel simultaneous monitoring
- Data capture and storage to execution context

### User Interface
- **Main Menu** - Product management and page navigation
- **Motion Page** - Position management, graph editor, device status
- **Recipe Page** - Visual recipe building and editing
- **Run Program Page** - Program execution with progress monitoring
- **Hardware Instrument Page** - Device configuration and manual control
- **Auxiliary Display** - Secondary window for camera view and live charts
- **Jog Window** - Manual motion control with keyboard shortcuts
