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
#include <QTimer>  // ✅ ADDED for periodic channel refresh
#include <QComboBox>
#include <map>
#include <string>

struct ProcessInstance;
struct ApplicationContext;
class DeviceComboBox;
class QDoubleSpinBox;

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

  // Get parameter values
  std::map<std::string, std::string> getParameters() const;
  std::string getNickname() const;
  bool isEnabled() const;

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

  // ✅ NEW: Helper to add DataStore channel selector with auto-refresh
  void addDataChannelSelector(const std::string& key, const std::string& label,
    const std::string& value);

  // ✅ NEW: Helper to add IO pin name selector (dropdown with pin names from IO device)
  // ioDeviceParamKey: the parameter key of the IO device selector (e.g., "io_device")
  // pinType: "output" or "input"
  void addIOPinSelector(const std::string& key, const std::string& label,
    const std::string& value, const std::string& ioDeviceParamKey,
    const std::string& pinType = "output");

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