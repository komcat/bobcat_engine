# Build SiPhOG Rev1 Program

Program for building Silicon Photonics Optical Glass (SiPhOG) assemblies with automated lens placement, alignment, and UV curing.

## Program Overview

| Property | Value |
|----------|-------|
| Program Name | Build SIPHOG |
| File | `Build Siphog rev1.json` |
| Version | 1.0 |

## Recipe Execution Order

| # | Recipe | Enabled | Waits For | Purpose |
|---|--------|---------|-----------|---------|
| 0 | Init SMU and TEC | Yes | - | Initialize measurement equipment |
| 1 | Initial Machine | Yes | - | Home all devices, set velocities |
| 2 | Probe SLED and PIC | Yes | [0] | Probe test points, turn on laser |
| 3 | Pick Place Left Lens 1 | Yes | [1] | Pick and place collimate lens |
| 4 | Pick and Place Right Lens | Yes | [1] | Pick and place focus lens |
| 5 | UV | Yes | - | Fine align + UV cure (SMU mode) |
| 6 | P05 Dispense Left Lens | No | - | Dispense adhesive on left lens |
| 7 | P06 Dispense RIGHT Lens | No | - | Dispense adhesive on right lens |
| 8 | P89 Reject Left Lens | Yes | - | Reject left lens to bin |
| 9 | Reject Right Lens | Yes | - | Reject right lens to bin |
| 10 | UV_WITHBOARDDRIVE | Yes | - | Fine align + UV cure (board drive) |
| 11 | Measurement AF Bake | Yes | - | Post-bake measurement |
| 12 | Both Grippers Home | Yes | - | Return grippers to home |
| 13 | Left gripper Place lens | Yes | - | Move left gripper to place position |
| 14 | Right gripper Place lens | Yes | - | Move right gripper to place position |

---

## Recipe Details

### Recipe 0: Init SMU and TEC

**Purpose:** Initialize measurement equipment for optical power monitoring.

| Step | Action | Parameters |
|------|--------|------------|
| 1 | TEC Connect | Device: CLD101x_Direct |
| 2 | SMU Connect | Device: Keithley_6482 |
| 3 | SMU Set Voltage | Device: Keithley_6482, Voltage: 1V |
| 4 | SMU Output ON | Device: Keithley_6482 |
| 5 | Wait | 300ms |
| 6 | SMU Start Polling | Channel: 1, Mode: ULTRA_FAST, Interval: 100ms |
| 7 | Wait | 1000ms |
| 8 | SMU Stop Polling | Device: Keithley_6482 |
| 9 | Wait | 500ms |
| 10 | SMU Start Polling | Channel: 1, Mode: FAST, Interval: 100ms |

**Output:** SMU continuously polling photo current on Channel 1.

---

### Recipe 1: Initial Machine

**Purpose:** Initialize machine to known safe state.

| Step | Action | Parameters |
|------|--------|------------|
| 1 | Set Velocity | gantry-main: 50 mm/s |
| 2 | Set Velocity | hex-left: 8 mm/s |
| 3 | Set Velocity | hex-right: 8 mm/s |
| 4 | Slide Retract | UV_Head |
| 5 | Slide Retract | Dispenser_Head |
| 6 | Slide Retract | Pick_Up_Tool |
| 7 | Move to Position | hex-left → home |
| 8 | Move to Position | hex-right → home |
| 9 | Move to Position | gantry-main → safe |
| 10 | Set Output | Vacuum_Base: OFF |

**Output:** All slides retracted, hexapods at home, gantry at safe position.

---

### Recipe 2: Probe SLED and PIC

**Purpose:** Position for probing SLED and PIC test points, enable laser.

**Prerequisite:** Recipe 0 (Init SMU and TEC) must complete first.

