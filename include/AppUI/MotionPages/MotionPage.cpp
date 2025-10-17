// MotionPage.cpp
#include "MotionPage.h"
#include "NewPositionDialog.h"
#include <QHBoxLayout>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDebug>

MotionPage::MotionPage(UnifiedMotionManager* motionManager,
  TransformationMatrixLoader* matrixLoader,
  QWidget* parent)
  : QWidget(parent)
  , m_motionManager(motionManager)
  , m_matrixLoader(matrixLoader)
  , m_controlPanel(nullptr)
  , m_graphicsView(nullptr)
  , m_livePositionTimer(nullptr)
  , m_labelOffsetsFilePath("config/position_labels.json")  // ADD THIS
{
  setupUI();
  loadDevices();
  loadLabelOffsets();  // ADD THIS

  // Setup live position polling timer
  m_livePositionTimer = new QTimer(this);
  m_livePositionTimer->setInterval(100);
  connect(m_livePositionTimer, &QTimer::timeout, this, &MotionPage::onUpdateLivePosition);
  m_livePositionTimer->start();

  // Connect label offset changes to save
  connect(m_graphicsView, &MotionGraphicsView::labelOffsetChanged,
    this, &MotionPage::onLabelOffsetChanged);  // ADD THIS
}


// MotionPage.cpp - Add to setupUI() connections
// MotionPage.cpp - Update setupUI()
void MotionPage::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // NEW - Top bar with back button
  QWidget* topBar = new QWidget();
  topBar->setStyleSheet("QWidget { background-color: white; border-bottom: 2px solid #bdc3c7; }");
  topBar->setFixedHeight(60);

  QHBoxLayout* topLayout = new QHBoxLayout(topBar);
  topLayout->setContentsMargins(15, 10, 15, 10);

  // Back button
  m_backButton = new QPushButton("← Back to Main Menu");
  m_backButton->setFixedHeight(40);
  m_backButton->setStyleSheet(
    "QPushButton {"
    "  background-color: #95a5a6;"
    "  color: white;"
    "  border: 2px solid #7f8c8d;"
    "  border-radius: 6px;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "  padding: 8px 20px;"
    "}"
    "QPushButton:hover {"
    "  background-color: #7f8c8d;"
    "}"
    "QPushButton:pressed {"
    "  background-color: #6c7a89;"
    "}"
  );
  topLayout->addWidget(m_backButton);

  // Title
  QLabel* titleLabel = new QLabel("Motion Control");
  titleLabel->setStyleSheet(
    "QLabel {"
    "  color: #2c3e50;"
    "  font-size: 20px;"
    "  font-weight: bold;"
    "}"
  );
  topLayout->addWidget(titleLabel, 1, Qt::AlignCenter);

  // Spacer to balance the layout
  topLayout->addSpacing(m_backButton->sizeHint().width());

  mainLayout->addWidget(topBar);

  // Content area with left panel and graphics view
  QWidget* contentWidget = new QWidget();
  QHBoxLayout* contentLayout = new QHBoxLayout(contentWidget);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);

  // Left Panel (20%)
  m_controlPanel = new MotionControlPanel();
  m_controlPanel->setMinimumWidth(250);
  m_controlPanel->setMaximumWidth(400);
  contentLayout->addWidget(m_controlPanel, 2);

  // Right Panel (80%)
  m_graphicsView = new MotionGraphicsView();
  contentLayout->addWidget(m_graphicsView, 8);

  mainLayout->addWidget(contentWidget);

  // Connect back button
  connect(m_backButton, &QPushButton::clicked, this, &MotionPage::backToMainMenu);

  // Connect Signals
  connect(m_controlPanel, &MotionControlPanel::deviceChanged,
    this, &MotionPage::onDeviceChanged);

  connect(m_controlPanel, &MotionControlPanel::moveToPositionClicked,
    this, &MotionPage::onMoveToPositionClicked);

  connect(m_controlPanel, &MotionControlPanel::updatePositionClicked,
    this, &MotionPage::onUpdatePositionClicked);

  connect(m_controlPanel, &MotionControlPanel::createPositionClicked,
    this, &MotionPage::onCreatePositionClicked);

  connect(m_controlPanel, &MotionControlPanel::homeClicked,
    this, &MotionPage::onHomeClicked);

  connect(m_controlPanel, &MotionControlPanel::stopClicked,
    this, &MotionPage::onStopClicked);

  connect(m_graphicsView, &MotionGraphicsView::positionSelected,
    this, &MotionPage::onPositionSelected);

  connect(m_graphicsView, &MotionGraphicsView::positionDeselected,
    this, &MotionPage::onPositionDeselected);

  setStyleSheet("QWidget { background-color: #ecf0f1; }");
}

