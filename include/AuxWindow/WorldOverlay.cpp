// WorldOverlay.cpp
#include "WorldOverlay.h"
#include <QJsonDocument>

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
      break;
    default:
      break;
  }

  obj["line_width_px"] = lineWidthPx;
  obj["label"] = label;
  obj["show_label"] = showLabel;
  obj["font_size"] = fontSize;

  return obj;
}

Marker Marker::fromJson(const QJsonObject& json) {
  Marker m;
  m.id = json["id"].toString();
  m.type = stringToMarkerType(json["type"].toString());
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

  m.label = json["label"].toString();
  m.showLabel = json["show_label"].toBool(true);
  m.fontSize = json["font_size"].toInt(12);

  return m;
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
