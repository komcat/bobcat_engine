# Run Program Page

Execute recipes and monitor program progress.

## Layout

| Left Panel | Center Panel | Right Panel |
|------------|--------------|-------------|
| Execution Controls | Execution Log | Hexapod Scanning |
| Recipes in Program | | Vision Result |

## Execution Controls

### Execute Program Button
Click to start program execution. Runs all enabled recipes in sequence.

### Options

| Option | Description |
|--------|-------------|
| Auto-run next recipe | Automatically proceed to next recipe |
| Use recipe dependencies | Check dependencies before execution |

### Progress Bar
Shows completion percentage (0-100%).

## Recipes in Program

Lists all recipes in current program:
- **Number**: Execution order (01, 02, 03...)
- **Checkmark**: Indicates completed recipe
- **[COMPLETED]**: Current recipe finished
- Click to select and view details

## Execution Log

Real-time log showing:
- Motion commands and positions
- Recipe start/completion messages
- Execution context summary

### Log Summary (End of Execution)

```
Duration: 310097 ms
Steps Executed: 75
Values Captured: 3
Captured Values:
  - $DRY_VALUE = 0.00129494
  - $UV_VALUE = 0.00111452
  - Keithley_6482.current_ch1 = 0.00111452
```

## Hexapod Scanning Panel

### Data Channel
Select SMU channel for live value display.

### Control Buttons

| Button | Function |
|--------|----------|
| Coarse (Left/Right Hex) | Large step scanning |
| Fine (Left/Right Hex) | Small step scanning |
| STOP Scanning | Halt current scan |

### Live Value Display
Shows current measurement with SI prefix (nA, µA, mA).

## Vision Result

Displays captured vision results from camera processes.

- Shows "No vision result" when empty
- Updates after vision capture commands

## Captured Values

Values captured during execution are stored in context:
- Access via `$VARIABLE_NAME` syntax
- Saved to `logs/context_*.json` after completion
- Use in subsequent recipe steps
