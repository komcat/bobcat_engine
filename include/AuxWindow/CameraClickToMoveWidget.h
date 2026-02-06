// CameraClickToMoveWidget.h - Camera view with click-to-move functionality
#pragma once

#include <QWidget>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTimer>
#include <QSettings>
#include <memory>

class CameraManager;
class UnifiedMotionManager;
class LiveVideoSubscriber;
struct CameraFrameData;
class WorldOverlayManager;
class WorldOverlayRenderer;
struct ApplicationContext;

/**
 * @brief Camera widget with click-to-move feature for gantry alignment
 *
 * Features:
 * - Camera selection dropdown (Camera_1 only for click-to-move)
 * - "Click to Move" checkbox to enable/disable feature
 * - Center crosshair overlay (green) - fixed at image center
 * - Tracking crosshair (cyan) - follows mouse pointer
 * - Click on image to move gantry-main relative to clicked position
 * - Pixel-to-mm conversion factors stored in QSettings
 *
 * Usage:
 *   CameraClickToMoveWidget* widget = new CameraClickToMoveWidget(cameraManager, motionManager);
 *   widget->show();
 */
class CameraClickToMoveWidget : public QWidget
{
  Q_OBJECT

public:
  explicit CameraClickToMoveWidget(CameraManager* cameraManager,
                                    UnifiedMotionManager* motionManager,
                                    QWidget* parent = nullptr,
                                    const std::string& subscriberId = "AuxWindow_ClickToMove");
  ~CameraClickToMoveWidget();

  // Camera selection
  void setCamera(const std::string& cameraId);
  std::string getCurrentCamera() const;
  void selectFirstCameraDelayed();  // ✅ ADD: Public method to trigger delayed selection

  // Click-to-move settings
  void setPixelToMmFactorX(double factor);
  void setPixelToMmFactorY(double factor);
  double getPixelToMmFactorX() const { return m_pixelToMmFactorX; }
  double getPixelToMmFactorY() const { return m_pixelToMmFactorY; }

  // ✅ NEW: Programmatic control of features (for command handler)
  void setClickToMoveEnabled(bool enabled);
  void setCrosshairEnabled(bool enabled);
  bool isClickToMoveEnabled() const { return m_clickToMoveEnabled; }
  bool isCrosshairEnabled() const { return m_centerCrosshairEnabled; }

  // Overlay support - allows showing World Overlay markers on live view
  void setOverlayManager(WorldOverlayManager* overlayManager);
  void setOverlayEnabled(bool enabled);
  bool isOverlayEnabled() const { return m_overlayEnabled; }

signals:
  void cameraChanged(const std::string& cameraId);
  void clickToMoveTriggered(double offsetX_mm, double offsetY_mm);

private slots:
  void onCameraSelected(int index);
  void onRefreshCameras();
  void onUpdateFrame();
  void onClickToMoveToggled(bool checked);
  void onCrosshairToggled(bool checked);
  void onOverlayToggled(bool checked);
  void onZoomChanged(int zoomPercent);
  void onAutoFocusClicked();

protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void showEvent(QShowEvent* event) override;  // ✅ ADD: Delayed camera initialization

private:
  void setupUI();
  void refreshCameraList();
  void subscribeToCameraFeed(const std::string& cameraId);
  void unsubscribeFromCameraFeed();
  void loadSettingsFromQSettings();
  void saveSettingsToQSettings();
  void handleImageClick(const QPoint& pixmapClickPos);
  void updateDisplay();  // Redraw current image with crosshairs

  CameraManager* m_cameraManager;
  UnifiedMotionManager* m_motionManager;

  // UI Components
  QComboBox* m_cameraSelector;
  QPushButton* m_autoFocusButton;
  QLabel* m_cameraInfoLabel;
  QLabel* m_imageLabel;  // Displays camera image
  QCheckBox* m_clickToMoveCheckbox;
  QCheckBox* m_crosshairCheckbox;
  QCheckBox* m_overlayCheckbox;
  QPushButton* m_zoom1xButton;
  QPushButton* m_zoom15xButton;
  QPushButton* m_zoom2xButton;

  // Camera feed
  std::shared_ptr<LiveVideoSubscriber> m_subscriber;
  QTimer* m_frameUpdateTimer;
  QTimer* m_refreshTimer;
  std::string m_currentCameraId;
  std::string m_subscriberId;  // ✅ Unique subscriber ID for this widget instance

  // Image display
  QImage m_currentImage;
  int m_zoomPercent;

  // Click-to-move state
  bool m_clickToMoveEnabled;
  bool m_centerCrosshairEnabled;
  QPoint m_mousePosInPixmap;  // Mouse position relative to pixmap
  bool m_mouseOverImage;

  // Calibration factors (stored in QSettings)
  double m_pixelToMmFactorX;
  double m_pixelToMmFactorY;

  // ✅ ADD: Track initial camera selection
  bool m_initialCameraSelected;

  // Settings keys
  static constexpr const char* SETTINGS_KEY_PIXEL_TO_MM_X = "CameraClickToMove/PixelToMmFactorX";
  static constexpr const char* SETTINGS_KEY_PIXEL_TO_MM_Y = "CameraClickToMove/PixelToMmFactorY";

  // Overlay support
  WorldOverlayManager* m_overlayManager = nullptr;
  WorldOverlayRenderer* m_overlayRenderer = nullptr;
  bool m_overlayEnabled = false;
  double m_gantryX = 0.0;
  double m_gantryY = 0.0;
  double m_gantryZ = 0.0;
};
