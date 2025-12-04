// WorldOverlay.h
// Data structures for world-locked overlay system
#pragma once

#include <QString>
#include <QColor>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>

namespace WorldOverlay {

// =============================================================================
// Marker Types
// =============================================================================
enum class MarkerType {
  Circle,
  Crosshair,
  Rectangle,
  Diamond,
  Point,
  Line,
  Text
};

QString markerTypeToString(MarkerType type);
MarkerType stringToMarkerType(const QString& str);

// =============================================================================
// Marker - A single overlay marker with world coordinates
// =============================================================================
struct Marker {
  QString id;                    // Unique ID (e.g., "fid_top_left")
  MarkerType type = MarkerType::Circle;
  double worldX = 0.0;           // World X coordinate (mm)
  double worldY = 0.0;           // World Y coordinate (mm)
  double worldZ = 0.0;           // World Z coordinate (mm) - optional

  // Type-specific properties
  double radiusPx = 50.0;        // For Circle
  double sizePx = 30.0;          // For Crosshair, Diamond, Point
  double widthPx = 50.0;         // For Rectangle
  double heightPx = 50.0;        // For Rectangle
  double lineWidthPx = 2.0;      // Line thickness
  double endWorldX = 0.0;        // For Line - end point
  double endWorldY = 0.0;        // For Line - end point

  QString label;                 // Optional label text
  bool showLabel = true;         // Show label next to marker
  int fontSize = 12;             // Label font size

  // Serialization
  QJsonObject toJson() const;
  static Marker fromJson(const QJsonObject& json);
};

// =============================================================================
// Layer Types
// =============================================================================
enum class LayerType {
  Template,    // Taught/reference positions (manually created)
  Detected,    // Auto-populated by vision detection
  Runtime,     // Temporary markers during execution
  Debug        // Development/troubleshooting markers
};

QString layerTypeToString(LayerType type);
LayerType stringToLayerType(const QString& str);

// =============================================================================
// Layer - A collection of markers with shared style
// =============================================================================
struct Layer {
  QString name;                  // Layer name (e.g., "Taught Fiducials")
  LayerType type = LayerType::Template;
  bool visible = true;           // Show/hide this layer
  bool locked = false;           // Prevent editing
  QColor color = QColor("#00FF00");  // Default color (green)
  double opacity = 0.8;          // 0.0 to 1.0

  QList<Marker> markers;         // Markers in this layer

  // Serialization
  QJsonObject toJson() const;
  static Layer fromJson(const QJsonObject& json);

  // Marker management
  void addMarker(const Marker& marker);
  void removeMarker(const QString& markerId);
  Marker* findMarker(const QString& markerId);
  const Marker* findMarker(const QString& markerId) const;
  void clearMarkers();
};

// =============================================================================
// OverlayFile - A complete overlay file with multiple layers
// =============================================================================
struct OverlayFile {
  QString name;                  // File display name
  QString description;           // Optional description
  QString camera;                // Associated camera (e.g., "gantry-main")
  double pxPerMm = 100.0;        // Pixels per mm calibration

  QList<Layer> layers;           // All layers in this file

  // Serialization
  QJsonObject toJson() const;
  static OverlayFile fromJson(const QJsonObject& json);

  // Layer management
  void addLayer(const Layer& layer);
  void removeLayer(const QString& layerName);
  Layer* findLayer(const QString& layerName);
  const Layer* findLayer(const QString& layerName) const;

  // Get all visible markers (for rendering)
  QList<QPair<const Layer*, const Marker*>> getVisibleMarkers() const;
};

} // namespace WorldOverlay