| Step | Action | Parameters |
|------|--------|------------|
| 1-3 | Slide Retract | UV_Head, Dispenser_Head, Pick_Up_Tool |
| 4-6 | Set Velocity | gantry: 35, hex-left: 5, hex-right: 5 mm/s |
| 7 | Move to Position | gantry-main → safe |
| 8-9 | Move to Position | hex-left, hex-right → clear_vision |
| 10 | Set Output | Vacuum_Base: ON |
| 11 | Move to Position | gantry-main → seesled |
| 12 | **Ask User** | "Probe SLED?" |
| 13 | Set TEC Temp | CLD101x_Direct: 25°C, ON |
| 14 | Set Laser Current | CLD101x_Direct: 250mA, ON |
| 15 | Move to Position | gantry-main → seepic |
| 16 | **Ask User** | "Probe PIC?" |
| 17-19 | Log/Read DataStore | SMU_Keithley_6482_Ch1 |
| 20 | Wait | 500ms |
| 21 | Move to Position | gantry-main → SN view |

**User Interactions:**
- Confirm SLED probe connection
- Confirm PIC probe connection

**Output:** Laser enabled at 250mA, probes connected, camera viewing serial number.

---

### Recipe 3: Pick Place Left Lens 1

**Purpose:** Pick collimate lens from tray and place on left hexapod position.

**Prerequisite:** Recipe 1 (Initial Machine) must complete first.

| Step | Action | Parameters |
|------|--------|------------|
| 1 | Move to Position | gantry-main → safe |
| 2 | Clear Output | L_Gripper (release) |
| 3 | Move to Position | hex-left → approachlensgrip |
| 4 | Move to Position | hex-left → lensgrip |
| 5 | Move to Position | gantry-main → See Grip collimate lens |
| 6 | **Ask User** | "Is lens in place, good to grip?" |
| 7 | Set Output | L_Gripper (grip) |
| 8 | Wait | 1500ms |
| 9 | Clear Output | L_Gripper (release) |
| 10 | Wait | 1500ms |
| 11 | Set Output | L_Gripper (grip final) |
| 12 | **Ask User** | "Lens are good after gripping?" |
| 13 | Move to Position | gantry-main → See Collimate Lens |
| 14 | Move to Position | hex-left → approachlensgrip |
| 15 | Move to Position | hex-left → approachlensplace |
| 16 | Move to Position | hex-left → lensplace |

**User Interactions:**
- Confirm lens is positioned correctly for gripping
- Confirm lens is secure after grip cycle

**Motion Sequence:**
1. Gantry clears to safe
2. Left gripper opens
3. Left hexapod approaches then moves to grip position
4. Camera views grip area
5. Grip-release-grip cycle (ensures proper seating)
6. Hexapod moves to place position

---

### Recipe 4: Pick and Place Right Lens

**Purpose:** Pick focus lens from tray and place on right hexapod position.

**Prerequisite:** Recipe 1 (Initial Machine) must complete first.

Same flow as Recipe 3 but uses:
- **hex-right** instead of hex-left
- **R_Gripper** instead of L_Gripper
- **See Grip Focus Lens** / **See Focus Lens** camera positions

---

### Recipe 5: UV

**Purpose:** Fine alignment using XCAN algorithm, then UV cure with SMU feedback.

| Step | Action | Parameters |
|------|--------|------------|
| 1 | Slide Retract | UV_Head |
| 2-3 | Move to Position | gantry-main → safe → UV |
| 4 | Set Laser Current | 150mA, ON |
| 5 | Slide Extend | UV_Head |
| 6 | **XCAN Fine** | hex-left, Channel: SMU_Keithley_6482_Ch1 |
| 7 | **XCAN Fine** | hex-right, Channel: SMU_Keithley_6482_Ch1 |
| 8 | SMU Read Current | Capture as $DRY_VALUE |
| 9 | Trigger UV PLC | UV_PLC1 pulse |
| 10 | Wait | 210 seconds (3.5 minutes UV cure) |
| 11 | Slide Retract | UV_Head |
| 12 | Move to Graph Node | gantry-main → safe |
| 13-14 | Clear Output | L_Gripper, R_Gripper |
| 15 | Wait | 3000ms |
| 16 | SMU Read Current | Capture as $UV_VALUE |
| 17-18 | Move Relative | hex-left, hex-right: +4mm X (lift) |
| 19-22 | Move to Position | Both hexapods → home → clear_vision |
| 23 | Clear Output | Vacuum_Base |
| 24-25 | Set Laser/TEC | OFF |
| 26 | Move to Graph Node | gantry-main → SN view |

