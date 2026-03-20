// JogPanelWindow.h - Tabbed interface with Jog and Quick Output
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QButtonGroup>
#include <QTimer>
#include <QTabWidget>
#include <QGroupBox>
#include <QSettings>
#include <vector>
#include <string>
#include <future>

class QSpinBox;

class TransformationMatrixLoader;
class CoordinateTransformer;
struct ApplicationContext;
class JogKeyBindingHandler;
class QuickOutputWidget;
class QuickOutputManager;
class LightingControlWidget;
class JogWorker;

class JogPanelWindow : public QWidget
{
  Q_OBJECT

    friend class JogKeyBindingHandler;

public:
  explicit JogPanelWindow(
    ApplicationContext* context,
    TransformationMatrixLoader* matrixLoader,
    QWidget* parent = nullptr
  );
  ~JogPanelWindow();

  // Show/hide window
  void showPanel();
  void hidePanel();
  bool isPanelVisible() const;

  // ✅ NEW: Refresh device list
  void refreshDeviceList();

  // Get currently selected device
  std::string getSelectedDevice() const;

  // Get current step size in microns
  double getCurrentStepSize() const;

  // Keyboard control indicator
  void setKeyboardActive(bool active);

  // Public methods for keyboard control
  void jogX(double direction);  // direction: +1.0 or -1.0
  void jogY(double direction);
  void jogZ(double direction);
  void jogU(double direction);
  void jogV(double direction);
  void jogW(double direction);
  void cycleStepSizeUp();
  void cycleStepSizeDown();

signals:
  void visibilityChanged(bool visible);

protected:
  void closeEvent(QCloseEvent* event) override;

private slots:
  void onDeviceSelected();
  void onStepSizeSelected();
  void updatePositionDisplay();  // Update position labels
  void updateUVWButtonState();   // Enable/disable U/V/W buttons based on device axes

  // JogWorker signal handlers
  void onJogStarted(QString deviceName, QString axis, double direction);
  void onJogCompleted(QString deviceName, QString axis, bool success, QString message);
  void onJogWorkerLog(QString message);

private:
  void setupUI();
  void createJogTab();
  void createQuickOutputTab();
  void createLightingTab();
  void createDevicePanel(QWidget* parent);
  void createStepSizePanel(QWidget* parent);
  void createMovementPanel(QWidget* parent);
  void createAutoFocusPanel(QWidget* parent);
  void createExposurePanel(QWidget* parent);

  void sendJogCommand(const std::string& axis, double direction);
  void runAutoFocus(const std::string& camera, const std::string& motionDevice,
                    double zRangeMm, double zStepMm, QPushButton* button);
  void applyExposure(const std::string& cameraId, QSpinBox* spinBox);
  void updateStepSizeDisplay();
  void flashButton(QPushButton* button);
  void saveWindowSettings();       // Save window position to settings
  void restoreWindowSettings();    // Restore window position from settings

  ApplicationContext* m_context;
  TransformationMatrixLoader* m_matrixLoader;

  // Tab widget
  QTabWidget* m_tabWidget;

  // Jog tab widgets
  QWidget* m_jogWidget;
  QLabel* m_keyboardIndicator;
  QButtonGroup* m_deviceButtonGroup;
  QButtonGroup* m_stepSizeButtonGroup;
  QLabel* m_currentStepLabel;
  QGroupBox* m_deviceGroupBox;  // ✅ NEW: Store device panel for refresh
  QWidget* m_devicePanelParent;  // ✅ NEW: Store parent widget for refresh

  // Movement buttons
  QPushButton* m_btnXPlus;
  QPushButton* m_btnXMinus;
  QPushButton* m_btnYPlus;
  QPushButton* m_btnYMinus;
  QPushButton* m_btnZPlus;
  QPushButton* m_btnZMinus;
  QPushButton* m_btnUPlus;
  QPushButton* m_btnUMinus;
  QPushButton* m_btnVPlus;
  QPushButton* m_btnVMinus;
  QPushButton* m_btnWPlus;
  QPushButton* m_btnWMinus;

  // ✅ NEW: Position display labels
  QLabel* m_posXLabel;
  QLabel* m_posYLabel;
  QLabel* m_posZLabel;
  QLabel* m_posULabel;
  QLabel* m_posVLabel;
  QLabel* m_posWLabel;

  // ✅ NEW: Timer for position updates
  QTimer* m_positionUpdateTimer;

  // Quick Output tab
  QuickOutputManager* m_quickOutputManager;
  QuickOutputWidget* m_quickOutputWidget;

  // Lighting tab
  LightingControlWidget* m_lightingWidget;

  // Jog worker for async motion commands
  JogWorker* m_jogWorker;

  // State
  std::string m_selectedDevice;
  double m_currentStepSize; // in microns
  bool m_keyboardActive;

  // Auto-focus state
  std::future<void> m_autoFocusFuture;

  // Step sizes in microns
  const std::vector<double> m_stepSizes = {
    0.1, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 250.0, 500.0, 1000.0, 2000.0, 5000.0
  };
};