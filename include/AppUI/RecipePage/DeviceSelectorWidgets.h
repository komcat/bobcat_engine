// DeviceSelectorWidgets.h
#pragma once

#include <QComboBox>
#include <QDialog>
#include <QListWidget>
#include <string>
#include <vector>

struct ApplicationContext;

/**
 * @brief Smart ComboBox that populates from ApplicationContext
 */
class DeviceComboBox : public QComboBox
{
  Q_OBJECT

public:
  enum class DeviceType {
    Motion,      // Motion devices (gantry-main, hex-left, etc.)
    Camera,      // Camera devices
    IO,          // IO devices (IOBottom, etc.)
    Pneumatic,   // Pneumatic slides
    Lens,        // Camera lenses (from CameraManager)
    TEC,         // TEC devices (thermal controllers)
    PSU,         // Power supply units
    SMU,         // SMU devices (Source Measure Units)
    Lighting     // Lighting controllers
  };

  explicit DeviceComboBox(ApplicationContext* context, DeviceType type, QWidget* parent = nullptr);

  // Get selected device name
  std::string getSelectedDevice() const;

  // Set selected device
  void setSelectedDevice(const std::string& deviceName);

  // Refresh device list from context
  void refreshDevices();

private:
  void populateDevices();

  ApplicationContext* m_context;
  DeviceType m_deviceType;
};

/**
 * @brief Dialog for selecting nodes from motion manager
 * Shows more details than a simple dropdown
 */
class NodeSelectorDialog : public QDialog
{
  Q_OBJECT

public:
  explicit NodeSelectorDialog(ApplicationContext* context,
    const std::string& deviceName,
    const std::string& currentNode,
    QWidget* parent = nullptr);

  // Get selected node name
  std::string getSelectedNode() const;

  // Static convenience method
  static std::string SelectNode(ApplicationContext* context,
    const std::string& deviceName,
    const std::string& currentNode,
    QWidget* parent = nullptr);

private:
  void setupUI();
  void loadNodes();
  void onNodeDoubleClicked(QListWidgetItem* item);
  void onSearchTextChanged(const QString& text);

  ApplicationContext* m_context;
  std::string m_deviceName;
  std::string m_selectedNode;

  QListWidget* m_nodeList;
  QLineEdit* m_searchEdit;
  QPushButton* m_okButton;
  QPushButton* m_cancelButton;

  std::vector<std::string> m_allNodes;
};

/**
 * @brief ComboBox for selecting vision profiles from product folder
 * Scans config/<product_name>/vision/ for .json files
 */
class VisionProfileComboBox : public QComboBox
{
  Q_OBJECT

public:
  explicit VisionProfileComboBox(ApplicationContext* context, QWidget* parent = nullptr);

  // Get selected profile path (full path)
  std::string getSelectedProfile() const;

  // Set selected profile (can be full path or just filename)
  void setSelectedProfile(const std::string& profilePath);

  // Refresh profile list
  void refreshProfiles();

  // Get the vision config directory path
  std::string getVisionConfigDir() const;

private:
  void populateProfiles();

  ApplicationContext* m_context;
  std::string m_visionConfigDir;
  std::vector<std::string> m_profilePaths;  // Full paths
};