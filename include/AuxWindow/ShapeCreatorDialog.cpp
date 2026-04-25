// ShapeCreatorDialog.cpp
#include "ShapeCreatorDialog.h"
#include "WorldOverlayManager.h"
#include "WorldOverlay.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMouseEvent>
#include <QPainter>
#include <QMessageBox>
#include <QInputDialog>
#include <iostream>
#include <cmath>

ShapeCreatorDialog::ShapeCreatorDialog(const QImage& stillFrame,
                                         double gantryX, double gantryY, double gantryZ,
                                         double pxPerMm,
                                         WorldOverlayManager* overlayManager,
                                         QWidget* parent)
  : QDialog(parent)
  , m_stillFrame(stillFrame)
  , m_gantryX(gantryX)
  , m_gantryY(gantryY)
  , m_gantryZ(gantryZ)
  , m_pxPerMm(pxPerMm)
  , m_overlayManager(overlayManager)
{
  setWindowTitle("Shape Creator");
  setModal(true);
  setMinimumSize(900, 700);

  setupUI();

  std::cout << "[ShapeCreatorDialog] Created with frame " << stillFrame.width()
            << "x" << stillFrame.height()
            << " at gantry (" << gantryX << ", " << gantryY << ", " << gantryZ << ")"
            << " pxPerMm=" << pxPerMm << std::endl;
}

ShapeCreatorDialog::~ShapeCreatorDialog()
{
  std::cout << "[ShapeCreatorDialog] Destroyed" << std::endl;
}

