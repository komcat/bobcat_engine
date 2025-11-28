// RecipeWorker.cpp
#include "RecipeWorker.h"
#include "SequenceEngine.h"
#include "RecipeManager.h"
#include "ProcessSequenceFactory.h"
#include "SequenceBuilder.h"
#include "SequenceContext.h"
#include "product_position_manager.h"
#include "unified_motion_manager.h"
#include "LogManager.h"
#include <QMetaObject>
#include <QString>
#include <iostream>
#include <chrono>

RecipeWorker::RecipeWorker(
  SequenceEngine* engine,
  ProcessSequenceFactory* factory,
  ProductPositionManager* positionManager,
  UnifiedMotionManager* motionManager,
  const std::string& recipeFilePath,
  QObject* parent)
  : QThread(parent)
  , m_engine(engine)
  , m_factory(factory)
  , m_positionManager(positionManager)
  , m_motionManager(motionManager)
  , m_recipeFilePath(recipeFilePath)
  , m_stopRequested(false)
  , m_userPromptCallback(nullptr)
{
}

void RecipeWorker::requestStop()
{
  m_stopRequested.store(true);
  emit logMessage("Recipe stop requested");

  // ✅ CRITICAL: Forward stop to SequenceEngine
  if (m_engine) {
    m_engine->RequestStop();
    emit logMessage("Stop forwarded to SequenceEngine");
  }
}

void RecipeWorker::setStepByStepMode(bool enabled)
{
  if (m_engine) {
    m_engine->SetStepByStepMode(enabled);
  }
}

void RecipeWorker::requestContinue()
{
  if (m_engine) {
    m_engine->RequestContinue();
  }
}

void RecipeWorker::requestRunToEnd()
{
  if (m_engine) {
    m_engine->RequestRunToEnd();
  }
}

void RecipeWorker::setBreakpoints(const std::vector<bool>& breakpoints)
{
  m_breakpoints = breakpoints;
}

void RecipeWorker::setUserPromptCallback(UserPromptCallback callback)
{
  m_userPromptCallback = callback;
}

void RecipeWorker::setManualAdjustCallback(ManualAdjustCallback callback)
{
  m_manualAdjustCallback = callback;
}

void RecipeWorker::setUserInputCallback(UserInputCallback callback)
{
  m_userInputCallback = callback;
}

void RecipeWorker::setExternalContext(SequenceContext* context)
{
  m_externalContext = context;
}