void MotionPage::loadDevices()
{
  if (!m_motionManager) {
    return;
  }

  // Get all device names from motion manager
  std::vector<std::string> deviceNames = m_motionManager->GetAllDeviceNames();

  QStringList devices;
  for (const auto& name : deviceNames) {
    devices << QString::fromStdString(name);
  }

  m_controlPanel->setDevices(devices);

  // Load positions for first device
  if (!devices.isEmpty()) {
    m_currentDevice = devices.first();
    loadPositionsForDevice(m_currentDevice);
  }
}


void MotionPage::loadPositionsForDevice(const QString& deviceName)
{
  m_currentDevice = deviceName;
  m_currentPositions.clear();

  // Try multiple possible paths for motor_devices.json
  QStringList possiblePaths = {
      "config/motor_devices.local.json",
      "motor_devices.local.json",
      "config/motor_devices.json",
      "motor_devices.json",
      "../config/motor_devices.json"
  };

  QFile file;
  bool fileFound = false;
  QString filePath;

  for (const QString& path : possiblePaths) {
    file.setFileName(path);
    if (file.exists()) {
      qDebug() << "Found motor config file at:" << path;
      filePath = path;
      fileFound = true;
      break;
    }
  }

  if (!fileFound) {
    qWarning() << "Could not find motor_devices.json in any location";
    m_graphicsView->clearPositions();
    return;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Could not open motor_devices.json";
    m_graphicsView->clearPositions();
    return;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject()) {
    qWarning() << "Invalid JSON format in motor_devices.json";
    m_graphicsView->clearPositions();
    return;
  }

  QJsonObject root = doc.object();

  // Check if named_positions section exists
  if (!root.contains("named_positions")) {
    qWarning() << "No 'named_positions' section found in motor_devices.json";
    m_graphicsView->clearPositions();
    return;
  }

  QJsonObject namedPositions = root["named_positions"].toObject();

  if (!namedPositions.contains(deviceName)) {
    qWarning() << "No positions found for device:" << deviceName;
    m_graphicsView->clearPositions();
    return;
  }

  QJsonObject devicePositions = namedPositions[deviceName].toObject();

  qDebug() << "Loading positions for device:" << deviceName;

  // Transform positions to global coordinates
  for (auto it = devicePositions.begin(); it != devicePositions.end(); ++it) {
    QString posName = it.key();
    QJsonObject pos = it.value().toObject();

    double localX = pos["x"].toDouble();
    double localY = pos["y"].toDouble();
    double localZ = pos["z"].toDouble();

    // Transform to global coordinates
    double globalX, globalY, globalZ;
    transformLocalToGlobal(localX, localY, localZ, globalX, globalY, globalZ);

    QVector<double> values = {
        globalX,  // Store global X
        globalY,  // Store global Y
        globalZ,  // Store global Z
        pos["u"].toDouble(),
        pos["v"].toDouble(),
        pos["w"].toDouble()
    };

    m_currentPositions[posName] = values;
    qDebug() << "  -" << posName
      << "Local:" << localX << localY << localZ
      << "-> Global:" << globalX << globalY << globalZ;
  }

  // Load label offsets for this device
  loadLabelOffsets();

  // Update graphics view with positions AND label offsets
  m_graphicsView->updatePositions(m_currentPositions, m_labelOffsets);

  // Auto-fit and center view
  m_graphicsView->resetZoom();

  qDebug() << "Successfully loaded" << m_currentPositions.size()
    << "positions for" << deviceName;
}



