// SequenceEngine.cpp
#include "SequenceEngine.h"
#include "LogManager.h"
#include "unified_motion_manager.h"
#include "EziIOManager.h"
#include "PneumaticManager.h"
#include "SystemCommandHandler.h"
#include "CameraManager.h"
#include "SystemCommandHandler.h"
#include "MotionCommandHandler.h"
#include "TECCommandHandler.h"
#include "LightingCommandHandler.h"
#include "DataStoreCommandHandler.h"
#include "PSUCommandHandler.h"
#include "SMUCommandHandler.h"  // ✅ ADDED
#include "DUTCommandHandler.h"
#include "SequenceContextCommandHandler.h"
#include "CalibrationCommandHandler.h"
#include "AuxWindowCommandHandler.h"
#include "RelativePositionCommandHandler.h"
#include "CoordinateAlignCommandHandler.h"
#include "AccuracyTrainerCommandHandler.h"
#include "CameraOffsetCommandHandler.h"
#include "PlanarAdjustmentCommandHandler.h"
#include "ZOffsetCommandHandler.h"
#include "CustomServerCommandHandler.h"
#include "SequenceContext.h"
#include "AuxWindowManager.h"
#include "TECManager.h"
#include "PowerSupplyManager.h"
#include "SMUManager.h"  // ✅ ADDED
#include "lighting/LightingManager.h"
#include "IOCommandHandler.h"
#include "PneumaticCommandHandler.h"
#include "CameraCommandHandler.h"
#include "safety/SafetyWatchManager.h"
#include "safety/SafetyEventLog.h"
#include "GdiWindow/GdiCameraWindowManager.h"
#include "customserver/BeamServerClient.h"
#ifdef HAS_OPENCV
#include "VisionCommandHandler.h"
#include "vision/VisionResult.h"
#endif
#include "SequenceBuilder.h"
#include <iostream>
#include <cmath>
#include <chrono>

//=============================================================================
// SEQUENCE ENGINE IMPLEMENTATION
//=============================================================================

SequenceEngine::SequenceEngine(ApplicationContext* context)
  : m_context(context)
  , m_logger(nullptr)
  , m_valid(true)  // Initialize as valid, set to false on error
  , m_systemHandler(nullptr)
  , m_stopRequested(false)  // ✅ Initialize stop flag
  , m_retraceEditMode(false)  // ✅ Initialize retrace edit mode
  , m_stepStartedCallback(nullptr)
  , m_stepCompletedCallback(nullptr)
  , m_actionStartedCallback(nullptr)
  , m_currentStepIndex(-1)
  , m_currentActionIndex(0)
  , m_breakpointCallback(nullptr)
  , m_stepByStepMode(false)
  , m_continueRequested(false)
{
  if (!m_context) {
    std::cerr << "[SequenceEngine ERROR] ApplicationContext is null - initialization failed" << std::endl;
    m_valid = false;
    return;
  }

  // Register all command handlers
  // ✅ UPDATED: Set DataStore, ProductPositionManager, and DeviceLinkManager on MotionCommandHandler
  auto motionHandler = std::make_unique<MotionCommandHandler>();
  motionHandler->SetDataStore(m_context->dataStore);
  motionHandler->SetProductPositionManager(m_context->productPositionManager);
  motionHandler->SetDeviceLinkManager(m_context->deviceLinkManager);
  m_motionHandler = motionHandler.get();  // ✅ Store raw pointer before move
  m_dispatcher.RegisterHandler("motion", std::move(motionHandler));

  m_dispatcher.RegisterHandler("io", std::make_unique<IOCommandHandler>());
  m_dispatcher.RegisterHandler("pneumatic", std::make_unique<PneumaticCommandHandler>());

  // ✅ UPDATED: Set context on CameraCommandHandler to access currentDUT
  auto cameraHandler = std::make_unique<CameraCommandHandler>();
  cameraHandler->SetContext(m_context);
  m_dispatcher.RegisterHandler("camera", std::move(cameraHandler));

  m_dispatcher.RegisterHandler("tec", std::make_unique<TECCommandHandler>());
  m_dispatcher.RegisterHandler("lighting", std::make_unique<LightingCommandHandler>());
  m_dispatcher.RegisterHandler("psu", std::make_unique<PSUCommandHandler>());

  // ✅ UPDATED: Set context on SMU handler to access polling manager
  auto smuHandler = std::make_unique<SMUCommandHandler>();
  smuHandler->SetContext(m_context);
  m_dispatcher.RegisterHandler("smu", std::move(smuHandler));

  m_dispatcher.RegisterHandler("datastore", std::make_unique<DataStoreCommandHandler>(m_context->dataStore));
  m_dispatcher.RegisterHandler("dut", std::make_unique<DUTCommandHandler>(m_context));
  m_dispatcher.RegisterHandler("context", std::make_unique<SequenceContextCommandHandler>());
  m_dispatcher.RegisterHandler("calibration", std::make_unique<CalibrationCommandHandler>(m_context));
  m_dispatcher.RegisterHandler("auxwindow", std::make_unique<AuxWindowCommandHandler>(m_context->auxWindowManager));

  // Register relative position handler for parent-child position tracking
  auto relPosHandler = std::make_unique<RelativePositionCommandHandler>();
  relPosHandler->SetMotionManager(m_context->motionManager);
  m_dispatcher.RegisterHandler("relpos", std::move(relPosHandler));

  // Register coordinate alignment handler for 3-point vision-based alignment
  auto coordAlignHandler = std::make_unique<CoordinateAlignCommandHandler>();
  coordAlignHandler->SetContext(m_context);
  coordAlignHandler->SetConfigPath("config/");
  // Note: Files are read at runtime when needed (not loaded into memory at startup)
  m_dispatcher.RegisterHandler("coordalign", std::move(coordAlignHandler));

  // Register accuracy trainer handler for headless alignment
  auto atHandler = std::make_unique<AccuracyTrainerCommandHandler>();
  atHandler->SetContext(m_context);
  m_accuracyTrainerHandler = atHandler.get();
  m_dispatcher.RegisterHandler("accuracytrainer", std::move(atHandler));

  // Register camera offset handler for low-mag/high-mag switching
  auto camOffsetHandler = std::make_unique<CameraOffsetCommandHandler>();
  camOffsetHandler->SetContext(m_context);
  m_dispatcher.RegisterHandler("cameraoffset", std::move(camOffsetHandler));

  // Register planar adjustment handler for tilt correction
  auto planarHandler = std::make_unique<PlanarAdjustmentCommandHandler>();
  planarHandler->SetContext(m_context);
  m_planarAdjustmentHandler = planarHandler.get();
  m_dispatcher.RegisterHandler("planaradjustment", std::move(planarHandler));

  // Register Z offset handler for Z offset correction
  auto zOffsetHandler = std::make_unique<ZOffsetCommandHandler>();
  zOffsetHandler->SetContext(m_context);
  m_zOffsetHandler = zOffsetHandler.get();
  m_dispatcher.RegisterHandler("zoffset", std::move(zOffsetHandler));

  // Register custom server handler for beam detection
  auto customServerHandler = std::make_unique<CustomServerCommandHandler>();
  customServerHandler->SetContext(m_context);
  m_dispatcher.RegisterHandler("customserver", std::move(customServerHandler));

#ifdef HAS_OPENCV
  // Register vision command handler for vision profile execution
  auto visionHandler = std::make_unique<VisionCommandHandler>();
  visionHandler->SetContext(m_context);
  m_dispatcher.RegisterHandler("vision", std::move(visionHandler));
#endif

  // Register system handler
  auto systemHandler = std::make_unique<SystemCommandHandler>();
  m_systemHandler = systemHandler.get();
  m_dispatcher.RegisterHandler("system", std::move(systemHandler));

  // Subscribe to safety watch - auto-stop on critical safety flag
  if (m_context->safetyWatchManager) {
    QObject::connect(m_context->safetyWatchManager, &SafetyWatchManager::safetyTriggered,
      [this](QString title, QString message, bool critical) {
        Q_UNUSED(title);
        Q_UNUSED(message);
        if (critical) {
          std::cout << "[SequenceEngine] Critical safety flag - requesting stop" << std::endl;
          RequestStop();
        }
      });
    std::cout << "[SequenceEngine] Subscribed to SafetyWatchManager" << std::endl;
  }
}

