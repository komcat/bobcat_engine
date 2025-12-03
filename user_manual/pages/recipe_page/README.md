# Recipe Page

Recipe editing and process configuration.

## Recipe Structure

A recipe contains ordered process steps that execute sequentially.

## Process List (Left Panel)

- **Add**: Add new process step
- **Remove**: Delete selected step
- **Up/Down**: Reorder steps
- **Enable/Disable**: Toggle step execution

## Process Configuration (Right Panel)

### Property Table

Two-column editor for process parameters:
- **Property**: Parameter name
- **Value**: Editable value (dropdown, text, checkbox)

### Dependent Dropdowns

Some parameters auto-refresh based on parent selection:

| Parent | Child | Behavior |
|--------|-------|----------|
| Motion Device | Graph Name | Refreshes available graphs |
| Graph Name | End Node | Refreshes available nodes |
| IO Device | Pin Name | Refreshes available pins |

**Example:** Changing Motion Device from `hex-left` to `gantry-main` automatically reloads the Graph Name dropdown with graphs configured for `gantry-main`.

## Process Categories

### Motion Control
- Move to Position
- Move Relative
- Move to Graph Node
- Set Velocity

### Aux Window
- Enable Crosshair
- Enable Click-to-Move
- Set Chart Channel
- Set Chart Target
- Clear Chart Target

### I/O Control
- Set Output
- Wait for Input

### System
- Delay
- Prompt User

## Saving Recipes

- Recipes saved as JSON in `recipes/` folder
- Use File > Save Recipe or Ctrl+S
