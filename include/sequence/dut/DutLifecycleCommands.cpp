// DutLifecycleCommands.cpp — see header for description.
#include "DutLifecycleCommands.h"

#include "../DUTCommandHandler.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "dut/DeviceUnderTest.h"
#include "products/product_position_manager.h"

#include <iostream>
#include <utility>

namespace {

// Resolve productName + serialNumber via the standard fallback chain used by
// Create / Load / LoadOrCreate, then close any existing DUT in preparation
// for the caller to install a fresh one. Returns false (and populates
// errorMessage) if either name remains empty after the fallbacks.
//
// op describes the upcoming action for the auto-close log line — e.g.
// "creating new one", "loading new one", "loading/creating new one".
bool ResolveAndPrepareDut(
    const std::map<std::string, std::string>& params,
    SequenceContext* context,
    const std::string& op,
    std::string& productName,
    std::string& serialNumber,
    std::string& errorMessage,
    LogManager* logger,
    const DutCommandDeps& deps)
{
  productName = deps.parent->GetParam(params, "productName");
  serialNumber = deps.parent->GetParam(params, "serialNumber");

  // If productName is empty, try to use loaded product from ProductPositionManager
  if (productName.empty() && deps.app->productPositionManager) {
    productName = deps.app->productPositionManager->GetCurrentProductName();
    if (!productName.empty()) {
      if (logger) {
        logger->Debug("Using loaded product name from Position Manager: " + productName, "DUT");
      }
      std::cout << "[DUTCommandHandler] Auto-using product: " << productName << std::endl;
    }
  }

  if (productName.empty()) {
    errorMessage = "Missing product name. Either set 'productName' parameter or load a Product Position File.";
    if (logger) {
      logger->Error(errorMessage, "DUT");
    }
    return false;
  }

  // If serialNumber is empty, try to read from context (set by AskForInput)
  if (serialNumber.empty() && context) {
    std::string contextSerialNumber = context->GetString("serialNumber", "");
    if (!contextSerialNumber.empty()) {
      serialNumber = contextSerialNumber;
      if (logger) {
        logger->Info("Using serial number from context: " + serialNumber, "DUT");
      }
    }
  }

  // Final check - serial number must be provided
  if (serialNumber.empty()) {
    errorMessage = "Missing serial number. Please provide serialNumber parameter or use AskForInput to prompt user.";
    if (logger) {
      logger->Error(errorMessage, "DUT");
    }
    return false;
  }

  // Close existing DUT if one is active
  if (deps.app->currentDUT != nullptr) {
    std::cout << "[DUTCommandHandler] Auto-closing existing DUT before " << op << std::endl;
    delete deps.app->currentDUT;
    deps.app->currentDUT = nullptr;
  }

  return true;
}

}  // namespace

