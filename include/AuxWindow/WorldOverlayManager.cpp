// WorldOverlayManager.cpp
#include "WorldOverlayManager.h"
#include "CameraConfigManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
#include <QDateTime>
#include <iostream>

WorldOverlayManager::WorldOverlayManager(QObject* parent)
  : QObject(parent)
{
  // Default overlay directory
  m_overlayDir = "config/overlays";
}

WorldOverlayManager::~WorldOverlayManager()
{
}

void WorldOverlayManager::setOverlayDirectory(const QString& path)
{
  m_overlayDir = path;

  // Create directory if it doesn't exist
  QDir dir(path);
  if (!dir.exists()) {
    dir.mkpath(".");
    std::cout << "[WorldOverlayManager] Created overlay directory: "
              << path.toStdString() << std::endl;
  }
}

bool WorldOverlayManager::loadOverlayFile(const QString& filename)
{
  QString filepath = m_overlayDir + "/" + filename;

  // Check cache first
  if (m_cache.contains(filename)) {
    m_currentOverlay = std::make_unique<WorldOverlay::OverlayFile>(m_cache[filename]);
    m_currentFilename = filename;
    emit overlayLoaded(filename);
    std::cout << "[WorldOverlayManager] Loaded from cache: " << filename.toStdString() << std::endl;
    return true;
  }

  // Load from file
  QFile file(filepath);
  if (!file.open(QIODevice::ReadOnly)) {
    std::cout << "[WorldOverlayManager] ERROR: Cannot open file: "
              << filepath.toStdString() << std::endl;
    return false;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    std::cout << "[WorldOverlayManager] ERROR: JSON parse error: "
              << parseError.errorString().toStdString() << std::endl;
    return false;
  }

  if (!doc.isObject()) {
    std::cout << "[WorldOverlayManager] ERROR: Root is not an object" << std::endl;
    return false;
  }

  WorldOverlay::OverlayFile overlay = WorldOverlay::OverlayFile::fromJson(doc.object());

  // Cache the loaded file
  m_cache[filename] = overlay;

  // Set as current
  m_currentOverlay = std::make_unique<WorldOverlay::OverlayFile>(overlay);
  m_currentFilename = filename;

  emit overlayLoaded(filename);
  std::cout << "[WorldOverlayManager] Loaded overlay: " << filename.toStdString()
            << " (" << overlay.layers.size() << " layers)" << std::endl;

  return true;
}

bool WorldOverlayManager::saveOverlayFile(const QString& filename, const WorldOverlay::OverlayFile& file)
{
  QString filepath = m_overlayDir + "/" + filename;

  // Ensure directory exists
  QDir dir(m_overlayDir);
  if (!dir.exists()) {
    dir.mkpath(".");
  }

  QFile outputFile(filepath);
  if (!outputFile.open(QIODevice::WriteOnly)) {
    std::cout << "[WorldOverlayManager] ERROR: Cannot write to file: "
              << filepath.toStdString() << std::endl;
    return false;
  }

  QJsonDocument doc(file.toJson());
  outputFile.write(doc.toJson(QJsonDocument::Indented));
  outputFile.close();

  // Update cache
  m_cache[filename] = file;

  emit overlaySaved(filename);
  std::cout << "[WorldOverlayManager] Saved overlay: " << filename.toStdString() << std::endl;

  return true;
}

bool WorldOverlayManager::saveCurrentOverlay()
{
  if (!m_currentOverlay || m_currentFilename.isEmpty()) {
    std::cout << "[WorldOverlayManager] ERROR: No current overlay to save" << std::endl;
    return false;
  }

  return saveOverlayFile(m_currentFilename, *m_currentOverlay);
}

QStringList WorldOverlayManager::availableOverlayFiles() const
{
  QDir dir(m_overlayDir);
  QStringList filters;
  filters << "*.json" << "*.overlay";

  return dir.entryList(filters, QDir::Files, QDir::Name);
}

