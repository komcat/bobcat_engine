// WorldOverlayWidget.cpp
#include "WorldOverlayWidget.h"
#include "WorldOverlayManager.h"
#include "WorldOverlayRenderer.h"
#include "WorldOverlay.h"
#include "ShapeCreatorDialog.h"
#include "AlignmentWizardDialog.h"
#include "CameraManager.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include "LiveVideoSubscriber.h"
#include "CameraFrameData.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMenu>
#include <QColorDialog>
#include <iostream>
#include <cmath>

WorldOverlayWidget::WorldOverlayWidget(CameraManager* cameraManager,
                                        UnifiedMotionManager* motionManager,
                                        QWidget* parent)
  : QWidget(parent)
  , m_cameraManager(cameraManager)
  , m_motionManager(motionManager)
{
  // Create overlay manager
  m_overlayManager = new WorldOverlayManager(this);
  m_overlayManager->setOverlayDirectory("config/overlays");

  // Load pixel-to-mm calibration from system config
  m_overlayManager->loadPxPerMmFromCalibration("config/SYSTEM/val_pixelmm.json");

  // Create renderer
  m_renderer = new WorldOverlayRenderer(this);

  setupUI();
  setupConnections();

  // Load pixel-to-mm calibration for click-to-move
  loadPixelToMmCalibration();

  // Setup timers
  m_updateTimer = new QTimer(this);
  connect(m_updateTimer, &QTimer::timeout, this, &WorldOverlayWidget::updateOverlay);

  m_positionTimer = new QTimer(this);
  connect(m_positionTimer, &QTimer::timeout, this, &WorldOverlayWidget::updateGantryPosition);
  m_positionTimer->start(50);  // 20 Hz position updates

  // Populate camera dropdown
  populateCameraDropdown();

  // Enable mouse tracking for crosshair
  setMouseTracking(true);

  std::cout << "[WorldOverlayWidget] Created" << std::endl;
}

WorldOverlayWidget::~WorldOverlayWidget()
{
  stopCamera();
  std::cout << "[WorldOverlayWidget] Destroyed" << std::endl;
}

void WorldOverlayWidget::setProductPositionManager(ProductPositionManager* positionManager)
{
  m_positionManager = positionManager;

  // Update overlay directory based on current product
  if (positionManager && positionManager->HasLoadedProduct()) {
    QString productName = QString::fromStdString(positionManager->GetCurrentProductName());
    updateOverlayDirectoryForProduct(productName);
  } else {
    // Fallback to default if no product loaded
    m_overlayManager->setOverlayDirectory("config/overlays");
    std::cout << "[WorldOverlayWidget] ProductPositionManager set (no product, using default overlay dir)" << std::endl;
  }
}

void WorldOverlayWidget::updateOverlayDirectoryForProduct(const QString& productName)
{
  QString overlayDir;
  if (productName.isEmpty()) {
    overlayDir = "config/overlays";
  } else {
    overlayDir = QString("config/%1/overlays").arg(productName);
  }

  m_overlayManager->setOverlayDirectory(overlayDir);

  // Clear current overlay and refresh UI
  m_overlayFileDropdown->clear();
  populateLayerList();
  m_markerList->clear();

  std::cout << "[WorldOverlayWidget] Overlay directory updated to: "
            << overlayDir.toStdString() << std::endl;
}

void WorldOverlayWidget::setupUI()
{
  setStyleSheet("QWidget { background-color: #1e1e1e; color: #ffffff; }");

  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(4, 4, 4, 4);
  mainLayout->setSpacing(4);

  // Create splitter for resizable panels
  m_splitter = new QSplitter(Qt::Horizontal);

  // Camera panel (left - larger)
  setupCameraPanel();
  m_splitter->addWidget(m_cameraPanel);

  // Control panel (right - smaller)
  setupControlPanel();
  m_splitter->addWidget(m_controlPanel);

  // Set initial sizes (70% camera, 30% controls)
  m_splitter->setSizes({700, 300});

  mainLayout->addWidget(m_splitter);
}