SequenceEngine::~SequenceEngine()
{
  JoinZombieThreads();
  ClearLog();
}

void SequenceEngine::SetLogger(LogManager* logger)
{
  m_logger = logger;
  m_dispatcher.SetLogger(logger);
  LogInfo("Logger configured");
}

// ✅ NEW: Stop control methods
void SequenceEngine::RequestStop()
{
  m_stopRequested.store(true);
  LogWarning("Stop requested - will halt at next checkpoint");
}

void SequenceEngine::ClearStopFlag()
{
  m_stopRequested.store(false);
}

void SequenceEngine::SetActionTimeoutMs(int timeoutMs)
{
  m_actionTimeoutMs = (timeoutMs > 0) ? timeoutMs : 60000;
}

void SequenceEngine::BuildDeviceRegistry()
{
  LogInfo("Building device registry...");

  m_deviceToManagerMap.clear();
  m_deviceToTypeMap.clear();

  // Scan motion devices
  if (m_context->motionManager) {
    auto devices = m_context->motionManager->GetAllDeviceNames();
    for (const auto& name : devices) {
      m_deviceToManagerMap[name] = m_context->motionManager;
      m_deviceToTypeMap[name] = "motion";
    }
    LogInfo("Registered " + std::to_string(devices.size()) + " motion devices");
  }

  // Scan IO devices
  if (m_context->ioManager) {
    auto devices = m_context->ioManager->GetAllDeviceNames();
    for (const auto& name : devices) {
      m_deviceToManagerMap[name] = m_context->ioManager;
      m_deviceToTypeMap[name] = "io";
    }
    LogInfo("Registered " + std::to_string(devices.size()) + " IO devices");
  }

  // Scan pneumatic slides
  if (m_context->pneumaticManager) {
    auto slides = m_context->pneumaticManager->GetAllSlideNames();
    for (const auto& name : slides) {
      m_deviceToManagerMap[name] = m_context->pneumaticManager;
      m_deviceToTypeMap[name] = "pneumatic";
    }
    LogInfo("Registered " + std::to_string(slides.size()) + " pneumatic slides");
  }

  // Camera devices
  if (m_context->cameraManager) {
    auto cameras = m_context->cameraManager->GetCameraIds();
    for (const auto& name : cameras) {
      m_deviceToManagerMap[name] = m_context->cameraManager;
      m_deviceToTypeMap[name] = "camera";
    }
    LogInfo("Registered " + std::to_string(cameras.size()) + " camera devices");
  }

  // TEC devices
  if (m_context->tecManager) {
    auto devices = m_context->tecManager->GetAllDeviceNames();
    for (const auto& name : devices) {
      m_deviceToManagerMap[name] = m_context->tecManager;
      m_deviceToTypeMap[name] = "tec";
    }
    LogInfo("Registered " + std::to_string(devices.size()) + " TEC devices");
  }

  // PSU devices
  if (m_context->powerSupplyManager) {
    auto devices = m_context->powerSupplyManager->getDeviceNames();
    for (const auto& name : devices) {
      m_deviceToManagerMap[name] = m_context->powerSupplyManager;
      m_deviceToTypeMap[name] = "psu";
    }
    LogInfo("Registered " + std::to_string(devices.size()) + " PSU devices");
  }

  // SMU devices
  if (m_context->smuManager) {
    auto devices = m_context->smuManager->GetDeviceNames();
    for (const auto& name : devices) {
      m_deviceToManagerMap[name] = m_context->smuManager;
      m_deviceToTypeMap[name] = "smu";
    }
    LogInfo("Registered " + std::to_string(devices.size()) + " SMU devices");
  }

  // Lighting controllers (register by device_name from config)
  if (m_context->lightingManager) {
    auto deviceIds = m_context->lightingManager->GetControllerIds();
    for (const auto& deviceId : deviceIds) {
      const auto* config = m_context->lightingManager->GetControllerConfig(deviceId);
      if (config) {
        // Register by device_name (e.g., "light-main")
        m_deviceToManagerMap[config->deviceName] = m_context->lightingManager;
        m_deviceToTypeMap[config->deviceName] = "lighting";
      }
    }
    LogInfo("Registered " + std::to_string(deviceIds.size()) + " lighting controllers");
  }

  // Beam server client (custom server)
  if (m_context->beamServerClient) {
    m_deviceToManagerMap["beamserver"] = m_context->beamServerClient;
    m_deviceToTypeMap["beamserver"] = "customserver";
    LogInfo("Registered beamserver device");
  }

  // Register DataStore as virtual device
  if (m_context->dataStore) {
    m_deviceToManagerMap["datastore"] = m_context->dataStore;
    m_deviceToTypeMap["datastore"] = "datastore";
    LogInfo("Registered datastore");
  }

  // Register SequenceContext handler (special case - context passed at runtime)
  m_deviceToManagerMap["context"] = nullptr;
  m_deviceToTypeMap["context"] = "context";
  LogInfo("Registered context command handler");

  // System commands (special case - no manager needed, nullptr is OK)
  m_deviceToManagerMap["system"] = nullptr;
  m_deviceToTypeMap["system"] = "system";
  LogInfo("Registered system command handler");

  // Calibration commands (special case - uses ApplicationContext, nullptr for manager)
  m_deviceToManagerMap["calibration"] = nullptr;
  m_deviceToTypeMap["calibration"] = "calibration";
  LogInfo("Registered calibration command handler");

  // DUT commands (special case - uses ApplicationContext, nullptr for manager)
  m_deviceToManagerMap["dut"] = nullptr;
  m_deviceToTypeMap["dut"] = "dut";
  LogInfo("Registered DUT command handler");

  // AuxWindow commands (special case - controls auxiliary display window)
  if (m_context->auxWindowManager) {
    m_deviceToManagerMap["auxwindow"] = m_context->auxWindowManager;
    m_deviceToTypeMap["auxwindow"] = "auxwindow";
    LogInfo("Registered auxiliary window command handler");
  }

  // Vision commands (special case - uses ApplicationContext internally, nullptr for manager)
  m_deviceToManagerMap["vision"] = nullptr;
  m_deviceToTypeMap["vision"] = "vision";
  LogInfo("Registered vision command handler");

  // Relative position commands (special case - uses motion manager internally)
  m_deviceToManagerMap["relpos"] = nullptr;
  m_deviceToTypeMap["relpos"] = "relpos";
  LogInfo("Registered relative position command handler");

  // Coordinate alignment commands (special case - uses ApplicationContext internally)
  m_deviceToManagerMap["coordalign"] = nullptr;
  m_deviceToTypeMap["coordalign"] = "coordalign";
  LogInfo("Registered coordinate alignment command handler");

  // Accuracy trainer commands (virtual device - uses ApplicationContext internally)
  m_deviceToManagerMap["accuracytrainer"] = nullptr;
  m_deviceToTypeMap["accuracytrainer"] = "accuracytrainer";
  LogInfo("Registered accuracy trainer command handler");

  // Camera offset commands (virtual device - uses ApplicationContext internally)
  m_deviceToManagerMap["cameraoffset"] = nullptr;
  m_deviceToTypeMap["cameraoffset"] = "cameraoffset";
  LogInfo("Registered camera offset command handler");

  // Planar adjustment commands (virtual device - uses ApplicationContext internally)
  m_deviceToManagerMap["planaradjustment"] = nullptr;
  m_deviceToTypeMap["planaradjustment"] = "planaradjustment";
  LogInfo("Registered planar adjustment command handler");

  // Z offset commands (virtual device - uses ApplicationContext internally)
  m_deviceToManagerMap["zoffset"] = nullptr;
  m_deviceToTypeMap["zoffset"] = "zoffset";
  LogInfo("Registered z offset command handler");

  // Parallel motion commands (special case - uses motion manager for multiple devices)
  if (m_context->motionManager) {
    m_deviceToManagerMap["parallel"] = m_context->motionManager;
    m_deviceToTypeMap["parallel"] = "motion";
    LogInfo("Registered parallel motion command handler");
  }

  LogInfo("Device registry built: " + std::to_string(m_deviceToManagerMap.size()) + " devices");
}