void RecipeWorker::run()
{
  try {
    // ✅ Step 1: Load recipe file
    RecipeManager recipeManager;
    if (!recipeManager.loadRecipe(m_recipeFilePath)) {
      emit executionCompleted(false, "Failed to load recipe file");
      return;
    }

    emit logMessage(QString("Loaded recipe: %1")
      .arg(QString::fromStdString(recipeManager.getRecipeName())));

    // ✅ Step 2: Load product positions if specified
    std::string productFile = recipeManager.getProductFile();
    if (!productFile.empty()) {
      emit logMessage(QString("Loading product positions from: %1")
        .arg(QString::fromStdString(productFile)));

      if (!m_positionManager->LoadProduct(productFile)) {
        emit executionCompleted(false, "Failed to load product positions: " + QString::fromStdString(productFile));
        return;
      }

      // ✅ Export positions to motion manager
      m_positionManager->ExportToMotionManager(*m_motionManager);

      emit logMessage(QString("Loaded %1 positions for %2 devices")
        .arg(m_positionManager->GetTotalPositionCount())
        .arg(m_positionManager->GetAllDeviceNames().size()));
    }
    else {
      emit logMessage("Warning: Recipe has no product_file specified");
    }

    // ✅ Step 3: Build device registry (registers all devices including TEC/laser)
    emit logMessage("Building device registry...");
    m_engine->BuildDeviceRegistry();
    emit logMessage("Device registry built successfully");

    // ✅ Step 3.5: Set up LogManager callback to forward command logs to execution log
    LogManager* logger = m_engine->GetLogger();
    if (logger) {
      logger->SetUICallback([this](const LogManager::LogEntry& entry) {
        // Forward log messages to execution log via signal
        // Format: [Source] Message
        QString logText;
        if (!entry.source.empty()) {
          logText = QString("[%1] %2")
            .arg(QString::fromStdString(entry.source))
            .arg(QString::fromStdString(entry.message));
        } else {
          logText = QString::fromStdString(entry.message);
        }

        // Emit in a thread-safe way
        QMetaObject::invokeMethod(this, "logMessage", Qt::QueuedConnection,
          Q_ARG(QString, logText));
      });
      emit logMessage("Log forwarding enabled for execution log");
    }

    // ✅ Step 4: Set up UserPrompt callback
    if (m_userPromptCallback) {
      // User provided callback - use it
      m_engine->SetUserPromptCallback(m_userPromptCallback);
      emit logMessage("UserPrompt callback configured");
    }
    else {
      // No callback - auto-accept all prompts for automated execution
      m_engine->SetUserPromptCallback(
        [this](const std::string& title, const std::string& message) -> bool {
        emit logMessage(QString("Auto-accepting prompt: %1 - %2")
          .arg(QString::fromStdString(title))
          .arg(QString::fromStdString(message)));
        return true;  // Auto-accept
      }
      );
      emit logMessage("UserPrompt auto-accept mode enabled");
    }

    // ✅ Step 4.5: Set up ManualAdjust callback
    if (m_manualAdjustCallback) {
      // User provided callback - use it
      m_engine->SetManualAdjustCallback(m_manualAdjustCallback);
      emit logMessage("ManualAdjust callback configured");
    }
    else {
      // No callback - auto-accept all adjustments for automated execution
      m_engine->SetManualAdjustCallback(
        [this](const std::string& message, const std::string& deviceName,
               double stepSizeUm, bool enableX, bool enableY, bool enableZ) -> bool {
        emit logMessage(QString("Auto-accepting manual adjust: %1 (device: %2)")
          .arg(QString::fromStdString(message))
          .arg(QString::fromStdString(deviceName)));
        return true;  // Auto-accept
      }
      );
      emit logMessage("ManualAdjust auto-accept mode enabled");
    }

    // ✅ Step 4.6: Set up UserInput callback
    if (m_userInputCallback) {
      // User provided callback - use it
      m_engine->SetUserInputCallback(m_userInputCallback);
      emit logMessage("UserInput callback configured");
    }
    else {
      // No callback - return empty string (fail the input)
      m_engine->SetUserInputCallback(
        [this](const std::string& title, const std::string& prompt, const std::string& defaultValue) -> std::pair<bool, std::string> {
        emit logMessage(QString("Auto-failing user input (no callback): %1 - %2")
          .arg(QString::fromStdString(title))
          .arg(QString::fromStdString(prompt)));
        return std::make_pair(false, "");  // Auto-fail
      }
      );
      emit logMessage("UserInput auto-fail mode enabled (no callback configured)");
    }

    // Count enabled steps
    int totalEnabledSteps = 0;
    for (const auto& step : recipeManager.getAllSteps()) {
      if (step->enabled) {
        totalEnabledSteps++;
      }
    }

    emit executionStarted(totalEnabledSteps);
    emit logMessage(QString("Recipe has %1 enabled step(s)").arg(totalEnabledSteps));

    // ✅ Step 5: Build sequence
    std::string recipeName = recipeManager.getRecipeName();
    SequenceBuilder* sequence = m_engine->CreateSequence(recipeName);

    try {
      emit logMessage("Building sequence...");
      buildSequenceFromRecipe(sequence, &recipeManager);

      // Check if stopped during build
      if (m_stopRequested.load()) {
        delete sequence;
        emit executionStopped();
        emit executionCompleted(false, "Execution stopped by user");
        return;
      }

      // ✅ Step 6: Execute sequence (SequenceEngine will check stop flag internally)
      emit sequenceExecutionStarted();
      emit logMessage("Executing sequence...");

      // Set up step execution callbacks (pub-sub) to forward events to signals
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

      // Set up breakpoints if provided
      if (!m_breakpoints.empty()) {
        m_engine->SetBreakpoints(m_breakpoints);
        emit logMessage(QString("Breakpoints set for %1 steps").arg(m_breakpoints.size()));
      }

      // Set up breakpoint callback to notify UI when paused
      m_engine->SetBreakpointCallback([this](int stepIndex, const std::string& stepName) {
        // Emit pause signal to UI (SequenceEngine will handle the actual pause via condition_variable)
        QMetaObject::invokeMethod(this, "executionPaused", Qt::QueuedConnection,
          Q_ARG(int, stepIndex),
          Q_ARG(QString, QString::fromStdString(stepName)),
          Q_ARG(bool, true));  // isBreakpoint = true
      });

      // Create execution context for value capture and conditionals
      // ✅ Use external context if provided (for ProgramWorker integration)
      std::unique_ptr<SequenceContext> localContext;
      SequenceContext* context = m_externalContext;
      if (!context) {
        localContext = std::make_unique<SequenceContext>();
        context = localContext.get();
      }

      // Set up wait progress callback to emit countdown updates
      context->SetWaitProgressCallback([this](int remainingMs, int totalMs) {
        // Emit in a thread-safe way using Qt's queued connection
        QMetaObject::invokeMethod(this, "waitProgress", Qt::QueuedConnection,
          Q_ARG(int, remainingMs), Q_ARG(int, totalMs));
      });

      bool success = m_engine->ExecuteSequence(sequence, context);

      delete sequence;
      sequence = nullptr;

      // ✅ Log execution context summary
      if (context) {
        auto summary = context->GetSummary();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          summary.endTime - summary.startTime).count();

        emit logMessage("========================================");
        emit logMessage("Execution Context Summary:");
        emit logMessage(QString("  Duration: %1 ms").arg(static_cast<qulonglong>(duration)));
        emit logMessage(QString("  Steps Executed: %1").arg(summary.totalSteps));
        emit logMessage(QString("  Values Captured: %1").arg(static_cast<int>(summary.capturedValues.size())));

        // Log all captured values
        if (!summary.capturedValues.empty()) {
          emit logMessage("  Captured Values:");
          for (const auto& pair : summary.capturedValues) {
            const std::string& key = pair.first;
            const SequenceContext::Value& value = pair.second;

            if (value.type == SequenceContext::ValueType::NUMERIC) {
              emit logMessage(QString("    - %1 = %2")
                .arg(QString::fromStdString(key))
                .arg(value.numericValue));
            } else if (value.type == SequenceContext::ValueType::STRING) {
              emit logMessage(QString("    - %1 = \"%2\"")
                .arg(QString::fromStdString(key))
                .arg(QString::fromStdString(value.stringValue)));
            } else if (value.type == SequenceContext::ValueType::BOOLEAN) {
              emit logMessage(QString("    - %1 = %2")
                .arg(QString::fromStdString(key))
                .arg(value.booleanValue ? "true" : "false"));
            }
          }
        }

        // Save context to JSON file for debugging (in logs folder)
        std::string timestamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        std::string filename = "logs/context_" + recipeName + "_" + timestamp + ".json";
        context->SaveToFile(filename);
        emit logMessage(QString("  Context saved to: %1").arg(QString::fromStdString(filename)));

        emit logMessage("========================================");
      }

      QString resultMsg = success
        ? QString("Recipe '%1' completed successfully!").arg(QString::fromStdString(recipeName))
        : "Recipe execution failed! Check logs for details.";

      // ✅ Clear LogManager callback before exiting
      LogManager* logger = m_engine->GetLogger();
      if (logger) {
        logger->ClearUICallback();
      }

      std::cout << "[RecipeWorker] Emitting executionCompleted signal: " << resultMsg.toStdString() << std::endl;
      emit executionCompleted(success, resultMsg);
      std::cout << "[RecipeWorker] Signal emitted, run() is about to return (thread will exit)" << std::endl;
    }
    catch (...) {
      if (sequence) {
        delete sequence;
      }
      throw;
    }
  }
  catch (const std::exception& e) {
    // ✅ Clear LogManager callback on error too
    LogManager* logger = m_engine->GetLogger();
    if (logger) {
      logger->ClearUICallback();
    }

    emit executionCompleted(false, QString("Exception: %1").arg(e.what()));
  }
}

