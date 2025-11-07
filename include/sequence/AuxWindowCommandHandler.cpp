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

  auto* window = m_auxWindowManager->getWindow();
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
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown auxwindow method: " + methodName;
    return false;
  }
}