WorldOverlay::OverlayFile* WorldOverlayManager::createNewOverlay(const QString& name, const QString& camera)
{
  m_currentOverlay = std::make_unique<WorldOverlay::OverlayFile>();
  m_currentOverlay->name = name;
  m_currentOverlay->camera = camera;
  m_currentOverlay->pxPerMm = m_defaultPxPerMm;  // Use calibrated value

  // Create default layers
  WorldOverlay::Layer templateLayer;
  templateLayer.name = "Template";
  templateLayer.type = WorldOverlay::LayerType::Template;
  templateLayer.color = QColor("#00FF00");  // Green
  templateLayer.visible = true;
  m_currentOverlay->addLayer(templateLayer);

  WorldOverlay::Layer detectedLayer;
  detectedLayer.name = "Detected";
  detectedLayer.type = WorldOverlay::LayerType::Detected;
  detectedLayer.color = QColor("#FF00FF");  // Magenta
  detectedLayer.visible = true;
  m_currentOverlay->addLayer(detectedLayer);

  WorldOverlay::Layer runtimeLayer;
  runtimeLayer.name = "Runtime";
  runtimeLayer.type = WorldOverlay::LayerType::Runtime;
  runtimeLayer.color = QColor("#FFFF00");  // Yellow
  runtimeLayer.visible = true;
  m_currentOverlay->addLayer(runtimeLayer);

  // Generate filename
  m_currentFilename = name.toLower().replace(" ", "_") + ".json";

  std::cout << "[WorldOverlayManager] Created new overlay: " << name.toStdString()
            << " for camera: " << camera.toStdString() << std::endl;

  emit overlayModified();
  return m_currentOverlay.get();
}

WorldOverlay::Layer* WorldOverlayManager::addLayer(const QString& name, WorldOverlay::LayerType type)
{
  if (!m_currentOverlay) {
    return nullptr;
  }

  WorldOverlay::Layer layer;
  layer.name = name;
  layer.type = type;
  layer.visible = true;

  // Set default color based on type
  switch (type) {
    case WorldOverlay::LayerType::Template:
      layer.color = QColor("#00FF00");  // Green
      break;
    case WorldOverlay::LayerType::Detected:
      layer.color = QColor("#FF00FF");  // Magenta
      break;
    case WorldOverlay::LayerType::Runtime:
      layer.color = QColor("#FFFF00");  // Yellow
      break;
    case WorldOverlay::LayerType::Debug:
      layer.color = QColor("#FF0000");  // Red
      break;
  }

  m_currentOverlay->addLayer(layer);
  emit layerAdded(name);
  emit overlayModified();

  return m_currentOverlay->findLayer(name);
}

bool WorldOverlayManager::removeLayer(const QString& layerName)
{
  if (!m_currentOverlay) {
    return false;
  }

  m_currentOverlay->removeLayer(layerName);
  emit layerRemoved(layerName);
  emit overlayModified();

  return true;
}

WorldOverlay::Layer* WorldOverlayManager::findLayer(const QString& layerName)
{
  if (!m_currentOverlay) {
    return nullptr;
  }
  return m_currentOverlay->findLayer(layerName);
}

WorldOverlay::Marker* WorldOverlayManager::addMarker(const QString& layerName, const WorldOverlay::Marker& marker)
{
  if (!m_currentOverlay) {
    return nullptr;
  }

  WorldOverlay::Layer* layer = m_currentOverlay->findLayer(layerName);
  if (!layer) {
    std::cout << "[WorldOverlayManager] ERROR: Layer not found: "
              << layerName.toStdString() << std::endl;
    return nullptr;
  }

  WorldOverlay::Marker newMarker = marker;

  // Generate ID if not provided
  if (newMarker.id.isEmpty()) {
    newMarker.id = generateMarkerId();
  }

  layer->addMarker(newMarker);
  emit markerAdded(layerName, newMarker.id);
  emit overlayModified();

  return layer->findMarker(newMarker.id);
}

bool WorldOverlayManager::removeMarker(const QString& layerName, const QString& markerId)
{
  if (!m_currentOverlay) {
    return false;
  }

  WorldOverlay::Layer* layer = m_currentOverlay->findLayer(layerName);
  if (!layer) {
    return false;
  }

  layer->removeMarker(markerId);
  emit markerRemoved(layerName, markerId);
  emit overlayModified();

  return true;
}

bool WorldOverlayManager::updateMarker(const QString& layerName, const WorldOverlay::Marker& marker)
{
  if (!m_currentOverlay) {
    return false;
  }

  WorldOverlay::Layer* layer = m_currentOverlay->findLayer(layerName);
  if (!layer) {
    return false;
  }

  WorldOverlay::Marker* existing = layer->findMarker(marker.id);
  if (!existing) {
    return false;
  }

  *existing = marker;
  emit markerUpdated(layerName, marker.id);
  emit overlayModified();

  return true;
}

void WorldOverlayManager::clearDetectedLayer()
{
  if (!m_currentOverlay) {
    return;
  }

  WorldOverlay::Layer* layer = m_currentOverlay->findLayer("Detected");
  if (layer) {
    layer->clearMarkers();
    emit overlayModified();
    std::cout << "[WorldOverlayManager] Cleared Detected layer" << std::endl;
  }
}

