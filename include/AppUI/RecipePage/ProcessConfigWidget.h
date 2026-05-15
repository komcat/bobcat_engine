// ProcessConfigWidget.h
#pragma once

#include <QWidget>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>  // For periodic channel refresh
#include <QComboBox>
#include <map>
#include <string>

struct ProcessInstance;
struct ApplicationContext;
class DeviceComboBox;
class QDoubleSpinBox;
class VisionProfileComboBox;
class PropertyTableWidget;

/**
 * @brief Base class for process configuration widgets
 *
 * Provides common functionality for all process config widgets:
 * - Enabled checkbox
 * - Nickname editing
 * - Parameter change signals
 */
class ProcessConfigWidget : public QWidget
{
  Q_OBJECT

public:
  explicit ProcessConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);
  virtual ~ProcessConfigWidget() = default;

  // Load data from process instance
  virtual void loadFromInstance(const ProcessInstance* instance);

  // Get parameter values (virtual to allow derived classes to override)
  virtual std::map<std::string, std::string> getParameters() const;
  std::string getNickname() const;
  bool isEnabled() const;

  // Get application context (for plugins that need product-relative paths, etc.)
  ApplicationContext* getContext() const { return m_context; }

  // ✅ Get checkbox widget by key (for custom validation)
  QCheckBox* getCheckbox(const std::string& key) const {
    auto it = m_parameterCheckboxes.find(key);
    return (it != m_parameterCheckboxes.end()) ? it->second : nullptr;
  }

signals:
  void parameterChanged();
  void nicknameChanged(const QString& nickname);
  void enabledChanged(bool enabled);

