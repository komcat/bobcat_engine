// RecipeWorker.cpp
#include "RecipeWorker.h"
#include "SequenceEngine.h"
#include "RecipeManager.h"
#include "ProcessSequenceFactory.h"
#include "SequenceBuilder.h"
#include "product_position_manager.h"
#include "unified_motion_manager.h"
#include "LogManager.h"
#include <QMetaObject>
#include <iostream>

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

void RecipeWorker::setUserPromptCallback(UserPromptCallback callback)
{
  m_userPromptCallback = callback;
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

      bool success = m_engine->ExecuteSequence(sequence);

      delete sequence;
      sequence = nullptr;

      QString resultMsg = success
        ? QString("Recipe '%1' completed successfully!").arg(QString::fromStdString(recipeName))
        : "Recipe execution failed! Check logs for details.";

      // ✅ Clear LogManager callback before exiting
      LogManager* logger = m_engine->GetLogger();
      if (logger) {
        logger->ClearUICallback();
      }

      emit executionCompleted(success, resultMsg);
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