std::optional<bool> DutLifecycleCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const DutCommandDeps& deps)
{
  LogManager* m_logger = deps.app ? deps.app->logManager : nullptr;

  // ============================================================================
  // CREATE
  // ============================================================================
  if (methodName == "Create") {
    std::string productName, serialNumber;
    if (!ResolveAndPrepareDut(params, context, "creating new one",
                              productName, serialNumber,
                              errorMessage, m_logger, deps)) {
      return false;
    }

    // Create new DUT and store in context
    auto dutOpt = DeviceUnderTest::create(productName, serialNumber);
    if (!dutOpt.has_value()) {
      errorMessage = "Failed to create DUT: " + productName + " / " + serialNumber;
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }

    deps.app->currentDUT = new DeviceUnderTest(std::move(dutOpt.value()));

    if (m_logger) {
      m_logger->Info("Created DUT: " + productName + " / " + serialNumber, "DUT");
    }
    std::cout << "[DUTCommandHandler] Created DUT: " << productName << " / " << serialNumber << std::endl;
    return true;
  }

  // ============================================================================
  // LOAD
  // ============================================================================
  if (methodName == "Load") {
    std::string productName, serialNumber;
    if (!ResolveAndPrepareDut(params, context, "loading new one",
                              productName, serialNumber,
                              errorMessage, m_logger, deps)) {
      return false;
    }

    // Load existing DUT and store in context
    auto dutOpt = DeviceUnderTest::load(productName, serialNumber);
    if (!dutOpt.has_value()) {
      errorMessage = "Failed to load DUT: " + productName + " / " + serialNumber;
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }

    deps.app->currentDUT = new DeviceUnderTest(std::move(dutOpt.value()));

    if (m_logger) {
      m_logger->Info("Loaded DUT: " + productName + " / " + serialNumber +
                     " (" + std::to_string(deps.app->currentDUT->getEntryCount()) + " entries)", "DUT");
    }
    std::cout << "[DUTCommandHandler] Loaded DUT: " << productName << " / " << serialNumber
              << " (" << deps.app->currentDUT->getEntryCount() << " entries)" << std::endl;
    return true;
  }

  // ============================================================================
  // LOAD OR CREATE
  // ============================================================================
  if (methodName == "LoadOrCreate") {
    std::string productName, serialNumber;
    if (!ResolveAndPrepareDut(params, context, "loading/creating new one",
                              productName, serialNumber,
                              errorMessage, m_logger, deps)) {
      return false;
    }

    // Check if DUT file exists
    if (DeviceUnderTest::exists(productName, serialNumber)) {
      // Load existing DUT (preserves all previous data)
      auto dutOpt = DeviceUnderTest::load(productName, serialNumber);
      if (!dutOpt.has_value()) {
        errorMessage = "Failed to load existing DUT: " + productName + " / " + serialNumber;
        if (m_logger) {
          m_logger->Error(errorMessage, "DUT");
        }
        return false;
      }

      deps.app->currentDUT = new DeviceUnderTest(std::move(dutOpt.value()));

      if (m_logger) {
        m_logger->Info("Loaded existing DUT: " + productName + " / " + serialNumber +
                       " (" + std::to_string(deps.app->currentDUT->getEntryCount()) + " existing entries - new data will be appended)", "DUT");
      }
      std::cout << "[DUTCommandHandler] Loaded existing DUT: " << productName << " / " << serialNumber
                << " (" << deps.app->currentDUT->getEntryCount() << " existing entries)" << std::endl;
    }
    else {
      // Create new DUT (file doesn't exist yet)
      auto dutOpt = DeviceUnderTest::create(productName, serialNumber);
      if (!dutOpt.has_value()) {
        errorMessage = "Failed to create new DUT: " + productName + " / " + serialNumber;
        if (m_logger) {
          m_logger->Error(errorMessage, "DUT");
        }
        return false;
      }

      deps.app->currentDUT = new DeviceUnderTest(std::move(dutOpt.value()));

      if (m_logger) {
        m_logger->Info("Created new DUT: " + productName + " / " + serialNumber, "DUT");
      }
      std::cout << "[DUTCommandHandler] Created new DUT: " << productName << " / " << serialNumber << std::endl;
    }

    return true;
  }

  // ============================================================================
  // CLOSE
  // ============================================================================
  if (methodName == "Close") {
    if (deps.app->currentDUT == nullptr) {
      errorMessage = "No active DUT to close";
      if (m_logger) {
        m_logger->Warning(errorMessage, "DUT");
      }
      return false;
    }

    std::string serialNumber = deps.app->currentDUT->getSerialNumber();
    std::string productName = deps.app->currentDUT->getProductName();

    delete deps.app->currentDUT;
    deps.app->currentDUT = nullptr;

    if (m_logger) {
      m_logger->Info("Closed DUT: " + productName + " / " + serialNumber, "DUT");
    }
    std::cout << "[DUTCommandHandler] Closed DUT: " << productName << " / " << serialNumber << std::endl;
    return true;
  }

  return std::nullopt;  // not our family
}