SequenceBuilder* SequenceEngine::CreateSequence(const std::string& name)
{
  return new SequenceBuilder(name, this);
}

void* SequenceEngine::ResolveManager(const std::string& deviceName)
{
  auto it = m_deviceToManagerMap.find(deviceName);
  return (it != m_deviceToManagerMap.end()) ? it->second : nullptr;
}

std::string SequenceEngine::ResolveManagerType(const std::string& deviceName)
{
  auto it = m_deviceToTypeMap.find(deviceName);
  return (it != m_deviceToTypeMap.end()) ? it->second : "";
}

bool SequenceEngine::IsDeviceConnected(const std::string& deviceName) const
{
  auto typeIt = m_deviceToTypeMap.find(deviceName);
  if (typeIt == m_deviceToTypeMap.end()) return false;

  const std::string& type = typeIt->second;

  if (type == "motion" && m_context->motionManager) {
    auto status = m_context->motionManager->GetConnectionStatus();
    auto it = status.find(deviceName);
    return (it != status.end()) && it->second;
  }
  if (type == "io" && m_context->ioManager) {
    auto* device = m_context->ioManager->GetDevice(deviceName);
    return device && device->IsConnected();
  }
  if (type == "camera" && m_context->cameraManager) {
    auto status = m_context->cameraManager->GetCameraStatus(deviceName);
    return status.connected;
  }
  if (type == "tec" && m_context->tecManager) {
    auto* device = m_context->tecManager->GetDevice(deviceName);
    return device && device->IsConnected();
  }
  if (type == "psu" && m_context->powerSupplyManager) {
    auto psu = m_context->powerSupplyManager->getPowerSupply(deviceName);
    return psu && psu->isConnected();
  }
  if (type == "smu" && m_context->smuManager) {
    auto smu = m_context->smuManager->GetDevice(deviceName);
    return smu && smu->IsConnected();
  }
  if (type == "pneumatic" && m_context->pneumaticManager) {
    auto* slide = m_context->pneumaticManager->GetSlide(deviceName);
    return slide && slide->IsIOConnected();
  }
  if (type == "lighting" && m_context->lightingManager) {
    // Lighting uses deviceId internally; map deviceName -> deviceId
    for (const auto& id : m_context->lightingManager->GetControllerIds()) {
      const auto* cfg = m_context->lightingManager->GetControllerConfig(id);
      if ((cfg && cfg->deviceName == deviceName) || id == deviceName) {
        return m_context->lightingManager->IsConnected(id);
      }
    }
    return false;
  }
  if (type == "customserver" && m_context->beamServerClient) {
    return m_context->beamServerClient->IsConnected();
  }

  // Virtual devices (system, context, etc.) are always "connected"
  return true;
}

void SequenceEngine::SetUserPromptCallback(UserPromptCallback callback)
{
  if (m_systemHandler) {
    m_systemHandler->SetUserPromptCallback(callback);
  }
}

void SequenceEngine::SetUserNotificationCallback(UserNotificationCallback callback)
{
  if (m_systemHandler) {
    m_systemHandler->SetUserNotificationCallback(callback);
  }
}

void SequenceEngine::SetManualAdjustCallback(ManualAdjustCallback callback)
{
  if (m_systemHandler) {
    m_systemHandler->SetManualAdjustCallback(callback);
  }
  if (m_planarAdjustmentHandler) {
    m_planarAdjustmentHandler->SetManualAdjustCallback(callback);
  }
  if (m_zOffsetHandler) {
    m_zOffsetHandler->SetManualAdjustCallback(callback);
  }
}

void SequenceEngine::SetUserInputCallback(UserInputCallback callback)
{
  if (m_systemHandler) {
    m_systemHandler->SetUserInputCallback(callback);
  }
}

void SequenceEngine::SetManualAlignPointCallback(ManualAlignPointCallback callback)
{
  if (m_systemHandler) {
    m_systemHandler->SetManualAlignPointCallback(callback);
  }
  if (m_accuracyTrainerHandler) {
    m_accuracyTrainerHandler->SetManualAlignPointCallback(callback);
  }
}

void SequenceEngine::SetStepStartedCallback(StepStartedCallback callback)
{
  m_stepStartedCallback = callback;
}

void SequenceEngine::SetStepCompletedCallback(StepCompletedCallback callback)
{
  m_stepCompletedCallback = callback;
}

void SequenceEngine::SetActionStartedCallback(ActionStartedCallback callback)
{
  m_actionStartedCallback = callback;
}

void SequenceEngine::SetVisionResultCallback(VisionResultCallback callback)
{
  m_visionResultCallback = callback;
}

void SequenceEngine::BuildActionList(SequenceBuilder* sequence)
{
  m_actionList.clear();
  if (sequence) {
    FlattenSteps(sequence->GetSteps());
  }
  std::cout << "[SequenceEngine] Built action list with " << m_actionList.size() << " actions" << std::endl;
}

void SequenceEngine::FlattenSteps(const std::vector<std::shared_ptr<ISequenceStep>>& steps)
{
  for (const auto& step : steps) {
    if (!step) continue;

    switch (step->GetType()) {
    case ISequenceStep::Type::ACTION: {
      Action* action = static_cast<Action*>(step.get());
      m_actionList.emplace_back(action->deviceName, action->methodName);
      break;
    }
    case ISequenceStep::Type::LOOP: {
      LoopBlock* loop = static_cast<LoopBlock*>(step.get());
      // For loops, we add actions for each iteration (approximate)
      // This gives us a rough look-ahead for loops
      for (int i = 0; i < loop->count; ++i) {
        FlattenSteps(loop->actions);
      }
      break;
    }
    case ISequenceStep::Type::CONDITIONAL: {
      ConditionalBlock* cond = static_cast<ConditionalBlock*>(step.get());
      // For conditionals, add both branches (we don't know which will execute)
      // This means look-ahead may not be 100% accurate for conditionals
      FlattenSteps(cond->thenActions);
      FlattenSteps(cond->elseActions);
      break;
    }
    case ISequenceStep::Type::STEP_MARKER:
      // Step markers don't add actions
      break;
    }
  }
}

void SequenceEngine::SetBreakpointCallback(BreakpointCallback callback)
{
  m_breakpointCallback = callback;
}

void SequenceEngine::SetBreakpoints(const std::vector<bool>& breakpoints)
{
  m_breakpoints = breakpoints;
}

void SequenceEngine::SetStepByStepMode(bool enabled)
{
  m_stepByStepMode.store(enabled);
  if (enabled) {
    m_continueRequested.store(false);
  }
}

void SequenceEngine::RequestContinue()
{
  std::lock_guard<std::mutex> lock(m_pauseMutex);
  m_continueRequested.store(true);
  m_pauseCondition.notify_all();
}

void SequenceEngine::RequestRunToEnd()
{
  std::lock_guard<std::mutex> lock(m_pauseMutex);
  m_stepByStepMode.store(false);
  m_continueRequested.store(true);
  m_pauseCondition.notify_all();
}

// ============================================================================
// RETRACE EDIT MODE IMPLEMENTATION
// ============================================================================

void SequenceEngine::SetRetraceEditMode(bool enabled)
{
  m_retraceEditMode.store(enabled);

  // Propagate to MotionCommandHandler
  if (m_motionHandler) {
    m_motionHandler->SetRetraceEditMode(enabled);
    if (enabled) {
      m_motionHandler->ResetRetraceStatistics();
    }
  }

  if (enabled) {
    // Reset statistics when starting a new retrace session
    m_retracePositionsVisited = 0;
    m_retracePositionsSaved = 0;
    LogInfo("Retrace Edit Mode ENABLED - will pause after each named position move");
  }
  else {
    LogInfo("Retrace Edit Mode DISABLED");
  }
}

