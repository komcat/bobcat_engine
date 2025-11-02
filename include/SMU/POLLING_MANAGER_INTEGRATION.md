# SMU Polling Manager - Integration Guide

## Architecture Overview

**Before (BROKEN):**
```
Recipe Worker Thread → Keithley6482Adapter::StartPolling() → QTimer (main thread)
❌ ERROR: QObject::startTimer: Timers cannot be started from another thread
```

**After (FIXED):**
```
Recipe/UI Thread → SMUPollingManager::StartPolling() → std::thread worker
✅ Thread-safe, no Qt timer conflicts
```

## Benefits

✅ **Thread-safe** - Can be called from any thread (recipe worker, UI, etc.)
✅ **Centralized state** - One source of truth for polling status
✅ **No Qt conflicts** - Uses `std::thread`, not `QTimer`
✅ **UI sync** - Signals automatically update UI state
✅ **Clean separation** - Polling logic independent of device implementation

---

## Integration Steps

### Step 1: Add SMUPollingManager to ApplicationContext

**File:** `ApplicationContext.h`

```cpp
#include "SMUPollingManager.h"

struct ApplicationContext {
  // ... existing fields ...
  SMUManager* smuManager = nullptr;
  SMUPollingManager* smuPollingManager = nullptr;  // ✅ ADD THIS
  // ... rest ...
};
```

### Step 2: Create and Initialize in Application.cpp

**File:** `Application.cpp` (in `initializeSMUSystem()`)

```cpp
// After creating SMUManager
m_smuManager = std::make_unique<SMUManager>(this);
m_context.smuManager = m_smuManager.get();

// ✅ ADD: Create polling manager
m_smuPollingManager = std::make_unique<SMUPollingManager>(this);
m_context.smuPollingManager = m_smuPollingManager.get();

// ✅ ADD: Connect polling manager to DataStore
connect(m_smuPollingManager.get(), &SMUPollingManager::measurementReady,
        this, [this](QString deviceName, int channel, double value, QString timestamp) {
    QString channelName = QString("SMU_%1_Ch%2").arg(deviceName).arg(channel);
    m_context.dataStore->SetValue(channelName.toStdString(), static_cast<float>(value));

    // Optional debug logging
    static int debugCounter = 0;
    if (debugCounter++ < 5 || debugCounter % 10 == 0) {
      std::cout << "[SMU→DataStore] " << channelName.toStdString()
                << " = " << value << " A (count=" << debugCounter << ")" << std::endl;
    }
});

std::cout << "[Application] SMU Polling Manager initialized" << std::endl;
```

**File:** `Application.h`

```cpp
#include "SMUPollingManager.h"

class Application {
private:
  std::unique_ptr<SMUManager> m_smuManager;
  std::unique_ptr<SMUPollingManager> m_smuPollingManager;  // ✅ ADD THIS
};
```

### Step 3: Update SMUCommandHandler

**File:** `SMUCommandHandler.cpp`

Replace the StartPolling implementation:

```cpp
#include "SMUPollingManager.h"

bool SMUCommandHandler::ExecuteCommand(/*...*/)
{
  SMUManager* smuManager = static_cast<SMUManager*>(manager);

  // ✅ NEW: Get polling manager from context
  SMUPollingManager* pollingManager = m_context->smuPollingManager;
  if (!pollingManager) {
    errorMessage = "SMU Polling Manager not available";
    return false;
  }

  if (methodName == "StartPolling") {
    // Extract parameters...
    std::string mode = modeIt->second;
    int intervalMs = 100;
    int channel = 1;
    // ... parse params ...

    // Get device
    auto device = smuManager->GetDevice(deviceName);
    if (!device) {
      errorMessage = "SMU device not found: " + deviceName;
      return false;
    }

    // Check connection
    if (!device->IsConnected()) {
      errorMessage = "SMU device not connected: " + deviceName;
      return false;
    }

    // Configure mode
    auto keithley = std::dynamic_pointer_cast<Keithley6482Adapter>(device);
    if (keithley) {
      Keithley6482Adapter::MeasurementMode measMode = /* parse mode */;
      if (!keithley->ConfigureMeasurementMode(measMode)) {
        errorMessage = "Failed to configure measurement mode";
        return false;
      }
      keithley->SelectChannel(channel);
    }

    // ✅ NEW: Use polling manager (thread-safe!)
    if (!pollingManager->StartPolling(deviceName, device, channel, intervalMs)) {
      errorMessage = "Failed to start polling";
      return false;
    }

    if (m_logger) {
      m_logger->Info("Started polling on " + deviceName, "SMUHandler");
    }
    return true;
  }

  else if (methodName == "StopPolling") {
    // ✅ NEW: Use polling manager
    pollingManager->StopPolling(deviceName);

    // Store sample count in context
    if (context) {
      uint64_t samples = pollingManager->GetSampleCount(deviceName);
      context->SetValue(deviceName + ".sampleCount", static_cast<float>(samples),
                       deviceName, methodName);
    }
    return true;
  }
}
```

### Step 4: Update SMUCommandHandler.h

**File:** `SMUCommandHandler.h`

