// ApplicationSettings.h
#pragma once
#include <string>
#include <map>

/**
 * @brief Application-wide settings loaded from INI file
 *
 * Provides centralized access to configurable application parameters.
 * Loaded from config/system/applicationSettings.ini (or .local.ini).
 *
 * Usage:
 *   int timeout = context->applicationSettings->GetInt("ChainExecution", "AutoContinueSeconds", 10);
 */
class ApplicationSettings
{
public:
  ApplicationSettings();
  ~ApplicationSettings() = default;

  /**
   * @brief Load settings from INI file
   * @param filepath Path to INI file
   * @return true if loaded successfully
   */
  bool LoadFromFile(const std::string& filepath);

  /**
   * @brief Get string value from settings
   * @param section Section name (e.g., "ChainExecution")
   * @param key Key name (e.g., "AutoContinueSeconds")
   * @param defaultValue Default if not found
   * @return Setting value or default
   */
  std::string GetString(const std::string& section, const std::string& key,
                        const std::string& defaultValue = "") const;

  /**
   * @brief Get integer value from settings
   */
  int GetInt(const std::string& section, const std::string& key,
             int defaultValue = 0) const;

  /**
   * @brief Get double value from settings
   */
  double GetDouble(const std::string& section, const std::string& key,
                   double defaultValue = 0.0) const;

  /**
   * @brief Get boolean value from settings
   */
  bool GetBool(const std::string& section, const std::string& key,
               bool defaultValue = false) const;

  /**
   * @brief Set a value in the in-memory settings map
   * @param section Section name (e.g., "ChainExecution")
   * @param key Key name (e.g., "AutoContinueSeconds")
   * @param value Value to set
   */
  void Set(const std::string& section, const std::string& key,
           const std::string& value);

  // === Convenience accessors for common settings ===

  /**
   * @brief Get chain execution auto-continue timeout in seconds
   * @return Seconds to wait before auto-continue (default: 3)
   */
  int GetChainExecutionTimeoutSeconds() const;

  /**
   * @brief Check if settings file was loaded successfully
   */
  bool IsLoaded() const { return m_loaded; }

  /**
   * @brief Get path of loaded settings file
   */
  std::string GetLoadedFilePath() const { return m_loadedFilePath; }

private:
  // Key format: "Section.Key" -> value
  std::map<std::string, std::string> m_settings;
  bool m_loaded;
  std::string m_loadedFilePath;

  std::string MakeKey(const std::string& section, const std::string& key) const;
  void ParseLine(const std::string& line, std::string& currentSection);
  static std::string Trim(const std::string& str);
};