void SequenceEngine::SetRetraceEditCallback(RetraceEditCallback callback)
{
  m_retraceEditCallback = callback;

  // Convert to MotionCommandHandler callback format and propagate
  if (m_motionHandler && callback) {
    m_motionHandler->SetRetraceEditCallback(
      [callback](const RetraceMotionInfo& info,
                 RetraceCurrentPosition& currentPosition,
                 bool& saveRequested) -> bool {
        return callback(info, currentPosition, saveRequested);
      }
    );
  }
}

bool SequenceEngine::ValidateSequence(SequenceBuilder* sequence, std::vector<std::string>& errors)
{
  if (!sequence) {
    errors.push_back("Sequence is null");
    return false;
  }

  LogInfo("🔍 Validating sequence: " + sequence->GetName());

  for (const auto& step : sequence->GetSteps()) {
    if (!ValidateStep(step.get(), errors)) {
      return false;
    }
  }

  if (errors.empty()) {
    LogInfo("✅ Validation passed");
    return true;
  }
  else {
    LogWarning("❌ Validation failed with " + std::to_string(errors.size()) + " errors");
    return false;
  }
}

bool SequenceEngine::ValidateStep(ISequenceStep* step, std::vector<std::string>& errors)
{
  if (!step) return false;

  switch (step->GetType()) {
  case ISequenceStep::Type::ACTION: {
    Action* action = static_cast<Action*>(step);

    // Check if device type exists in registry
    std::string managerType = ResolveManagerType(action->deviceName);
    if (managerType.empty()) {
      errors.push_back("Device not found: " + action->deviceName);
      return false;
    }

    // If the dispatcher has a handler for this type, it can work with nullptr manager (virtual devices)
    void* manager = ResolveManager(action->deviceName);
    if (!manager && !m_dispatcher.HasHandler(managerType)) {
      errors.push_back("Manager not found for device: " + action->deviceName);
      return false;
    }

    break;
  }

  case ISequenceStep::Type::LOOP: {
    LoopBlock* loop = static_cast<LoopBlock*>(step);

    if (loop->count <= 0) {
      errors.push_back("Loop count must be > 0");
      return false;
    }

    // Validate actions inside loop
    for (const auto& action : loop->actions) {
      if (!ValidateStep(action.get(), errors)) {
        return false;
      }
    }
    break;
  }

  case ISequenceStep::Type::CONDITIONAL: {
    ConditionalBlock* cond = static_cast<ConditionalBlock*>(step);

    // Conditional now evaluates context keys, so just validate branches

    // Validate then branch
    for (const auto& action : cond->thenActions) {
      if (!ValidateStep(action.get(), errors)) {
        return false;
      }
    }

    // Validate else branch
    for (const auto& action : cond->elseActions) {
      if (!ValidateStep(action.get(), errors)) {
        return false;
      }
    }
    break;
  }

  case ISequenceStep::Type::STEP_MARKER: {
    // Step markers are always valid - they're just tracking markers
    break;
  }
  }

  return true;
}

bool SequenceEngine::ExecuteSequence(SequenceBuilder* sequence, SequenceContext* context)
{
  if (!sequence) {
    LogError("Sequence is null");
    return false;
  }

  if (!context) {
    LogError("SequenceContext is null");
    return false;
  }

  // Initialize context
  context->SetSequenceName(sequence->GetName());
  context->SetExecutionStartTime();
  context->ClearAll();

  // ✅ Clear stop flag before execution
  ClearStopFlag();
  m_lastInjectedVisionSeq = 0;
  m_cvInjectEnabled = true;

  // ✅ Set engine stop callback in context for safety-critical stop detection
  context->SetEngineStopCallback([this]() {
    return m_stopRequested.load();
  });

  // Validate first
  std::vector<std::string> errors;
  if (!ValidateSequence(sequence, errors)) {
    for (const auto& err : errors) {
      LogError(err);
    }
    return false;
  }

  LogInfo("Executing sequence: " + sequence->GetName());

  // Log sequence start to safety event log
  if (m_context->safetyEventLog) {
    m_context->safetyEventLog->LogSequenceStart(sequence->GetName());
  }

  // Reset step tracking
  m_currentStepIndex = -1;

  // ✅ Build action list for look-ahead
  BuildActionList(sequence);
  m_currentActionIndex = 0;

  try {
    for (size_t i = 0; i < sequence->GetSteps().size(); ++i) {
      // ✅ CHECK STOP FLAG BEFORE EACH STEP (check both engine and context flags)
      if (m_stopRequested.load() || context->IsStopRequested()) {
        LogWarning("Sequence execution stopped by user request");
        if (m_context->safetyEventLog) {
          m_context->safetyEventLog->LogSequenceStop(sequence->GetName(), false, "user request");
        }
        // Notify failure for current step if any
        if (m_currentStepIndex >= 0 && m_stepCompletedCallback) {
          m_stepCompletedCallback(m_currentStepIndex, false);
        }
        context->SetExecutionEndTime();
        JoinZombieThreads();
        return false;
      }

      context->SetCurrentStep(static_cast<int>(i), "Step_" + std::to_string(i));
      context->IncrementStepCount();

      if (!ExecuteStep(sequence->GetSteps()[i].get(), context)) {
        LogError("Step execution failed");
        if (m_context->safetyEventLog) {
          m_context->safetyEventLog->LogSequenceError(sequence->GetName(), "Step execution failed at index " + std::to_string(i));
        }
        // Notify failure for current step if any
        if (m_currentStepIndex >= 0 && m_stepCompletedCallback) {
          m_stepCompletedCallback(m_currentStepIndex, false);
        }
        context->SetExecutionEndTime();
        JoinZombieThreads();
        return false;
      }
    }

    // Complete the last step if there was one
    if (m_currentStepIndex >= 0 && m_stepCompletedCallback) {
      m_stepCompletedCallback(m_currentStepIndex, true);
    }

    context->SetExecutionEndTime();
    LogInfo("Sequence completed in " +
           std::to_string(context->GetExecutionDuration().count()) + " ms");
    if (m_context->safetyEventLog) {
      m_context->safetyEventLog->LogSequenceStop(sequence->GetName(), true);
    }
    JoinZombieThreads();
    return true;
  }
  catch (const std::exception& e) {
    LogError("Exception during execution: " + std::string(e.what()));
    if (m_context->safetyEventLog) {
      m_context->safetyEventLog->LogSequenceError(sequence->GetName(), std::string("Exception: ") + e.what());
    }
    // Notify failure for current step if any
    if (m_currentStepIndex >= 0 && m_stepCompletedCallback) {
      m_stepCompletedCallback(m_currentStepIndex, false);
    }
    context->SetExecutionEndTime();
    JoinZombieThreads();
    return false;
  }
}

