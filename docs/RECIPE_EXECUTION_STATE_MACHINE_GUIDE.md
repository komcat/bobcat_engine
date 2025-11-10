# Recipe Execution State Machine Guide

## Overview

The **RecipeExecutionStateMachine** is a sophisticated system for managing complex recipe execution dependencies in programs. It supports:

- **Multiple Prerequisites**: A recipe can require multiple other recipes to complete first
- **Multiple Successors**: One recipe completion can enable multiple subsequent recipes
- **Directed Acyclic Graph (DAG)**: Recipes form a dependency graph with automatic cycle detection
- **State Tracking**: Real-time tracking of recipe execution states (Pending, Ready, Running, Completed, Failed, Skipped)
- **Automatic Ready Detection**: Recipes automatically become ready when all prerequisites complete
- **Failure Propagation**: If a recipe fails, all dependent recipes are automatically skipped

## Architecture

### Components

1. **RecipeExecutionStateMachine** (`include/ProgramManager/RecipeExecutionStateMachine.h`)
   - Core state machine managing the dependency graph
   - Handles state transitions and validation

2. **RecipeReference** (updated in `include/ProgramManager/ProgramManager.h`)
   - Now includes `predecessorIndices` vector for storing dependencies

3. **ProgramManager** (updated in `include/ProgramManager/ProgramManager.h`)
   - New methods for managing recipe dependencies
   - Save/Load support for dependency data in JSON

## Recipe Execution States

```
Pending   → Recipe waiting for prerequisites to complete
Ready     → All prerequisites met, ready to execute
Running   → Currently executing
Completed → Successfully finished
Failed    → Execution failed
Skipped   → Skipped due to disabled or failed prerequisites
Disabled  → Manually disabled by user
```

## Usage Examples

### Example 1: Linear Dependencies

```
Recipe 1: Initial Machine  (no dependencies)
Recipe 2: Probe SLED       (depends on Recipe 1)
Recipe 3: Measure Power    (depends on Recipe 2)
```

**Code:**
```cpp
ProgramManager programMgr;

// Add recipes
programMgr.addRecipe("config/product/recipes/initial_machine.json", "Initial Machine");
programMgr.addRecipe("config/product/recipes/probe_sled.json", "Probe SLED");
programMgr.addRecipe("config/product/recipes/measure_power.json", "Measure Power");

// Set up dependencies
programMgr.addRecipeDependency(1, 0);  // Recipe 2 depends on Recipe 1
programMgr.addRecipeDependency(2, 1);  // Recipe 3 depends on Recipe 2
```

**Execution Flow:**
1. Recipe 1 executes first
2. When Recipe 1 completes → Recipe 2 becomes ready
3. When Recipe 2 completes → Recipe 3 becomes ready

### Example 2: Parallel Branches with Final Convergence

```
Recipe 1: Initial Machine     (no dependencies)
Recipe 2: Pick Left Lens      (depends on Recipe 1)
Recipe 3: Pick Right Lens     (depends on Recipe 1)
Recipe 4: Place Left Lens     (depends on Recipe 2)
Recipe 5: Place Right Lens    (depends on Recipe 3)
Recipe 6: Final Assembly      (depends on Recipes 4 AND 5)
```

**Code:**
```cpp
// Add all recipes
programMgr.addRecipe("config/product/recipes/initial.json", "Initial Machine");
programMgr.addRecipe("config/product/recipes/pick_left.json", "Pick Left Lens");
programMgr.addRecipe("config/product/recipes/pick_right.json", "Pick Right Lens");
programMgr.addRecipe("config/product/recipes/place_left.json", "Place Left Lens");
programMgr.addRecipe("config/product/recipes/place_right.json", "Place Right Lens");
programMgr.addRecipe("config/product/recipes/final.json", "Final Assembly");

// Set up parallel branches
programMgr.addRecipeDependency(1, 0);  // Pick Left depends on Initial
programMgr.addRecipeDependency(2, 0);  // Pick Right depends on Initial
programMgr.addRecipeDependency(3, 1);  // Place Left depends on Pick Left
programMgr.addRecipeDependency(4, 2);  // Place Right depends on Pick Right
programMgr.addRecipeDependency(5, 3);  // Final depends on Place Left
programMgr.addRecipeDependency(5, 4);  // Final depends on Place Right
```

