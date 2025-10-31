// MotionNode.h
#pragma once

#include <QString>
#include <QPointF>
#include <QJsonObject>

/**
 * @brief Represents a motion node in the position graph
 *
 * Motion nodes are linked to named positions and represent
 * waypoints in a motion workflow graph.
 */
class MotionNode
{
public:
  MotionNode();
  MotionNode(const QString& id, const QString& label,
             const QString& device, const QString& position, const QPointF& uiPosition);

  // Node properties
  QString id;           // Unique identifier (e.g., "node_1")
  QString label;        // Display name (e.g., "Grip Lens")
  QString device;       // Device name this node belongs to
  QString position;     // Links to position name in positions section

  // UI properties for graph canvas
  QPointF uiPosition;   // Canvas position (x, y)
  QString color;        // Node color (default: #27ae60)

  // Serialization
  QJsonObject toJson() const;
  static MotionNode fromJson(const QJsonObject& json);

  // Validation
  bool isValid() const;

  // Operators
  bool operator==(const MotionNode& other) const;
};