public:
  // Helper methods for creating UI (used by plugins and derived classes)

  // Helper to add pneumatic slide selector (with browse button)
  void addSlideSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add a parameter field
  void addParameterField(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add device selector (dropdown)
  void addDeviceSelector(const std::string& key, const std::string& label,
    const std::string& value, int deviceType);

  // Helper to add vision profile selector (dropdown of .json files from product vision folder)
  void addVisionProfileSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add node selector (with browse button)
  // deviceParamKey: the parameter key of the device selector (e.g., "device")
  void addNodeSelector(const std::string& key, const std::string& label,
    const std::string& value, const std::string& deviceParamKey);

  // Helper to add checkbox parameter
  void addCheckboxParameter(const std::string& key, const std::string& label,
    const std::string& value, const std::string& tooltip = "");

  // Helper to add numeric parameter with validation (QDoubleSpinBox)
  void addNumericParameter(const std::string& key, const std::string& label,
    const std::string& value, double minValue, double maxValue,
    double step = 1.0, int decimals = 1, const std::string& suffix = "");

  // Helper to add graph selector dropdown (populates based on device)
  // deviceParamKey: the parameter key of the device selector (e.g., "device")
  void addGraphSelector(const std::string& key, const std::string& label,
    const std::string& value, const std::string& deviceParamKey);

  // Helper to add graph node selector dropdown (populates based on device + graph)
  // deviceParamKey: the parameter key of the device selector (e.g., "device")
  // graphParamKey: the parameter key of the graph selector (e.g., "graphName")
  void addGraphNodeSelector(const std::string& key, const std::string& label,
    const std::string& value, const std::string& deviceParamKey,
    const std::string& graphParamKey);

  // Helper to add graph selector with FIXED device name (e.g., "gantry-main")
  void addGraphSelectorForDevice(const std::string& key, const std::string& label,
    const std::string& value, const std::string& fixedDeviceName);

  // Helper to add graph node selector with FIXED device name
  // graphParamKey: the parameter key of the graph selector (e.g., "graphName")
  void addGraphNodeSelectorForDevice(const std::string& key, const std::string& label,
    const std::string& value, const std::string& fixedDeviceName,
    const std::string& graphParamKey);

  // ✅ NEW: Helper to add DataStore channel selector with auto-refresh
  void addDataChannelSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // ✅ NEW: Helper to add IO pin name selector (dropdown with pin names from IO device)
  // ioDeviceParamKey: the parameter key of the IO device selector (e.g., "io_device")
  // pinType: "output" or "input"
  void addIOPinSelector(const std::string& key, const std::string& label,
    const std::string& value, const std::string& ioDeviceParamKey,
    const std::string& pinType = "output");

  // ✅ NEW: Helper to add simple dropdown parameter with fixed options
  // Set editable=true to allow manual text input (e.g., custom regex patterns)
  void addDropdownParameter(const std::string& key, const std::string& label,
    const std::string& value, const std::vector<std::string>& options,
    bool editable = false);

  // ✅ NEW: Helper to add PSU channel selector (dynamic based on PSU device config)
  // psuDeviceParamKey: the parameter key of the PSU device selector (e.g., "psu_device")
  void addPSUChannelSelector(const std::string& key, const std::string& label,
    const std::string& value, const std::string& psuDeviceParamKey);

  // Helper to add taught coordinate system selector (with browse button showing available systems)
  void addTaughtCoordinateSystemSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add local point selector (with browse button showing available local points)
  void addLocalPointSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add file selector (QLineEdit + Browse button using QFileDialog::getOpenFileName)
  void addFileSelector(const std::string& key, const std::string& label,
    const std::string& value, const std::string& fileFilter = "All Files (*.*)");

  // Helper to add image selector (QLineEdit + Browse + Clear). On Browse, copies the
  // selected image to <product>/recipe_images/<UID>.<ext> and stores the absolute path
  // of the copy so the recipe no longer depends on the original location.
  void addImageSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add recipe file selector (QLineEdit + Browse button using FileSelectorDialog)
  // Scans config/<product>/recipes/ for available recipe JSON files
  void addRecipeSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add Lua script selector (QLineEdit + Browse button using FileSelectorDialog)
  // Scans config/<product>/scripts/ for available .lua files
  void addScriptSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add folder selector (QLineEdit + Browse button using QFileDialog::getExistingDirectory)
  void addFolderSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add accuracy trainer training set selector (QLineEdit + Browse button)
  void addTrainingSetSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add placement correction set selector (QLineEdit + Browse button)
  void addPlacementSetSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add camera offset calibration set selector (QLineEdit + Browse button)
  void addCameraOffsetSetSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add planar adjustment set selector (QLineEdit + Browse button)
  void addPlanarAdjustmentSetSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add Z offset set selector (QLineEdit + Browse button)
  void addZOffsetSetSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add yaw trainer set selector (QLineEdit + Browse button)
  void addYawTrainerSetSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add edge transfer set selector (QLineEdit + Browse button)
  void addEdgeTransferSetSelector(const std::string& key, const std::string& label,
    const std::string& value);

protected:
  // Override this to create custom parameter fields
  virtual void createParameterFields(const ProcessInstance* instance) = 0;

  ApplicationContext* m_context;
  QFormLayout* m_formLayout;
  QCheckBox* m_enabledCheckBox;
  QLineEdit* m_nicknameEdit;

  std::map<std::string, QLineEdit*> m_parameterEdits;
  std::map<std::string, DeviceComboBox*> m_deviceCombos;
  std::map<std::string, QCheckBox*> m_parameterCheckboxes;
  std::map<std::string, QDoubleSpinBox*> m_parameterSpinBoxes;
  std::map<std::string, QComboBox*> m_parameterCombos;  // For graph/node selectors

private slots:
  // ✅ NEW: Periodic refresh of DataStore channels
  void refreshDataChannels();

private:
  // ✅ NEW: Track data channel selectors for auto-refresh
  std::map<std::string, QComboBox*> m_dataChannelCombos;
  QTimer* m_channelRefreshTimer;

  // ✅ NEW: Track vision profile selectors (need special handling for full path)
  std::map<std::string, VisionProfileComboBox*> m_visionProfileCombos;
};

/**
 * @brief Generic configuration widget that uses process plugins
 *
 * This widget delegates UI creation to the process plugin, enabling
 * a plugin architecture where each process type controls its own UI.
 */
class PluginBasedConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit PluginBasedConfigWidget(ApplicationContext* context, const std::string& processType, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;

private:
  std::string m_processType;
};

// ============================================================================
// All process-specific widgets have been migrated to the plugin architecture.
// Old widget classes removed - now using PluginBasedConfigWidget with IProcessPlugin.
// ============================================================================

/**
 * @brief Table-based configuration widget for processes that prefer VS-style property grid
 *
 * Uses PropertyTableWidget internally for a modern table-based property editor.
 * Plugins that return true from PrefersTableUI() will use this widget instead of
 * PluginBasedConfigWidget.
 */
class TableBasedConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit TableBasedConfigWidget(ApplicationContext* context, const std::string& processType, QWidget* parent = nullptr);

  // Override getParameters to delegate to PropertyTableWidget
  std::map<std::string, std::string> getParameters() const override;

protected:
  void createParameterFields(const ProcessInstance* instance) override;

private:
  std::string m_processType;
  PropertyTableWidget* m_propertyTable;
};