// PropertyTableWidget.h
// Table-based property editor (like Visual Studio's property grid)
#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <map>
#include <string>
#include <functional>

struct ApplicationContext;

/**
 * @brief Table-based property editor widget
 *
 * Provides a Visual Studio-style property grid with two columns:
 * - Property (left): Property name/label
 * - Value (right): Editable value widget
 *
 * Supports various value types:
 * - Text fields
 * - Dropdowns (combo boxes)
 * - Checkboxes
 * - Numeric spinboxes
 * - Device selectors
 */
class PropertyTableWidget : public QWidget
{
  Q_OBJECT

public:
  explicit PropertyTableWidget(ApplicationContext* context, QWidget* parent = nullptr);
  virtual ~PropertyTableWidget() = default;

  // Add different property types
  void addTextProperty(const std::string& key, const std::string& label,
                       const std::string& value, const std::string& tooltip = "");

  void addDropdownProperty(const std::string& key, const std::string& label,
                           const std::string& value, const std::vector<std::string>& options,
                           const std::string& tooltip = "");

  void addCheckboxProperty(const std::string& key, const std::string& label,
                           bool value, const std::string& tooltip = "");

  void addNumericProperty(const std::string& key, const std::string& label,
                          double value, double minValue, double maxValue,
                          double step = 1.0, int decimals = 1,
                          const std::string& suffix = "", const std::string& tooltip = "");

  void addDeviceProperty(const std::string& key, const std::string& label,
                         const std::string& value, int deviceType,
                         const std::string& tooltip = "");

  // SMU device selector with browse button (shows registered SMU devices with details)
  void addSMUDeviceProperty(const std::string& key, const std::string& label,
                            const std::string& value, const std::string& tooltip = "");

  // Motion device selector with browse button (shows only connected devices)
  void addMotionDeviceProperty(const std::string& key, const std::string& label,
                               const std::string& value, const std::string& tooltip = "");

  // Motion device selector with axis filtering (e.g., "UVW" for 6-axis devices)
  void addMotionDevicePropertyWithAxes(const std::string& key, const std::string& label,
                                       const std::string& value, const std::string& requiredAxes,
                                       const std::string& tooltip = "");

  void addIOPinProperty(const std::string& key, const std::string& label,
                        const std::string& value, const std::string& ioDeviceKey,
                        const std::string& pinType = "output", const std::string& tooltip = "");

  // Pneumatic slide selector with browse button
  void addSlideProperty(const std::string& key, const std::string& label,
                        const std::string& value, const std::string& tooltip = "");

  // Node selector with browse button (for named positions)
  void addNodeProperty(const std::string& key, const std::string& label,
                       const std::string& value, const std::string& deviceKey,
                       const std::string& tooltip = "");

  // Graph selector dropdown (populates based on device)
  void addGraphProperty(const std::string& key, const std::string& label,
                        const std::string& value, const std::string& deviceKey,
                        const std::string& tooltip = "");

  // Graph node selector dropdown (populates based on device + graph)
  void addGraphNodeProperty(const std::string& key, const std::string& label,
                            const std::string& value, const std::string& deviceKey,
                            const std::string& graphKey, const std::string& tooltip = "");

  // DataStore channel selector dropdown
  void addDataChannelProperty(const std::string& key, const std::string& label,
                              const std::string& value, const std::string& tooltip = "");

  // Add a category header row (non-editable, spans both columns)
  void addCategoryHeader(const std::string& categoryName);

  // Add separator line
  void addSeparator();

  // Get all parameter values
  std::map<std::string, std::string> getParameters() const;

  // Get specific parameter value
  std::string getParameter(const std::string& key) const;

  // Clear all properties
  void clear();

signals:
  void propertyChanged(const QString& key, const QString& value);
  void propertyChanged();  // Generic signal for any change

protected:
  ApplicationContext* m_context;
  QTableWidget* m_table;

  // Storage for widgets and values
  std::map<std::string, QWidget*> m_valueWidgets;
  std::map<std::string, int> m_keyToRow;

  // Helper to create styled label for property column
  QWidget* createPropertyLabel(const std::string& label, const std::string& tooltip = "");

  // Helper to add a row with property label and value widget
  int addPropertyRow(const std::string& key, const std::string& label,
                     QWidget* valueWidget, const std::string& tooltip = "");

  // Apply common table styling
  void applyTableStyle();

  // Get string value from widget
  std::string getWidgetValue(const std::string& key) const;

  // Update table height to fit content (no internal scrollbar)
  void updateTableHeight();
};
