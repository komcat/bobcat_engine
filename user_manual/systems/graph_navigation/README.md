# Graph Navigation

Path planning using motion graphs for safe multi-point movements.

## Concept

Motion graphs define safe paths between positions with:
- **Nodes**: Named positions (e.g., home, pickup, place)
- **Edges**: Allowed paths between nodes
- **Waypoints**: Intermediate safe positions

## Benefits

- Automatic collision avoidance
- Consistent path execution
- Safe retract/approach sequences

## Move to Graph Node

### Parameters

| Parameter | Description |
|-----------|-------------|
| Motion Device | Target motion controller |
| Graph Name | Graph to use (e.g., "default") |
| End Node | Destination node name |

### Execution

1. System finds current position
2. Calculates shortest path to end node
3. Moves through intermediate nodes
4. Arrives at destination

## Graph Configuration

Graphs defined in `motor_devices.json` under each device:

```json
{
  "graphs": {
    "default": {
      "nodes": ["home", "safe", "pickup", "place"],
      "edges": [
        ["home", "safe"],
        ["safe", "pickup"],
        ["safe", "place"]
      ]
    }
  }
}
```

## Best Practices

- Always include a "home" node
- Create "safe" intermediate nodes for retract positions
- Test graph paths manually before automation
- Use graphs for complex multi-axis movements