// NEW - Coordinate transformation method
void MotionPage::transformLocalToGlobal(double localX, double localY, double localZ,
  double& globalX, double& globalY, double& globalZ)
{
  // Check if we have transformation matrix
  if (!m_matrixLoader || m_currentDevice.isEmpty()) {
    // No transformation - pass through
    globalX = localX;
    globalY = localY;
    globalZ = localZ;
    return;
  }

  // Get transformation matrix for current device
  const auto* matrix = m_matrixLoader->GetMatrix(m_currentDevice.toStdString());

  if (!matrix) {
    // No matrix found - pass through
    qWarning() << "No transformation matrix for device:" << m_currentDevice;
    globalX = localX;
    globalY = localY;
    globalZ = localZ;
    return;
  }

  // Apply 3x3 transformation matrix
  // [globalX]   [M11 M12 M13] [localX]
  // [globalY] = [M21 M22 M23] [localY]
  // [globalZ]   [M31 M32 M33] [localZ]

  globalX = (*matrix)[0] * localX + (*matrix)[1] * localY + (*matrix)[2] * localZ;
  globalY = (*matrix)[3] * localX + (*matrix)[4] * localY + (*matrix)[5] * localZ;
  globalZ = (*matrix)[6] * localX + (*matrix)[7] * localY + (*matrix)[8] * localZ;
}



void MotionPage::onDeviceChanged(const QString& deviceName)
{
  m_currentDevice = deviceName;
  loadPositionsForDevice(deviceName);  // This now auto-fits the view

  // Clear selection
  m_controlPanel->clearSelectedPosition();
  m_selectedPositionName.clear();
}

// MotionPage.cpp - Fix the format string
void MotionPage::onUpdatePositionClicked()
{
  if (m_selectedPositionName.isEmpty()) {
    QMessageBox::warning(this, "No Selection",
      "Please select a position from the map first.");
    return;
  }

  // Get current device position
  double x, y, z, u, v, w;
  if (!getCurrentDevicePosition(x, y, z, u, v, w)) {
    QMessageBox::critical(this, "Error",
      "Failed to get current device position.");
    return;
  }

  // Confirm with user - FIX: Use .arg() instead of printf-style formatting
  QMessageBox::StandardButton reply = QMessageBox::question(
    this,
    "Update Position",
    QString("Update saved position '%1' with current device position?\n\n"
      "Current: X=%2, Y=%3, Z=%4\n"
      "         U=%5, V=%6, W=%7")
    .arg(m_selectedPositionName)
    .arg(x, 0, 'f', 3)
    .arg(y, 0, 'f', 3)
    .arg(z, 0, 'f', 3)
    .arg(u, 0, 'f', 3)
    .arg(v, 0, 'f', 3)
    .arg(w, 0, 'f', 3),
    QMessageBox::Yes | QMessageBox::No
  );

  if (reply == QMessageBox::Yes) {
    if (updatePositionInFile(m_currentDevice, m_selectedPositionName,
      x, y, z, u, v, w)) {
      QMessageBox::information(this, "Success",
        "Position updated successfully!");

      // Reload positions
      loadPositionsForDevice(m_currentDevice);
    }
    else {
      QMessageBox::critical(this, "Error",
        "Failed to update position in file.");
    }
  }
}


