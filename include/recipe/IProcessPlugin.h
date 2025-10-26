// IProcessPlugin.h
#pragma once

#include <string>
#include <map>
#include <memory>

// Forward declarations
class SequenceBuilder;
class ProcessConfigWidget;
struct ProcessInstance;

/**
 * @brief Interface for process type plugins
 *
 * Each process type (Pick, Place, Manual Adjust, etc.) implements this interface
 * to provide metadata, default parameters, UI configuration, and sequence building.
 *
 * This enables a plugin architecture where:
 * - Each process is defined in ONE place
 * - Processes self-register at startup
 * - Adding new processes requires only ONE new plugin class
 */
class IProcessPlugin
{
public:
  virtual ~IProcessPlugin() = default;

  //===========================================================================
  // METADATA
  //===========================================================================

  /**
   * @brief Get the process type name (e.g., "Manual Adjust", "Pick", "Wait")
   * @return Process type name (must be unique)
   */
  virtual std::string GetName() const = 0;

  /**
   * @brief Get the category this process belongs to
   * @return Category name: "Motion", "System", "I/O & Pneumatics",
   *         "TEC & Laser", "Power Supply", "Data", "Control Flow"
   */
  virtual std::string GetCategory() const = 0;

  /**
   * @brief Get a human-readable description of what this process does
   * @return Description string (optional, can return empty string)
   */
  virtual std::string GetDescription() const = 0;

  //===========================================================================
  // PARAMETERS
  //===========================================================================

  /**
   * @brief Get default parameters for a new instance of this process
   * @return Map of parameter name -> default value
   *
   * Example:
   *   {{"message", "Please adjust"}, {"device_name", "gantry-main"}, {"step_size_um", "10"}}
   */
  virtual std::map<std::string, std::string> GetDefaultParameters() const = 0;

  //===========================================================================
  // UI CONFIGURATION
  //===========================================================================

  /**
   * @brief Create the configuration UI for this process
   * @param widget The ProcessConfigWidget to populate with controls
   * @param instance The process instance with current parameter values
   *
   * Use helper methods on widget:
   * - widget->addParameterField(key, label, value)
   * - widget->addDeviceSelector(key, label, value, deviceType)
   * - widget->addCheckboxParameter(key, label, value)
   * - widget->addNodeSelector(key, label, value, deviceName)
   * - widget->addSlideSelector(key, label, value)
   */
  virtual void CreateConfigUI(
    ProcessConfigWidget* widget,
    const ProcessInstance* instance) = 0;

  //===========================================================================
  // SEQUENCE BUILDING
  //===========================================================================

  /**
   * @brief Build the sequence actions for this process
   * @param sequence The SequenceBuilder to add actions to
   * @param step The process instance with parameter values
   *
   * Use sequence->AddAction(), sequence->BeginLoop(), etc.
   *
   * Example:
   *   sequence->AddAction("system", "ManualAdjust", params);
   */
  virtual void BuildSequence(
    SequenceBuilder* sequence,
    const ProcessInstance* step) = 0;
};
