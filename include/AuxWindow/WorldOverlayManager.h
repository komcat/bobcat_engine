// WorldOverlayManager.h
// Manages loading, saving, and caching overlay files
#pragma once

#include "WorldOverlay.h"
#include <QString>
#include <QMap>
#include <QDir>
#include <QObject>
#include <memory>

class CameraConfigManager;
class CameraManager;
class UnifiedMotionManager;

/**
 * @brief Manages world overlay files and provides overlay data access
 *
 * Features:
 * - Load/save overlay JSON files
 * - Cache loaded files for performance
 * - Track active overlay per camera
 * - Coordinate transformation helpers
 */
class WorldOverlayManager : public QObject
{
  Q_OBJECT

public:
  explicit WorldOverlayManager(QObject* parent = nullptr);
  ~WorldOverlayManager();

  // Directory management
  void setOverlayDirectory(const QString& path);
  QString overlayDirectory() const { return m_overlayDir; }

  // File operations
  bool loadOverlayFile(const QString& filename);
  bool saveOverlayFile(const QString& filename, const WorldOverlay::OverlayFile& file);
  bool saveCurrentOverlay();
  QStringList availableOverlayFiles() const;

  // Active overlay management
  WorldOverlay::OverlayFile* currentOverlay() { return m_currentOverlay.get(); }
  const WorldOverlay::OverlayFile* currentOverlay() const { return m_currentOverlay.get(); }
  QString currentOverlayFilename() const { return m_currentFilename; }
  bool hasCurrentOverlay() const { return m_currentOverlay != nullptr; }

  // Create new overlay
  WorldOverlay::OverlayFile* createNewOverlay(const QString& name, const QString& camera);

  // Layer operations on current overlay
  WorldOverlay::Layer* addLayer(const QString& name, WorldOverlay::LayerType type);
  bool removeLayer(const QString& layerName);
  WorldOverlay::Layer* findLayer(const QString& layerName);

  // Marker operations
  WorldOverlay::Marker* addMarker(const QString& layerName, const WorldOverlay::Marker& marker);
  bool removeMarker(const QString& layerName, const QString& markerId);
  bool updateMarker(const QString& layerName, const WorldOverlay::Marker& marker);

  // Clear runtime/detected layers
  void clearDetectedLayer();
  void clearRuntimeLayer();

  // Load pixel-to-mm calibration from system config
  double loadPxPerMmFromCalibration(const QString& calibrationFile = "config/SYSTEM/val_pixelmm.json");
  double loadPxPerMmFromCalibration(const std::string& cameraId, CameraConfigManager* configMgr);
  double getDefaultPxPerMm() const { return m_defaultPxPerMm; }

  // Coordinate conversion helpers
  struct ScreenPoint {
    double x;
    double y;
    bool visible;
  };

  // Convert world coordinates to screen coordinates
  ScreenPoint worldToScreen(double worldX, double worldY,
                            double gantryX, double gantryY,
                            double viewCenterX, double viewCenterY,
                            double pxPerMm, double zoomFactor) const;

  // Convert screen coordinates to world coordinates
  void screenToWorld(double screenX, double screenY,
                     double gantryX, double gantryY,
                     double viewCenterX, double viewCenterY,
                     double pxPerMm, double zoomFactor,
                     double& worldX, double& worldY) const;

signals:
  void overlayLoaded(const QString& filename);
  void overlaySaved(const QString& filename);
  void overlayModified();
  void layerAdded(const QString& layerName);
  void layerRemoved(const QString& layerName);
  void markerAdded(const QString& layerName, const QString& markerId);
  void markerRemoved(const QString& layerName, const QString& markerId);
  void markerUpdated(const QString& layerName, const QString& markerId);

private:
  QString m_overlayDir;
  QString m_currentFilename;
  std::unique_ptr<WorldOverlay::OverlayFile> m_currentOverlay;

  // Cached files for quick switching
  QMap<QString, WorldOverlay::OverlayFile> m_cache;

  // Default px_per_mm from calibration
  double m_defaultPxPerMm = 403.23;  // 1 / 0.00248

  // Generate unique marker ID
  QString generateMarkerId() const;
};
