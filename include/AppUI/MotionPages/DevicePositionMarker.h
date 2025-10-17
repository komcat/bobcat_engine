// DevicePositionMarker.h
#pragma once
#include <QGraphicsItemGroup>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QString>

/**
 * @brief Live position marker for current device position
 * - Yellow cross marker
 * - Semi-transparent label box with device name on top
 * - Updates in real-time
 */
class DevicePositionMarker : public QGraphicsItemGroup
{
public:
  explicit DevicePositionMarker(const QString& deviceName, QGraphicsItem* parent = nullptr);

  // Update position
  void updatePosition(double x, double y);

  // Update device name
  void setDeviceName(const QString& name);

  // Show/hide marker
  void setVisible(bool visible);

private:
  void createMarker();
  void createLabel();

  QString m_deviceName;

  // Marker components
  QGraphicsLineItem* m_crossH;    // Horizontal line
  QGraphicsLineItem* m_crossV;    // Vertical line
  QGraphicsRectItem* m_labelBox;  // Label background
  QGraphicsTextItem* m_labelText; // Device name text

  static constexpr double CROSS_SIZE = 20.0;
  static constexpr double LABEL_OFFSET_Y = -50.0;
};