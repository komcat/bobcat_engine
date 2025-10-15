// MotionControllerPageBase.h - Base class for motion controller pages
#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QTimer>
#include <QPushButton>
#include <string>
#include <vector>

struct ApplicationContext;
class UnifiedMotionManager;

/**
 * @brief Base class for motion controller display pages
 *
 * Features:
 * - Device list with enable/connection status
 * - Real-time position display (100ms polling)
 * - Connect/Disconnect buttons per device
 * - Extensible for different controller types (PI, ACS, Newport, etc.)
 */
class MotionControllerPageBase : public QWidget
{
  Q_OBJECT

public:
  explicit MotionControllerPageBase(ApplicationContext* context, QWidget* parent = nullptr);
  virtual ~MotionControllerPageBase();

  // Call this after construction to initialize UI
  void initialize();

  // Pure virtual - must be implemented by subclass
  virtual std::string getControllerType() const = 0;

  // Virtual - can be overridden for customization
  virtual std::vector<std::string> getAxisNames() const;
  virtual QString getPageTitle() const;

protected:
  // Lifecycle
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

  // UI Setup
  void setupUI();
  void createDeviceTable();
  void setupPolling();

  // Data updates
  void refreshDeviceList();
  void updateDeviceRow(int row, const std::string& deviceName);
  void updatePositions();

  // Button actions
  void onConnectClicked(const std::string& deviceName);
  void onDisconnectClicked(const std::string& deviceName);
  void onRefreshClicked();

  // Helpers
  UnifiedMotionManager* getMotionManager();
  int findDeviceRow(const std::string& deviceName) const;

protected:
  ApplicationContext* m_context;

  // UI Components
  QTableWidget* m_deviceTable;
  QPushButton* m_refreshButton;
  QTimer* m_pollTimer;

  // Table column indices (dynamic based on axis count)
  enum ColumnIndex {
    COL_NAME = 0,
    COL_ENABLED,
    COL_CONNECTED,
    COL_AXIS_START  // Axis columns start here
  };

  // Helper to get action column index
  int getActionColumnIndex() const {
    return COL_AXIS_START + getAxisNames().size();
  }

  // Settings
  static constexpr int POLL_INTERVAL_MS = 100;
};