bool SequenceEngine::ExecuteStep(ISequenceStep* step, SequenceContext* context)
{
  if (!step) return false;

  // ✅ CHECK STOP FLAG AT START OF EACH STEP (check both engine and context flags)
  if (m_stopRequested.load() || context->IsStopRequested()) {
    LogWarning("⏹️ Step execution stopped by user request");
    return false;
  }

  switch (step->GetType()) {
  case ISequenceStep::Type::ACTION: {
    Action* action = static_cast<Action*>(step);

    LogInfo("⚙️ Executing: " + action->deviceName + "." + action->methodName);

    // ✅ Notify action started callback with look-ahead info
    if (m_actionStartedCallback) {
      ActionInfo current(action->deviceName, action->methodName);
      ActionInfo next;
      ActionInfo afterNext;

      // Look ahead in the action list
      if (m_currentActionIndex + 1 < static_cast<int>(m_actionList.size())) {
        next = m_actionList[m_currentActionIndex + 1];
      }
      if (m_currentActionIndex + 2 < static_cast<int>(m_actionList.size())) {
        afterNext = m_actionList[m_currentActionIndex + 2];
      }

      m_actionStartedCallback(current, next, afterNext);
      m_currentActionIndex++;
    }

    // Get manager and type
    void* manager = ResolveManager(action->deviceName);
    std::string managerType = ResolveManagerType(action->deviceName);

    if (managerType.empty()) {
      LogError("❓ Unknown device type for: " + action->deviceName);
      return false;
    }

    // If the dispatcher has a handler for this type, it can work with nullptr manager (virtual devices)
    // "parallel" is a virtual coordinator — its sub-commands check their own devices
    if (action->deviceName != "parallel" && !manager && !m_dispatcher.HasHandler(managerType)) {
      LogError("❓ Manager not found for device: " + action->deviceName);
      return false;
    }

    // Pre-check device connectivity for hardware commands
    // Skip for Connect/Disconnect commands — their purpose is to change connectivity state
    if (managerType != "system" && managerType != "context" &&
        managerType != "calibration" && managerType != "dut" &&
        managerType != "vision" && managerType != "relpos" &&
        managerType != "coordalign" && managerType != "datastore" &&
        managerType != "auxwindow" && managerType != "customserver" &&
        action->deviceName != "parallel" &&
        action->methodName != "Connect" && action->methodName != "Disconnect" &&
        action->methodName != "Reset") {
      if (!IsDeviceConnected(action->deviceName)) {
        std::string disconnMsg = "Device not connected: " + action->deviceName;
        LogError(disconnMsg);
        if (m_context->safetyEventLog) {
          m_context->safetyEventLog->LogEvent(SafetySeverity::WARNING, "SequenceEngine",
              "DEVICE_DISCONNECTED", disconnMsg, ErrorCode::NotConnected);
        }
        return false;
      }
    }

    // Inject latest continuous vision result into context before dispatch
    InjectContinuousVisionResult(context);

    // Inject latest beam stream result into context before dispatch
    InjectBeamStreamResult(context);
    InjectBeamStreamResult2(context);

    // Execute command — check for per-action timeout override
    // Actions with timeout=0 (e.g., accuracy trainer with manual callbacks) bypass timeout
    std::string errorMessage;
    auto actionStart = std::chrono::steady_clock::now();

    bool useTimeout = true;
    auto timeoutIt = action->params.find("timeout");
    if (timeoutIt != action->params.end() && timeoutIt->second == "0") {
      useTimeout = false;
      LogInfo("Timeout disabled for: " + action->deviceName + "." + action->methodName);
    }

    // User-interactive commands wait indefinitely — no timeout
    if (action->methodName == "ManualAdjust" ||
        action->methodName == "UserPrompt" ||
        action->methodName == "AskForInput") {
      useTimeout = false;
    }

    bool dispatchOk = false;
    if (useTimeout) {
      dispatchOk = DispatchWithTimeout(managerType, manager, action->deviceName,
        action->methodName, action->params, errorMessage, context);
    } else {
      dispatchOk = m_dispatcher.ExecuteCommand(managerType, manager, action->deviceName,
        action->methodName, action->params, errorMessage, context);
    }

    if (!dispatchOk) {
      // Post-dispatch disconnect diagnostic (TOCTOU: device may have disconnected mid-command)
      if (!IsDeviceConnected(action->deviceName)) {
        std::string disconnMsg = "Device disconnected during command: " +
            action->deviceName + "." + action->methodName;
        LogError(disconnMsg);
        if (m_context->safetyEventLog) {
          m_context->safetyEventLog->LogEvent(SafetySeverity::WARNING, "SequenceEngine",
              "DEVICE_DISCONNECTED_DURING_COMMAND", disconnMsg, ErrorCode::NotConnected);
        }
      } else {
        LogError("Command failed: " + errorMessage);
      }
      return false;
    }

    // Log slow commands that completed within timeout (non-critical warning)
    auto actionDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - actionStart).count();
    if (actionDurationMs > m_actionTimeoutMs / 2) {
      std::string slowMsg = action->deviceName + "." + action->methodName +
          " took " + std::to_string(actionDurationMs) + "ms (limit: " +
          std::to_string(m_actionTimeoutMs) + "ms)";
      LogWarning("Slow command: " + slowMsg);
    }

    break;
  }

  case ISequenceStep::Type::LOOP: {
    LoopBlock* loop = static_cast<LoopBlock*>(step);

    LogInfo("🔄 Starting loop: " + std::to_string(loop->count) + " iterations");

    for (int i = 0; i < loop->count; i++) {
      // ✅ CHECK STOP FLAG BEFORE EACH LOOP ITERATION (check both engine and context flags)
      if (m_stopRequested.load() || context->IsStopRequested()) {
        LogWarning("⏹️ Loop stopped by user request at iteration " + std::to_string(i + 1));
        return false;
      }

      context->PushLoopIteration(i);

      LogInfo("  🔁 Loop iteration " + std::to_string(i + 1) + "/" + std::to_string(loop->count));

      for (const auto& action : loop->actions) {
        if (!ExecuteStep(action.get(), context)) {
          return false;
        }
      }

      context->PopLoopIteration();
    }

    LogInfo("✅ Loop completed");
    break;
  }

  case ISequenceStep::Type::CONDITIONAL: {
    ConditionalBlock* cond = static_cast<ConditionalBlock*>(step);

    bool conditionResult = EvaluateCondition(cond, context);

    LogInfo("Condition [" + cond->contextKey + " " +
           ComparisonToString(cond->comparison) + " " +
           std::to_string(cond->threshold) + "] = " +
           (conditionResult ? "TRUE" : "FALSE"));

    if (conditionResult) {
      LogInfo("  Executing THEN branch");
      for (const auto& action : cond->thenActions) {
        if (!ExecuteStep(action.get(), context)) {
          return false;
        }
      }
    }
    else {
      LogInfo("  Executing ELSE branch");
      for (const auto& action : cond->elseActions) {
        if (!ExecuteStep(action.get(), context)) {
          return false;
        }
      }
    }

    break;
  }

  case ISequenceStep::Type::STEP_MARKER: {
    StepMarker* marker = static_cast<StepMarker*>(step);

    // Complete the previous step if there was one
    if (m_currentStepIndex >= 0 && m_stepCompletedCallback) {
      m_stepCompletedCallback(m_currentStepIndex, true);
    }

    // Check for breakpoint or step-by-step mode BEFORE starting this step
    bool hasBreakpoint = (marker->stepIndex < static_cast<int>(m_breakpoints.size()) && m_breakpoints[marker->stepIndex]);
    bool shouldPause = hasBreakpoint || m_stepByStepMode.load();

    if (shouldPause && !m_stopRequested.load()) {
      LogInfo("⏸️ Paused at step " + std::to_string(marker->stepIndex + 1) + ": " + marker->stepName +
              (hasBreakpoint ? " (breakpoint)" : " (step-by-step)"));

      // Notify via breakpoint callback if set (for UI updates)
      if (m_breakpointCallback) {
        m_breakpointCallback(marker->stepIndex, marker->stepName);
      }

      // Always wait for continue signal (condition_variable)
      std::unique_lock<std::mutex> lock(m_pauseMutex);
      m_continueRequested.store(false);
      m_pauseCondition.wait(lock, [this]() {
        return m_continueRequested.load() || m_stopRequested.load();
      });

      if (m_stopRequested.load()) {
        LogInfo("Execution stopped by user");
        return false;
      }

      LogInfo("▶️ Continuing execution...");
    }

    // Notify subscribers that a new step is starting
    m_currentStepIndex = marker->stepIndex;
    if (m_stepStartedCallback) {
      m_stepStartedCallback(marker->stepIndex, marker->stepName, marker->stepType);
    }

    LogInfo(">>> Step " + std::to_string(marker->stepIndex + 1) + ": " + marker->stepName);
    break;
  }
  }

  return true;
}

