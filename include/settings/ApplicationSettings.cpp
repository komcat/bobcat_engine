// ApplicationSettings.cpp
#include "ApplicationSettings.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

ApplicationSettings::ApplicationSettings()
  : m_loaded(false)
{
}

bool ApplicationSettings::LoadFromFile(const std::string& filepath)
{
  std::ifstream file(filepath);
  if (!file.is_open()) {
    std::cout << "[ApplicationSettings] Failed to open: " << filepath << std::endl;
    return false;
  }

  m_settings.clear();
  std::string currentSection;
  std::string line;

  while (std::getline(file, line)) {
    ParseLine(line, currentSection);
  }

  m_loaded = true;
  m_loadedFilePath = filepath;

  std::cout << "[ApplicationSettings] Loaded " << m_settings.size()
            << " settings from: " << filepath << std::endl;

  return true;
}

void ApplicationSettings::ParseLine(const std::string& line, std::string& currentSection)
{
  std::string trimmed = Trim(line);

  // Skip empty lines and comments
  if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
    return;
  }

  // Section header: [SectionName]
  if (trimmed[0] == '[' && trimmed.back() == ']') {
    currentSection = trimmed.substr(1, trimmed.size() - 2);
    return;
  }

  // Key=Value pair
  size_t equalsPos = trimmed.find('=');
  if (equalsPos != std::string::npos) {
    std::string key = Trim(trimmed.substr(0, equalsPos));
    std::string value = Trim(trimmed.substr(equalsPos + 1));

    // Remove inline comments
    size_t commentPos = value.find(';');
    if (commentPos != std::string::npos) {
      value = Trim(value.substr(0, commentPos));
    }
    commentPos = value.find('#');
    if (commentPos != std::string::npos) {
      value = Trim(value.substr(0, commentPos));
    }

    if (!key.empty()) {
      std::string fullKey = MakeKey(currentSection, key);
      m_settings[fullKey] = value;
    }
  }
}

std::string ApplicationSettings::MakeKey(const std::string& section, const std::string& key) const
{
  if (section.empty()) {
    return key;
  }
  return section + "." + key;
}

std::string ApplicationSettings::Trim(const std::string& str)
{
  size_t start = str.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = str.find_last_not_of(" \t\r\n");
  return str.substr(start, end - start + 1);
}

std::string ApplicationSettings::GetString(const std::string& section, const std::string& key,
                                           const std::string& defaultValue) const
{
  std::string fullKey = MakeKey(section, key);
  auto it = m_settings.find(fullKey);
  if (it != m_settings.end()) {
    return it->second;
  }
  return defaultValue;
}

int ApplicationSettings::GetInt(const std::string& section, const std::string& key,
                                int defaultValue) const
{
  std::string value = GetString(section, key, "");
  if (value.empty()) {
    return defaultValue;
  }
  try {
    return std::stoi(value);
  } catch (...) {
    return defaultValue;
  }
}

double ApplicationSettings::GetDouble(const std::string& section, const std::string& key,
                                      double defaultValue) const
{
  std::string value = GetString(section, key, "");
  if (value.empty()) {
    return defaultValue;
  }
  try {
    return std::stod(value);
  } catch (...) {
    return defaultValue;
  }
}

bool ApplicationSettings::GetBool(const std::string& section, const std::string& key,
                                  bool defaultValue) const
{
  std::string value = GetString(section, key, "");
  if (value.empty()) {
    return defaultValue;
  }

  // Convert to lowercase for comparison
  std::string lower = value;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
    return true;
  }
  if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
    return false;
  }
  return defaultValue;
}

// === Convenience accessors ===

int ApplicationSettings::GetChainExecutionTimeoutSeconds() const
{
  return GetInt("ChainExecution", "AutoContinueSeconds", 3);
}
