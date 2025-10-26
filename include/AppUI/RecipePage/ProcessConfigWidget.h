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
  void addNodeSelector(const std::string& key, const std::string& label,
    const std::string& value, const std::string& deviceName);

  ApplicationContext* m_context;
  QFormLayout* m_formLayout;
  QCheckBox* m_enabledCheckBox;
  QLineEdit* m_nicknameEdit;

  std::map<std::string, QLineEdit*> m_parameterEdits;
  std::map<std::string, DeviceComboBox*> m_deviceCombos;
  std::map<std::string, QCheckBox*> m_parameterCheckboxes;  // ← ADD THIS LINE
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

/**
 * @brief Configuration widget for Move to Position process
 */
class MoveToPositionConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit MoveToPositionConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Move Relative process
 */
class MoveRelativeConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit MoveRelativeConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Set Output process
 */
class SetOutputConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit SetOutputConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

// ============================================================================
// NEW PROCESS WIDGETS
// ============================================================================

/**
 * @brief Configuration widget for Wait process
 */
class WaitConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit WaitConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};


// ← ADD THIS NEW CLASS HERE:
/**
 * @brief Configuration widget for Ask User process
 */
class AskUserConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit AskUserConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Log Message process
 */
class LogMessageConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit LogMessageConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Move Relative Multi-Axis process
 */
class MoveRelativeMultiAxisConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit MoveRelativeMultiAxisConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Move to Position with Offset process
 */
class MoveToPositionWithOffsetConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit MoveToPositionWithOffsetConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Slide Extend process
 */
class SlideExtendConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit SlideExtendConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Slide Retract process
 */
class SlideRetractConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit SlideRetractConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};


/**
 * @brief Configuration widget for Set TEC Temp process
 */
class SetTECTempConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit SetTECTempConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Set Laser Current process
 */
class SetLaserCurrentConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit SetLaserCurrentConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Read DataStore process
 */
class ReadDataStoreConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit ReadDataStoreConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};


/**
 * @brief Configuration widget for Read Temperature process
 */
class ReadTemperatureConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit ReadTemperatureConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Read Laser Current process
 */
class ReadLaserCurrentConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit ReadLaserCurrentConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for PSU Output On/Off process
 */
class PSUOutputConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit PSUOutputConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for PSU Set Voltage process
 */
class PSUSetVoltageConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit PSUSetVoltageConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for PSU Set Current process
 */
class PSUSetCurrentConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit PSUSetCurrentConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for PSU Read Voltage process
 */
class PSUReadVoltageConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit PSUReadVoltageConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for PSU Read Current process
 */
class PSUReadCurrentConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit PSUReadCurrentConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Group Start process
 */
class GroupStartConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit GroupStartConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Group End process
 */
class GroupEndConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit GroupEndConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Conditional If process
 */
class ConditionalIfConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit ConditionalIfConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Conditional Else process
 */
class ConditionalElseConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit ConditionalElseConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};

/**
 * @brief Configuration widget for Conditional End process
 */
class ConditionalEndConfigWidget : public ProcessConfigWidget
{
  Q_OBJECT

public:
  explicit ConditionalEndConfigWidget(ApplicationContext* context, QWidget* parent = nullptr);

protected:
  void createParameterFields(const ProcessInstance* instance) override;
};