bool SequenceEngine::EvaluateCondition(const ConditionalBlock* cond, SequenceContext* context)
{
  if (!cond || !context) return false;

  // Check if key exists
  if (!context->HasValue(cond->contextKey)) {
    if (cond->comparison == ConditionalBlock::ComparisonType::NOT_EXISTS) {
      return true;
    }
    LogWarning("Context key not found: " + cond->contextKey + ", defaulting to FALSE");
    return false;
  }

  if (cond->comparison == ConditionalBlock::ComparisonType::EXISTS) {
    return true;
  }

  float value = context->GetNumeric(cond->contextKey, 0.0f);

  switch (cond->comparison) {
    case ConditionalBlock::ComparisonType::GREATER_THAN:
      return value > cond->threshold;
    case ConditionalBlock::ComparisonType::LESS_THAN:
      return value < cond->threshold;
    case ConditionalBlock::ComparisonType::EQUAL:
      return std::abs(value - cond->threshold) < 0.0001f;
    case ConditionalBlock::ComparisonType::NOT_EQUAL:
      return std::abs(value - cond->threshold) >= 0.0001f;
    case ConditionalBlock::ComparisonType::GREATER_OR_EQUAL:
      return value >= cond->threshold;
    case ConditionalBlock::ComparisonType::LESS_OR_EQUAL:
      return value <= cond->threshold;
    case ConditionalBlock::ComparisonType::IS_TRUE:
      return context->GetBoolean(cond->contextKey, false);
    case ConditionalBlock::ComparisonType::IS_FALSE:
      return !context->GetBoolean(cond->contextKey, true);
    default:
      return false;
  }
}

std::string SequenceEngine::ComparisonToString(ConditionalBlock::ComparisonType comp) const
{
  switch (comp) {
    case ConditionalBlock::ComparisonType::GREATER_THAN: return ">";
    case ConditionalBlock::ComparisonType::LESS_THAN: return "<";
    case ConditionalBlock::ComparisonType::EQUAL: return "==";
    case ConditionalBlock::ComparisonType::NOT_EQUAL: return "!=";
    case ConditionalBlock::ComparisonType::GREATER_OR_EQUAL: return ">=";
    case ConditionalBlock::ComparisonType::LESS_OR_EQUAL: return "<=";
    case ConditionalBlock::ComparisonType::IS_TRUE: return "is true";
    case ConditionalBlock::ComparisonType::IS_FALSE: return "is false";
    case ConditionalBlock::ComparisonType::EXISTS: return "exists";
    case ConditionalBlock::ComparisonType::NOT_EXISTS: return "not exists";
    default: return "unknown";
  }
}

//=============================================================================
// TIMEOUT ENFORCEMENT
//=============================================================================

bool SequenceEngine::DispatchWithTimeout(
    const std::string& managerType, void* manager,
    const std::string& deviceName, const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage, SequenceContext* context)
{
  // Compute effective timeout: base timeout + any inherent command duration
  // (e.g., system.Wait with milliseconds=90000 needs 90s + base timeout as buffer)
  int effectiveTimeoutMs = m_actionTimeoutMs;
  auto msIt = params.find("milliseconds");
  if (msIt != params.end()) {
    int commandDurationMs = std::stoi(msIt->second);
    if (commandDurationMs > 0) {
      effectiveTimeoutMs += commandDurationMs;
    }
  }

  // All shared state is heap-allocated so it survives this function returning
  // (zombie threads on timeout still hold references to these)
  auto done = std::make_shared<std::atomic<bool>>(false);
  auto success = std::make_shared<std::atomic<bool>>(false);
  auto sharedError = std::make_shared<std::string>();
  auto mtx = std::make_shared<std::mutex>();
  auto cv = std::make_shared<std::condition_variable>();

  std::thread dispatchThread([this, managerType, manager, deviceName, methodName,
                              params, context, done, success, sharedError, mtx, cv]() {
    std::string localError;
    bool result = m_dispatcher.ExecuteCommand(
        managerType, manager, deviceName, methodName, params, localError, context);
    {
      std::lock_guard<std::mutex> lock(*mtx);
      *sharedError = localError;
      success->store(result);
      done->store(true);
    }
    cv->notify_one();
  });

  // Wait for completion or timeout
  {
    std::unique_lock<std::mutex> lock(*mtx);
    bool completed = cv->wait_for(lock,
        std::chrono::milliseconds(effectiveTimeoutMs),
        [&done]() { return done->load(); });

    if (!completed) {
      // TIMEOUT - command still running
      std::string timeoutMsg = deviceName + "." + methodName +
          " timed out after " + std::to_string(effectiveTimeoutMs) + "ms";
      LogError("TIMEOUT: " + timeoutMsg);
      if (m_context->safetyEventLog) {
        m_context->safetyEventLog->LogEvent(SafetySeverity::CRITICAL, "SequenceEngine",
            "ACTION_TIMEOUT_ENFORCED", timeoutMsg, ErrorCode::Timeout);
      }
      m_stopRequested.store(true);  // Signal cooperative stop
      m_zombieThreads.push_back(std::move(dispatchThread));
      errorMessage = "Command timed out: " + timeoutMsg;
      return false;
    }
  }

  dispatchThread.join();
  errorMessage = *sharedError;
  return success->load();
}

void SequenceEngine::JoinZombieThreads()
{
  for (auto& t : m_zombieThreads) {
    if (t.joinable()) t.join();
  }
  m_zombieThreads.clear();
}

//=============================================================================
// LOGGING HELPERS
//=============================================================================

void SequenceEngine::InjectContinuousVisionResult(SequenceContext* context)
{
#ifdef HAS_OPENCV
  if (!m_cvInjectEnabled.load()) return;
  if (!m_context->gdiCameraWindowManager) return;
  if (!m_context->gdiCameraWindowManager->isVisionProcessingActive()) return;

  // Skip if result hasn't changed since last injection
  uint64_t seq = m_context->gdiCameraWindowManager->getVisionResultSequence();
  if (seq == m_lastInjectedVisionSeq) return;

  VisionResult result = m_context->gdiCameraWindowManager->getLatestVisionResult();
  m_lastInjectedVisionSeq = seq;

  // Status keys
  context->SetValue("cv.result.success", result.success ? "true" : "false");
  context->SetValue("cv.result.plugin", result.pluginName);
  context->SetValue("cv.result.processing_time_ms", std::to_string(result.processingTimeMs));

  // Measurements
  for (const auto& [key, val] : result.measurements) {
    context->SetValue("cv.result." + key, std::to_string(val));
  }

  // Points
  context->SetValue("cv.point.count", std::to_string(result.points.size()));
  for (size_t i = 0; i < result.points.size(); ++i) {
    std::string prefix = "cv.point." + std::to_string(i) + ".";
    const auto& pt = result.points[i];
    context->SetValue(prefix + "x", std::to_string(pt.pixelCoord.x));
    context->SetValue(prefix + "y", std::to_string(pt.pixelCoord.y));
    context->SetValue(prefix + "mm_x", std::to_string(pt.mmCoord.x));
    context->SetValue(prefix + "mm_y", std::to_string(pt.mmCoord.y));
    context->SetValue(prefix + "confidence", std::to_string(pt.confidence));
  }

  // Circles
  context->SetValue("cv.circle.count", std::to_string(result.circles.size()));
  for (size_t i = 0; i < result.circles.size(); ++i) {
    std::string prefix = "cv.circle." + std::to_string(i) + ".";
    const auto& c = result.circles[i];
    context->SetValue(prefix + "x", std::to_string(c.centerPx.x));
    context->SetValue(prefix + "y", std::to_string(c.centerPx.y));
    context->SetValue(prefix + "radius", std::to_string(c.radiusPx));
    context->SetValue(prefix + "mm_x", std::to_string(c.centerMm.x));
    context->SetValue(prefix + "mm_y", std::to_string(c.centerMm.y));
    context->SetValue(prefix + "radius_mm", std::to_string(c.radiusMm));
  }

  // Lines
  context->SetValue("cv.line.count", std::to_string(result.lines.size()));
  for (size_t i = 0; i < result.lines.size(); ++i) {
    std::string prefix = "cv.line." + std::to_string(i) + ".";
    const auto& l = result.lines[i];
    context->SetValue(prefix + "start_x", std::to_string(l.startPx.x));
    context->SetValue(prefix + "start_y", std::to_string(l.startPx.y));
    context->SetValue(prefix + "end_x", std::to_string(l.endPx.x));
    context->SetValue(prefix + "end_y", std::to_string(l.endPx.y));
  }

  // Rectangles
  context->SetValue("cv.rectangle.count", std::to_string(result.rectangles.size()));
  for (size_t i = 0; i < result.rectangles.size(); ++i) {
    std::string prefix = "cv.rectangle." + std::to_string(i) + ".";
    const auto& r = result.rectangles[i];
    context->SetValue(prefix + "center_x", std::to_string(r.centerPx.x));
    context->SetValue(prefix + "center_y", std::to_string(r.centerPx.y));
    context->SetValue(prefix + "width", std::to_string(r.widthPx));
    context->SetValue(prefix + "height", std::to_string(r.heightPx));
    context->SetValue(prefix + "angle", std::to_string(r.angleDeg));
  }

  // Beams
  context->SetValue("cv.beam.count", std::to_string(result.beams.size()));
  for (size_t i = 0; i < result.beams.size(); ++i) {
    std::string prefix = "cv.beam." + std::to_string(i) + ".";
    const auto& b = result.beams[i];
    context->SetValue(prefix + "x", std::to_string(b.centerPx.x));
    context->SetValue(prefix + "y", std::to_string(b.centerPx.y));
    context->SetValue(prefix + "mm_x", std::to_string(b.centerMm.x));
    context->SetValue(prefix + "mm_y", std::to_string(b.centerMm.y));
    context->SetValue(prefix + "fwhm_major", std::to_string(b.fwhmMajorPx));
    context->SetValue(prefix + "fwhm_minor", std::to_string(b.fwhmMinorPx));
    context->SetValue(prefix + "mean_intensity", std::to_string(b.meanIntensity));
  }

  // Convenience: cv.result.center_x/center_y (priority: rect > point > circle > beam)
  if (!result.rectangles.empty()) {
    context->SetValue("cv.result.center_x", std::to_string(result.rectangles[0].centerPx.x));
    context->SetValue("cv.result.center_y", std::to_string(result.rectangles[0].centerPx.y));
  } else if (!result.points.empty()) {
    context->SetValue("cv.result.center_x", std::to_string(result.points[0].pixelCoord.x));
    context->SetValue("cv.result.center_y", std::to_string(result.points[0].pixelCoord.y));
  } else if (!result.circles.empty()) {
    context->SetValue("cv.result.center_x", std::to_string(result.circles[0].centerPx.x));
    context->SetValue("cv.result.center_y", std::to_string(result.circles[0].centerPx.y));
  } else if (!result.beams.empty()) {
    context->SetValue("cv.result.center_x", std::to_string(result.beams[0].centerPx.x));
    context->SetValue("cv.result.center_y", std::to_string(result.beams[0].centerPx.y));
  }
#else
  (void)context;
#endif
}

