// AuxWindowCommandHandler.cpp
#include "AuxWindowCommandHandler.h"
#include "SequenceContext.h"
#include "LogManager.h"
#include "AuxWindowManager.h"
#include "AuxViewWindow.h"
#include "CameraClickToMoveWidget.h"
#include "LiveChartWidget.h"
#include <iostream>

AuxWindowCommandHandler::AuxWindowCommandHandler(AuxWindowManager* auxWindowManager)
  : m_auxWindowManager(auxWindowManager)
{
  std::cout << "[AuxWindowCommandHandler] Created" << std::endl;
}

bool AuxWindowCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  // manager is unused for auxwindow commands (can be nullptr)
  (void)manager;
  (void)deviceName;
  (void)context;

  if (!m_auxWindowManager) {
    errorMessage = "AuxWindowManager is null";
    return false;
  }

  // ensureWindow(), not getWindow(): the aux window is built lazily at
  // startup, so a recipe may be the first thing to touch it.
  auto* window = m_auxWindowManager->ensureWindow();
  if (!window) {
    errorMessage = "Auxiliary window is not initialized";
    return false;
  }

  // ============================================================================
  // ENABLE CROSSHAIR
  // ============================================================================
  if (methodName == "EnableCrosshair") {
    auto enabledIt = params.find("enabled");
    if (enabledIt == params.end()) {
      errorMessage = "Missing required parameter: enabled";
      return false;
    }

    bool enabled = (enabledIt->second == "true" || enabledIt->second == "1");

    auto* cameraWidget = window->getCameraWidget();
    if (!cameraWidget) {
      errorMessage = "Camera widget is not available";
      return false;
    }

    cameraWidget->setCrosshairEnabled(enabled);

    if (m_logger) {
      m_logger->Info("Set camera crosshair: " + std::string(enabled ? "ON" : "OFF"), "AuxWindow");
    }
    std::cout << "[AuxWindowCommandHandler] Crosshair " << (enabled ? "enabled" : "disabled") << std::endl;

    return true;
  }

  // ============================================================================
  // ENABLE CLICK TO MOVE
  // ============================================================================
  else if (methodName == "EnableClickToMove") {
    auto enabledIt = params.find("enabled");
    if (enabledIt == params.end()) {
      errorMessage = "Missing required parameter: enabled";
      return false;
    }

    bool enabled = (enabledIt->second == "true" || enabledIt->second == "1");

    auto* cameraWidget = window->getCameraWidget();
    if (!cameraWidget) {
      errorMessage = "Camera widget is not available";
      return false;
    }

    cameraWidget->setClickToMoveEnabled(enabled);

    if (m_logger) {
      m_logger->Info("Set click-to-move: " + std::string(enabled ? "ON" : "OFF"), "AuxWindow");
    }
    std::cout << "[AuxWindowCommandHandler] Click-to-move " << (enabled ? "enabled" : "disabled") << std::endl;

    return true;
  }

  // ============================================================================
  // SET CHART CHANNEL
  // ============================================================================
  else if (methodName == "SetChartChannel") {
    auto channelIt = params.find("channel");
    if (channelIt == params.end()) {
      errorMessage = "Missing required parameter: channel";
      return false;
    }

    std::string channelId = channelIt->second;

    auto* chartWidget = window->getChartWidget();
    if (!chartWidget) {
      errorMessage = "Chart widget is not available";
      return false;
    }

    chartWidget->setChannel(channelId);

    if (m_logger) {
      m_logger->Info("Set chart channel to: " + channelId, "AuxWindow");
    }
    std::cout << "[AuxWindowCommandHandler] Chart channel set to: " << channelId << std::endl;

    return true;
  }

  // ============================================================================
  // SET TARGET
  // ============================================================================
  else if (methodName == "SetTarget") {
    auto valueIt = params.find("value");
    if (valueIt == params.end()) {
      errorMessage = "Missing required parameter: value";
      return false;
    }

    float targetValue;
    try {
      targetValue = std::stof(valueIt->second);
    } catch (const std::exception& /*e*/) {
      errorMessage = "Invalid target value: " + valueIt->second;
      return false;
    }

    auto* chartWidget = window->getChartWidget();
    if (!chartWidget) {
      errorMessage = "Chart widget is not available";
      return false;
    }

    // Check if channel is specified, otherwise use current channel
    auto channelIt = params.find("channel");
    if (channelIt != params.end()) {
      chartWidget->setTarget(channelIt->second, targetValue);
      if (m_logger) {
        m_logger->Info("Set target for " + channelIt->second + ": " + valueIt->second, "AuxWindow");
      }
      std::cout << "[AuxWindowCommandHandler] Target set for " << channelIt->second << ": " << targetValue << std::endl;
    } else {
      chartWidget->setTarget(targetValue);
      if (m_logger) {
        m_logger->Info("Set target for current channel: " + valueIt->second, "AuxWindow");
      }
      std::cout << "[AuxWindowCommandHandler] Target set for current channel: " << targetValue << std::endl;
    }

    return true;
  }

  // ============================================================================
  // CLEAR TARGET
  // ============================================================================
  else if (methodName == "ClearTarget") {
    auto* chartWidget = window->getChartWidget();
    if (!chartWidget) {
      errorMessage = "Chart widget is not available";
      return false;
    }

    // Check if channel is specified, otherwise use current channel
    auto channelIt = params.find("channel");
    if (channelIt != params.end()) {
      chartWidget->clearTarget(channelIt->second);
      if (m_logger) {
        m_logger->Info("Cleared target for " + channelIt->second, "AuxWindow");
      }
      std::cout << "[AuxWindowCommandHandler] Target cleared for " << channelIt->second << std::endl;
    } else {
      chartWidget->clearTarget();
      if (m_logger) {
        m_logger->Info("Cleared target for current channel", "AuxWindow");
      }
      std::cout << "[AuxWindowCommandHandler] Target cleared for current channel" << std::endl;
    }

    return true;
  }

  // ============================================================================
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown auxwindow method: " + methodName;
    return false;
  }
}