```cpp
#include "ICommandHandler.h"

class SMUCommandHandler : public ICommandHandler
{
public:
  // ✅ ADD: Need context to access polling manager
  void SetContext(ApplicationContext* context) { m_context = context; }

  bool ExecuteCommand(/*...*/) override;
  std::string GetManagerType() const override { return "smu"; }

private:
  ApplicationContext* m_context = nullptr;  // ✅ ADD THIS
};
```

### Step 5: Set Context in SequenceEngine

**File:** `SequenceEngine.cpp` (constructor)

```cpp
SequenceEngine::SequenceEngine(ApplicationContext* context)
  : m_context(context)
{
  // Register handlers...
  auto smuHandler = std::make_unique<SMUCommandHandler>();
  smuHandler->SetContext(context);  // ✅ ADD THIS
  m_dispatcher.RegisterHandler("smu", std::move(smuHandler));
}
```

### Step 6: Update SMUControlPage (UI)

**File:** `SMUControlPage.cpp`

Replace direct device polling calls with polling manager:

```cpp
void SMUControlPage::onStartPollingClicked()
{
  if (!m_context || !m_context->smuPollingManager) return;
  if (!m_selectedDevice) return;

  int intervalMs = /* get from UI */;
  int channel = m_selectedChannel;

  // ✅ Use polling manager instead of device->StartPolling()
  if (m_context->smuPollingManager->StartPolling(
        m_selectedDevice->GetName(),
        m_selectedDevice,
        channel,
        intervalMs)) {
    updatePollingUI();  // Update button states
  }
}

void SMUControlPage::onStopPollingClicked()
{
  if (!m_context || !m_context->smuPollingManager) return;
  if (!m_selectedDevice) return;

  // ✅ Use polling manager
  m_context->smuPollingManager->StopPolling(m_selectedDevice->GetName());
  updatePollingUI();
}

// ✅ Connect to polling state signal
void SMUControlPage::onDeviceSelected(const QString& deviceName)
{
  // ... existing code ...

  // Connect to polling manager signals
  connect(m_context->smuPollingManager, &SMUPollingManager::pollingStateChanged,
          this, [this](QString name, bool isPolling) {
    if (name == m_selectedDevice->GetName()) {
      updatePollingUI();
    }
  });
}

void SMUControlPage::updatePollingUI()
{
  if (!m_context || !m_context->smuPollingManager || !m_selectedDevice) return;

  bool isPolling = m_context->smuPollingManager->IsPolling(m_selectedDevice->GetName());

  // ✅ Update button states based on polling status
  m_startPollingButton->setEnabled(!isPolling);
  m_stopPollingButton->setEnabled(isPolling);
  m_pollingStatusLabel->setText(isPolling ? "Status: Running" : "Status: Stopped");

  if (isPolling) {
    uint64_t samples = m_context->smuPollingManager->GetSampleCount(m_selectedDevice->GetName());
    m_sampleCountLabel->setText(QString("Samples: %1").arg(samples));
  }
}
```

---

## Testing

### Test 1: Recipe Command
```
1. Open Recipe page
2. Add "SMU Start Polling" step
3. Run recipe
4. Check console: Should see "[SMUPollingManager] Starting polling..."
5. Check Data Store page: Should see SMU_Keithley_6482_Ch1 updating
6. ✅ NO threading errors!
```

### Test 2: UI Control
```
1. Open Hardware → SMU page
2. Click "Start Polling"
3. Check button states: Start disabled, Stop enabled
4. Status should show "Running"
5. Switch to Recipe page, run recipe
6. ✅ Recipe should stop UI polling and start its own
```

### Test 3: XCAN Integration
```
1. Create recipe: SMU Start Polling → Wait 300ms → XCAN → SMU Stop Polling
2. Run recipe
3. XCAN should receive measurements
4. ✅ No "Measurement timeout" error
```

---

## Migration Notes

### What Gets Removed:
- ❌ `Keithley6482Adapter::StartPolling()` - polling logic stays in adapter
- ❌ Direct `QTimer` usage for polling in device classes
- ❌ `QMetaObject::invokeMethod` hacks in command handlers

### What Gets Added:
- ✅ `SMUPollingManager` - centralized polling control
- ✅ `std::thread` workers - no Qt timer conflicts
- ✅ Signals for UI synchronization
- ✅ Thread-safe command interface

### Backward Compatibility:
- Keep `Keithley6482Adapter::ReadChannel()` - still used for single readings
- Keep `SelectChannel()`, `ConfigureMeasurementMode()` - still needed
- Polling functionality moves to manager, but device interface unchanged

---

## Summary

This architecture properly separates concerns:

```
┌─────────────────────────────────────────┐
│     Recipe Commands / UI Controls        │
│     (Any thread, any time)              │
└────────────────┬────────────────────────┘
                 │ Thread-safe API
                 ▼
┌─────────────────────────────────────────┐
│       SMUPollingManager                  │
│   - State management                     │
│   - Worker thread coordination          │
│   - Signal emission                      │
└──┬──────────────────────────────────┬───┘
   │                                  │
   │ std::thread                      │ Qt signals
   │ workers                          │
   ▼                                  ▼
┌──────────────┐              ┌──────────────┐
│ Device Read  │              │  DataStore   │
│ (blocking)   │              │  UI Updates  │
└──────────────┘              └──────────────┘
```

**Result:** Clean, thread-safe, maintainable polling architecture! 🎉
