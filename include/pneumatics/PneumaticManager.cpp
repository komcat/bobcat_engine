// PneumaticManager.cpp - Implementation
#include "PneumaticManager.h"
#include <iostream>
#include <algorithm>

PneumaticManager::PneumaticManager(EziIOManager* ioManager)
  : m_ioManager(ioManager)
{
  std::cout << "[PneumaticManager] Manager created" << std::endl;
}

PneumaticManager::~PneumaticManager() {
  std::cout << "[PneumaticManager] Shutting down..." << std::endl;
  StopAll();
  m_slides.clear();
}

// ============================================================================
// SLIDE MANAGEMENT
// ============================================================================

bool PneumaticManager::AddSlide(const PneumaticConfig& config) {
  // Check if slide already exists
  if (m_slides.find(config.Name) != m_slides.end()) {
    std::cerr << "[PneumaticManager] Slide '" << config.Name << "' already exists" << std::endl;
    return false;
  }

  // Create new slide
  auto slide = std::make_shared<PneumaticSlide>(config, m_ioManager);

  // Set global callbacks if they exist
  if (m_globalPositionCallback) {
    slide->SetPositionCallback(m_globalPositionCallback);
  }
  if (m_globalErrorCallback) {
    slide->SetErrorCallback(m_globalErrorCallback);
  }

  // Add to collection
  m_slides[config.Name] = slide;

  std::cout << "[PneumaticManager] Added slide: " << config.Name << std::endl;
  return true;
}

bool PneumaticManager::RemoveSlide(const std::string& slideName) {
  auto it = m_slides.find(slideName);
  if (it == m_slides.end()) {
    std::cerr << "[PneumaticManager] Slide '" << slideName << "' not found" << std::endl;
    return false;
  }

  // Stop the slide before removing
  it->second->Stop();

  // Remove from collection
  m_slides.erase(it);

  std::cout << "[PneumaticManager] Removed slide: " << slideName << std::endl;
  return true;
}

bool PneumaticManager::HasSlide(const std::string& slideName) const {
  return m_slides.find(slideName) != m_slides.end();
}

IPneumaticSlide* PneumaticManager::GetSlide(const std::string& slideName) {
  auto it = m_slides.find(slideName);
  return (it != m_slides.end()) ? it->second.get() : nullptr;
}

// ============================================================================
// NON-BLOCKING OPERATIONS
// ============================================================================

PneumaticError PneumaticManager::Extend(const std::string& slideName) {
  IPneumaticSlide* slide = nullptr;
  PneumaticError validationResult = ValidateSlide(slideName, &slide);
  if (validationResult != PneumaticError::SUCCESS) {
    return validationResult;
  }

  return slide->Extend();
}

PneumaticError PneumaticManager::Retract(const std::string& slideName) {
  IPneumaticSlide* slide = nullptr;
  PneumaticError validationResult = ValidateSlide(slideName, &slide);
  if (validationResult != PneumaticError::SUCCESS) {
    return validationResult;
  }

  return slide->Retract();
}

PneumaticError PneumaticManager::Stop(const std::string& slideName) {
  IPneumaticSlide* slide = nullptr;
  PneumaticError validationResult = ValidateSlide(slideName, &slide);
  if (validationResult != PneumaticError::SUCCESS) {
    return validationResult;
  }

  return slide->Stop();
}

void PneumaticManager::StopAll() {
  std::cout << "[PneumaticManager] Stopping all slides..." << std::endl;

  for (auto& [name, slide] : m_slides) {
    slide->Stop();
  }

  std::cout << "[PneumaticManager] All slides stopped" << std::endl;
}

// ============================================================================
// BLOCKING OPERATIONS
// ============================================================================

PneumaticError PneumaticManager::ExtendAndWait(const std::string& slideName, uint32_t pollIntervalMs) {
  IPneumaticSlide* slide = nullptr;
  PneumaticError validationResult = ValidateSlide(slideName, &slide);
  if (validationResult != PneumaticError::SUCCESS) {
    return validationResult;
  }

  return slide->ExtendAndWait(pollIntervalMs);
}

