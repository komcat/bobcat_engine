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

  // Template coordinates (taught on master part, NEVER modified after teaching)
  double templateX = 0.0;        // Template X coordinate (mm) - original master position
  double templateY = 0.0;        // Template Y coordinate (mm)
  double templateZ = 0.0;        // Template Z coordinate (mm)

  // World coordinates (computed from template + local system transform)
  double worldX = 0.0;           // World X coordinate (mm) - current transformed position
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
  double endTemplateX = 0.0;     // For Line - template end point
  double endTemplateY = 0.0;     // For Line - template end point

  QString label;                 // Optional label text
  bool showLabel = true;         // Show label next to marker
  int fontSize = 12;             // Label font size

  bool isReferencePoint = false; // True if this is F1 or F2 reference point

  // Serialization
  QJsonObject toJson() const;
  static Marker fromJson(const QJsonObject& json);

  // Initialize template from world (when first teaching)
  void initializeTemplate() {
    templateX = worldX;
    templateY = worldY;
    templateZ = worldZ;
    endTemplateX = endWorldX;
    endTemplateY = endWorldY;
  }
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
// LocalCoordinateSystem - 2-point alignment system for part-to-part transform
// =============================================================================
struct LocalCoordinateSystem {
  // Master reference points (taught once on master part)
  double masterF1X = 0.0, masterF1Y = 0.0, masterF1Z = 0.0;
  double masterF2X = 0.0, masterF2Y = 0.0, masterF2Z = 0.0;
  bool masterTaught = false;     // True if master F1/F2 have been taught

  // Reference images for F1/F2 (captured when teaching master)
  // Filenames are 14-character unique IDs (e.g., "F1_20231204A1B2.png")
  QString masterF1RefImage;      // Filename of F1 reference image
  QString masterF2RefImage;      // Filename of F2 reference image

  // Current part reference points (re-taught per part change)
  double currentF1X = 0.0, currentF1Y = 0.0, currentF1Z = 0.0;
  double currentF2X = 0.0, currentF2Y = 0.0, currentF2Z = 0.0;
  bool currentTaught = false;    // True if current F1/F2 have been taught

  // Axis definition
  bool f1f2IsXAxis = true;       // True = F1→F2 defines X-axis, False = Y-axis

  // Computed transform (auto-calculated when F1/F2 changes)
  double rotation = 0.0;         // Radians
  double translateX = 0.0;       // mm
  double translateY = 0.0;       // mm
  double scale = 1.0;            // Scale factor (optional, usually 1.0)
  bool transformValid = false;   // True if transform has been computed

  // Serialization
  QJsonObject toJson() const;
  static LocalCoordinateSystem fromJson(const QJsonObject& json);

  // Compute transform from master to current
  void computeTransform();

  // Apply transform to a point (template → world)
  void transformPoint(double templateX, double templateY,
                      double& worldX, double& worldY) const;

  // Get rotation in degrees for display
  double getRotationDegrees() const { return rotation * 180.0 / 3.14159265358979; }

  // Generate unique 14-character reference image filename
  static QString generateRefImageFilename(const QString& prefix);
};

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

  // Local coordinate system for this layer
  LocalCoordinateSystem localSystem;

  // Serialization
  QJsonObject toJson() const;
  static Layer fromJson(const QJsonObject& json);

  // Marker management
  void addMarker(const Marker& marker);
  void removeMarker(const QString& markerId);
  Marker* findMarker(const QString& markerId);
  const Marker* findMarker(const QString& markerId) const;
  void clearMarkers();

  // Transform all markers using local coordinate system
  void applyTransformToMarkers();

  // Reset all markers to template positions (no transform)
  void resetMarkersToTemplate();
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