**Key Operations:**
- XCAN fine alignment optimizes optical coupling
- UV cure time: 210 seconds
- Captures $DRY_VALUE before cure, $UV_VALUE after cure

---

### Recipe 6: P05 Dispense Left Lens (Disabled)

**Purpose:** Apply adhesive to collimate lens location.

| Step | Action | Parameters |
|------|--------|------------|
| 1-3 | Slide Retract | UV_Head, Pick_Up_Tool, Dispenser_Head |
| 4 | Set Velocity | gantry-main: 25 mm/s |
| 5 | Move to Position | gantry-main → safe |
| 6-7 | Move to Graph Node | hex-left, hex-right → Reject Lens |
| 8 | **Dispense** | Collimate Lens location |
| 9 | Move to Position | gantry-main → safe |
| 10 | Move to Graph Node | gantry-main → See Collimate Lens |

**Dispense Parameters:**
- Dispense interval: 300ms
- Safety height: 1mm
- Approach velocity: 5 mm/s
- Safety velocity: 0.1 mm/s

---

### Recipe 7: P06 Dispense RIGHT Lens (Disabled)

**Purpose:** Apply adhesive to focus lens location.

Same flow as Recipe 6 but targets Focus Lens position.

---

### Recipe 8: P89 Reject Left Lens

**Purpose:** Move left lens to reject bin and release.

| Step | Action | Parameters |
|------|--------|------------|
| 1 | Move Relative | hex-left: +3mm X (lift) |
| 2 | Move to Position | hex-left → home |
| 3 | Move to Graph Node | hex-left → Reject Lens |
| 4 | **Ask User** | "Do you want to ungrip the lens?" |
| 5 | Clear Output | L_Gripper (release) |
| 6 | Wait | 3000ms |
| 7 | Move to Graph Node | hex-left → home |

**User Interaction:** Confirm before releasing lens to reject bin.

---

### Recipe 9: Reject Right Lens

**Purpose:** Move right lens to reject bin and release.

Same flow as Recipe 8 but uses hex-right and R_Gripper.

---

### Recipe 10: UV_WITHBOARDDRIVE

**Purpose:** Fine alignment and UV cure using board drive signal (Photo_Current_uA channel).

Similar to Recipe 5 but:
- Uses **Photo_Current_uA** channel instead of SMU
- Laser current setting disabled (board provides drive)
- SMU read operations disabled

**Key Difference:** For use when external board drives the SLED instead of internal laser controller.

---

### Recipe 11: Measurement AF Bake

**Purpose:** Post-bake measurement verification.

| Step | Action | Parameters |
|------|--------|------------|
| 1-3 | Slide Retract | All slides |
| 4-6 | Set Velocity | gantry: 35, hexapods: 5 mm/s |
| 7-9 | Move to Position | Hexapods → clear_vision |
| 10 | Set Output | Vacuum_Base: ON |
| 11 | Move to Position | gantry-main → seesled |
| 12 | **Ask User** | "Probe SLED?" |
| 13 | Set TEC Temp | 25°C, ON |
| 14 | Wait | 250ms |
| 15 | Set Laser Current | 150mA, ON |
| 16 | Move to Position | gantry-main → seepic |
| 17 | **Ask User** | "Probe PIC?" |
| 18-20 | Log/Read DataStore | SMU_Keithley_6482_Ch1 |
| 21 | Wait | 500ms |
| 22 | Move to Position | gantry-main → SN view |
| 23 | **Ask User** | "Record data and ok to end process" |
| 24-25 | Set Laser/TEC | OFF |
| 26 | Clear Output | Vacuum_Base |

