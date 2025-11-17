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
#include "DataStoreCommandHandler.h"
#include "PSUCommandHandler.h"
#include "SMUCommandHandler.h"  // ✅ ADDED
#include "DUTCommandHandler.h"
#include "SequenceContextCommandHandler.h"
#include "CalibrationCommandHandler.h"
#include "AuxWindowCommandHandler.h"
#include "SequenceContext.h"
#include "AuxWindowManager.h"
#include "TECManager.h"
#include "PowerSupplyManager.h"
#include "SMUManager.h"  // ✅ ADDED
#include "IOCommandHandler.h"
#include "PneumaticCommandHandler.h"
#include "CameraCommandHandler.h"
#ifdef HAS_OPENCV
#include "VisionCommandHandler.h"
#endif
#include "SequenceBuilder.h"
#include <iostream>
#include <cmath>

//=============================================================================
// SEQUENCE ENGINE IMPLEMENTATION
//=============================================================================

SequenceEngine::SequenceEngine(ApplicationContext* context)
  : m_context(context)
  , m_logger(nullptr)
  , m_systemHandler(nullptr)
  , m_stopRequested(false)  // ✅ Initialize stop flag
{
  if (!m_context) {
    throw std::runtime_error("SequenceEngine: ApplicationContext is null");
  }

  // Register all command handlers
  // ✅ UPDATED: Set DataStore and ProductPositionManager on MotionCommandHandler
  auto motionHandler = std::make_unique<MotionCommandHandler>();
  motionHandler->SetDataStore(m_context->dataStore);
  motionHandler->SetProductPositionManager(m_context->productPositionManager);
  m_dispatcher.RegisterHandler("motion", std::move(motionHandler));

  m_dispatcher.RegisterHandler("io", std::make_unique<IOCommandHandler>());
  m_dispatcher.RegisterHandler("pneumatic", std::make_unique<PneumaticCommandHandler>());

  // ✅ UPDATED: Set context on CameraCommandHandler to access currentDUT
  auto cameraHandler = std::make_unique<CameraCommandHandler>();
  cameraHandler->SetContext(m_context);
  m_dispatcher.RegisterHandler("camera", std::move(cameraHandler));

  m_dispatcher.RegisterHandler("tec", std::make_unique<TECCommandHandler>());
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
}

SequenceEngine::~SequenceEngine()
{
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

    // System, context, calibration, dut, and vision commands don't need a manager pointer, others do
    if (managerType != "system" && managerType != "context" && managerType != "calibration" && managerType != "dut" && managerType != "vision") {
      void* manager = ResolveManager(action->deviceName);
      if (!manager) {
        errors.push_back("Manager not found for device: " + action->deviceName);
        return false;
      }
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

  LogInfo("▶️ Executing sequence: " + sequence->GetName());

  try {
    for (size_t i = 0; i < sequence->GetSteps().size(); ++i) {
      // ✅ CHECK STOP FLAG BEFORE EACH STEP (check both engine and context flags)
      if (m_stopRequested.load() || context->IsStopRequested()) {
        LogWarning("⏹️ Sequence execution stopped by user request");
        return false;
      }

      context->SetCurrentStep(static_cast<int>(i), "Step_" + std::to_string(i));
      context->IncrementStepCount();

      if (!ExecuteStep(sequence->GetSteps()[i].get(), context)) {
        LogError("❌ Step execution failed");
        return false;
      }
    }

    context->SetExecutionEndTime();
    LogInfo("✅ Sequence completed in " +
           std::to_string(context->GetExecutionDuration().count()) + " ms");
    return true;
  }
  catch (const std::exception& e) {
    LogError("💥 Exception during execution: " + std::string(e.what()));
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

    // Get manager and type
    void* manager = ResolveManager(action->deviceName);
    std::string managerType = ResolveManagerType(action->deviceName);

    if (managerType.empty()) {
      LogError("❓ Unknown device type for: " + action->deviceName);
      return false;
    }

    // System/context/calibration/dut/vision commands can have nullptr manager, others cannot
    if (managerType != "system" && managerType != "context" && managerType != "calibration" && managerType != "dut" && managerType != "vision" && !manager) {
      LogError("❓ Manager not found for device: " + action->deviceName);
      return false;
    }

    // Execute command via dispatcher with context
    std::string errorMessage;
    if (!m_dispatcher.ExecuteCommand(managerType, manager, action->deviceName,
      action->methodName, action->params, errorMessage, context)) {
      LogError("❌ Command failed: " + errorMessage);
      return false;
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

    LogInfo("🔀 Condition [" + cond->contextKey + " " +
           ComparisonToString(cond->comparison) + " " +
           std::to_string(cond->threshold) + "] = " +
           (conditionResult ? "TRUE ✓" : "FALSE ✗"));

    if (conditionResult) {
      LogInfo("  ➡️ Executing THEN branch");
      for (const auto& action : cond->thenActions) {
        if (!ExecuteStep(action.get(), context)) {
          return false;
        }
      }
    }
    else {
      LogInfo("  ➡️ Executing ELSE branch");
      for (const auto& action : cond->elseActions) {
        if (!ExecuteStep(action.get(), context)) {
          return false;
        }
      }
    }

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
// LOGGING HELPERS
//=============================================================================

void SequenceEngine::LogInfo(const std::string& message)
{
  m_executionLog.push_back("[INFO] " + message);

  if (m_logger) {
    m_logger->Info(message, "SequenceEngine");
  }
  else {
    std::cout << "[SequenceEngine] " << message << std::endl;
  }
}

void SequenceEngine::LogWarning(const std::string& message)
{
  m_executionLog.push_back("[WARNING] " + message);

  if (m_logger) {
    m_logger->Warning(message, "SequenceEngine");
  }
  else {
    std::cout << "[SequenceEngine] WARNING: " << message << std::endl;
  }
}

void SequenceEngine::LogError(const std::string& message)
{
  m_executionLog.push_back("[ERROR] " + message);

  if (m_logger) {
    m_logger->Error(message, "SequenceEngine");
  }
  else {
    std::cerr << "[SequenceEngine] ERROR: " << message << std::endl;
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
    throw std::runtime_error("EndLoop called without matching BeginLoop");
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
    throw std::runtime_error("Else called without matching BeginIf");
  }

  // Switch to ELSE branch
  m_blockStack.back().type = BlockType::CONDITIONAL_ELSE;
}

void SequenceBuilder::EndIf()
{
  if (m_blockStack.empty() ||
    (m_blockStack.back().type != BlockType::CONDITIONAL_THEN &&
      m_blockStack.back().type != BlockType::CONDITIONAL_ELSE)) {
    throw std::runtime_error("EndIf called without matching BeginIf");
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