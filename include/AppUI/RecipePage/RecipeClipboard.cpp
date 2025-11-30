// RecipeClipboard.cpp
#include "RecipeClipboard.h"
#include "RecipeManager.h"  // For ProcessInstance

void RecipeClipboard::clear()
{
  m_steps.clear();
}

bool RecipeClipboard::hasContent() const
{
  return !m_steps.empty();
}

int RecipeClipboard::count() const
{
  return static_cast<int>(m_steps.size());
}

void RecipeClipboard::copyStep(const ProcessInstance* step)
{
  clear();

  if (step) {
    ClipboardStep clipStep;
    clipStep.processType = step->processType;
    clipStep.userNickname = step->userNickname;
    clipStep.enabled = step->enabled;
    clipStep.parameters = step->parameters;
    m_steps.push_back(clipStep);
  }
}

void RecipeClipboard::copySteps(const std::vector<const ProcessInstance*>& steps)
{
  clear();

  for (const auto* step : steps) {
    if (step) {
      ClipboardStep clipStep;
      clipStep.processType = step->processType;
      clipStep.userNickname = step->userNickname;
      clipStep.enabled = step->enabled;
      clipStep.parameters = step->parameters;
      m_steps.push_back(clipStep);
    }
  }
}

const std::vector<RecipeClipboard::ClipboardStep>& RecipeClipboard::getSteps() const
{
  return m_steps;
}

std::string RecipeClipboard::getSummary() const
{
  if (m_steps.empty()) {
    return "Clipboard empty";
  }

  if (m_steps.size() == 1) {
    return "1 step: " + m_steps[0].userNickname;
  }

  return std::to_string(m_steps.size()) + " steps copied";
}
