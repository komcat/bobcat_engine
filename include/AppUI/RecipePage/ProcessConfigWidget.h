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
#include <map>
#include <string>

struct ProcessInstance;
struct ApplicationContext;
class DeviceComboBox;

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

protected:
  // Override this to create custom parameter fields
  virtual void createParameterFields(const ProcessInstance* instance) = 0;

  // Helper to add a parameter field
  void addParameterField(const std::string& key, const std::string& label,
    const std::string& value);

  // Helper to add device selector (dropdown)
  void addDeviceSelector(const std::string& key, const std::string& label,
    const std::string& value, int deviceType);

  // Helper to add node selector (with browse button)
  void addNodeSelector(const std::string& key, const std::string& label,
    const std::string& value, const std::string& deviceName);

  ApplicationContext* m_context;
  QFormLayout* m_formLayout;
  QCheckBox* m_enabledCheckBox;
  QLineEdit* m_nicknameEdit;

  std::map<std::string, QLineEdit*> m_parameterEdits;
  std::map<std::string, DeviceComboBox*> m_deviceCombos;
};

/**
 * @brief Configuration widget for Pick process
 */
class PickConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit PickConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Place process
 */
class PlaceConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit PlaceConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Pick and Place process
 */
class PickAndPlaceConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit PickAndPlaceConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};