PneumaticError PneumaticManager::RetractAndWait(const std::string& slideName, uint32_t pollIntervalMs) {
  IPneumaticSlide* slide = nullptr;
  PneumaticError validationResult = ValidateSlide(slideName, &slide);
  if (validationResult != PneumaticError::SUCCESS) {
    return validationResult;
  }

  return slide->RetractAndWait(pollIntervalMs);
}

// ============================================================================
// STATUS & MONITORING
// ============================================================================

PneumaticPosition PneumaticManager::GetPosition(const std::string& slideName) {
  IPneumaticSlide* slide = GetSlide(slideName);
  if (!slide) {
    std::cerr << "[PneumaticManager] Slide '" << slideName << "' not found" << std::endl;
    return PneumaticPosition::UNKNOWN;
  }

  return slide->UpdatePosition();
}

PneumaticStatus PneumaticManager::GetStatus(const std::string& slideName) {
  IPneumaticSlide* slide = GetSlide(slideName);
  if (!slide) {
    std::cerr << "[PneumaticManager] Slide '" << slideName << "' not found" << std::endl;
    PneumaticStatus status;
    status.Valid = false;
    return status;
  }

  return slide->GetStatus();
}

void PneumaticManager::UpdateAllPositions() {
  for (auto& [name, slide] : m_slides) {
    slide->UpdatePosition();
  }
}

std::vector<std::string> PneumaticManager::GetAllSlideNames() const {
  std::vector<std::string> names;
  for (const auto& [name, slide] : m_slides) {
    names.push_back(name);
  }
  return names;
}

std::vector<std::string> PneumaticManager::GetSlidesByPosition(PneumaticPosition position) {
  std::vector<std::string> names;

  for (auto& [name, slide] : m_slides) {
    slide->UpdatePosition();
    if (slide->GetPosition() == position) {
      names.push_back(name);
    }
  }

  return names;
}

// ============================================================================
// CALLBACKS
// ============================================================================

void PneumaticManager::SetGlobalPositionCallback(PneumaticPositionCallback callback) {
  m_globalPositionCallback = callback;

  // Apply to all existing slides
  for (auto& [name, slide] : m_slides) {
    slide->SetPositionCallback(callback);
  }

  std::cout << "[PneumaticManager] Global position callback set for "
    << m_slides.size() << " slides" << std::endl;
}

void PneumaticManager::SetGlobalErrorCallback(PneumaticErrorCallback callback) {
  m_globalErrorCallback = callback;

  // Apply to all existing slides
  for (auto& [name, slide] : m_slides) {
    slide->SetErrorCallback(callback);
  }

  std::cout << "[PneumaticManager] Global error callback set for "
    << m_slides.size() << " slides" << std::endl;
}

void PneumaticManager::SetSlidePositionCallback(const std::string& slideName,
  PneumaticPositionCallback callback) {
  IPneumaticSlide* slide = GetSlide(slideName);
  if (!slide) {
    std::cerr << "[PneumaticManager] Slide '" << slideName << "' not found" << std::endl;
    return;
  }

  slide->SetPositionCallback(callback);
}

void PneumaticManager::SetSlideErrorCallback(const std::string& slideName,
  PneumaticErrorCallback callback) {
  IPneumaticSlide* slide = GetSlide(slideName);
  if (!slide) {
    std::cerr << "[PneumaticManager] Slide '" << slideName << "' not found" << std::endl;
    return;
  }

  slide->SetErrorCallback(callback);
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

PneumaticError PneumaticManager::ValidateSlide(const std::string& slideName,
  IPneumaticSlide** slide) {
  *slide = GetSlide(slideName);

  if (!*slide) {
    std::cerr << "[PneumaticManager] Slide '" << slideName << "' not found" << std::endl;
    return PneumaticError::SLIDE_NOT_FOUND;
  }

  return PneumaticError::SUCCESS;
}