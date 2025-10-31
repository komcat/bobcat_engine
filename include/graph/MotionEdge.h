// MotionEdge.h
#pragma once

#include <QString>
#include <QJsonObject>

/**
 * @brief Represents a bidirectional edge between two motion nodes
 *
 * Motion edges connect nodes and define the traversal between positions.
 * All edges are bidirectional by design.
 */
class MotionEdge
{
public:
  MotionEdge();
  MotionEdge(const QString& id, const QString& from, const QString& to,
             double maxSpeed = 0.0);

  // Edge properties
  QString id;           // Unique identifier (e.g., "edge_1")
  QString fromNodeId;   // Source node ID
  QString toNodeId;     // Target node ID

  // Motion parameters
  double maxSpeed;      // Maximum speed (0 = unlimited, default)

  // UI properties
  QString label;        // Optional edge label
  QString color;        // Edge color (default: #ffffff)

  // Serialization
  QJsonObject toJson() const;
  static MotionEdge fromJson(const QJsonObject& json);

  // Validation
  bool isValid() const;
  bool connectsNode(const QString& nodeId) const;
  QString getOtherNode(const QString& nodeId) const;

  // Operators
  bool operator==(const MotionEdge& other) const;
};
