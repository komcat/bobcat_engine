// MotionDeviceSelectorDialog.h
#pragma once

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <string>
#include <vector>

struct ApplicationContext;

/**
 * @brief Dialog for selecting motion devices with live connection status
 * Shows only connected devices by default, with option to show all
 * Can filter by required axes (e.g., "UVW" for 6-axis devices)
 */
class MotionDeviceSelectorDialog : public QDialog
{
  Q_OBJECT

public:
  explicit MotionDeviceSelectorDialog(ApplicationContext* context,
    const std::string& currentDevice,
    bool showOnlyConnected = true,
    const std::string& requiredAxes = "",  // e.g., "UVW" to filter 6-axis devices
    QWidget* parent = nullptr);

  // Get selected device name
  std::string getSelectedDevice() const;

  // Static convenience method
  static std::string SelectDevice(ApplicationContext* context,
    const std::string& currentDevice,
    bool showOnlyConnected = true,
    QWidget* parent = nullptr);

  // Static convenience method with axis filtering
  static std::string SelectDeviceWithAxes(ApplicationContext* context,
    const std::string& currentDevice,
    const std::string& requiredAxes,
    bool showOnlyConnected = true,
    QWidget* parent = nullptr);

private:
  void setupUI();
  void loadDevices();
  void refreshDeviceStatus();
  void onDeviceDoubleClicked(QListWidgetItem* item);
  void onSearchTextChanged(const QString& text);
  void onShowAllToggled(bool checked);
  bool deviceHasRequiredAxes(const std::string& deviceName) const;

  ApplicationContext* m_context;
  std::string m_selectedDevice;
  bool m_showOnlyConnected;
  std::string m_requiredAxes;  // Required axes filter (e.g., "UVW")

  QListWidget* m_deviceList;
  QLineEdit* m_searchEdit;
  QPushButton* m_okButton;
  QPushButton* m_cancelButton;
  QPushButton* m_refreshButton;
  QCheckBox* m_showAllCheckbox;

  std::vector<std::string> m_allDevices;
};