void SequenceEngine::InjectBeamStreamResult(SequenceContext* context)
{
  if (!m_bsInjectEnabled.load()) return;
  if (!m_context->gdiCameraWindowManager) return;
  if (!m_context->gdiCameraWindowManager->isBeamStreamActive()) return;

  // Skip if result hasn't changed since last injection
  uint64_t seq = m_context->gdiCameraWindowManager->getBeamStreamResultSequence();
  if (seq == m_lastInjectedBeamStreamSeq) return;

  nlohmann::json result = m_context->gdiCameraWindowManager->getLatestBeamStreamResult();
  m_lastInjectedBeamStreamSeq = seq;

  // Status keys
  context->SetValue("bs.active", "true");
  context->SetValue("bs.fps", std::to_string(m_context->gdiCameraWindowManager->getBeamStreamFps()));

  if (result.contains("process_ms")) {
    context->SetValue("bs.process_ms", std::to_string(result["process_ms"].get<double>()));
  }

  // Beams array
  if (result.contains("beams") && result["beams"].is_array()) {
    const auto& beams = result["beams"];
    context->SetValue("bs.beam.count", std::to_string(beams.size()));

    for (size_t i = 0; i < beams.size(); ++i) {
      std::string prefix = "bs.beam." + std::to_string(i) + ".";
      const auto& beam = beams[i];

      // Label / side
      if (beam.contains("side")) {
        context->SetValue(prefix + "label", beam["side"].get<std::string>());
      }

      // Centroid in pixels
      context->SetValue(prefix + "centroid_x_px", std::to_string(beam.value("centroid_x_px", 0.0)));
      context->SetValue(prefix + "centroid_y_px", std::to_string(beam.value("centroid_y_px", 0.0)));

      // D4sigma in pixels (if present)
      if (beam.contains("d4sigma_x_px")) {
        context->SetValue(prefix + "d4sigma_x_px", std::to_string(beam["d4sigma_x_px"].get<double>()));
      }
      if (beam.contains("d4sigma_y_px")) {
        context->SetValue(prefix + "d4sigma_y_px", std::to_string(beam["d4sigma_y_px"].get<double>()));
      }

      // D4sigma in microns (if present)
      if (beam.contains("d4sigma_x_um")) {
        context->SetValue(prefix + "d4sigma_x_um", std::to_string(beam["d4sigma_x_um"].get<double>()));
      }
      if (beam.contains("d4sigma_y_um")) {
        context->SetValue(prefix + "d4sigma_y_um", std::to_string(beam["d4sigma_y_um"].get<double>()));
      }

      // Peak intensity (if present)
      if (beam.contains("peak_intensity")) {
        context->SetValue(prefix + "peak_intensity", std::to_string(beam["peak_intensity"].get<double>()));
      }

      // ROI bounds (if present)
      if (beam.contains("roi_x")) {
        context->SetValue(prefix + "roi_x", std::to_string(beam["roi_x"].get<int>()));
      }
      if (beam.contains("roi_y")) {
        context->SetValue(prefix + "roi_y", std::to_string(beam["roi_y"].get<int>()));
      }
      if (beam.contains("roi_w")) {
        context->SetValue(prefix + "roi_w", std::to_string(beam["roi_w"].get<int>()));
      }
      if (beam.contains("roi_h")) {
        context->SetValue(prefix + "roi_h", std::to_string(beam["roi_h"].get<int>()));
      }
    }

    // Convenience: bs.beam.first.* aliases for the first beam
    if (!beams.empty()) {
      context->SetValue("bs.beam.first.centroid_x_px", std::to_string(beams[0].value("centroid_x_px", 0.0)));
      context->SetValue("bs.beam.first.centroid_y_px", std::to_string(beams[0].value("centroid_y_px", 0.0)));
      if (beams[0].contains("d4sigma_x_px")) {
        context->SetValue("bs.beam.first.d4sigma_x_px", std::to_string(beams[0]["d4sigma_x_px"].get<double>()));
      }
      if (beams[0].contains("d4sigma_y_px")) {
        context->SetValue("bs.beam.first.d4sigma_y_px", std::to_string(beams[0]["d4sigma_y_px"].get<double>()));
      }
      if (beams[0].contains("side")) {
        context->SetValue("bs.beam.first.label", beams[0]["side"].get<std::string>());
      }
    }
  } else {
    context->SetValue("bs.beam.count", "0");
  }
}