void MotionPage::onCreatePositionClicked()
{
  // Get current device position
  double x, y, z, u, v, w;
  if (!getCurrentDevicePosition(x, y, z, u, v, w)) {
    QMessageBox::critical(this, "Error",
      "Failed to get current device position.");
    return;
  }

  // Show dialog
  NewPositionDialog dialog(m_currentDevice, x, y, z, u, v, w, this);

  if (dialog.exec() == QDialog::Accepted) {
    QString name = dialog.getPositionName();

    // Check if position already exists
    if (m_currentPositions.contains(name)) {
      QMessageBox::warning(this, "Position Exists",
        QString("Position '%1' already exists. "
          "Please use a different name.").arg(name));
      return;
    }

    // Save to file
    if (savePositionToFile(m_currentDevice, name,
      dialog.getX(), dialog.getY(), dialog.getZ(),
      dialog.getU(), dialog.getV(), dialog.getW())) {
      QMessageBox::information(this, "Success",
        QString("Position '%1' created successfully!").arg(name));

      // Reload positions
      loadPositionsForDevice(m_currentDevice);
    }
    else {
      QMessageBox::critical(this, "Error",
        "Failed to save position to file.");
    }
  }
}

void MotionPage::onHomeClicked()
{
  if (m_currentDevice.isEmpty()) {
    return;
  }

  QMessageBox::StandardButton reply = QMessageBox::question(
    this,
    "Home Device",
    QString("Send device '%1' to home position?").arg(m_currentDevice),
    QMessageBox::Yes | QMessageBox::No
  );

  if (reply == QMessageBox::Yes) {
    bool success = m_motionManager->MoveToNamedPosition(
      m_currentDevice.toStdString(),
      "home"
    );

    if (!success) {
      QMessageBox::critical(this, "Error",
        "Failed to home device.");
    }
  }
}

void MotionPage::onStopClicked()
{
  QMessageBox::StandardButton reply = QMessageBox::question(
    this,
    "EMERGENCY STOP",
    "STOP ALL MOTION ON ALL DEVICES?",
    QMessageBox::Yes | QMessageBox::No
  );

  if (reply == QMessageBox::Yes) {
    m_motionManager->EmergencyStopAll();

    QMessageBox::information(this, "Stopped",
      "All devices have been stopped.");
  }
}

void MotionPage::onPositionSelected(const QString& name, double x, double y, double z)
{
  m_selectedPositionName = name;

  // Get full position data (including u, v, w)
  if (m_currentPositions.contains(name)) {
    QVector<double> pos = m_currentPositions[name];
    m_controlPanel->setSelectedPosition(name,
      pos[0], pos[1], pos[2],
      pos[3], pos[4], pos[5]);
  }
}

void MotionPage::onPositionDeselected()
{
  m_selectedPositionName.clear();
  m_controlPanel->clearSelectedPosition();
}

bool MotionPage::getCurrentDevicePosition(double& x, double& y, double& z,
  double& u, double& v, double& w)
{
  if (!m_motionManager || m_currentDevice.isEmpty()) {
    return false;
  }

  // Get current position from motion manager
  PositionStruct pos = m_motionManager->GetCurrentPosition(
    m_currentDevice.toStdString()
  );

  x = pos.x;
  y = pos.y;
  z = pos.z;
  u = pos.u;
  v = pos.v;
  w = pos.w;

  return true;
}

