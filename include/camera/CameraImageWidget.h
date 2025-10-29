#pragma once

#include <QWidget>
#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QVBoxLayout>
#include "CameraFrameData.h"
#include <atomic>

/**
 * @brief Reusable camera image viewport widget
 *
 * Pure image display with optimized rendering:
 * - Reusable image buffer (no allocation per frame)
 * - Fast scaling for real-time display
 * - Optional zoom support
 *
 * Usage:
 *   CameraImageWidget* viewport = new CameraImageWidget();
 *   viewport->setFrame(frameData);  // Update with new frame
 */
class CameraImageWidget : public QWidget
{
  Q_OBJECT

public:
  explicit CameraImageWidget(QWidget* parent = nullptr);
  ~CameraImageWidget() = default;

  // Main API - set frame to display
  void setFrame(const CameraFrameData& frameData);

  // Clear display
  void clearDisplay();

  // Zoom control (optional)
  void setZoomPercent(int percent);  // 100 = no zoom, 50 = 2x zoom, etc.
  int getZoomPercent() const { return m_zoomPercent.load(); }

  // Crosshair overlay
  void setCrosshairEnabled(bool enabled);
  bool isCrosshairEnabled() const { return m_crosshairEnabled.load(); }
  void setCrosshairColor(const QColor& color);

  // Display properties
  void setMinimumDisplaySize(int width, int height);
  void setBackgroundColor(const QColor& color);
  void setPlaceholderText(const QString& text);

  // Scaling mode
  enum class ScalingMode {
    Fast,       // Qt::FastTransformation (default for real-time)
    Smooth      // Qt::SmoothTransformation (better quality, slower)
  };
  void setScalingMode(ScalingMode mode);

  // Force refresh - useful when page is shown
  void forceRefresh();

signals:
  void frameDisplayed();  // Emitted after frame is rendered
  void renderError(QString error);

protected:
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;  // ✅ NEW - Ensure background is painted
  void showEvent(QShowEvent* event) override;     // ✅ NEW - Force refresh on show

private:
  // UI Component
  QLabel* m_displayLabel;

  // Optimized rendering
  QImage m_reusableBuffer;
  std::atomic<int> m_zoomPercent;
  ScalingMode m_scalingMode;

  // Crosshair overlay
  std::atomic<bool> m_crosshairEnabled;
  QColor m_crosshairColor;

  // Helper methods
  QImage applyZoom(const QImage& source);
  bool updateReusableBuffer(const CameraFrameData& frameData);
  void renderFrame(const QImage& image);
};