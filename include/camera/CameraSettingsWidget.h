// CameraSettingsWidget.h
// Standalone camera exposure settings widget with live preview
// Reusable in CameraView, AuxViewWindow, etc.

#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QComboBox>
#include <QGroupBox>
#include <QTimer>
#include <QSplitter>
#include <QLineEdit>
#include <QPushButton>
#include <string>
#include <memory>

class CameraManager;
class LiveVideoSubscriber;
class CameraImageWidget;

/**
 * @brief Standalone widget for camera exposure settings with live preview
 *
 * Features:
 * - Live camera preview (updates in real-time)
 * - Camera selector dropdown
 * - Auto Exposure checkbox
 * - Exposure text input with Apply button (μs)
 * - Handles ACE/ACE2 camera models correctly
 */
class CameraSettingsWidget : public QWidget
{
  Q_OBJECT

public:
  explicit CameraSettingsWidget(CameraManager* cameraManager, QWidget* parent = nullptr);
  ~CameraSettingsWidget();

  // Live preview control
  void setPreviewEnabled(bool enabled);
  bool isPreviewEnabled() const { return m_previewEnabled; }

  // Get/set current camera
  std::string getCurrentCamera() const { return m_currentCameraId; }
  void setCurrentCamera(const std::string& cameraId);

  // Refresh camera list
  void refreshCameraList();

  // Reload settings from camera
  void reloadSettings();

signals:
  void cameraChanged(const QString& cameraId);
  void settingsChanged();

private slots:
  void onCameraSelectionChanged(int index);
  void onAutoExposureChanged(Qt::CheckState state);  // Qt6 uses Qt::CheckState
  void onExposureApplyClicked();
  void onDisplayTimer();

private:
  void setupUI();
  void updateExposureLabel(int microseconds);
  void applySettings();
  void loadCurrentSettings();
  void startPreview();
  void stopPreview();

  CameraManager* m_cameraManager;
  std::string m_currentCameraId;

  // UI elements
  QComboBox* m_cameraCombo;
  QCheckBox* m_autoExposureCheck;
  QLineEdit* m_exposureInput;        // Text input for exposure value
  QLabel* m_exposureUnitLabel;       // Unit label (us/ms)
  QPushButton* m_exposureApplyBtn;   // Apply button

  // Live preview
  bool m_previewEnabled = true;
  CameraImageWidget* m_previewWidget = nullptr;
  std::shared_ptr<LiveVideoSubscriber> m_subscriber;
  QTimer* m_displayTimer = nullptr;
};
