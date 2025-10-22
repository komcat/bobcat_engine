// SequenceEngine.cpp
#include "SequenceEngine.h"
#include "LogManager.h"
#include "unified_motion_manager.h"
#include "EziIOManager.h"
#include "PneumaticManager.h"
#include "CameraManager.h"
#include "SystemCommandHandler.h"
#include "MotionCommandHandler.h"
#include "TECCommandHandler.h"  // ✅ ADD THIS
#include "TECManager.h"
#include "IOCommandHandler.h"
#include "PneumaticCommandHandler.h"
#include "CameraCommandHandler.h"
#include "SequenceBuilder.h"
#include <iostream>

//=============================================================================
// SEQUENCE ENGINE IMPLEMENTATION
//=============================================================================

SequenceEngine::SequenceEngine(ApplicationContext* context)
  : m_context(context)
  , m_logger(nullptr)
  , m_systemHandler(nullptr)
{
  if (!m_context) {
    throw std::runtime_error("SequenceEngine: ApplicationContext is null");
  }

  // Register all command handlers
  m_dispatcher.RegisterHandler("motion", std::make_unique<MotionCommandHandler>());
  m_dispatcher.RegisterHandler("io", std::make_unique<IOCommandHandler>());
  m_dispatcher.RegisterHandler("pneumatic", std::make_unique<PneumaticCommandHandler>());
  m_dispatcher.RegisterHandler("camera", std::make_unique<CameraCommandHandler>());
  m_dispatcher.RegisterHandler("tec", std::make_unique<TECCommandHandler>());  // ✅ ADD THIS

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

  // Camera (special case - single instance)
  if (m_context->cameraManager) {
    m_deviceToManagerMap["camera"] = m_context->cameraManager;
    m_deviceToTypeMap["camera"] = "camera";
    LogInfo("Registered camera manager");
  }


  if (m_context->tecManager) {
    auto devices = m_context->tecManager->GetAllDeviceNames();
    for (const auto& name : devices) {
      m_deviceToManagerMap[name] = m_context->tecManager;
      m_deviceToTypeMap[name] = "tec";
    }
    LogInfo("Registered " + std::to_string(devices.size()) + " TEC devices");
  }

  // Register system as virtual device (no manager needed)
  m_deviceToManagerMap["system"] = nullptr;
  m_deviceToTypeMap["system"] = "system";
  LogInfo("Registered system handler");

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

bool SequenceEngine::ValidateSequence(SequenceBuilder* sequence, std::vector<std::string>& errors)
{
  if (!sequence) {
    errors.push_back("Sequence is null");
    return false;
  }

  LogInfo("Validating sequence: " + sequence->GetName());

  for (const auto& step : sequence->GetSteps()) {
    if (!ValidateStep(step.get(), errors)) {
      return false;
    }
  }

  if (errors.empty()) {
    LogInfo("Validation passed");
    return true;
  }
  else {
    LogWarning("Validation failed with " + std::to_string(errors.size()) + " errors");
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

    // System commands don't need a manager, others do
    if (managerType != "system") {
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

    // Validate condition device (allow system for conditions too)
    std::string managerType = ResolveManagerType(cond->conditionDevice);
    if (managerType.empty()) {
      errors.push_back("Condition device not found: " + cond->conditionDevice);
      return false;
    }

    if (managerType != "system") {
      void* manager = ResolveManager(cond->conditionDevice);
      if (!manager) {
        errors.push_back("Manager not found for condition device: " + cond->conditionDevice);
        return false;
      }
    }

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

bool SequenceEngine::ExecuteSequence(SequenceBuilder* sequence)
{
  if (!sequence) {
    LogError("Sequence is null");
    return false;
  }

  // Validate first
  std::vector<std::string> errors;
  if (!ValidateSequence(sequence, errors)) {
    for (const auto& err : errors) {
      LogError(err);
    }
    return false;
  }

  LogInfo("Executing sequence: " + sequence->GetName());

  try {
    for (const auto& step : sequence->GetSteps()) {
      if (!ExecuteStep(step.get())) {
        LogError("Step execution failed");
        return false;
      }
    }

    LogInfo("Sequence completed successfully");
    return true;
  }
  catch (const std::exception& e) {
    LogError("Exception during execution: " + std::string(e.what()));
    return false;
  }
}

bool SequenceEngine::ExecuteStep(ISequenceStep* step)
{
  if (!step) return false;

  switch (step->GetType()) {
  case ISequenceStep::Type::ACTION: {
    Action* action = static_cast<Action*>(step);

    LogInfo("Executing: " + action->deviceName + "." + action->methodName);

    // Get manager and type
    void* manager = ResolveManager(action->deviceName);
    std::string managerType = ResolveManagerType(action->deviceName);

    if (managerType.empty()) {
      LogError("Unknown device type for: " + action->deviceName);
      return false;
    }

    // System commands can have nullptr manager, others cannot
    if (managerType != "system" && !manager) {
      LogError("Manager not found for device: " + action->deviceName);
      return false;
    }

    // Execute command via dispatcher
    std::string errorMessage;
    if (!m_dispatcher.ExecuteCommand(managerType, manager, action->deviceName,
      action->methodName, action->params, errorMessage)) {
      LogError("Command failed: " + errorMessage);
      return false;
    }

    break;
  }

  case ISequenceStep::Type::LOOP: {
    LoopBlock* loop = static_cast<LoopBlock*>(step);

    LogInfo("Starting loop: " + std::to_string(loop->count) + " iterations");

    for (int i = 0; i < loop->count; i++) {
      LogInfo("  Loop iteration " + std::to_string(i + 1) + "/" + std::to_string(loop->count));

      for (const auto& action : loop->actions) {
        if (!ExecuteStep(action.get())) {
          return false;
        }
      }
    }

    LogInfo("Loop completed");
    break;
  }

  case ISequenceStep::Type::CONDITIONAL: {
    ConditionalBlock* cond = static_cast<ConditionalBlock*>(step);

    LogInfo("Evaluating condition: " + cond->conditionDevice + "." + cond->conditionMethod);

    // TODO: Actually call condition method and get boolean result
    bool conditionResult = false;  // Placeholder

    LogInfo("Condition result: " + std::string(conditionResult ? "TRUE" : "FALSE"));

    if (conditionResult) {
      LogInfo("Executing THEN branch");
      for (const auto& action : cond->thenActions) {
        if (!ExecuteStep(action.get())) {
          return false;
        }
      }
    }
    else {
      LogInfo("Executing ELSE branch");
      for (const auto& action : cond->elseActions) {
        if (!ExecuteStep(action.get())) {
          return false;
        }
      }
    }

    break;
  }
  }

  return true;
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