void RecipeWorker::buildSequenceFromRecipe(SequenceBuilder* sequence, RecipeManager* recipeManager)
{
  int enabledStepIndex = 0;
  int totalStepIndex = 0;

  for (const auto& step : recipeManager->getAllSteps()) {
    totalStepIndex++;

    // Check for stop request
    if (m_stopRequested.load()) {
      emit logMessage("Build stopped by user");
      throw std::runtime_error("Build stopped by user");
    }

    if (!step->enabled) {
      emit logMessage(QString("Skipping disabled step: %1")
        .arg(QString::fromStdString(step->userNickname)));
      continue;
    }

    emit stepBuildStarted(
      enabledStepIndex,
      QString::fromStdString(step->userNickname),
      QString::fromStdString(step->processType)
    );

    std::cout << "[RecipeWorker] Building step " << (enabledStepIndex + 1) << ": "
      << step->userNickname << " (Type: " << step->processType << ")" << std::endl;

    // Insert step marker for execution tracking (pub-sub)
    sequence->AddStepMarker(enabledStepIndex, step->userNickname, step->processType);

    std::string errorMsg;
    if (!m_factory->BuildStep(sequence, step.get(), errorMsg)) {
      std::cerr << "[RecipeWorker] " << errorMsg << std::endl;
      throw std::runtime_error(errorMsg);
    }

    emit stepBuildCompleted(enabledStepIndex);
    enabledStepIndex++;
  }

  emit logMessage(QString("Sequence built with %1 enabled step(s)")
    .arg(enabledStepIndex));
}