bool MotionPage::savePositionToFile(const QString& deviceName, const QString& posName,
  double x, double y, double z,
  double u, double v, double w)
{
  // Try multiple possible paths for motor_devices.json
  QStringList possiblePaths = {
      "config/motor_devices.local.json",
      "motor_devices.local.json",
      "config/motor_devices.json",
      "motor_devices.json",
      "../config/motor_devices.json"
  };

  QString filePath;
  bool fileFound = false;

  for (const QString& path : possiblePaths) {
    if (QFile::exists(path)) {
      filePath = path;
      fileFound = true;
      qDebug() << "Found motor config file at:" << path;
      break;
    }
  }

  if (!fileFound) {
    qWarning() << "Could not find motor_devices.json in any location";
    qWarning() << "Tried paths:" << possiblePaths;

    QMessageBox::critical(this, "File Not Found",
      QString("Could not find motor_devices.json.\nTried:\n%1")
      .arg(possiblePaths.join("\n")));
    return false;
  }

  // Read existing file
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Could not open" << filePath << "for reading";
    QMessageBox::critical(this, "File Read Error",
      QString("Could not open %1 for reading.\nError: %2")
      .arg(filePath)
      .arg(file.errorString()));
    return false;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    qWarning() << "JSON parse error:" << parseError.errorString();
    QMessageBox::critical(this, "JSON Parse Error",
      QString("Failed to parse JSON file.\nError: %1")
      .arg(parseError.errorString()));
    return false;
  }

  if (!doc.isObject()) {
    qWarning() << "Invalid JSON format - root is not an object";
    QMessageBox::critical(this, "Invalid JSON",
      "JSON file does not have the expected structure.");
    return false;
  }

  QJsonObject root = doc.object();

  // Ensure "named_positions" key exists
  if (!root.contains("named_positions")) {
    qWarning() << "JSON does not contain 'named_positions' key, creating it";
    root["named_positions"] = QJsonObject();
  }

  QJsonObject namedPositions = root["named_positions"].toObject();

  // Get or create device positions object
  QJsonObject devicePositions;
  if (namedPositions.contains(deviceName)) {
    devicePositions = namedPositions[deviceName].toObject();
  }
  else {
    qDebug() << "Creating new positions object for device:" << deviceName;
    devicePositions = QJsonObject();
  }

  // Add/update the position
  QJsonObject newPos;
  newPos["x"] = x;
  newPos["y"] = y;
  newPos["z"] = z;
  newPos["u"] = u;
  newPos["v"] = v;
  newPos["w"] = w;

  devicePositions[posName] = newPos;
  namedPositions[deviceName] = devicePositions;
  root["named_positions"] = namedPositions;

  // Write back to file
  doc.setObject(root);

  if (!file.open(QIODevice::WriteOnly)) {
    qWarning() << "Could not open" << filePath << "for writing";
    QMessageBox::critical(this, "File Write Error",
      QString("Could not open %1 for writing.\nError: %2\nCheck file permissions.")
      .arg(filePath)
      .arg(file.errorString()));
    return false;
  }

  qint64 bytesWritten = file.write(doc.toJson(QJsonDocument::Indented));
  file.close();

  if (bytesWritten == -1) {
    qWarning() << "Failed to write to file";
    QMessageBox::critical(this, "Write Error",
      "Failed to write data to file.");
    return false;
  }

  // Reload positions from file
  m_motionManager->ReloadNamedPositions(filePath.toStdString());


  qDebug() << "Successfully saved position" << posName << "for device" << deviceName;
  qDebug() << "  Position:" << x << y << z << u << v << w;

  return true;
}



bool MotionPage::updatePositionInFile(const QString& deviceName, const QString& posName,
  double x, double y, double z,
  double u, double v, double w)
{
  // Same implementation as savePositionToFile since updating = overwriting
  return savePositionToFile(deviceName, posName, x, y, z, u, v, w);
}



void MotionPage::onUpdateLivePosition()
{
  if (m_currentDevice.isEmpty() || !m_motionManager) {
    m_graphicsView->showLiveMarker(false);
    return;
  }

  // Get current position (local coordinates)
  double localX, localY, localZ, u, v, w;
  if (getCurrentDevicePosition(localX, localY, localZ, u, v, w)) {
    // Transform to global coordinates
    double globalX, globalY, globalZ;
    transformLocalToGlobal(localX, localY, localZ, globalX, globalY, globalZ);

    // Update live marker with global coordinates
    m_graphicsView->updateLiveMarker(m_currentDevice, globalX, globalY);
  }
  else {
    m_graphicsView->showLiveMarker(false);
  }
}