**Purpose:** Verify optical coupling after thermal bake cycle.

---

### Recipe 12: Both Grippers Home

**Purpose:** Return both hexapod grippers to home position.

| Step | Action | Parameters |
|------|--------|------------|
| 1 | Move to Graph Node | hex-left → home |
| 2 | Move to Graph Node | hex-right → home |

---

### Recipe 13: Left gripper Place lens

**Purpose:** Move left gripper to lens place position for inspection.

| Step | Action | Parameters |
|------|--------|------------|
| 1 | Move to Graph Node | hex-left → lensplace |
| 2 | Move to Graph Node | gantry-main → See Collimate Lens |

---

### Recipe 14: Right gripper Place lens

**Purpose:** Move right gripper to lens place position for inspection.

| Step | Action | Parameters |
|------|--------|------------|
| 1 | Move to Graph Node | hex-right → lensplace |
| 2 | Move to Graph Node | gantry-main → See Focus Lens |

---

## Typical Process Flow

### Standard Build Sequence

```
1. Init SMU and TEC (Recipe 0)
   └── Start measurement equipment

2. Initial Machine (Recipe 1)
   └── Home all devices

3. Probe SLED and PIC (Recipe 2) [waits for Recipe 0]
   └── Connect probes, enable laser

4. Pick Place Left Lens (Recipe 3) [waits for Recipe 1]
   │   └── Pick and place collimate lens
   │
   └── Pick Place Right Lens (Recipe 4) [waits for Recipe 1]
       └── Pick and place focus lens (parallel)

5. UV or UV_WITHBOARDDRIVE (Recipe 5 or 10)
   └── Fine align both lenses, UV cure

6. Measurement AF Bake (Recipe 11)
   └── Post-process verification
```

### Error Recovery

- **Reject Left Lens (Recipe 8):** If left lens placement fails
- **Reject Right Lens (Recipe 9):** If right lens placement fails
- **Both Grippers Home (Recipe 12):** Return to known state

---

## Hardware Used

### Motion Devices
| Device | Type | Purpose |
|--------|------|---------|
| gantry-main | ACS | Camera positioning, tool head positioning |
| hex-left | PI Hexapod | Left lens gripper, 6-DOF alignment |
| hex-right | PI Hexapod | Right lens gripper, 6-DOF alignment |

### Pneumatic Slides
| Slide | Purpose |
|-------|---------|
| UV_Head | UV curing lamp positioning |
| Dispenser_Head | Adhesive dispenser positioning |
| Pick_Up_Tool | Pick and place tool positioning |

### I/O Outputs
| Output | Purpose |
|--------|---------|
| L_Gripper | Left gripper vacuum control |
| R_Gripper | Right gripper vacuum control |
| Vacuum_Base | Work holder vacuum |
| UV_PLC1 | UV lamp trigger signal |
| Dispenser_Shot | Adhesive dispense trigger |

### Measurement Equipment
| Device | Purpose |
|--------|---------|
| Keithley_6482 | Photo current measurement (2 channel) |
| CLD101x_Direct | Laser diode + TEC controller |

---

## Key Named Positions

### Gantry Positions
| Position | Purpose |
|----------|---------|
| safe | Clear position for tool changes |
| UV | UV curing position |
| seesled | Camera view of SLED |
| seepic | Camera view of PIC |
| SN view | Camera view of serial number |
| See Collimate Lens | View left lens placement |
| See Focus Lens | View right lens placement |
| See Grip collimate lens | View left lens grip area |
| See Grip Focus Lens | View right lens grip area |

### Hexapod Positions
| Position | Purpose |
|----------|---------|
| home | Safe home position |
| clear_vision | Clear camera field of view |
| approachlensgrip | Approach position before gripping |
| lensgrip | Lens gripping position |
| approachlensplace | Approach position before placing |
| lensplace | Lens placement position |
| Reject Lens | Lens reject bin position |
