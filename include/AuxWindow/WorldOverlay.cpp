// WorldOverlay.cpp
#include "WorldOverlay.h"
#include <QJsonDocument>
#include <QDateTime>
#include <QRandomGenerator>
#include <cmath>

namespace WorldOverlay {

// =============================================================================
// MarkerType helpers
// =============================================================================
QString markerTypeToString(MarkerType type) {
  switch (type) {
    case MarkerType::Circle:    return "circle";
    case MarkerType::Crosshair: return "crosshair";
    case MarkerType::Rectangle: return "rectangle";
    case MarkerType::Diamond:   return "diamond";
    case MarkerType::Point:     return "point";
    case MarkerType::Line:      return "line";
    case MarkerType::Text:      return "text";
    default:                    return "circle";
  }
}

MarkerType stringToMarkerType(const QString& str) {
  if (str == "circle")    return MarkerType::Circle;
  if (str == "crosshair") return MarkerType::Crosshair;
  if (str == "rectangle") return MarkerType::Rectangle;
  if (str == "diamond")   return MarkerType::Diamond;
  if (str == "point")     return MarkerType::Point;
  if (str == "line")      return MarkerType::Line;
  if (str == "text")      return MarkerType::Text;
  return MarkerType::Circle;  // Default
}

// =============================================================================
// LayerType helpers
// =============================================================================
QString layerTypeToString(LayerType type) {
  switch (type) {
    case LayerType::Template: return "template";
    case LayerType::Detected: return "detected";
    case LayerType::Runtime:  return "runtime";
    case LayerType::Debug:    return "debug";
    default:                  return "template";
  }
}

LayerType stringToLayerType(const QString& str) {
  if (str == "template") return LayerType::Template;
  if (str == "detected") return LayerType::Detected;
  if (str == "runtime")  return LayerType::Runtime;
  if (str == "debug")    return LayerType::Debug;
  return LayerType::Template;  // Default
}

// =============================================================================
// Marker implementation
// =============================================================================
QJsonObject Marker::toJson() const {
  QJsonObject obj;
  obj["id"] = id;
  obj["type"] = markerTypeToString(type);

  // Template coordinates (master positions)
  obj["template_x"] = templateX;
  obj["template_y"] = templateY;
  obj["template_z"] = templateZ;

  // World coordinates (current transformed positions)
  obj["world_x"] = worldX;
  obj["world_y"] = worldY;
  obj["world_z"] = worldZ;

  // Type-specific properties
  switch (type) {
    case MarkerType::Circle:
      obj["radius_px"] = radiusPx;
      break;
    case MarkerType::Crosshair:
    case MarkerType::Diamond:
    case MarkerType::Point:
      obj["size_px"] = sizePx;
      break;
    case MarkerType::Rectangle:
      obj["width_px"] = widthPx;
      obj["height_px"] = heightPx;
      break;
    case MarkerType::Line:
      obj["end_world_x"] = endWorldX;
      obj["end_world_y"] = endWorldY;
      obj["end_template_x"] = endTemplateX;
      obj["end_template_y"] = endTemplateY;
      break;
    default:
      break;
  }

  obj["line_width_px"] = lineWidthPx;
  obj["label"] = label;
  obj["show_label"] = showLabel;
  obj["font_size"] = fontSize;
  obj["is_reference_point"] = isReferencePoint;

  return obj;
}

Marker Marker::fromJson(const QJsonObject& json) {
  Marker m;
  m.id = json["id"].toString();
  m.type = stringToMarkerType(json["type"].toString());

  // Template coordinates
  m.templateX = json["template_x"].toDouble(json["world_x"].toDouble());
  m.templateY = json["template_y"].toDouble(json["world_y"].toDouble());
  m.templateZ = json["template_z"].toDouble(json["world_z"].toDouble(0.0));

  // World coordinates
  m.worldX = json["world_x"].toDouble();
  m.worldY = json["world_y"].toDouble();
  m.worldZ = json["world_z"].toDouble(0.0);

  m.radiusPx = json["radius_px"].toDouble(50.0);
  m.sizePx = json["size_px"].toDouble(30.0);
  m.widthPx = json["width_px"].toDouble(50.0);
  m.heightPx = json["height_px"].toDouble(50.0);
  m.lineWidthPx = json["line_width_px"].toDouble(2.0);
  m.endWorldX = json["end_world_x"].toDouble();
  m.endWorldY = json["end_world_y"].toDouble();
  m.endTemplateX = json["end_template_x"].toDouble(m.endWorldX);
  m.endTemplateY = json["end_template_y"].toDouble(m.endWorldY);

  m.label = json["label"].toString();
  m.showLabel = json["show_label"].toBool(true);
  m.fontSize = json["font_size"].toInt(12);
  m.isReferencePoint = json["is_reference_point"].toBool(false);

  return m;
}

// =============================================================================
// LocalCoordinateSystem implementation
// =============================================================================
QJsonObject LocalCoordinateSystem::toJson() const {
  QJsonObject obj;

  // Master reference points
  obj["master_f1_x"] = masterF1X;
  obj["master_f1_y"] = masterF1Y;
  obj["master_f1_z"] = masterF1Z;
  obj["master_f2_x"] = masterF2X;
  obj["master_f2_y"] = masterF2Y;
  obj["master_f2_z"] = masterF2Z;
  obj["master_taught"] = masterTaught;

  // Reference images
  if (!masterF1RefImage.isEmpty()) {
    obj["master_f1_ref_image"] = masterF1RefImage;
  }
  if (!masterF2RefImage.isEmpty()) {
    obj["master_f2_ref_image"] = masterF2RefImage;
  }

  // Current part reference points
  obj["current_f1_x"] = currentF1X;
  obj["current_f1_y"] = currentF1Y;
  obj["current_f1_z"] = currentF1Z;
  obj["current_f2_x"] = currentF2X;
  obj["current_f2_y"] = currentF2Y;
  obj["current_f2_z"] = currentF2Z;
  obj["current_taught"] = currentTaught;

  // Settings
  obj["f1f2_is_x_axis"] = f1f2IsXAxis;

  // Computed transform
  obj["rotation"] = rotation;
  obj["translate_x"] = translateX;
  obj["translate_y"] = translateY;
  obj["scale"] = scale;
  obj["transform_valid"] = transformValid;

  return obj;
}

LocalCoordinateSystem LocalCoordinateSystem::fromJson(const QJsonObject& json) {
  LocalCoordinateSystem lcs;

  // Master reference points
  lcs.masterF1X = json["master_f1_x"].toDouble();
  lcs.masterF1Y = json["master_f1_y"].toDouble();
  lcs.masterF1Z = json["master_f1_z"].toDouble();
  lcs.masterF2X = json["master_f2_x"].toDouble();
  lcs.masterF2Y = json["master_f2_y"].toDouble();
  lcs.masterF2Z = json["master_f2_z"].toDouble();
  lcs.masterTaught = json["master_taught"].toBool(false);

  // Reference images
  lcs.masterF1RefImage = json["master_f1_ref_image"].toString();
  lcs.masterF2RefImage = json["master_f2_ref_image"].toString();

  // Current part reference points
  lcs.currentF1X = json["current_f1_x"].toDouble();
  lcs.currentF1Y = json["current_f1_y"].toDouble();
  lcs.currentF1Z = json["current_f1_z"].toDouble();
  lcs.currentF2X = json["current_f2_x"].toDouble();
  lcs.currentF2Y = json["current_f2_y"].toDouble();
  lcs.currentF2Z = json["current_f2_z"].toDouble();
  lcs.currentTaught = json["current_taught"].toBool(false);

  // Settings
  lcs.f1f2IsXAxis = json["f1f2_is_x_axis"].toBool(true);

  // Computed transform
  lcs.rotation = json["rotation"].toDouble();
  lcs.translateX = json["translate_x"].toDouble();
  lcs.translateY = json["translate_y"].toDouble();
  lcs.scale = json["scale"].toDouble(1.0);
  lcs.transformValid = json["transform_valid"].toBool(false);

  return lcs;
}

QString LocalCoordinateSystem::generateRefImageFilename(const QString& prefix) {
  // Generate unique 14-character filename: prefix + 8 chars from timestamp + 4 random chars
  // Example: "F1_20231204A1B2.png" (F1_ + YYYYMMDD + 4 alphanumeric)

  QDateTime now = QDateTime::currentDateTime();
  QString dateStr = now.toString("yyyyMMdd");  // 8 characters

  // Generate 4 random alphanumeric characters
  const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  QString randomStr;
  for (int i = 0; i < 4; ++i) {
    int index = QRandomGenerator::global()->bounded(chars.length());
    randomStr += chars[index];
  }

  // Format: prefix_YYYYMMDDXXXX.png (e.g., "F1_20231204A1B2.png")
  return QString("%1_%2%3.png").arg(prefix).arg(dateStr).arg(randomStr);
}

void LocalCoordinateSystem::computeTransform() {
  if (!masterTaught || !currentTaught) {
    transformValid = false;
    return;
  }

  // Compute master axis vector (F1 → F2)
  double masterDX = masterF2X - masterF1X;
  double masterDY = masterF2Y - masterF1Y;
  double masterLength = std::sqrt(masterDX * masterDX + masterDY * masterDY);

  // Compute current axis vector (F1 → F2)
  double currentDX = currentF2X - currentF1X;
  double currentDY = currentF2Y - currentF1Y;
  double currentLength = std::sqrt(currentDX * currentDX + currentDY * currentDY);

  if (masterLength < 0.001 || currentLength < 0.001) {
    // F1 and F2 are too close, can't compute meaningful transform
    transformValid = false;
    return;
  }

  // Compute angles
  double masterAngle = std::atan2(masterDY, masterDX);
  double currentAngle = std::atan2(currentDY, currentDX);

  // Rotation = difference in angles
  rotation = currentAngle - masterAngle;

  // Scale = ratio of lengths (optional, usually keep at 1.0)
  scale = currentLength / masterLength;

  // Translation: after rotating master F1 around origin, translate to current F1
  // The transform is: point' = rotate(point - masterF1, rotation) * scale + currentF1
  // So translation is effectively handled by the origin shift

  // For simplicity, we compute translation as the shift from rotated master F1 to current F1
  // But since we rotate around masterF1, the translation is just: currentF1 - masterF1 (after rotation)
  translateX = currentF1X - masterF1X;
  translateY = currentF1Y - masterF1Y;

  transformValid = true;
}

void LocalCoordinateSystem::transformPoint(double templateX, double templateY,
                                           double& worldX, double& worldY) const {
  if (!transformValid) {
    // No transform, use template directly
    worldX = templateX;
    worldY = templateY;
    return;
  }

  // Transform: rotate around master F1, scale, then translate
  // 1. Translate to master F1 origin
  double dx = templateX - masterF1X;
  double dy = templateY - masterF1Y;

  // 2. Rotate
  double cosR = std::cos(rotation);
  double sinR = std::sin(rotation);
  double rotatedX = dx * cosR - dy * sinR;
  double rotatedY = dx * sinR + dy * cosR;

  // 3. Scale (optional)
  rotatedX *= scale;
  rotatedY *= scale;

  // 4. Translate to current F1
  worldX = rotatedX + currentF1X;
  worldY = rotatedY + currentF1Y;
}

// =============================================================================
// Layer implementation
// =============================================================================
QJsonObject Layer::toJson() const {
  QJsonObject obj;
  obj["name"] = name;
  obj["type"] = layerTypeToString(type);
  obj["visible"] = visible;
  obj["locked"] = locked;
  obj["color"] = color.name();
  obj["opacity"] = opacity;

  // Local coordinate system
  obj["local_system"] = localSystem.toJson();

  QJsonArray markersArray;
  for (const auto& marker : markers) {
    markersArray.append(marker.toJson());
  }
  obj["markers"] = markersArray;

  return obj;
}

Layer Layer::fromJson(const QJsonObject& json) {
  Layer layer;
  layer.name = json["name"].toString();
  layer.type = stringToLayerType(json["type"].toString());
  layer.visible = json["visible"].toBool(true);
  layer.locked = json["locked"].toBool(false);
  layer.color = QColor(json["color"].toString("#00FF00"));
  layer.opacity = json["opacity"].toDouble(0.8);

  // Local coordinate system
  if (json.contains("local_system")) {
    layer.localSystem = LocalCoordinateSystem::fromJson(json["local_system"].toObject());
  }

  QJsonArray markersArray = json["markers"].toArray();
  for (const auto& markerVal : markersArray) {
    layer.markers.append(Marker::fromJson(markerVal.toObject()));
  }

  return layer;
}

void Layer::addMarker(const Marker& marker) {
  markers.append(marker);
}

void Layer::removeMarker(const QString& markerId) {
  for (int i = 0; i < markers.size(); ++i) {
    if (markers[i].id == markerId) {
      markers.removeAt(i);
      return;
    }
  }
}

Marker* Layer::findMarker(const QString& markerId) {
  for (auto& marker : markers) {
    if (marker.id == markerId) {
      return &marker;
    }
  }
  return nullptr;
}

const Marker* Layer::findMarker(const QString& markerId) const {
  for (const auto& marker : markers) {
    if (marker.id == markerId) {
      return &marker;
    }
  }
  return nullptr;
}

void Layer::clearMarkers() {
  markers.clear();
}

void Layer::applyTransformToMarkers() {
  if (!localSystem.transformValid) {
    return;
  }

  for (auto& marker : markers) {
    // Transform main point
    localSystem.transformPoint(marker.templateX, marker.templateY,
                               marker.worldX, marker.worldY);
    // Keep Z as template Z (or add Z offset if needed)
    marker.worldZ = marker.templateZ;

    // Transform end point for lines
    if (marker.type == MarkerType::Line) {
      localSystem.transformPoint(marker.endTemplateX, marker.endTemplateY,
                                 marker.endWorldX, marker.endWorldY);
    }
  }
}

void Layer::resetMarkersToTemplate() {
  for (auto& marker : markers) {
    marker.worldX = marker.templateX;
    marker.worldY = marker.templateY;
    marker.worldZ = marker.templateZ;
    marker.endWorldX = marker.endTemplateX;
    marker.endWorldY = marker.endTemplateY;
  }
}

// =============================================================================
// OverlayFile implementation
// =============================================================================
QJsonObject OverlayFile::toJson() const {
  QJsonObject obj;
  obj["name"] = name;
  obj["description"] = description;
  obj["camera"] = camera;
  obj["px_per_mm"] = pxPerMm;

  QJsonArray layersArray;
  for (const auto& layer : layers) {
    layersArray.append(layer.toJson());
  }
  obj["layers"] = layersArray;

  return obj;
}

OverlayFile OverlayFile::fromJson(const QJsonObject& json) {
  OverlayFile file;
  file.name = json["name"].toString();
  file.description = json["description"].toString();
  file.camera = json["camera"].toString();
  file.pxPerMm = json["px_per_mm"].toDouble(100.0);

  QJsonArray layersArray = json["layers"].toArray();
  for (const auto& layerVal : layersArray) {
    file.layers.append(Layer::fromJson(layerVal.toObject()));
  }

  return file;
}

void OverlayFile::addLayer(const Layer& layer) {
  layers.append(layer);
}

void OverlayFile::removeLayer(const QString& layerName) {
  for (int i = 0; i < layers.size(); ++i) {
    if (layers[i].name == layerName) {
      layers.removeAt(i);
      return;
    }
  }
}

Layer* OverlayFile::findLayer(const QString& layerName) {
  for (auto& layer : layers) {
    if (layer.name == layerName) {
      return &layer;
    }
  }
  return nullptr;
}

const Layer* OverlayFile::findLayer(const QString& layerName) const {
  for (const auto& layer : layers) {
    if (layer.name == layerName) {
      return &layer;
    }
  }
  return nullptr;
}

QList<QPair<const Layer*, const Marker*>> OverlayFile::getVisibleMarkers() const {
  QList<QPair<const Layer*, const Marker*>> result;

  for (const auto& layer : layers) {
    if (layer.visible) {
      for (const auto& marker : layer.markers) {
        result.append(qMakePair(&layer, &marker));
      }
    }
  }

  return result;
}

} // namespace WorldOverlay