void WorldOverlayWidget::setupCameraPanel()
{
  m_cameraPanel = new QWidget();
  m_cameraPanel->setStyleSheet("QWidget { background-color: #252525; }");

  QVBoxLayout* layout = new QVBoxLayout(m_cameraPanel);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);

  // Camera selector row
  QHBoxLayout* cameraRow = new QHBoxLayout();

  QLabel* cameraLabel = new QLabel("Camera:");
  cameraLabel->setStyleSheet("QLabel { color: #a0a0a0; }");
  cameraRow->addWidget(cameraLabel);

  m_cameraDropdown = new QComboBox();
  m_cameraDropdown->setStyleSheet(R"(
    QComboBox {
      background-color: #3d3d3d;
      color: white;
      padding: 4px 8px;
      border: 1px solid #555;
      border-radius: 4px;
      min-width: 150px;
    }
  )");
  cameraRow->addWidget(m_cameraDropdown);

  m_startButton = new QPushButton("Start");
  m_startButton->setStyleSheet(R"(
    QPushButton {
      background-color: #0078d4;
      color: white;
      padding: 4px 12px;
      border: none;
      border-radius: 4px;
    }
    QPushButton:hover { background-color: #1084d8; }
    QPushButton:disabled { background-color: #555; }
  )");
  cameraRow->addWidget(m_startButton);

  m_stopButton = new QPushButton("Stop");
  m_stopButton->setEnabled(false);
  m_stopButton->setStyleSheet(R"(
    QPushButton {
      background-color: #d41a1a;
      color: white;
      padding: 4px 12px;
      border: none;
      border-radius: 4px;
    }
    QPushButton:hover { background-color: #e02020; }
    QPushButton:disabled { background-color: #555; }
  )");
  cameraRow->addWidget(m_stopButton);

  cameraRow->addStretch();

  m_positionLabel = new QLabel("Gantry: X=0.000 Y=0.000");
  m_positionLabel->setStyleSheet("QLabel { color: #00ff00; font-family: monospace; }");
  cameraRow->addWidget(m_positionLabel);

  layout->addLayout(cameraRow);

  // Controls row: Click-to-move and Crosshair checkboxes
  QHBoxLayout* controlsRow = new QHBoxLayout();

  m_clickToMoveCheckbox = new QCheckBox("Click to Move");
  m_clickToMoveCheckbox->setStyleSheet("QCheckBox { color: #aaaaaa; font-size: 13px; }");
  controlsRow->addWidget(m_clickToMoveCheckbox);

  m_crosshairCheckbox = new QCheckBox("Crosshair");
  m_crosshairCheckbox->setStyleSheet("QCheckBox { color: #aaaaaa; font-size: 13px; }");
  controlsRow->addWidget(m_crosshairCheckbox);

  controlsRow->addStretch();

  // Mouse position label (shows pixmap coords for debugging)
  m_mousePositionLabel = new QLabel("Mouse: --");
  m_mousePositionLabel->setStyleSheet("QLabel { color: #00ffff; font-family: monospace; font-size: 11px; }");
  controlsRow->addWidget(m_mousePositionLabel);

  layout->addLayout(controlsRow);

  // Camera view area
  m_cameraView = new QLabel();
  m_cameraView->setAlignment(Qt::AlignCenter);
  m_cameraView->setStyleSheet("QLabel { background-color: #1a1a1a; border: 1px solid #333; }");
  m_cameraView->setText("Select a camera and click Start");
  m_cameraView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_cameraView->setMinimumSize(400, 300);
  m_cameraView->setMouseTracking(true);
  m_cameraView->installEventFilter(this);
  layout->addWidget(m_cameraView, 1);
}

void WorldOverlayWidget::setupControlPanel()
{
  m_controlPanel = new QWidget();
  m_controlPanel->setStyleSheet("QWidget { background-color: #2d2d2d; }");
  m_controlPanel->setMinimumWidth(250);
  m_controlPanel->setMaximumWidth(400);

  QVBoxLayout* layout = new QVBoxLayout(m_controlPanel);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(8);

  // Overlay file group
  QGroupBox* fileGroup = new QGroupBox("Overlay File");
  fileGroup->setStyleSheet(R"(
    QGroupBox {
      color: #a0a0a0;
      border: 1px solid #444;
      border-radius: 4px;
      margin-top: 12px;
      padding-top: 8px;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      padding: 0 4px;
    }
  )");

  QVBoxLayout* fileLayout = new QVBoxLayout(fileGroup);

  m_overlayFileDropdown = new QComboBox();
  m_overlayFileDropdown->setStyleSheet("QComboBox { background-color: #3d3d3d; color: white; padding: 4px; }");
  fileLayout->addWidget(m_overlayFileDropdown);

  QHBoxLayout* fileButtons = new QHBoxLayout();
  m_newOverlayButton = new QPushButton("New");
  m_loadOverlayButton = new QPushButton("Load");
  m_saveOverlayButton = new QPushButton("Save");

  QString buttonStyle = R"(
    QPushButton {
      background-color: #3d3d3d;
      color: white;
      padding: 4px 8px;
      border: 1px solid #555;
      border-radius: 3px;
    }
    QPushButton:hover { background-color: #4d4d4d; }
  )";
  m_newOverlayButton->setStyleSheet(buttonStyle);
  m_loadOverlayButton->setStyleSheet(buttonStyle);
  m_saveOverlayButton->setStyleSheet(buttonStyle);

  fileButtons->addWidget(m_newOverlayButton);
  fileButtons->addWidget(m_loadOverlayButton);
  fileButtons->addWidget(m_saveOverlayButton);
  fileLayout->addLayout(fileButtons);

  layout->addWidget(fileGroup);

  // Layers group
  QGroupBox* layerGroup = new QGroupBox("Layers");
  layerGroup->setStyleSheet(fileGroup->styleSheet());

  QVBoxLayout* layerLayout = new QVBoxLayout(layerGroup);

  m_layerList = new QListWidget();
  m_layerList->setStyleSheet(R"(
    QListWidget {
      background-color: #1e1e1e;
      color: white;
      border: 1px solid #444;
    }
    QListWidget::item {
      padding: 4px;
    }
    QListWidget::item:selected {
      background-color: #0078d4;
    }
  )");
  m_layerList->setMaximumHeight(150);
  layerLayout->addWidget(m_layerList);

  QHBoxLayout* layerButtons = new QHBoxLayout();
  m_addLayerButton = new QPushButton("+");
  m_removeLayerButton = new QPushButton("-");
  m_layerColorButton = new QPushButton();
  m_layerColorButton->setFixedSize(24, 24);
  m_layerColorButton->setToolTip("Change layer color");
  m_layerColorButton->setStyleSheet("QPushButton { background-color: #00ff00; border: 1px solid #555; border-radius: 3px; }");
  m_addLayerButton->setStyleSheet(buttonStyle);
  m_removeLayerButton->setStyleSheet(buttonStyle);
  m_addLayerButton->setFixedWidth(30);
  m_removeLayerButton->setFixedWidth(30);
  layerButtons->addWidget(m_addLayerButton);
  layerButtons->addWidget(m_removeLayerButton);
  layerButtons->addWidget(m_layerColorButton);
  layerButtons->addStretch();

  QLabel* opacityLabel = new QLabel("Opacity:");
  opacityLabel->setStyleSheet("QLabel { color: #a0a0a0; }");
  layerButtons->addWidget(opacityLabel);

  m_opacitySlider = new QSlider(Qt::Horizontal);
  m_opacitySlider->setRange(0, 100);
  m_opacitySlider->setValue(80);
  m_opacitySlider->setFixedWidth(80);
  layerButtons->addWidget(m_opacitySlider);

  layerLayout->addLayout(layerButtons);

  // Alignment wizard button
  m_alignmentWizardButton = new QPushButton("Alignment Wizard...");
  m_alignmentWizardButton->setStyleSheet(R"(
    QPushButton {
      background-color: #6b3fa0;
      color: white;
      padding: 6px 12px;
      border: none;
      border-radius: 3px;
    }
    QPushButton:hover { background-color: #7b4fb0; }
  )");
  layerLayout->addWidget(m_alignmentWizardButton);

  layout->addWidget(layerGroup);

  // Markers group
  QGroupBox* markerGroup = new QGroupBox("Markers");
  markerGroup->setStyleSheet(fileGroup->styleSheet());

  QVBoxLayout* markerLayout = new QVBoxLayout(markerGroup);

  // Marker list
  m_markerList = new QListWidget();
  m_markerList->setStyleSheet(R"(
    QListWidget {
      background-color: #2d2d2d;
      color: white;
      border: 1px solid #3d3d3d;
      border-radius: 3px;
    }
    QListWidget::item {
      padding: 4px;
    }
    QListWidget::item:selected {
      background-color: #0078d4;
    }
    QListWidget::item:hover:!selected {
      background-color: #3d3d3d;
    }
  )");
  m_markerList->setMaximumHeight(100);
  m_markerList->setContextMenuPolicy(Qt::CustomContextMenu);
  markerLayout->addWidget(m_markerList);

  QHBoxLayout* markerTypeRow = new QHBoxLayout();
  QLabel* typeLabel = new QLabel("Type:");
  typeLabel->setStyleSheet("QLabel { color: #a0a0a0; }");
  markerTypeRow->addWidget(typeLabel);

  m_markerTypeDropdown = new QComboBox();
  m_markerTypeDropdown->addItem("Circle", static_cast<int>(WorldOverlay::MarkerType::Circle));
  m_markerTypeDropdown->addItem("Crosshair", static_cast<int>(WorldOverlay::MarkerType::Crosshair));
  m_markerTypeDropdown->addItem("Rectangle", static_cast<int>(WorldOverlay::MarkerType::Rectangle));
  m_markerTypeDropdown->addItem("Diamond", static_cast<int>(WorldOverlay::MarkerType::Diamond));
  m_markerTypeDropdown->addItem("Point", static_cast<int>(WorldOverlay::MarkerType::Point));
  m_markerTypeDropdown->setStyleSheet("QComboBox { background-color: #3d3d3d; color: white; padding: 4px; }");
  markerTypeRow->addWidget(m_markerTypeDropdown);
  markerLayout->addLayout(markerTypeRow);

  QHBoxLayout* markerButtons = new QHBoxLayout();
  m_addMarkerButton = new QPushButton("Add at Position");
  m_removeMarkerButton = new QPushButton("Remove");
  m_clearLayerButton = new QPushButton("Clear Layer");
  m_createShapeButton = new QPushButton("Create Shape...");

  m_addMarkerButton->setStyleSheet(R"(
    QPushButton {
      background-color: #0078d4;
      color: white;
      padding: 6px 12px;
      border: none;
      border-radius: 3px;
    }
    QPushButton:hover { background-color: #1084d8; }
  )");
  m_createShapeButton->setStyleSheet(R"(
    QPushButton {
      background-color: #107c10;
      color: white;
      padding: 6px 12px;
      border: none;
      border-radius: 3px;
    }
    QPushButton:hover { background-color: #148c14; }
  )");
  m_removeMarkerButton->setStyleSheet(buttonStyle);
  m_clearLayerButton->setStyleSheet(buttonStyle);

  markerButtons->addWidget(m_addMarkerButton);
  markerButtons->addWidget(m_removeMarkerButton);
  markerLayout->addLayout(markerButtons);

  QHBoxLayout* markerButtons2 = new QHBoxLayout();
  markerButtons2->addWidget(m_createShapeButton);
  markerButtons2->addWidget(m_clearLayerButton);
  markerLayout->addLayout(markerButtons2);

  layout->addWidget(markerGroup);

  layout->addStretch();
}

void WorldOverlayWidget::setupConnections()
{
  // Camera controls
  connect(m_cameraDropdown, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &WorldOverlayWidget::onCameraChanged);
  connect(m_startButton, &QPushButton::clicked, this, &WorldOverlayWidget::startCamera);
  connect(m_stopButton, &QPushButton::clicked, this, &WorldOverlayWidget::stopCamera);

  // Overlay file controls
  connect(m_newOverlayButton, &QPushButton::clicked, this, &WorldOverlayWidget::onNewOverlay);
  connect(m_loadOverlayButton, &QPushButton::clicked, this, &WorldOverlayWidget::onLoadOverlay);
  connect(m_saveOverlayButton, &QPushButton::clicked, this, &WorldOverlayWidget::onSaveOverlay);

  // Layer controls
  connect(m_layerList, &QListWidget::itemSelectionChanged,
          this, &WorldOverlayWidget::onLayerSelectionChanged);
  connect(m_layerList, &QListWidget::itemChanged,
          this, &WorldOverlayWidget::onLayerVisibilityChanged);
  connect(m_addLayerButton, &QPushButton::clicked, this, &WorldOverlayWidget::onAddLayer);
  connect(m_removeLayerButton, &QPushButton::clicked, this, &WorldOverlayWidget::onRemoveLayer);
  connect(m_layerColorButton, &QPushButton::clicked, this, &WorldOverlayWidget::onLayerColorClicked);
  connect(m_alignmentWizardButton, &QPushButton::clicked, this, &WorldOverlayWidget::onAlignmentWizardClicked);

  // Marker controls
  connect(m_markerList, &QListWidget::itemSelectionChanged,
          this, &WorldOverlayWidget::onMarkerSelectionChanged);
  connect(m_markerList, &QListWidget::customContextMenuRequested,
          this, &WorldOverlayWidget::onMarkerContextMenu);
  connect(m_addMarkerButton, &QPushButton::clicked, this, &WorldOverlayWidget::onAddMarkerClicked);
  connect(m_removeMarkerButton, &QPushButton::clicked, this, &WorldOverlayWidget::onRemoveMarkerClicked);
  connect(m_clearLayerButton, &QPushButton::clicked, this, &WorldOverlayWidget::onClearLayerClicked);
  connect(m_createShapeButton, &QPushButton::clicked, this, &WorldOverlayWidget::onCreateShapeClicked);

  // Click-to-move and crosshair controls
  connect(m_clickToMoveCheckbox, &QCheckBox::toggled, this, &WorldOverlayWidget::onClickToMoveToggled);
  connect(m_crosshairCheckbox, &QCheckBox::toggled, this, &WorldOverlayWidget::onCrosshairToggled);

  // Overlay manager signals
  connect(m_overlayManager, &WorldOverlayManager::overlayLoaded,
          this, &WorldOverlayWidget::populateLayerList);
  connect(m_overlayManager, &WorldOverlayManager::overlayModified,
          this, [this]() { update(); });
}

void WorldOverlayWidget::populateCameraDropdown()
{
  m_cameraDropdown->clear();

  if (!m_cameraManager) {
    m_cameraDropdown->addItem("No cameras available");
    return;
  }

  auto cameraIds = m_cameraManager->GetCameraIds();
  for (const auto& id : cameraIds) {
    m_cameraDropdown->addItem(QString::fromStdString(id));
  }

  if (cameraIds.empty()) {
    m_cameraDropdown->addItem("No cameras available");
  }
}

void WorldOverlayWidget::populateLayerList()
{
  m_layerList->clear();

  if (!m_overlayManager->hasCurrentOverlay()) {
    return;
  }

  const WorldOverlay::OverlayFile* overlay = m_overlayManager->currentOverlay();
  for (const auto& layer : overlay->layers) {
    // Build layer name with alignment status indicator
    QString displayName = layer.name;
    if (layer.localSystem.masterTaught) {
      // Master F1/F2 has been taught - show indicator
      if (layer.localSystem.currentTaught && layer.localSystem.transformValid) {
        displayName += QString::fromUtf8(" \u2713");  // ✓ - fully aligned
      } else {
        displayName += QString::fromUtf8(" \u2699");  // ⚙ - master taught, needs current alignment
      }
    }

    QListWidgetItem* item = new QListWidgetItem(displayName);
    item->setData(Qt::UserRole, layer.name);  // Store actual layer name for lookup
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);

    // Set color indicator
    QPixmap colorPx(16, 16);
    colorPx.fill(layer.color);
    item->setIcon(QIcon(colorPx));

    m_layerList->addItem(item);
  }
}

void WorldOverlayWidget::updateGantryPosition()
{
  if (!m_motionManager) {
    return;
  }

  // Get actual gantry position (using "gantry-main" device)
  PositionStruct pos = m_motionManager->GetCurrentPosition("gantry-main");
  m_gantryX = pos.x;
  m_gantryY = pos.y;
  m_gantryZ = pos.z;

  // Apply smoothing for overlay rendering
  // This prevents markers from jumping ahead when click-to-move is triggered
  // Smoothed position gradually catches up to actual position
  m_gantryXSmoothed += (m_gantryX - m_gantryXSmoothed) * m_smoothingFactor;
  m_gantryYSmoothed += (m_gantryY - m_gantryYSmoothed) * m_smoothingFactor;

  // Snap to actual position if very close (avoid endless tiny adjustments)
  if (std::abs(m_gantryX - m_gantryXSmoothed) < 0.001) {
    m_gantryXSmoothed = m_gantryX;
  }
  if (std::abs(m_gantryY - m_gantryYSmoothed) < 0.001) {
    m_gantryYSmoothed = m_gantryY;
  }

  m_positionLabel->setText(QString("Gantry: X=%1 Y=%2 Z=%3")
                             .arg(m_gantryX, 0, 'f', 3)
                             .arg(m_gantryY, 0, 'f', 3)
                             .arg(m_gantryZ, 0, 'f', 3));
}

void WorldOverlayWidget::onCameraChanged(int index)
{
  if (index < 0) return;

  QString cameraName = m_cameraDropdown->currentText();
  if (cameraName == "No cameras available") return;

  m_currentCamera = cameraName;
  std::cout << "[WorldOverlayWidget] Camera selected: " << cameraName.toStdString() << std::endl;
}

void WorldOverlayWidget::startCamera()
{
  if (m_currentCamera.isEmpty() || !m_cameraManager) {
    return;
  }

  std::string cameraId = m_currentCamera.toStdString();

  // Start grabbing if not already running
  ICameraHardware* camera = m_cameraManager->GetCameraHardware(cameraId);
  if (camera && !camera->IsGrabbing()) {
    m_cameraManager->StartGrabbing(cameraId);
  }

  // Subscribe to camera frames
  m_subscriber = std::make_shared<LiveVideoSubscriber>(cameraId, "WorldOverlay");
  m_subscriber->InitializeStatusFromManager(m_cameraManager);
  m_cameraManager->SubscribeToFrames(m_subscriber);

  m_cameraRunning = true;
  m_startButton->setEnabled(false);
  m_stopButton->setEnabled(true);
  m_updateTimer->start(33);  // ~30 fps
  std::cout << "[WorldOverlayWidget] Camera started: " << cameraId << std::endl;
}

void WorldOverlayWidget::stopCamera()
{
  if (m_currentCamera.isEmpty() || !m_cameraManager) {
    return;
  }

  m_updateTimer->stop();

  // Unsubscribe from camera frames
  if (m_subscriber) {
    m_cameraManager->UnsubscribeFromFrames(m_subscriber->GetSubscriberId());
    m_subscriber.reset();
  }

  m_cameraRunning = false;
  m_startButton->setEnabled(true);
  m_stopButton->setEnabled(false);

  m_cameraView->setText("Camera stopped");
  std::cout << "[WorldOverlayWidget] Camera stopped" << std::endl;
}

void WorldOverlayWidget::onNewOverlay()
{
  bool ok;
  QString name = QInputDialog::getText(this, "New Overlay",
                                        "Overlay name:", QLineEdit::Normal,
                                        "New Overlay", &ok);
  if (!ok || name.isEmpty()) return;

  QString camera = m_cameraDropdown->currentText();
  m_overlayManager->createNewOverlay(name, camera);

  // Update dropdown to show new overlay filename
  QString filename = m_overlayManager->currentOverlayFilename();
  int index = m_overlayFileDropdown->findText(filename);
  if (index < 0) {
    m_overlayFileDropdown->addItem(filename);
    index = m_overlayFileDropdown->findText(filename);
  }
  m_overlayFileDropdown->setCurrentIndex(index);

  populateLayerList();

  emit overlayFileChanged(filename);
}

void WorldOverlayWidget::onLoadOverlay()
{
  QStringList files = m_overlayManager->availableOverlayFiles();
  if (files.isEmpty()) {
    QMessageBox::information(this, "Load Overlay", "No overlay files found in config/overlays/");
    return;
  }

  bool ok;
  QString file = QInputDialog::getItem(this, "Load Overlay",
                                        "Select overlay file:", files, 0, false, &ok);
  if (!ok || file.isEmpty()) return;

  if (m_overlayManager->loadOverlayFile(file)) {
    // Update dropdown to show loaded file
    int index = m_overlayFileDropdown->findText(file);
    if (index < 0) {
      m_overlayFileDropdown->addItem(file);
      index = m_overlayFileDropdown->findText(file);
    }
    m_overlayFileDropdown->setCurrentIndex(index);

    populateLayerList();
    emit overlayFileChanged(file);
  }
}

void WorldOverlayWidget::onSaveOverlay()
{
  if (!m_overlayManager->hasCurrentOverlay()) {
    QMessageBox::warning(this, "Save Overlay", "No overlay is currently loaded");
    return;
  }

  if (m_overlayManager->saveCurrentOverlay()) {
    QMessageBox::information(this, "Save Overlay", "Overlay saved successfully");
  } else {
    QMessageBox::warning(this, "Save Overlay", "Failed to save overlay");
  }
}

void WorldOverlayWidget::onLayerSelectionChanged()
{
  // Update UI based on selected layer
  QListWidgetItem* item = m_layerList->currentItem();
  if (!item) return;

  // Get actual layer name from UserRole (display text may have alignment symbols)
  QString layerName = item->data(Qt::UserRole).toString();
  WorldOverlay::Layer* layer = m_overlayManager->findLayer(layerName);
  if (layer) {
    m_opacitySlider->setValue(static_cast<int>(layer->opacity * 100));
    // Update color button to show layer's color
    m_layerColorButton->setStyleSheet(QString("QPushButton { background-color: %1; border: 1px solid #555; border-radius: 3px; }")
      .arg(layer->color.name()));
  }

  // Update marker list for the selected layer
  populateMarkerList();
}

void WorldOverlayWidget::onLayerVisibilityChanged(QListWidgetItem* item)
{
  if (!item) return;

  // Get actual layer name from UserRole (display text may have alignment symbols)
  QString layerName = item->data(Qt::UserRole).toString();
  WorldOverlay::Layer* layer = m_overlayManager->findLayer(layerName);
  if (layer) {
    layer->visible = (item->checkState() == Qt::Checked);
    update();
  }
}

void WorldOverlayWidget::onAddLayer()
{
  bool ok;
  QString name = QInputDialog::getText(this, "Add Layer",
                                        "Layer name:", QLineEdit::Normal,
                                        "New Layer", &ok);
  if (!ok || name.isEmpty()) return;

  m_overlayManager->addLayer(name, WorldOverlay::LayerType::Template);
  populateLayerList();
}

void WorldOverlayWidget::onRemoveLayer()
{
  QListWidgetItem* item = m_layerList->currentItem();
  if (!item) return;

  // Get actual layer name from UserRole (display text may have alignment symbols)
  QString layerName = item->data(Qt::UserRole).toString();

  if (QMessageBox::question(this, "Remove Layer",
                             QString("Remove layer '%1'?").arg(layerName),
                             QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    m_overlayManager->removeLayer(layerName);
    populateLayerList();
  }
}

void WorldOverlayWidget::onLayerColorClicked()
{
  QListWidgetItem* item = m_layerList->currentItem();
  if (!item) {
    QMessageBox::information(this, "Layer Color", "Please select a layer first.");
    return;
  }

  // Get actual layer name from UserRole (display text may have alignment symbols)
  QString layerName = item->data(Qt::UserRole).toString();
  WorldOverlay::Layer* layer = m_overlayManager->findLayer(layerName);
  if (!layer) return;

  // Open color picker dialog with current layer color
  QColor newColor = QColorDialog::getColor(layer->color, this, "Select Layer Color",
                                            QColorDialog::ShowAlphaChannel);

  if (newColor.isValid()) {
    layer->color = newColor;

    // Update color button
    m_layerColorButton->setStyleSheet(QString("QPushButton { background-color: %1; border: 1px solid #555; border-radius: 3px; }")
      .arg(newColor.name()));

    // Update layer list item icon
    QPixmap colorPx(16, 16);
    colorPx.fill(newColor);
    item->setIcon(QIcon(colorPx));

    update();  // Refresh overlay display
    std::cout << "[WorldOverlayWidget] Layer '" << layerName.toStdString()
              << "' color changed to " << newColor.name().toStdString() << std::endl;
  }
}

void WorldOverlayWidget::onAddMarkerClicked()
{
  QListWidgetItem* layerItem = m_layerList->currentItem();
  if (!layerItem) {
    QMessageBox::warning(this, "Add Marker", "Please select a layer first");
    return;
  }

  // Get actual layer name from UserRole (display text may have alignment symbols)
  QString layerName = layerItem->data(Qt::UserRole).toString();

  // Create marker at current gantry position (X, Y, Z)
  WorldOverlay::Marker marker;
  marker.type = static_cast<WorldOverlay::MarkerType>(
      m_markerTypeDropdown->currentData().toInt());
  marker.worldX = m_gantryX;
  marker.worldY = m_gantryY;
  marker.worldZ = m_gantryZ;

  // Initialize template coordinates (same as world when first teaching)
  marker.templateX = m_gantryX;
  marker.templateY = m_gantryY;
  marker.templateZ = m_gantryZ;

  bool ok;
  QString label = QInputDialog::getText(this, "Add Marker",
                                         "Marker label (optional):", QLineEdit::Normal,
                                         "", &ok);
  if (ok) {
    marker.label = label;
    marker.showLabel = !label.isEmpty();

    WorldOverlay::Marker* added = m_overlayManager->addMarker(layerName, marker);
    if (added) {
      populateMarkerList();  // Refresh marker list
      emit markerAdded(layerName, added->id);
      std::cout << "[WorldOverlayWidget] Added marker at X=" << m_gantryX
                << " Y=" << m_gantryY << " Z=" << m_gantryZ << std::endl;
    }
  }
}

void WorldOverlayWidget::onRemoveMarkerClicked()
{
  if (m_selectedMarkerId.isEmpty()) {
    QMessageBox::information(this, "Remove Marker", "Select a marker from the list first");
    return;
  }

  // Check if this is a shape group (starts with "shape:")
  if (m_selectedMarkerId.startsWith("shape:")) {
    QString shapeName = m_selectedMarkerId.mid(6);  // Remove "shape:" prefix

    // Find and remove all markers belonging to this shape
    if (m_overlayManager->hasCurrentOverlay()) {
      for (auto& layer : m_overlayManager->currentOverlay()->layers) {
        // Collect marker IDs to remove (can't modify during iteration)
        QStringList idsToRemove;
        for (const auto& marker : layer.markers) {
          QString label = marker.label.isEmpty() ? marker.id : marker.label;
          // Check if marker belongs to this shape (starts with "ShapeName.")
          if (label.startsWith(shapeName + ".") || label == shapeName) {
            idsToRemove.append(marker.id);
          }
        }

        // Remove all collected markers
        for (const QString& id : idsToRemove) {
          m_overlayManager->removeMarker(layer.name, id);
        }

        if (!idsToRemove.isEmpty()) {
          std::cout << "[WorldOverlayWidget] Removed shape '" << shapeName.toStdString()
                    << "' with " << idsToRemove.size() << " markers" << std::endl;
        }
      }
    }

    m_selectedMarkerId.clear();
    populateMarkerList();
    update();
    return;
  }

  // Standard single marker removal
  if (m_overlayManager->hasCurrentOverlay()) {
    for (auto& layer : m_overlayManager->currentOverlay()->layers) {
      if (layer.findMarker(m_selectedMarkerId)) {
        m_overlayManager->removeMarker(layer.name, m_selectedMarkerId);
        m_selectedMarkerId.clear();
        populateMarkerList();  // Refresh marker list
        update();
        return;
      }
    }
  }
}

void WorldOverlayWidget::onClearLayerClicked()
{
  QListWidgetItem* item = m_layerList->currentItem();
  if (!item) return;

  // Get actual layer name from UserRole (display text may have alignment symbols)
  QString layerName = item->data(Qt::UserRole).toString();

  if (QMessageBox::question(this, "Clear Layer",
                             QString("Clear all markers from '%1'?").arg(layerName),
                             QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    WorldOverlay::Layer* layer = m_overlayManager->findLayer(layerName);
    if (layer) {
      layer->clearMarkers();
      populateMarkerList();
      update();
    }
  }
}

void WorldOverlayWidget::populateMarkerList()
{
  m_markerList->clear();

  QListWidgetItem* layerItem = m_layerList->currentItem();
  if (!layerItem) return;

  // Get actual layer name from UserRole (display text may have alignment symbols)
  QString layerName = layerItem->data(Qt::UserRole).toString();
  WorldOverlay::Layer* layer = m_overlayManager->findLayer(layerName);
  if (!layer) return;

  // Group markers by shape name prefix (e.g., "Rect1.P1" -> "Rect1")
  // Shapes have labels like "ShapeName.P1", "ShapeName.P2", etc.
  // Standalone markers don't have a dot in the label
  QMap<QString, QVector<const WorldOverlay::Marker*>> shapeGroups;
  QVector<const WorldOverlay::Marker*> standaloneMarkers;

  for (const auto& marker : layer->markers) {
    QString name = marker.label.isEmpty() ? marker.id : marker.label;

    // Check if this is part of a shape group (has "ShapeName.P" pattern)
    int dotIndex = name.indexOf('.');
    if (dotIndex > 0 && dotIndex < name.length() - 1) {
      QString shapeName = name.left(dotIndex);
      shapeGroups[shapeName].append(&marker);
    } else {
      // Standalone marker or the main label for a shape
      standaloneMarkers.append(&marker);
    }
  }

  // Add shape groups first
  for (auto it = shapeGroups.begin(); it != shapeGroups.end(); ++it) {
    QString shapeName = it.key();
    const auto& markers = it.value();

    // Count points and lines in this shape
    int pointCount = 0;
    int lineCount = 0;
    double avgX = 0, avgY = 0, avgZ = 0;

    for (const auto* m : markers) {
      if (m->type == WorldOverlay::MarkerType::Point) {
        pointCount++;
      } else if (m->type == WorldOverlay::MarkerType::Line) {
        lineCount++;
      }
      avgX += m->worldX;
      avgY += m->worldY;
      avgZ += m->worldZ;
    }

    if (!markers.isEmpty()) {
      avgX /= markers.size();
      avgY /= markers.size();
      avgZ /= markers.size();
    }

    QString displayText = QString("[Shape] %1 (%2 pts, %3 lines) @ (%4, %5, %6)")
        .arg(shapeName)
        .arg(pointCount)
        .arg(lineCount)
        .arg(avgX, 0, 'f', 2)
        .arg(avgY, 0, 'f', 2)
        .arg(avgZ, 0, 'f', 2);

    QListWidgetItem* item = new QListWidgetItem(displayText);
    // Store the shape name as a special identifier with "shape:" prefix
    item->setData(Qt::UserRole, QString("shape:%1").arg(shapeName));
    item->setForeground(QBrush(QColor(0, 200, 255)));  // Cyan for shapes
    m_markerList->addItem(item);
  }

  // Add standalone markers
  for (const auto* marker : standaloneMarkers) {
    QString name = marker->label.isEmpty() ? marker->id : marker->label;
    QString displayText = QString("%1 (%2, %3, %4)")
        .arg(name)
        .arg(marker->worldX, 0, 'f', 2)
        .arg(marker->worldY, 0, 'f', 2)
        .arg(marker->worldZ, 0, 'f', 2);

    QListWidgetItem* item = new QListWidgetItem(displayText);
    item->setData(Qt::UserRole, marker->id);  // Store marker ID for later retrieval
    m_markerList->addItem(item);
  }
}

void WorldOverlayWidget::onMarkerSelectionChanged()
{
  QListWidgetItem* item = m_markerList->currentItem();
  if (!item) {
    m_selectedMarkerId.clear();
    return;
  }

  // Get marker ID from the item's data
  m_selectedMarkerId = item->data(Qt::UserRole).toString();
  update();  // Refresh to show selection highlight on the overlay
}

void WorldOverlayWidget::onMarkerContextMenu(const QPoint& pos)
{
  QListWidgetItem* item = m_markerList->itemAt(pos);
  if (!item) {
    return;
  }

  // Select the item under cursor
  m_markerList->setCurrentItem(item);
  m_selectedMarkerId = item->data(Qt::UserRole).toString();

  // Create context menu
  QMenu contextMenu(this);
  contextMenu.setStyleSheet(R"(
    QMenu {
      background-color: #2d2d2d;
      color: white;
      border: 1px solid #555;
    }
    QMenu::item:selected {
      background-color: #0078d4;
    }
  )");

  QAction* renameAction = contextMenu.addAction("Rename...");
  QAction* removeAction = contextMenu.addAction("Remove");

  // Show menu and handle selection
  QAction* selectedAction = contextMenu.exec(m_markerList->mapToGlobal(pos));

  if (selectedAction == renameAction) {
    onRenameMarker();
  } else if (selectedAction == removeAction) {
    onRemoveMarkerClicked();
  }
}

void WorldOverlayWidget::onRenameMarker()
{
  if (m_selectedMarkerId.isEmpty()) {
    return;
  }

  // Check if this is a shape group
  if (m_selectedMarkerId.startsWith("shape:")) {
    QString shapeName = m_selectedMarkerId.mid(6);

    bool ok;
    QString newName = QInputDialog::getText(this, "Rename Shape",
                                             "New shape name:", QLineEdit::Normal,
                                             shapeName, &ok);
    if (!ok || newName.isEmpty() || newName == shapeName) {
      return;
    }

    // Rename all markers belonging to this shape
    if (m_overlayManager->hasCurrentOverlay()) {
      for (auto& layer : m_overlayManager->currentOverlay()->layers) {
        for (auto& marker : layer.markers) {
          // Check if marker belongs to this shape (label starts with "ShapeName.")
          if (marker.label.startsWith(shapeName + ".")) {
            // Replace shape name prefix with new name
            marker.label = newName + marker.label.mid(shapeName.length());
          }
        }
      }
    }

    std::cout << "[WorldOverlayWidget] Renamed shape '" << shapeName.toStdString()
              << "' to '" << newName.toStdString() << "'" << std::endl;

    populateMarkerList();
    update();
    return;
  }

  // Find the marker and get current label
  QString currentLabel;
  WorldOverlay::Marker* marker = nullptr;

  if (m_overlayManager->hasCurrentOverlay()) {
    for (auto& layer : m_overlayManager->currentOverlay()->layers) {
      marker = layer.findMarker(m_selectedMarkerId);
      if (marker) {
        currentLabel = marker->label.isEmpty() ? marker->id : marker->label;
        break;
      }
    }
  }

  if (!marker) {
    return;
  }

  bool ok;
  QString newLabel = QInputDialog::getText(this, "Rename Marker",
                                            "New name:", QLineEdit::Normal,
                                            currentLabel, &ok);
  if (!ok || newLabel.isEmpty()) {
    return;
  }

  marker->label = newLabel;
  marker->showLabel = !newLabel.isEmpty();

  std::cout << "[WorldOverlayWidget] Renamed marker to '" << newLabel.toStdString() << "'" << std::endl;

  populateMarkerList();
  update();
}

void WorldOverlayWidget::onCreateShapeClicked()
{
  // Check prerequisites
  QListWidgetItem* layerItem = m_layerList->currentItem();
  if (!layerItem) {
    QMessageBox::warning(this, "Create Shape", "Please select a layer first");
    return;
  }

  // Get actual layer name from UserRole (display text may have alignment symbols)
  QString layerName = layerItem->data(Qt::UserRole).toString();

  if (m_currentFrame.isNull()) {
    QMessageBox::warning(this, "Create Shape", "No camera frame available. Start the camera first.");
    return;
  }

  // Get pxPerMm from overlay or default
  double pxPerMm = m_overlayManager->getDefaultPxPerMm();
  if (m_overlayManager->hasCurrentOverlay()) {
    pxPerMm = m_overlayManager->currentOverlay()->pxPerMm;
  }

  // Adjust pxPerMm for the scaled frame
  // m_currentFrame is now scaled, so we need to adjust pxPerMm accordingly
  double displayScale = (m_originalFrameWidth > 0)
      ? static_cast<double>(m_currentFrame.width()) / m_originalFrameWidth
      : 1.0;
  double effectivePxPerMm = pxPerMm * displayScale;

  // Create and show the shape creator dialog with a snapshot of current frame
  std::cout << "[WorldOverlayWidget] Creating ShapeCreatorDialog with:"
            << " frame " << m_currentFrame.width() << "x" << m_currentFrame.height()
            << " pxPerMm=" << effectivePxPerMm << " (original=" << pxPerMm << ", scale=" << displayScale << ")"
            << " gantry(" << m_gantryX << ", " << m_gantryY << ", " << m_gantryZ << ")"
            << std::endl;

  ShapeCreatorDialog dialog(m_currentFrame, m_gantryX, m_gantryY, m_gantryZ,
                             effectivePxPerMm, m_overlayManager, this);

  // Connect signal to add markers when shape is created
  connect(&dialog, &ShapeCreatorDialog::shapeCreated,
          [this, layerName](const QVector<WorldOverlay::Marker>& markers) {
    for (const auto& marker : markers) {
      m_overlayManager->addMarker(layerName, marker);
      std::cout << "[WorldOverlayWidget] Adding marker: " << marker.label.toStdString()
                << " at world(" << marker.worldX << ", " << marker.worldY << ", " << marker.worldZ << ")"
                << std::endl;
    }
    populateMarkerList();
    std::cout << "[WorldOverlayWidget] Added " << markers.size()
              << " markers from shape creator" << std::endl;
  });

  dialog.exec();
}

void WorldOverlayWidget::addMarkerAtCurrentPosition(const QString& layerName,
                                                     WorldOverlay::Marker marker)
{
  marker.worldX = m_gantryX;
  marker.worldY = m_gantryY;
  marker.worldZ = m_gantryZ;
  m_overlayManager->addMarker(layerName, marker);
}

void WorldOverlayWidget::onAlignmentWizardClicked()
{
  // Check if a layer is selected
  QListWidgetItem* layerItem = m_layerList->currentItem();
  if (!layerItem) {
    QMessageBox::warning(this, "Alignment Wizard", "Please select a layer first.");
    return;
  }

  // Get actual layer name from UserRole (display text may have alignment symbols)
  QString layerName = layerItem->data(Qt::UserRole).toString();

  // Get the layer
  WorldOverlay::Layer* layer = m_overlayManager->findLayer(layerName);
  if (!layer) {
    QMessageBox::warning(this, "Alignment Wizard", "Could not find selected layer.");
    return;
  }

  // Create and show alignment wizard (non-modal, so user can move gantry)
  AlignmentWizardDialog* dialog = new AlignmentWizardDialog(layer, m_motionManager, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);  // Auto-delete when closed

  // Set overlay directory for saving reference images
  dialog->setOverlayDirectory(m_overlayManager->overlayDirectory());

  // Set frame provider callback so wizard can capture reference images
  dialog->setFrameProvider([this]() -> QImage {
    return m_currentFrame.copy();  // Return a copy of the current camera frame
  });

  // Connect preview signal for live preview
  connect(dialog, &AlignmentWizardDialog::previewRequested,
          this, &WorldOverlayWidget::onAlignmentPreview);

  // Connect completion signal
  connect(dialog, &AlignmentWizardDialog::alignmentCompleted,
          this, &WorldOverlayWidget::onAlignmentCompleted);

  dialog->show();  // Non-blocking show
}

void WorldOverlayWidget::onAlignmentPreview(double rotation, double translateX, double translateY, double scale)
{
  // Get current layer
  QListWidgetItem* layerItem = m_layerList->currentItem();
  if (!layerItem) return;

  // Get actual layer name from UserRole (display text may have alignment symbols)
  QString layerName = layerItem->data(Qt::UserRole).toString();
  WorldOverlay::Layer* layer = m_overlayManager->findLayer(layerName);
  if (!layer) return;

  // Apply preview transform temporarily
  if (std::abs(rotation) < 0.0001 && std::abs(translateX) < 0.0001 &&
      std::abs(translateY) < 0.0001 && std::abs(scale - 1.0) < 0.0001) {
    // No transform - reset to template
    layer->resetMarkersToTemplate();
  } else {
    // Create temporary transform and apply
    WorldOverlay::LocalCoordinateSystem tempSystem = layer->localSystem;
    tempSystem.rotation = rotation;
    tempSystem.translateX = translateX;
    tempSystem.translateY = translateY;
    tempSystem.scale = scale;
    tempSystem.transformValid = true;

    // Transform each marker
    for (auto& marker : layer->markers) {
      tempSystem.transformPoint(marker.templateX, marker.templateY,
                                marker.worldX, marker.worldY);
      marker.worldZ = marker.templateZ;

      if (marker.type == WorldOverlay::MarkerType::Line) {
        tempSystem.transformPoint(marker.endTemplateX, marker.endTemplateY,
                                  marker.endWorldX, marker.endWorldY);
      }
    }
  }

  // Force redraw
  update();
}

void WorldOverlayWidget::onAlignmentCompleted()
{
  // Refresh UI after alignment
  populateMarkerList();
  update();

  // Save the overlay
  if (m_overlayManager->hasCurrentOverlay()) {
    m_overlayManager->saveCurrentOverlay();
    std::cout << "[WorldOverlayWidget] Alignment completed and saved." << std::endl;
  }
}

void WorldOverlayWidget::updateOverlay()
{
  if (!m_cameraRunning || !m_subscriber) {
    return;
  }

  // Check if there's a new frame available
  if (!m_subscriber->HasNewFrame()) {
    return;
  }

  // Get current frame from subscriber
  CameraFrameData frameData = m_subscriber->GetLatestFrame();
  if (!frameData.IsValid() || frameData.imageData.empty()) {
    return;
  }

  // Convert CameraFrameData to QImage - use direct pointer reference (no copy yet)
  QImage::Format format = (frameData.channels == 3)
    ? QImage::Format_RGB888
    : QImage::Format_Grayscale8;

  // Create QImage wrapping the raw data (zero-copy for viewing)
  QImage frameView(
    frameData.imageData.data(),
    frameData.width,
    frameData.height,
    frameData.width * frameData.channels,
    format
  );

  // Store original frame dimensions for click-to-move calculations
  m_originalFrameWidth = frameData.width;
  m_originalFrameHeight = frameData.height;

  // Get display size and calculate scale factor
  QSize viewSize = m_cameraView->size();
  int scaledWidth = static_cast<int>(frameData.width * std::min(
      static_cast<double>(viewSize.width()) / frameData.width,
      static_cast<double>(viewSize.height()) / frameData.height));
  int scaledHeight = static_cast<int>(frameData.height * std::min(
      static_cast<double>(viewSize.width()) / frameData.width,
      static_cast<double>(viewSize.height()) / frameData.height));

  // Scale the image directly using FastTransformation (much faster than SmoothTransformation)
  // The visual quality difference is minimal for live video
  QImage scaledFrame = frameView.scaled(scaledWidth, scaledHeight,
                                         Qt::KeepAspectRatio,
                                         Qt::FastTransformation);

  m_subscriber->MarkFrameConsumed();

  // Store a copy for shape creation dialog (only if needed - could optimize further)
  m_currentFrame = scaledFrame;

  // Get pxPerMm from overlay or use default from calibration
  double pxPerMm = m_overlayManager->getDefaultPxPerMm();
  if (m_overlayManager->hasCurrentOverlay()) {
    pxPerMm = m_overlayManager->currentOverlay()->pxPerMm;
  }

  // Calculate the display scale factor
  double displayScale = static_cast<double>(scaledWidth) / frameData.width;

  // Adjust pxPerMm by the display scale factor
  double effectivePxPerMm = pxPerMm * displayScale;

  // Render overlay directly on the scaled QImage (no QPixmap conversion yet)
  QImage rendered = m_renderer->renderOverlay(
      scaledFrame,
      m_overlayManager->currentOverlay(),
      m_gantryXSmoothed, m_gantryYSmoothed,
      scaledFrame.width() / 2.0, scaledFrame.height() / 2.0,
      effectivePxPerMm, m_zoomFactor,
      m_selectedMarkerId);

  // Draw crosshairs directly on the rendered QImage
  QPainter painter(&rendered);
  int centerX = rendered.width() / 2;
  int centerY = rendered.height() / 2;

  // Draw center crosshair (green, full width/height) if enabled
  if (m_crosshairEnabled) {
    painter.setPen(QPen(Qt::green, 2));
    painter.drawLine(0, centerY, rendered.width(), centerY);
    painter.drawLine(centerX, 0, centerX, rendered.height());
  }

  // Draw tracking crosshair (cyan) if click-to-move enabled and mouse over image
  if (m_clickToMoveEnabled && m_mouseOverImage) {
    painter.setPen(QPen(Qt::cyan, 1));
    painter.drawLine(m_mousePosInPixmap.x(), 0, m_mousePosInPixmap.x(), rendered.height());
    painter.drawLine(0, m_mousePosInPixmap.y(), rendered.width(), m_mousePosInPixmap.y());

    // Small cross at mouse position
    int crossSize = 15;
    painter.drawLine(m_mousePosInPixmap.x() - crossSize, m_mousePosInPixmap.y(),
                     m_mousePosInPixmap.x() + crossSize, m_mousePosInPixmap.y());
    painter.drawLine(m_mousePosInPixmap.x(), m_mousePosInPixmap.y() - crossSize,
                     m_mousePosInPixmap.x(), m_mousePosInPixmap.y() + crossSize);
  }

  painter.end();

  // Single conversion to QPixmap at the end for display
  m_cameraView->setPixmap(QPixmap::fromImage(rendered));
}

void WorldOverlayWidget::paintEvent(QPaintEvent* event)
{
  QWidget::paintEvent(event);
}

void WorldOverlayWidget::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
}

void WorldOverlayWidget::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    m_lastMousePos = event->pos();
    // TODO: Hit test markers for selection
  }
  QWidget::mousePressEvent(event);
}

void WorldOverlayWidget::mouseReleaseEvent(QMouseEvent* event)
{
  m_isDragging = false;
  QWidget::mouseReleaseEvent(event);
}

void WorldOverlayWidget::mouseMoveEvent(QMouseEvent* event)
{
  if (m_isDragging && !m_selectedMarkerId.isEmpty()) {
    // TODO: Drag marker to new position
  }
  QWidget::mouseMoveEvent(event);
}

void WorldOverlayWidget::wheelEvent(QWheelEvent* event)
{
  // Zoom with mouse wheel
  double delta = event->angleDelta().y() / 120.0;
  m_zoomFactor *= (1.0 + delta * 0.1);
  m_zoomFactor = qBound(0.1, m_zoomFactor, 10.0);

  update();
  QWidget::wheelEvent(event);
}

void WorldOverlayWidget::enterEvent(QEnterEvent* event)
{
  m_mouseOverImage = true;
  QWidget::enterEvent(event);
}

void WorldOverlayWidget::leaveEvent(QEvent* event)
{
  m_mouseOverImage = false;
  QWidget::leaveEvent(event);
}

bool WorldOverlayWidget::eventFilter(QObject* obj, QEvent* event)
{
  if (obj == m_cameraView) {
    QPixmap pixmap = m_cameraView->pixmap();

    if (event->type() == QEvent::MouseMove) {
      QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

      if (!pixmap.isNull()) {
        // Calculate where the pixmap is positioned within the label (centered)
        int pixmapOffsetX = (m_cameraView->width() - pixmap.width()) / 2;
        int pixmapOffsetY = (m_cameraView->height() - pixmap.height()) / 2;

        // Calculate mouse position relative to the pixmap itself
        int mouseInPixmapX = mouseEvent->pos().x() - pixmapOffsetX;
        int mouseInPixmapY = mouseEvent->pos().y() - pixmapOffsetY;

        // Calculate offset from center of pixmap
        int centerX = pixmap.width() / 2;
        int centerY = pixmap.height() / 2;
        int offsetFromCenterX = mouseInPixmapX - centerX;
        int offsetFromCenterY = mouseInPixmapY - centerY;

        // Check if mouse is actually over the pixmap (not just the label)
        if (mouseInPixmapX >= 0 && mouseInPixmapX < pixmap.width() &&
            mouseInPixmapY >= 0 && mouseInPixmapY < pixmap.height()) {
          m_mouseOverImage = true;
          m_mousePosInPixmap = QPoint(mouseInPixmapX, mouseInPixmapY);

          // Update mouse position label with detailed info
          m_mousePositionLabel->setText(QString("Pix:%1,%2 Ctr:%3,%4 Sz:%5x%6")
            .arg(mouseInPixmapX).arg(mouseInPixmapY)
            .arg(offsetFromCenterX).arg(offsetFromCenterY)
            .arg(pixmap.width()).arg(pixmap.height()));
        } else {
          m_mouseOverImage = false;
          m_mousePositionLabel->setText("Mouse: outside");
        }
      } else {
        m_mousePositionLabel->setText("Mouse: no pixmap");
      }
    }
    else if (event->type() == QEvent::Leave) {
      m_mouseOverImage = false;
      m_mousePositionLabel->setText("Mouse: --");
    }
    else if (event->type() == QEvent::MouseButtonPress) {
      QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
      if (mouseEvent->button() == Qt::LeftButton && m_clickToMoveEnabled && !pixmap.isNull()) {
        // Calculate pixmap offset within label
        int pixmapOffsetX = (m_cameraView->width() - pixmap.width()) / 2;
        int pixmapOffsetY = (m_cameraView->height() - pixmap.height()) / 2;

        // Get click position relative to pixmap
        int clickInPixmapX = mouseEvent->pos().x() - pixmapOffsetX;
        int clickInPixmapY = mouseEvent->pos().y() - pixmapOffsetY;

        // Only process click if within pixmap bounds
        if (clickInPixmapX >= 0 && clickInPixmapX < pixmap.width() &&
            clickInPixmapY >= 0 && clickInPixmapY < pixmap.height()) {
          handleImageClick(QPoint(clickInPixmapX, clickInPixmapY));
        }
      }
    }
  }
  return QWidget::eventFilter(obj, event);
}

void WorldOverlayWidget::onClickToMoveToggled(bool checked)
{
  m_clickToMoveEnabled = checked;

  if (checked) {
    m_cameraView->setCursor(Qt::CrossCursor);
  } else {
    m_cameraView->setCursor(Qt::ArrowCursor);
  }

  std::cout << "[WorldOverlayWidget] Click-to-move " << (checked ? "ENABLED" : "DISABLED") << std::endl;
}

void WorldOverlayWidget::onCrosshairToggled(bool checked)
{
  m_crosshairEnabled = checked;
  std::cout << "[WorldOverlayWidget] Crosshair " << (checked ? "enabled" : "disabled") << std::endl;
}

void WorldOverlayWidget::handleImageClick(const QPoint& pixmapClickPos)
{
  if (!m_motionManager || m_currentFrame.isNull()) {
    std::cerr << "[WorldOverlayWidget] Cannot move - no motion manager or image" << std::endl;
    return;
  }

  // Get the displayed pixmap
  QPixmap currentPixmap = m_cameraView->pixmap();
  if (currentPixmap.isNull()) {
    return;
  }

  // Calculate pixel offset from center (in displayed pixmap coordinates)
  int centerX = currentPixmap.width() / 2;
  int centerY = currentPixmap.height() / 2;

  int pixelOffsetX = pixmapClickPos.x() - centerX;
  int pixelOffsetY = pixmapClickPos.y() - centerY;

  // Convert to mm using pixel-to-mm calibration
  // The pxPerMm calibration is for the original camera resolution.
  // We need to scale by the ratio of original to displayed size.
  double pxPerMm = m_overlayManager->getDefaultPxPerMm();
  double displayScale = (m_originalFrameWidth > 0)
      ? static_cast<double>(currentPixmap.width()) / m_originalFrameWidth
      : 1.0;

  // Pixel offset in display coords / (pxPerMm * displayScale) = mm offset
  double offsetX_mm = pixelOffsetX / (pxPerMm * displayScale);
  double offsetY_mm = pixelOffsetY / (pxPerMm * displayScale);

  std::cout << "[WorldOverlayWidget] Click detected: Pixel offset (" << pixelOffsetX << ", " << pixelOffsetY << ")"
            << " = (" << offsetX_mm << " mm, " << offsetY_mm << " mm)" << std::endl;

  // Get current position
  PositionStruct currentPos = m_motionManager->GetCurrentPosition("gantry-main");

  // Calculate new absolute position
  double newX = currentPos.x + offsetX_mm;
  double newY = currentPos.y + offsetY_mm;
  double newZ = currentPos.z;

  std::cout << "[WorldOverlayWidget] Current position: X=" << currentPos.x
            << ", Y=" << currentPos.y << ", Z=" << currentPos.z << std::endl;
  std::cout << "[WorldOverlayWidget] Target position: X=" << newX
            << ", Y=" << newY << ", Z=" << newZ << std::endl;

  // Move to absolute position
  bool success = m_motionManager->MoveToAbs("gantry-main", newX, newY, newZ, 10.0, false);

  if (success) {
    std::cout << "[WorldOverlayWidget] Gantry move command sent successfully" << std::endl;
    emit clickToMoveTriggered(offsetX_mm, offsetY_mm);
  } else {
    std::cerr << "[WorldOverlayWidget] Failed to send gantry move command!" << std::endl;
  }
}

void WorldOverlayWidget::loadPixelToMmCalibration()
{
  // Load from val_pixelmm.json - the overlay manager already has this
  double pxPerMm = m_overlayManager->getDefaultPxPerMm();
  if (pxPerMm > 0) {
    m_pixelToMmX = 1.0 / pxPerMm;
    m_pixelToMmY = 1.0 / pxPerMm;
    std::cout << "[WorldOverlayWidget] Loaded pixel-to-mm calibration: " << m_pixelToMmX << std::endl;
  }
}