void MotionPage::onMoveToPositionClicked()
{
  if (m_selectedPositionName.isEmpty()) {
    QMessageBox::warning(this, "No Selection",
      "Please select a position from the map first.");
    return;
  }

  if (m_currentDevice.isEmpty()) {
    QMessageBox::warning(this, "No Device",
      "No device selected.");
    return;
  }

  // Move to the named position immediately (NON-BLOCKING)
  std::cout << "[MotionPage] Moving device '" << m_currentDevice.toStdString()
    << "' to position '" << m_selectedPositionName.toStdString() << "'" << std::endl;

  bool success = m_motionManager->MoveToNamedPosition(
    m_currentDevice.toStdString(),
    m_selectedPositionName.toStdString(),
    false  // ✅ NON-BLOCKING - UI stays responsive, marker updates live
  );

  if (!success) {
    // Only show error if something went wrong
    QMessageBox::critical(this, "Error",
      QString("Failed to start moving device '%1' to position '%2'")
      .arg(m_currentDevice)
      .arg(m_selectedPositionName));
  }
  // Silent success - user sees live marker moving on map via timer
}


// Add loadLabelOffsets method:
void MotionPage::loadLabelOffsets()
{
  m_labelOffsets.clear();

  QFile file(m_labelOffsetsFilePath);
  if (!file.open(QIODevice::ReadOnly)) {
    qDebug() << "No label offsets file found, using defaults";
    return;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject()) {
    qWarning() << "Invalid label offsets JSON format";
    return;
  }

  QJsonObject root = doc.object();

  // Load offsets for current device
  if (root.contains(m_currentDevice)) {
    QJsonObject deviceObj = root[m_currentDevice].toObject();

    for (auto it = deviceObj.begin(); it != deviceObj.end(); ++it) {
      QString posName = it.key();
      QJsonObject offsetObj = it.value().toObject();

      double offsetX = offsetObj["offsetX"].toDouble();
      double offsetY = offsetObj["offsetY"].toDouble();

      m_labelOffsets[posName] = QPointF(offsetX, offsetY);
    }
  }

  qDebug() << "Loaded" << m_labelOffsets.size() << "label offsets for" << m_currentDevice;
}

// Add saveLabelOffsets method:
void MotionPage::saveLabelOffsets()
{
  // Read existing file (to preserve other devices' offsets)
  QJsonObject root;
  QFile readFile(m_labelOffsetsFilePath);
  if (readFile.open(QIODevice::ReadOnly)) {
    QJsonDocument doc = QJsonDocument::fromJson(readFile.readAll());
    if (doc.isObject()) {
      root = doc.object();
    }
    readFile.close();
  }

  // Update offsets for current device
  QJsonObject deviceObj;
  for (auto it = m_labelOffsets.begin(); it != m_labelOffsets.end(); ++it) {
    QJsonObject offsetObj;
    offsetObj["offsetX"] = it.value().x();
    offsetObj["offsetY"] = it.value().y();
    deviceObj[it.key()] = offsetObj;
  }

  root[m_currentDevice] = deviceObj;

  // Write back to file
  QJsonDocument doc(root);
  QFile writeFile(m_labelOffsetsFilePath);
  if (!writeFile.open(QIODevice::WriteOnly)) {
    qWarning() << "Could not open" << m_labelOffsetsFilePath << "for writing";
    return;
  }

  writeFile.write(doc.toJson(QJsonDocument::Indented));
  writeFile.close();

  qDebug() << "Saved" << m_labelOffsets.size() << "label offsets for" << m_currentDevice;
}

// Add onLabelOffsetChanged slot:
void MotionPage::onLabelOffsetChanged(const QString& name, double offsetX, double offsetY)
{
  // Update in-memory offsets
  m_labelOffsets[name] = QPointF(offsetX, offsetY);

  // Save to file
  saveLabelOffsets();

  qDebug() << "Label offset changed:" << name << "(" << offsetX << "," << offsetY << ")";
}
