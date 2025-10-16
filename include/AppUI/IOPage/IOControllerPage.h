// IOControllerPage.h - IO device monitoring and control page
#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QListWidget>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <string>

struct ApplicationContext;
class EziIOManager;
class IEziIODevice;

/**
 * @brief IO Controller page with device list and pin control
 *
 * Layout:
 * - Left panel (30%): List of IO devices
 * - Right panel (70%): Split into Input Pins (left) and Output Pins (right)
 */
class IOControllerPage : public QWidget
{
  Q_OBJECT

public:
  explicit IOControllerPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~IOControllerPage();

protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private slots:
  void onDeviceSelected();
  void onRefreshClicked();
  void updateIOStatus();

private:
  void setupUI();
  void createDeviceListPanel();
  void createIOPinPanel();
  void refreshDeviceList();
  void refreshIOPinTable();
  void updatePinRow(QTableWidget* table, int row, int pinNumber,
    const std::string& pinName, bool isOutput, bool currentState);

  EziIOManager* getIOManager();

  ApplicationContext* m_context;

  // Left panel - device list
  QWidget* m_deviceListPanel;
  QListWidget* m_deviceList;
  QPushButton* m_refreshButton;

  // Right panel - IO pins (split into two tables)
  QWidget* m_ioPinPanel;
  QLabel* m_deviceNameLabel;
  QLabel* m_connectionStatusLabel;

  QTableWidget* m_inputPinsTable;   // ✅ NEW - separate input table
  QTableWidget* m_outputPinsTable;  // ✅ NEW - separate output table

  // Polling
  QTimer* m_pollTimer;

  // Current selection
  std::string m_selectedDevice;

  static constexpr int POLL_INTERVAL_MS = 100;
};