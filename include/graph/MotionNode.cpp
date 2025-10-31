// MotionNode.cpp
#include "MotionNode.h"
#include <QJsonArray>

MotionNode::MotionNode()
  : id("")
  , label("")
  , device("")
  , position("")
  , uiPosition(0.0, 0.0)
  , color("#27ae60")  // Default green color
{
}

MotionNode::MotionNode(const QString& id, const QString& label,
                       const QString& device, const QString& position, const QPointF& uiPosition)
  : id(id)
  , label(label)
  , device(device)
  , position(position)
  , uiPosition(uiPosition)
  , color("#27ae60")
{
}

QJsonObject MotionNode::toJson() const
{
  QJsonObject json;
  json["id"] = id;
  json["label"] = label;
  json["device"] = device;
  json["position"] = position;

  // UI properties
  QJsonObject uiJson;
  uiJson["x"] = uiPosition.x();
  uiJson["y"] = uiPosition.y();
  uiJson["color"] = color;
  json["ui"] = uiJson;

  return json;
}

MotionNode MotionNode::fromJson(const QJsonObject& json)
{
  MotionNode node;

  node.id = json["id"].toString();
  node.label = json["label"].toString();
  node.device = json["device"].toString();
  node.position = json["position"].toString();

  // UI properties
  if (json.contains("ui") && json["ui"].isObject()) {
    QJsonObject uiJson = json["ui"].toObject();
    double x = uiJson["x"].toDouble(0.0);
    double y = uiJson["y"].toDouble(0.0);
    node.uiPosition = QPointF(x, y);
    node.color = uiJson["color"].toString("#27ae60");
  }

  return node;
}

bool MotionNode::isValid() const
{
  return !id.isEmpty() && !position.isEmpty();
}

bool MotionNode::operator==(const MotionNode& other) const
{
  return id == other.id;
}