**Execution Flow:**
1. Recipe 1 (Initial Machine) executes first
2. When Recipe 1 completes → Recipes 2 and 3 both become ready (can execute in parallel if supported)
3. When Recipe 2 completes → Recipe 4 becomes ready
4. When Recipe 3 completes → Recipe 5 becomes ready
5. Recipe 6 waits until BOTH Recipe 4 AND Recipe 5 complete
6. When both Recipe 4 and 5 are done → Recipe 6 becomes ready

### Example 3: Complex Manufacturing Process

```
Recipe 1: Load Wafer              (no dependencies)
Recipe 2: Vision Alignment        (depends on Recipe 1)
Recipe 3: Dispense Adhesive       (depends on Recipe 2)
Recipe 4: Pick Component A        (depends on Recipe 2)
Recipe 5: Pick Component B        (depends on Recipe 2)
Recipe 6: Place Component A       (depends on Recipes 3 AND 4)
Recipe 7: Place Component B       (depends on Recipes 3 AND 5)
Recipe 8: UV Cure                 (depends on Recipes 6 AND 7)
Recipe 9: Unload Wafer            (depends on Recipe 8)
```

**Code:**
```cpp
// Dependencies form a complex DAG
programMgr.addRecipeDependency(1, 0);  // Vision depends on Load
programMgr.addRecipeDependency(2, 1);  // Dispense depends on Vision
programMgr.addRecipeDependency(3, 1);  // Pick A depends on Vision
programMgr.addRecipeDependency(4, 1);  // Pick B depends on Vision
programMgr.addRecipeDependency(5, 2);  // Place A depends on Dispense
programMgr.addRecipeDependency(5, 3);  // Place A depends on Pick A
programMgr.addRecipeDependency(6, 2);  // Place B depends on Dispense
programMgr.addRecipeDependency(6, 4);  // Place B depends on Pick B
programMgr.addRecipeDependency(7, 5);  // UV Cure depends on Place A
programMgr.addRecipeDependency(7, 6);  // UV Cure depends on Place B
programMgr.addRecipeDependency(8, 7);  // Unload depends on UV Cure
```

## API Reference

### RecipeExecutionStateMachine Methods

#### Initialization
```cpp
void initialize(int recipeCount, const std::vector<std::string>& recipeNames);
```

#### Dependency Management
```cpp
bool addDependency(int recipeIndex, int prerequisiteIndex);
void removeDependency(int recipeIndex, int prerequisiteIndex);
void clearDependencies(int recipeIndex);
void clearAllDependencies();
```

#### Validation
```cpp
bool validateGraph(std::vector<std::string>& errorMessages) const;
bool wouldCreateCycle(int recipeIndex, int prerequisiteIndex) const;
bool getTopologicalSort(std::vector<int>& sortedIndices) const;
```

#### Execution Control
```cpp
void resetExecution();
std::vector<int> getReadyRecipes() const;
void markRecipeStarted(int recipeIndex);
void markRecipeCompleted(int recipeIndex, bool success, const std::string& errorMessage = "");
bool isExecutionComplete() const;
```

#### Query Functions
```cpp
RecipeExecutionState getRecipeState(int recipeIndex) const;
const RecipeNode* getRecipeNode(int recipeIndex) const;
std::vector<int> getPredecessors(int recipeIndex) const;
std::vector<int> getSuccessors(int recipeIndex) const;
bool hasDependencies(int recipeIndex) const;
bool hasDependents(int recipeIndex) const;
std::string getExecutionSummary() const;
```

### ProgramManager Methods

#### Dependency Management
```cpp
bool addRecipeDependency(int recipeIndex, int prerequisiteIndex);
void removeRecipeDependency(int recipeIndex, int prerequisiteIndex);
void clearRecipeDependencies(int recipeIndex);
std::vector<int> getRecipeDependencies(int recipeIndex) const;
bool hasRecipeDependency(int recipeIndex, int prerequisiteIndex) const;
bool validateDependencies(std::vector<std::string>& errors) const;
```

## Integration with ProgramWorker

The state machine integrates with the existing `ProgramWorker` for execution. Here's a typical integration pattern:

