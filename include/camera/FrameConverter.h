// FrameConverter.h
// Simple utility to convert CameraFrameData to QImage
#pragma once

#include "CameraFrameData.h"
#include <QImage>
#include <QRect>
#include <cstring>

class FrameConverter {
public:
  /**
   * @brief Convert CameraFrameData to QImage
   * @param frameData Frame data from camera
   * @return QImage ready for display (or null on error)
   */
  static QImage ConvertToQImage(const CameraFrameData& frameData) {
    if (!frameData.IsValid()) {
      return QImage();
    }

    // Determine format based on channels
    QImage::Format format = (frameData.channels == 3)
      ? QImage::Format_RGB888
      : QImage::Format_Grayscale8;

    // Create QImage with deep copy of data
    QImage image(frameData.width, frameData.height, format);

    if (image.isNull()) {
      return QImage();
    }

    // Copy frame data to QImage
    size_t expectedSize = frameData.width * frameData.height * frameData.channels;
    if (frameData.imageData.size() != expectedSize) {
      return QImage();
    }

    std::memcpy(image.bits(), frameData.imageData.data(), frameData.imageData.size());

    return image;
  }

  /**
   * @brief Apply digital zoom by extracting ROI
   * @param image Source image
   * @param zoomPercent Zoom level (100=full, 50=2x zoom, 25=4x zoom)
   * @return Zoomed image (cropped ROI)
   */
  static QImage ApplyZoom(const QImage& image, int zoomPercent) {
    if (zoomPercent >= 100 || image.isNull()) {
      return image; // No zoom needed
    }

    // Clamp zoom
    if (zoomPercent < 1) zoomPercent = 1;

    // Calculate ROI dimensions
    double zoomFactor = zoomPercent / 100.0;
    int roiWidth = static_cast<int>(image.width() * zoomFactor);
    int roiHeight = static_cast<int>(image.height() * zoomFactor);

    // Ensure at least 1 pixel
    if (roiWidth < 1) roiWidth = 1;
    if (roiHeight < 1) roiHeight = 1;

    // Center the ROI
    int roiX = (image.width() - roiWidth) / 2;
    int roiY = (image.height() - roiHeight) / 2;

    // Extract and return ROI
    return image.copy(QRect(roiX, roiY, roiWidth, roiHeight));
  }
};