void SequenceEngine::InjectBeamStreamResult2(SequenceContext* context)
{
  if (!m_bsInjectEnabled.load()) return;
  if (!m_context->gdiCameraWindowManager2) return;
  if (!m_context->gdiCameraWindowManager2->isBeamStreamActive()) return;

  // Skip if result hasn't changed since last injection
  uint64_t seq = m_context->gdiCameraWindowManager2->getBeamStreamResultSequence();
  if (seq == m_lastInjectedBeamStreamSeq2) return;

  nlohmann::json result = m_context->gdiCameraWindowManager2->getLatestBeamStreamResult();
  m_lastInjectedBeamStreamSeq2 = seq;

  // Status keys (bs2.* prefix for second window)
  context->SetValue("bs2.active", "true");
  context->SetValue("bs2.fps", std::to_string(m_context->gdiCameraWindowManager2->getBeamStreamFps()));

  if (result.contains("process_ms")) {
    context->SetValue("bs2.process_ms", std::to_string(result["process_ms"].get<double>()));
  }

  // Beams array
  if (result.contains("beams") && result["beams"].is_array()) {
    const auto& beams = result["beams"];
    context->SetValue("bs2.beam.count", std::to_string(beams.size()));

    for (size_t i = 0; i < beams.size(); ++i) {
      std::string prefix = "bs2.beam." + std::to_string(i) + ".";
      const auto& beam = beams[i];

      // Label / side
      if (beam.contains("side")) {
        context->SetValue(prefix + "label", beam["side"].get<std::string>());
      }

      // Centroid in pixels
      context->SetValue(prefix + "centroid_x_px", std::to_string(beam.value("centroid_x_px", 0.0)));
      context->SetValue(prefix + "centroid_y_px", std::to_string(beam.value("centroid_y_px", 0.0)));

      // D4sigma in pixels (if present)
      if (beam.contains("d4sigma_x_px")) {
        context->SetValue(prefix + "d4sigma_x_px", std::to_string(beam["d4sigma_x_px"].get<double>()));
      }
      if (beam.contains("d4sigma_y_px")) {
        context->SetValue(prefix + "d4sigma_y_px", std::to_string(beam["d4sigma_y_px"].get<double>()));
      }

      // D4sigma in microns (if present)
      if (beam.contains("d4sigma_x_um")) {
        context->SetValue(prefix + "d4sigma_x_um", std::to_string(beam["d4sigma_x_um"].get<double>()));
      }
      if (beam.contains("d4sigma_y_um")) {
        context->SetValue(prefix + "d4sigma_y_um", std::to_string(beam["d4sigma_y_um"].get<double>()));
      }

      // Peak intensity (if present)
      if (beam.contains("peak_intensity")) {
        context->SetValue(prefix + "peak_intensity", std::to_string(beam["peak_intensity"].get<double>()));
      }

      // ROI bounds (if present)
      if (beam.contains("roi_x")) {
        context->SetValue(prefix + "roi_x", std::to_string(beam["roi_x"].get<int>()));
      }
      if (beam.contains("roi_y")) {
        context->SetValue(prefix + "roi_y", std::to_string(beam["roi_y"].get<int>()));
      }
      if (beam.contains("roi_w")) {
        context->SetValue(prefix + "roi_w", std::to_string(beam["roi_w"].get<int>()));
      }
      if (beam.contains("roi_h")) {
        context->SetValue(prefix + "roi_h", std::to_string(beam["roi_h"].get<int>()));
      }
    }

    // Convenience: bs2.beam.first.* aliases for the first beam
    if (!beams.empty()) {
      context->SetValue("bs2.beam.first.centroid_x_px", std::to_string(beams[0].value("centroid_x_px", 0.0)));
      context->SetValue("bs2.beam.first.centroid_y_px", std::to_string(beams[0].value("centroid_y_px", 0.0)));
      if (beams[0].contains("d4sigma_x_px")) {
        context->SetValue("bs2.beam.first.d4sigma_x_px", std::to_string(beams[0]["d4sigma_x_px"].get<double>()));
      }
      if (beams[0].contains("d4sigma_y_px")) {
        context->SetValue("bs2.beam.first.d4sigma_y_px", std::to_string(beams[0]["d4sigma_y_px"].get<double>()));
      }
      if (beams[0].contains("side")) {
        context->SetValue("bs2.beam.first.label", beams[0]["side"].get<std::string>());
      }
    }
  } else {
    context->SetValue("bs2.beam.count", "0");
  }
}

void SequenceEngine::LogInfo(const std::string& message)
{
  m_executionLog.push_back("[INFO] " + message);

  if (m_logger) {
    m_logger->Info(message, "SequenceEngine");
  }
  // Note: spdlog is always available now, no fallback needed
}

void SequenceEngine::LogWarning(const std::string& message)
{
  m_executionLog.push_back("[WARNING] " + message);

  if (m_logger) {
    m_logger->Warning(message, "SequenceEngine");
  }
}

void SequenceEngine::LogError(const std::string& message)
{
  m_executionLog.push_back("[ERROR] " + message);

  if (m_logger) {
    m_logger->Error(message, "SequenceEngine");
  }
}

//=============================================================================
// SEQUENCE BUILDER IMPLEMENTATION
//=============================================================================

SequenceBuilder::SequenceBuilder(const std::string& name, SequenceEngine* engine)
  : m_name(name), m_engine(engine)
{
}

SequenceBuilder::~SequenceBuilder()
{
}

void SequenceBuilder::AddAction(
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params)
{
  auto action = std::make_shared<Action>(deviceName, methodName, params);
  GetActiveStepList()->push_back(action);
}

void SequenceBuilder::AddStepMarker(int stepIndex, const std::string& stepName, const std::string& stepType)
{
  auto marker = std::make_shared<StepMarker>(stepIndex, stepName, stepType);
  GetActiveStepList()->push_back(marker);
}

void SequenceBuilder::BeginLoop(int count)
{
  auto loop = std::make_shared<LoopBlock>(count);

  // Add to current active list
  GetActiveStepList()->push_back(loop);

  // Push to stack
  BlockContext ctx;
  ctx.type = BlockType::LOOP;
  ctx.block = loop;
  m_blockStack.push_back(ctx);
}

void SequenceBuilder::EndLoop()
{
  if (m_blockStack.empty() || m_blockStack.back().type != BlockType::LOOP) {
    std::cerr << "[SequenceBuilder ERROR] EndLoop called without matching BeginLoop" << std::endl;
    return;
  }

  m_blockStack.pop_back();
}

void SequenceBuilder::BeginIf(const std::string& conditionDevice, const std::string& conditionMethod)
{
  auto cond = std::make_shared<ConditionalBlock>(conditionDevice, conditionMethod);

  // Add to current active list
  GetActiveStepList()->push_back(cond);

  // Push to stack (THEN branch)
  BlockContext ctx;
  ctx.type = BlockType::CONDITIONAL_THEN;
  ctx.block = cond;
  m_blockStack.push_back(ctx);
}

// NEW: Context-based conditional
void SequenceBuilder::BeginIf(const std::string& contextKey,
                               ConditionalBlock::ComparisonType comparison,
                               float threshold)
{
  auto cond = std::make_shared<ConditionalBlock>(contextKey, comparison, threshold);

  // Add to current active list
  GetActiveStepList()->push_back(cond);

  // Push to stack (THEN branch)
  BlockContext ctx;
  ctx.type = BlockType::CONDITIONAL_THEN;
  ctx.block = cond;
  m_blockStack.push_back(ctx);
}

void SequenceBuilder::Else()
{
  if (m_blockStack.empty() || m_blockStack.back().type != BlockType::CONDITIONAL_THEN) {
    std::cerr << "[SequenceBuilder ERROR] Else called without matching BeginIf" << std::endl;
    return;
  }

  // Switch to ELSE branch
  m_blockStack.back().type = BlockType::CONDITIONAL_ELSE;
}

void SequenceBuilder::EndIf()
{
  if (m_blockStack.empty() ||
    (m_blockStack.back().type != BlockType::CONDITIONAL_THEN &&
      m_blockStack.back().type != BlockType::CONDITIONAL_ELSE)) {
    std::cerr << "[SequenceBuilder ERROR] EndIf called without matching BeginIf" << std::endl;
    return;
  }

  m_blockStack.pop_back();
}

std::vector<std::shared_ptr<ISequenceStep>>* SequenceBuilder::GetActiveStepList()
{
  if (m_blockStack.empty()) {
    // Top level
    return &m_steps;
  }

  BlockContext& ctx = m_blockStack.back();

  if (ctx.type == BlockType::LOOP) {
    LoopBlock* loop = static_cast<LoopBlock*>(ctx.block.get());
    return &loop->actions;
  }
  else if (ctx.type == BlockType::CONDITIONAL_THEN) {
    ConditionalBlock* cond = static_cast<ConditionalBlock*>(ctx.block.get());
    return &cond->thenActions;
  }
  else if (ctx.type == BlockType::CONDITIONAL_ELSE) {
    ConditionalBlock* cond = static_cast<ConditionalBlock*>(ctx.block.get());
    return &cond->elseActions;
  }

  return &m_steps;
}