void ShapeCreatorDialog::setupUI()
{
  setStyleSheet("QDialog { background-color: #1e1e1e; color: #ffffff; }");

  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(8, 8, 8, 8);
  mainLayout->setSpacing(8);

  // Left panel - Image display
  QWidget* imagePanel = new QWidget();
  imagePanel->setStyleSheet("QWidget { background-color: #252525; }");
  QVBoxLayout* imageLayout = new QVBoxLayout(imagePanel);
  imageLayout->setContentsMargins(4, 4, 4, 4);

  m_imageLabel = new QLabel();
  m_imageLabel->setAlignment(Qt::AlignCenter);
  m_imageLabel->setStyleSheet("QLabel { background-color: #1a1a1a; border: 1px solid #3d3d3d; }");
  m_imageLabel->setMouseTracking(true);
  m_imageLabel->installEventFilter(this);
  m_imageLabel->setMinimumSize(640, 480);

  // Scale image to fit
  m_displayPixmap = QPixmap::fromImage(m_stillFrame);
  QPixmap scaledPixmap = m_displayPixmap.scaled(800, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  m_displayScale = static_cast<double>(scaledPixmap.width()) / m_stillFrame.width();
  m_imageLabel->setPixmap(scaledPixmap);

  imageLayout->addWidget(m_imageLabel, 1);

  // Mouse position label
  m_mousePositionLabel = new QLabel("Mouse: --");
  m_mousePositionLabel->setStyleSheet("QLabel { color: #00ff00; font-family: monospace; }");
  imageLayout->addWidget(m_mousePositionLabel);

  mainLayout->addWidget(imagePanel, 3);

  // Right panel - Controls
  QWidget* controlPanel = new QWidget();
  controlPanel->setStyleSheet("QWidget { background-color: #252525; }");
  controlPanel->setFixedWidth(280);
  QVBoxLayout* controlLayout = new QVBoxLayout(controlPanel);
  controlLayout->setContentsMargins(8, 8, 8, 8);
  controlLayout->setSpacing(8);

  // Instruction label
  m_instructionLabel = new QLabel("Click 3 corners of the rectangle.\nThe 4th corner will be calculated.");
  m_instructionLabel->setStyleSheet("QLabel { color: #ffff00; font-size: 12px; padding: 8px; background-color: #2d2d2d; border-radius: 4px; }");
  m_instructionLabel->setWordWrap(true);
  controlLayout->addWidget(m_instructionLabel);

  // Shape type selection
  QGroupBox* shapeGroup = new QGroupBox("Shape Type");
  shapeGroup->setStyleSheet(R"(
    QGroupBox {
      color: #ffffff;
      font-weight: bold;
      border: 1px solid #3d3d3d;
      border-radius: 4px;
      margin-top: 8px;
      padding-top: 8px;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      left: 8px;
      padding: 0 4px;
    }
  )");
  QVBoxLayout* shapeLayout = new QVBoxLayout(shapeGroup);

  m_shapeTypeDropdown = new QComboBox();
  m_shapeTypeDropdown->addItem("3-Point Rectangle", static_cast<int>(ShapeType::ThreePointRectangle));
  m_shapeTypeDropdown->addItem("Polygon (Multi-Point)", static_cast<int>(ShapeType::Polygon));
  m_shapeTypeDropdown->setStyleSheet("QComboBox { background-color: #3d3d3d; color: white; padding: 6px; }");
  shapeLayout->addWidget(m_shapeTypeDropdown);

  controlLayout->addWidget(shapeGroup);

  // Captured points list
  QGroupBox* pointsGroup = new QGroupBox("Captured Points");
  pointsGroup->setStyleSheet(shapeGroup->styleSheet());
  QVBoxLayout* pointsLayout = new QVBoxLayout(pointsGroup);

  m_pointsList = new QListWidget();
  m_pointsList->setStyleSheet(R"(
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
  )");
  m_pointsList->setMaximumHeight(150);
  pointsLayout->addWidget(m_pointsList);

  QHBoxLayout* pointButtonsLayout = new QHBoxLayout();
  m_undoButton = new QPushButton("Undo");
  m_clearButton = new QPushButton("Clear All");

  QString buttonStyle = R"(
    QPushButton {
      background-color: #3d3d3d;
      color: white;
      padding: 6px 12px;
      border: none;
      border-radius: 3px;
    }
    QPushButton:hover { background-color: #4d4d4d; }
    QPushButton:pressed { background-color: #2d2d2d; }
  )";
  m_undoButton->setStyleSheet(buttonStyle);
  m_clearButton->setStyleSheet(buttonStyle);

  pointButtonsLayout->addWidget(m_undoButton);
  pointButtonsLayout->addWidget(m_clearButton);
  pointsLayout->addLayout(pointButtonsLayout);

  controlLayout->addWidget(pointsGroup);

  // Gantry position info
  QGroupBox* infoGroup = new QGroupBox("Gantry Position (Fixed)");
  infoGroup->setStyleSheet(shapeGroup->styleSheet());
  QVBoxLayout* infoLayout = new QVBoxLayout(infoGroup);

  QLabel* posLabel = new QLabel(QString("X: %1\nY: %2\nZ: %3")
      .arg(m_gantryX, 0, 'f', 4)
      .arg(m_gantryY, 0, 'f', 4)
      .arg(m_gantryZ, 0, 'f', 4));
  posLabel->setStyleSheet("QLabel { color: #00ff00; font-family: monospace; }");
  infoLayout->addWidget(posLabel);

  controlLayout->addWidget(infoGroup);

  controlLayout->addStretch();

  // Action buttons
  QHBoxLayout* actionLayout = new QHBoxLayout();

  m_createButton = new QPushButton("Create Shape");
  m_createButton->setStyleSheet(R"(
    QPushButton {
      background-color: #0078d4;
      color: white;
      padding: 10px 20px;
      border: none;
      border-radius: 4px;
      font-weight: bold;
    }
    QPushButton:hover { background-color: #1084d8; }
    QPushButton:disabled { background-color: #555555; color: #888888; }
  )");
  m_createButton->setEnabled(false);

  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setStyleSheet(buttonStyle);

  actionLayout->addWidget(m_createButton);
  actionLayout->addWidget(m_cancelButton);
  controlLayout->addLayout(actionLayout);

  mainLayout->addWidget(controlPanel);

  // Connections
  connect(m_shapeTypeDropdown, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ShapeCreatorDialog::onShapeTypeChanged);
  connect(m_undoButton, &QPushButton::clicked, this, &ShapeCreatorDialog::onUndoLastPoint);
  connect(m_clearButton, &QPushButton::clicked, this, &ShapeCreatorDialog::onClearPoints);
  connect(m_createButton, &QPushButton::clicked, this, &ShapeCreatorDialog::onCreateShape);
  connect(m_cancelButton, &QPushButton::clicked, this, &ShapeCreatorDialog::onCancel);
}

bool ShapeCreatorDialog::eventFilter(QObject* obj, QEvent* event)
{
  if (obj == m_imageLabel) {
    if (event->type() == QEvent::MouseMove) {
      QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
      m_currentMousePos = mouseEvent->pos();
      m_mouseOverImage = true;

      // Calculate position relative to original image
      QPixmap pixmap = m_imageLabel->pixmap();
      if (!pixmap.isNull()) {
        int offsetX = (m_imageLabel->width() - pixmap.width()) / 2;
        int offsetY = (m_imageLabel->height() - pixmap.height()) / 2;

        int imgX = static_cast<int>((m_currentMousePos.x() - offsetX) / m_displayScale);
        int imgY = static_cast<int>((m_currentMousePos.y() - offsetY) / m_displayScale);

        // Calculate world coordinates
        CapturedPoint worldPt = screenToWorld(QPoint(imgX, imgY));

        m_mousePositionLabel->setText(QString("Img: (%1, %2) World: (%3, %4, %5)")
            .arg(imgX).arg(imgY)
            .arg(worldPt.worldX, 0, 'f', 4)
            .arg(worldPt.worldY, 0, 'f', 4)
            .arg(worldPt.worldZ, 0, 'f', 4));
      }

      updatePreview();
      return true;
    }
    else if (event->type() == QEvent::Leave) {
      m_mouseOverImage = false;
      m_mousePositionLabel->setText("Mouse: --");
      updatePreview();
      return true;
    }
    else if (event->type() == QEvent::MouseButtonPress) {
      QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        // Calculate position in original image coordinates
        QPixmap pixmap = m_imageLabel->pixmap();
        if (!pixmap.isNull()) {
          int offsetX = (m_imageLabel->width() - pixmap.width()) / 2;
          int offsetY = (m_imageLabel->height() - pixmap.height()) / 2;

          int imgX = static_cast<int>((mouseEvent->pos().x() - offsetX) / m_displayScale);
          int imgY = static_cast<int>((mouseEvent->pos().y() - offsetY) / m_displayScale);

          // Check bounds
          if (imgX >= 0 && imgX < m_stillFrame.width() &&
              imgY >= 0 && imgY < m_stillFrame.height()) {
            CapturedPoint pt = screenToWorld(QPoint(imgX, imgY));
            m_capturedPoints.append(pt);
            updatePointsList();
            updatePreview();

            std::cout << "[ShapeCreatorDialog] Captured point " << m_capturedPoints.size()
                      << ": screen(" << imgX << ", " << imgY << ")"
                      << " world(" << pt.worldX << ", " << pt.worldY << ", " << pt.worldZ << ")"
                      << std::endl;

            // Enable create button when enough points
            m_createButton->setEnabled(m_capturedPoints.size() >= m_requiredPoints);
          }
        }
        return true;
      }
    }
  }
  return QDialog::eventFilter(obj, event);
}

ShapeCreatorDialog::CapturedPoint ShapeCreatorDialog::screenToWorld(const QPoint& screenPos)
{
  CapturedPoint pt;
  pt.screenPos = screenPos;

  // Calculate offset from image center (in original image pixels)
  int centerX = m_stillFrame.width() / 2;
  int centerY = m_stillFrame.height() / 2;

  double offsetX = screenPos.x() - centerX;
  double offsetY = screenPos.y() - centerY;

  // Convert pixels to mm (using original image pxPerMm)
  double offsetXmm = offsetX / m_pxPerMm;
  double offsetYmm = offsetY / m_pxPerMm;

  // World coordinates: gantry position + offset
  // This must match WorldOverlayRenderer::worldToScreen inverse formula:
  //   screenX = centerX + (worldX - gantryX) * pxPerMm
  //   => worldX = gantryX + (screenX - centerX) / pxPerMm
  pt.worldX = m_gantryX + offsetXmm;
  pt.worldY = m_gantryY + offsetYmm;
  pt.worldZ = m_gantryZ;

  std::cout << "[ShapeCreatorDialog] screenToWorld: screen(" << screenPos.x() << "," << screenPos.y() << ")"
            << " center(" << centerX << "," << centerY << ")"
            << " offset_px(" << offsetX << "," << offsetY << ")"
            << " offset_mm(" << offsetXmm << "," << offsetYmm << ")"
            << " world(" << pt.worldX << "," << pt.worldY << "," << pt.worldZ << ")"
            << " pxPerMm=" << m_pxPerMm << std::endl;

  return pt;
}

void ShapeCreatorDialog::onShapeTypeChanged(int /*index*/)
{
  m_currentShapeType = static_cast<ShapeType>(m_shapeTypeDropdown->currentData().toInt());

  switch (m_currentShapeType) {
    case ShapeType::ThreePointRectangle:
      m_requiredPoints = 3;
      m_instructionLabel->setText("Click 3 corners of the rectangle.\nThe 4th corner will be calculated automatically.");
      break;
    case ShapeType::Polygon:
      m_requiredPoints = 3;  // Minimum 3 for a polygon
      m_instructionLabel->setText("Click points to form a polygon.\nMinimum 3 points required.");
      break;
    default:
      m_requiredPoints = 2;
      break;
  }

  onClearPoints();
}

void ShapeCreatorDialog::onClearPoints()
{
  m_capturedPoints.clear();
  updatePointsList();
  updatePreview();
  m_createButton->setEnabled(false);
}

void ShapeCreatorDialog::onUndoLastPoint()
{
  if (!m_capturedPoints.isEmpty()) {
    m_capturedPoints.removeLast();
    updatePointsList();
    updatePreview();
    m_createButton->setEnabled(m_capturedPoints.size() >= m_requiredPoints);
  }
}

void ShapeCreatorDialog::updatePointsList()
{
  m_pointsList->clear();
  for (int i = 0; i < m_capturedPoints.size(); ++i) {
    const auto& pt = m_capturedPoints[i];
    QString text = QString("P%1: (%2, %3, %4)")
        .arg(i + 1)
        .arg(pt.worldX, 0, 'f', 4)
        .arg(pt.worldY, 0, 'f', 4)
        .arg(pt.worldZ, 0, 'f', 4);
    m_pointsList->addItem(text);
  }
}

void ShapeCreatorDialog::updatePreview()
{
  // Redraw the image with captured points and preview
  QPixmap preview = QPixmap::fromImage(m_stillFrame);
  preview = preview.scaled(preview.width() * m_displayScale,
                           preview.height() * m_displayScale,
                           Qt::KeepAspectRatio, Qt::SmoothTransformation);

  QPainter painter(&preview);
  painter.setRenderHint(QPainter::Antialiasing);

  // Draw captured points
  QPen pointPen(Qt::green, 3);
  painter.setPen(pointPen);
  painter.setBrush(QBrush(Qt::green));

  QVector<QPoint> scaledPoints;
  for (const auto& pt : m_capturedPoints) {
    QPoint scaled(pt.screenPos.x() * m_displayScale,
                  pt.screenPos.y() * m_displayScale);
    scaledPoints.append(scaled);

    // Draw point
    painter.drawEllipse(scaled, 5, 5);
  }

  // Draw lines connecting points
  if (scaledPoints.size() >= 2) {
    QPen linePen(Qt::green, 2);
    painter.setPen(linePen);
    for (int i = 0; i < scaledPoints.size() - 1; ++i) {
      painter.drawLine(scaledPoints[i], scaledPoints[i + 1]);
    }
  }

  // For 3-point rectangle, show preview of 4th point
  if (m_currentShapeType == ShapeType::ThreePointRectangle && scaledPoints.size() == 3) {
    QPoint p4 = calculateFourthCorner(
        QPoint(m_capturedPoints[0].screenPos.x(), m_capturedPoints[0].screenPos.y()),
        QPoint(m_capturedPoints[1].screenPos.x(), m_capturedPoints[1].screenPos.y()),
        QPoint(m_capturedPoints[2].screenPos.x(), m_capturedPoints[2].screenPos.y()));

    QPoint scaledP4(p4.x() * m_displayScale, p4.y() * m_displayScale);

    // Draw 4th point (calculated)
    QPen calcPen(Qt::cyan, 3);
    painter.setPen(calcPen);
    painter.setBrush(QBrush(Qt::cyan));
    painter.drawEllipse(scaledP4, 5, 5);

    // Complete the rectangle
    QPen rectPen(Qt::cyan, 2, Qt::DashLine);
    painter.setPen(rectPen);
    painter.drawLine(scaledPoints[2], scaledP4);
    painter.drawLine(scaledP4, scaledPoints[0]);
  }

  // Close polygon if enough points
  if (m_currentShapeType == ShapeType::Polygon && scaledPoints.size() >= 3) {
    QPen closePen(Qt::green, 2, Qt::DashLine);
    painter.setPen(closePen);
    painter.drawLine(scaledPoints.last(), scaledPoints.first());
  }

  // Draw crosshair at current mouse position if over image
  if (m_mouseOverImage) {
    QPixmap pixmap = m_imageLabel->pixmap();
    int offsetX = (m_imageLabel->width() - pixmap.width()) / 2;
    int offsetY = (m_imageLabel->height() - pixmap.height()) / 2;

    int drawX = m_currentMousePos.x() - offsetX;
    int drawY = m_currentMousePos.y() - offsetY;

    if (drawX >= 0 && drawX < preview.width() && drawY >= 0 && drawY < preview.height()) {
      QPen crossPen(Qt::yellow, 1);
      painter.setPen(crossPen);
      painter.drawLine(drawX, 0, drawX, preview.height());
      painter.drawLine(0, drawY, preview.width(), drawY);
    }
  }

  painter.end();
  m_imageLabel->setPixmap(preview);
}

QPoint ShapeCreatorDialog::calculateFourthCorner(const QPoint& p1, const QPoint& p2, const QPoint& p3)
{
  // Given 3 corners of a rectangle (p1, p2, p3 where p2 is the corner between p1 and p3)
  // Calculate p4 = p1 + p3 - p2
  return QPoint(p1.x() + p3.x() - p2.x(),
                p1.y() + p3.y() - p2.y());
}

void ShapeCreatorDialog::onCreateShape()
{
  // Ask for shape name
  bool ok;
  QString shapeName = QInputDialog::getText(this, "Shape Name",
      "Enter a name for this shape:", QLineEdit::Normal,
      m_currentShapeType == ShapeType::ThreePointRectangle ? "Rect1" : "Poly1", &ok);

  if (!ok || shapeName.isEmpty()) {
    return;
  }

  m_createdMarkers.clear();

  switch (m_currentShapeType) {
    case ShapeType::ThreePointRectangle:
      createRectangleMarkers(shapeName);
      break;
    case ShapeType::Polygon:
      createPolygonMarkers(shapeName);
      break;
    default:
      break;
  }

  if (!m_createdMarkers.isEmpty()) {
    emit shapeCreated(m_createdMarkers);
    accept();
  }
}

void ShapeCreatorDialog::createRectangleMarkers(const QString& shapeName)
{
  if (m_capturedPoints.size() < 3) return;

  // Calculate 4th corner
  QPoint p4Screen = calculateFourthCorner(
      m_capturedPoints[0].screenPos,
      m_capturedPoints[1].screenPos,
      m_capturedPoints[2].screenPos);

  CapturedPoint p4 = screenToWorld(p4Screen);

  // Create 4 corner markers
  QVector<CapturedPoint> corners = {
      m_capturedPoints[0],
      m_capturedPoints[1],
      m_capturedPoints[2],
      p4
  };

  // Create corner point markers (hidden labels, just for reference)
  for (int i = 0; i < 4; ++i) {
    WorldOverlay::Marker marker;
    marker.type = WorldOverlay::MarkerType::Point;
    marker.worldX = corners[i].worldX;
    marker.worldY = corners[i].worldY;
    marker.worldZ = corners[i].worldZ;
    // Initialize template = world (same when first teaching)
    marker.templateX = corners[i].worldX;
    marker.templateY = corners[i].worldY;
    marker.templateZ = corners[i].worldZ;
    marker.label = QString("%1.P%2").arg(shapeName).arg(i + 1);
    marker.showLabel = false;  // Don't show individual point labels
    marker.sizePx = 6;
    m_createdMarkers.append(marker);
  }

  // Create 4 line markers for the rectangle edges
  for (int i = 0; i < 4; ++i) {
    int next = (i + 1) % 4;
    WorldOverlay::Marker line;
    line.type = WorldOverlay::MarkerType::Line;
    line.worldX = corners[i].worldX;
    line.worldY = corners[i].worldY;
    line.worldZ = corners[i].worldZ;
    line.endWorldX = corners[next].worldX;
    line.endWorldY = corners[next].worldY;
    // Initialize template = world (same when first teaching)
    line.templateX = corners[i].worldX;
    line.templateY = corners[i].worldY;
    line.templateZ = corners[i].worldZ;
    line.endTemplateX = corners[next].worldX;
    line.endTemplateY = corners[next].worldY;
    line.lineWidthPx = 2;
    // Label only the first line with the shape name
    if (i == 0) {
      line.label = shapeName;
      line.showLabel = true;
    }
    m_createdMarkers.append(line);
  }

  std::cout << "[ShapeCreatorDialog] Created rectangle '" << shapeName.toStdString()
            << "' with " << m_createdMarkers.size() << " markers" << std::endl;
}

void ShapeCreatorDialog::createPolygonMarkers(const QString& shapeName)
{
  if (m_capturedPoints.size() < 3) return;

  // Create point markers at each vertex (hidden labels)
  for (int i = 0; i < m_capturedPoints.size(); ++i) {
    WorldOverlay::Marker marker;
    marker.type = WorldOverlay::MarkerType::Point;
    marker.worldX = m_capturedPoints[i].worldX;
    marker.worldY = m_capturedPoints[i].worldY;
    marker.worldZ = m_capturedPoints[i].worldZ;
    // Initialize template = world (same when first teaching)
    marker.templateX = m_capturedPoints[i].worldX;
    marker.templateY = m_capturedPoints[i].worldY;
    marker.templateZ = m_capturedPoints[i].worldZ;
    marker.label = QString("%1.P%2").arg(shapeName).arg(i + 1);
    marker.showLabel = false;  // Don't show individual point labels
    marker.sizePx = 6;
    m_createdMarkers.append(marker);
  }

  // Create line markers for edges
  for (int i = 0; i < m_capturedPoints.size(); ++i) {
    int next = (i + 1) % m_capturedPoints.size();
    WorldOverlay::Marker line;
    line.type = WorldOverlay::MarkerType::Line;
    line.worldX = m_capturedPoints[i].worldX;
    line.worldY = m_capturedPoints[i].worldY;
    line.worldZ = m_capturedPoints[i].worldZ;
    line.endWorldX = m_capturedPoints[next].worldX;
    line.endWorldY = m_capturedPoints[next].worldY;
    // Initialize template = world (same when first teaching)
    line.templateX = m_capturedPoints[i].worldX;
    line.templateY = m_capturedPoints[i].worldY;
    line.templateZ = m_capturedPoints[i].worldZ;
    line.endTemplateX = m_capturedPoints[next].worldX;
    line.endTemplateY = m_capturedPoints[next].worldY;
    line.lineWidthPx = 2;
    // Label only the first line with the shape name
    if (i == 0) {
      line.label = shapeName;
      line.showLabel = true;
    }
    m_createdMarkers.append(line);
  }

  std::cout << "[ShapeCreatorDialog] Created polygon '" << shapeName.toStdString()
            << "' with " << m_createdMarkers.size() << " markers" << std::endl;
}

void ShapeCreatorDialog::onCancel()
{
  reject();
}

void ShapeCreatorDialog::mousePressEvent(QMouseEvent* event)
{
  QDialog::mousePressEvent(event);
}

void ShapeCreatorDialog::mouseMoveEvent(QMouseEvent* event)
{
  QDialog::mouseMoveEvent(event);
}

void ShapeCreatorDialog::paintEvent(QPaintEvent* event)
{
  QDialog::paintEvent(event);
}
