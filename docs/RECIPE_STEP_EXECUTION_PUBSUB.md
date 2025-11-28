# Recipe Step Execution Pub-Sub Pattern

This document describes the pub-sub (publish-subscribe) pattern used to track recipe step execution in real-time, enabling UI features like step highlighting during recipe execution.

## Overview

When a recipe is executed, the system needs to notify the UI about which step is currently being executed. This is achieved through a pub-sub pattern that flows from `SequenceEngine` through `RecipeWorker` to `RecipePage`.

## Architecture

```
┌─────────────────┐     callbacks      ┌──────────────┐     Qt signals     ┌────────────┐
│ SequenceEngine  │ ─────────────────> │ RecipeWorker │ ─────────────────> │ RecipePage │
│                 │                    │   (Thread)   │                    │    (UI)    │
│ - StepMarker    │                    │              │                    │            │
│ - Callbacks     │                    │ - Forwards   │                    │ - Highlight│
│                 │                    │   events     │                    │   step     │
└─────────────────┘                    └──────────────┘                    └────────────┘
```

## Components

### 1. StepMarker (SequenceEngine.h)

A special sequence step type that marks the boundary between recipe steps:

```cpp
struct StepMarker : public ISequenceStep {
  int stepIndex;           // Index of the enabled step (0-based)
  std::string stepName;    // User nickname of the step
  std::string stepType;    // Process type (e.g., "Wait", "Light: Turn On")

  StepMarker(int index, const std::string& name, const std::string& type)
    : stepIndex(index), stepName(name), stepType(type) {}

  Type GetType() const override { return Type::STEP_MARKER; }
};
```

### 2. Callbacks (SequenceEngine.h)

Two callback types for step execution events:

```cpp
// Called when a step starts executing
using StepStartedCallback = std::function<void(
  int stepIndex,
  const std::string& stepName,
  const std::string& stepType
)>;

// Called when a step completes
using StepCompletedCallback = std::function<void(
  int stepIndex,
  bool success
)>;
```

### 3. SequenceBuilder::AddStepMarker

Inserts a step marker into the sequence:

```cpp
void SequenceBuilder::AddStepMarker(int stepIndex, const std::string& stepName, const std::string& stepType)
{
  auto marker = std::make_shared<StepMarker>(stepIndex, stepName, stepType);
  GetActiveStepList()->push_back(marker);
}
```

## Flow of Events

### 1. Sequence Building (RecipeWorker::buildSequenceFromRecipe)

When building the sequence from a recipe, step markers are inserted before each enabled step:

```cpp
void RecipeWorker::buildSequenceFromRecipe(SequenceBuilder* sequence, RecipeManager* recipeManager)
{
  int enabledStepIndex = 0;

  for (const auto& step : recipeManager->getAllSteps()) {
    if (!step->enabled) continue;

    // Insert step marker BEFORE building the step's actions
    sequence->AddStepMarker(enabledStepIndex, step->userNickname, step->processType);

    // Build the actual step actions
    m_factory->BuildStep(sequence, step.get(), errorMsg);

    enabledStepIndex++;
  }
}
```

### 2. Callback Setup (RecipeWorker::run)

Before executing the sequence, callbacks are set up to forward events via Qt signals:

```cpp
// Set up step execution callbacks
m_engine->SetStepStartedCallback([this](int stepIndex, const std::string& stepName, const std::string& stepType) {
  QMetaObject::invokeMethod(this, "stepExecutionStarted", Qt::QueuedConnection,
    Q_ARG(int, stepIndex),
    Q_ARG(QString, QString::fromStdString(stepName)),
    Q_ARG(QString, QString::fromStdString(stepType)));
});

m_engine->SetStepCompletedCallback([this](int stepIndex, bool success) {
  QMetaObject::invokeMethod(this, "stepExecutionCompleted", Qt::QueuedConnection,
    Q_ARG(int, stepIndex),
    Q_ARG(bool, success));
});
```

### 3. Sequence Execution (SequenceEngine::ExecuteStep)

When a step marker is encountered during execution, callbacks are invoked:

```cpp
case ISequenceStep::Type::STEP_MARKER: {
  StepMarker* marker = static_cast<StepMarker*>(step);

  // Complete the previous step if there was one
  if (m_currentStepIndex >= 0 && m_stepCompletedCallback) {
    m_stepCompletedCallback(m_currentStepIndex, true);
  }

  // Notify subscribers that a new step is starting
  m_currentStepIndex = marker->stepIndex;
  if (m_stepStartedCallback) {
    m_stepStartedCallback(marker->stepIndex, marker->stepName, marker->stepType);
  }

  LogInfo(">>> Step " + std::to_string(marker->stepIndex + 1) + ": " + marker->stepName);
  break;
}
```

### 4. UI Update (RecipePage)

RecipePage connects to RecipeWorker signals and updates the UI:

```cpp
// Signal connections (in onRunRecipe)
connect(m_recipeWorker, &RecipeWorker::stepExecutionStarted,
  this, &RecipePage::onStepExecutionStarted);
connect(m_recipeWorker, &RecipeWorker::stepExecutionCompleted,
  this, &RecipePage::onStepExecutionCompleted);

// Slot handler
void RecipePage::onStepExecutionStarted(int stepIndex, QString stepName, QString stepType)
{
  highlightExecutingStep(stepIndex);
}
```

### 5. Step Highlighting (RecipePage::highlightExecutingStep)

Maps enabled step index to tree item and applies visual highlighting:

```cpp
void RecipePage::highlightExecutingStep(int stepIndex)
{
  // Map enabled step index to actual tree item index
  int enabledCount = 0;
  int targetTreeIndex = -1;

  for (int i = 0; i < m_recipeManager->getStepCount(); ++i) {
    ProcessInstance* step = m_recipeManager->getStep(i);
    if (step->enabled) {
      if (enabledCount == stepIndex) {
        targetTreeIndex = i;
        break;
      }
      enabledCount++;
    }
  }

  // Apply highlighting to target tree item
  // - Bold yellow text for executing step
  // - Normal text for other steps
}
```

## Key Points

1. **Thread Safety**: `QMetaObject::invokeMethod` with `Qt::QueuedConnection` ensures thread-safe communication from the worker thread to the UI thread.

2. **Step Index Mapping**: The `stepIndex` in callbacks refers to enabled steps only (0-based). The UI must map this to the actual tree item index which includes disabled steps.

3. **Completion Tracking**: The previous step is marked as completed when the next step marker is encountered, or when the sequence finishes.

4. **Cleanup**: Highlighting is cleared when:
   - Execution completes (`onExecutionCompleted`)
   - Execution is stopped (`onExecutionStopped`)

## Files Involved

| File | Role |
|------|------|
| `SequenceEngine.h` | Defines `StepMarker`, callbacks, and member variables |
| `SequenceEngine.cpp` | Implements callback setters and `STEP_MARKER` execution |
| `SequenceBuilder.h/cpp` | Implements `AddStepMarker()` |
| `RecipeWorker.h` | Declares `stepExecutionStarted/Completed` signals |
| `RecipeWorker.cpp` | Inserts step markers and sets up callback forwarding |
| `RecipePage.h` | Declares slot handlers and highlight methods |
| `RecipePage.cpp` | Connects signals and implements highlighting |