```cpp
// In ProgramWorker::run()

// 1. Create and initialize state machine
RecipeExecutionStateMachine stateMachine;
std::vector<std::string> recipeNames;
for (const auto& recipe : program->getAllRecipes()) {
    recipeNames.push_back(recipe->displayName);
}
stateMachine.initialize(program->getRecipeCount(), recipeNames);

// 2. Import dependencies from program
for (int i = 0; i < program->getRecipeCount(); ++i) {
    const auto& deps = program->getRecipeDependencies(i);
    for (int predIndex : deps) {
        stateMachine.addDependency(i, predIndex);
    }
}

// 3. Validate graph
std::vector<std::string> errors;
if (!stateMachine.validateGraph(errors)) {
    // Handle validation errors
    return;
}

// 4. Reset for execution
stateMachine.resetExecution();

// 5. Main execution loop
while (!stateMachine.isExecutionComplete()) {
    // Get all ready recipes
    std::vector<int> readyRecipes = stateMachine.getReadyRecipes();

    if (readyRecipes.empty() && !stateMachine.isExecutionComplete()) {
        // No recipes ready but execution not complete = deadlock or all recipes disabled
        break;
    }

    // Execute ready recipes (can be parallel if system supports it)
    for (int recipeIndex : readyRecipes) {
        stateMachine.markRecipeStarted(recipeIndex);

        bool success = executeRecipe(recipeIndex);

        stateMachine.markRecipeCompleted(recipeIndex, success,
            success ? "" : "Recipe execution failed");
    }
}

// 6. Check final status
int pending, ready, running, completed, failed, skipped;
stateMachine.getStateCounts(pending, ready, running, completed, failed, skipped);
```

## JSON File Format

Program files now include dependency information:

```json
{
  "version": "1.0",
  "program_name": "Complex Assembly Program",
  "recipes": [
    {
      "recipe_file": "config/PRODUCT/recipes/initial.json",
      "display_name": "Initial Machine",
      "enabled": true
    },
    {
      "recipe_file": "config/PRODUCT/recipes/pick_left.json",
      "display_name": "Pick Left Lens",
      "enabled": true,
      "predecessors": [0]
    },
    {
      "recipe_file": "config/PRODUCT/recipes/pick_right.json",
      "display_name": "Pick Right Lens",
      "enabled": true,
      "predecessors": [0]
    },
    {
      "recipe_file": "config/PRODUCT/recipes/final.json",
      "display_name": "Final Assembly",
      "enabled": true,
      "predecessors": [1, 2]
    }
  ]
}
```

## Cycle Detection

The state machine automatically detects cycles to prevent deadlocks:

```cpp
// This will fail - creates a cycle
programMgr.addRecipeDependency(0, 2);  // Recipe 1 depends on Recipe 3
programMgr.addRecipeDependency(1, 0);  // Recipe 2 depends on Recipe 1
programMgr.addRecipeDependency(2, 1);  // Recipe 3 depends on Recipe 2 - CYCLE!
// The last call will return false and not add the dependency
```

## Best Practices

1. **Keep Dependencies Simple**: Start with simple linear dependencies, then add complexity as needed

2. **Validate Early**: Always validate the dependency graph before execution:
   ```cpp
   std::vector<std::string> errors;
   if (!programMgr.validateDependencies(errors)) {
       // Show errors to user
   }
   ```

3. **Handle Failures**: When a recipe fails, all dependent recipes are automatically skipped. Design your process to handle this gracefully.

4. **Use Meaningful Names**: Give recipes clear, descriptive names to make dependency relationships obvious

5. **Document Complex Dependencies**: For complex processes, create a diagram showing the dependency graph

6. **Test Incremental**: Build and test your dependency graph incrementally, adding one dependency at a time

## Future Enhancements

Potential future improvements to the system:

- **Conditional Dependencies**: Dependencies that activate based on runtime conditions
- **Parallel Execution**: Execute multiple ready recipes simultaneously using threading
- **Dependency Templates**: Pre-defined dependency patterns for common workflows
- **Visual Dependency Editor**: GUI tool for visualizing and editing dependency graphs
- **Time-based Dependencies**: Recipes that must wait for a time delay
- **Resource Dependencies**: Recipes that share limited resources (e.g., motion controllers)

## Troubleshooting

### Problem: Recipe Never Becomes Ready
- **Check**: Verify all prerequisites are enabled and complete successfully
- **Check**: Ensure there are no cycles in the dependency graph
- **Check**: Confirm prerequisite indices are valid

### Problem: All Recipes Remain Pending
- **Check**: Ensure at least one recipe has no dependencies (entry point)
- **Check**: Verify state machine was properly initialized

### Problem: Unexpected Skips
- **Check**: Review execution log for failed prerequisites
- **Check**: Ensure all recipes in dependency chain are enabled

## Support

For questions or issues:
- Check existing program files for examples
- Review the unit tests in `test/RecipeExecutionStateMachineTest.cpp` (if available)
- Consult the main CLAUDE.md file for general architecture information