void WorldOverlayManager::clearRuntimeLayer()
{
  if (!m_currentOverlay) {
    return;
  }

  WorldOverlay::Layer* layer = m_currentOverlay->findLayer("Runtime");
  if (layer) {
    layer->clearMarkers();
    emit overlayModified();
    std::cout << "[WorldOverlayManager] Cleared Runtime layer" << std::endl;
  }
}

double WorldOverlayManager::loadPxPerMmFromCalibration(const QString& calibrationFile)
{
  QFile file(calibrationFile);
  if (!file.open(QIODevice::ReadOnly)) {
    std::cout << "[WorldOverlayManager] Could not open calibration file: "
              << calibrationFile.toStdString() << ", using default px_per_mm" << std::endl;
    return m_defaultPxPerMm;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    std::cout << "[WorldOverlayManager] JSON parse error in calibration file" << std::endl;
    return m_defaultPxPerMm;
  }

  QJsonObject root = doc.object();
  QJsonObject calibration = root["calibration"].toObject();

  double pixelToMmX = calibration["pixel_to_mm_x"].toDouble(0.00248);
  double pixelToMmY = calibration["pixel_to_mm_y"].toDouble(0.00248);

  // Use average if X and Y are different
  double pixelToMm = (pixelToMmX + pixelToMmY) / 2.0;

  if (pixelToMm > 0) {
    m_defaultPxPerMm = 1.0 / pixelToMm;
    std::cout << "[WorldOverlayManager] Loaded calibration: pixel_to_mm=" << pixelToMm
              << ", px_per_mm=" << m_defaultPxPerMm << std::endl;
  }

  return m_defaultPxPerMm;
}

double WorldOverlayManager::loadPxPerMmFromCalibration(const std::string& cameraId, CameraConfigManager* configMgr)
{
  if (configMgr && !cameraId.empty()) {
    CameraConfigData::PixelCalibration cal;
    if (configMgr->GetCalibration(cameraId, cal)) {
      double pixelToMm = (cal.pixelToMmX + cal.pixelToMmY) / 2.0;
      if (pixelToMm > 0) {
        m_defaultPxPerMm = 1.0 / pixelToMm;
        std::cout << "[WorldOverlayManager] Per-camera calibration for '" << cameraId
                  << "': pixel_to_mm=" << pixelToMm << ", px_per_mm=" << m_defaultPxPerMm << std::endl;
        return m_defaultPxPerMm;
      }
    }
    std::cout << "[WorldOverlayManager] No per-camera calibration for '" << cameraId
              << "', falling back to file" << std::endl;
  }

  // Fall back to file-based loading
  return loadPxPerMmFromCalibration();
}

WorldOverlayManager::ScreenPoint WorldOverlayManager::worldToScreen(
    double worldX, double worldY,
    double gantryX, double gantryY,
    double viewCenterX, double viewCenterY,
    double pxPerMm, double zoomFactor) const
{
  ScreenPoint result;

  // Calculate where marker appears on screen relative to gantry position
  // When gantry moves, the camera view shifts - objects appear to move opposite to gantry motion
  double deltaX = worldX - gantryX;  // Positive = marker is to the right of gantry
  double deltaY = worldY - gantryY;  // Positive = marker is above gantry (higher Y in world)

  // Convert to pixels and apply zoom
  double screenOffsetX = deltaX * pxPerMm * zoomFactor;
  double screenOffsetY = deltaY * pxPerMm * zoomFactor;

  // Add to view center
  // X: positive delta -> marker to the right -> add to X
  // Y: positive delta -> marker above gantry -> appears LOWER on screen (higher screen Y)
  result.x = viewCenterX + screenOffsetX;
  result.y = viewCenterY + screenOffsetY;

  // For now, always visible - could add bounds check later
  result.visible = true;

  return result;
}

void WorldOverlayManager::screenToWorld(
    double screenX, double screenY,
    double gantryX, double gantryY,
    double viewCenterX, double viewCenterY,
    double pxPerMm, double zoomFactor,
    double& worldX, double& worldY) const
{
  // Reverse of worldToScreen
  double screenOffsetX = screenX - viewCenterX;
  double screenOffsetY = screenY - viewCenterY;  // Same direction as worldToScreen

  double deltaX = screenOffsetX / (pxPerMm * zoomFactor);
  double deltaY = screenOffsetY / (pxPerMm * zoomFactor);

  // worldX = gantryX + deltaX (reverse of deltaX = worldX - gantryX)
  worldX = gantryX + deltaX;
  worldY = gantryY + deltaY;
}

QString WorldOverlayManager::generateMarkerId() const
{
  // Generate a unique ID using timestamp and random suffix
  QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
  QString uuid = QUuid::createUuid().toString(QUuid::Id128).left(8);
  return "marker_" + timestamp.right(8) + "_" + uuid;
}
