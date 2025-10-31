// MotionEdge.cpp
#include "MotionEdge.h"

MotionEdge::MotionEdge()
  : id("")
  , fromNodeId("")
  , toNodeId("")
  , maxSpeed(0.0)
  , label("")
  , color("#ffffff")  // Default white color
{
}

MotionEdge::MotionEdge(const QString& id, const QString& from,
                       const QString& to, double maxSpeed)
  : id(id)
  , fromNodeId(from)
  , toNodeId(to)
  , maxSpeed(maxSpeed)
  , label("")
  , color("#ffffff")
{
}

QJsonObject MotionEdge::toJson() const
{
  QJsonObject json;
  json["id"] = id;
  json["from"] = fromNodeId;
  json["to"] = toNodeId;
  json["max_speed"] = maxSpeed;

  if (!label.isEmpty()) {
    json["label"] = label;
  }

  // UI properties
  QJsonObject uiJson;
  uiJson["color"] = color;
  json["ui"] = uiJson;

  return json;
}

MotionEdge MotionEdge::fromJson(const QJsonObject& json)
{
  MotionEdge edge;

  edge.id = json["id"].toString();
  edge.fromNodeId = json["from"].toString();
  edge.toNodeId = json["to"].toString();
  edge.maxSpeed = json["max_speed"].toDouble(0.0);
  edge.label = json["label"].toString("");

  // UI properties
  if (json.contains("ui") && json["ui"].isObject()) {
    QJsonObject uiJson = json["ui"].toObject();
    edge.color = uiJson["color"].toString("#ffffff");
  }

  return edge;
}

bool MotionEdge::isValid() const
{
  return !id.isEmpty() && !fromNodeId.isEmpty() && !toNodeId.isEmpty()
    && fromNodeId != toNodeId;
}

bool MotionEdge::connectsNode(const QString& nodeId) const
{
  return fromNodeId == nodeId || toNodeId == nodeId;
}

QString MotionEdge::getOtherNode(const QString& nodeId) const
{
  if (fromNodeId == nodeId) {
    return toNodeId;
  }
  else if (toNodeId == nodeId) {
    return fromNodeId;
  }
  return QString();
}

bool MotionEdge::operator==(const MotionEdge& other) const
{
  return id == other.id;
}
