// VisionImageWidget.cpp
#include "VisionImageWidget.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QCursor>
#include <cmath>

// ============ ImageDisplayLabel Implementation ============

ImageDisplayLabel::ImageDisplayLabel(QWidget* parent)
    : QLabel(parent)
{
    setMouseTracking(true);
}

void ImageDisplayLabel::setROI(int startX, int startY, int endX, int endY)
{
    m_roiStartX = startX;
    m_roiStartY = startY;
    m_roiEndX = endX;
    m_roiEndY = endY;
}

void ImageDisplayLabel::setScaleFactors(double scaleX, double scaleY)
{
    m_scaleX = scaleX;
    m_scaleY = scaleY;
}

void ImageDisplayLabel::setPixmapSize(int width, int height)
{
    m_pixmapWidth = width;
    m_pixmapHeight = height;
}

void ImageDisplayLabel::setSearchBox(int startX, int startY, int endX, int endY)
{
    m_searchBoxStartX = startX;
    m_searchBoxStartY = startY;
    m_searchBoxEndX = endX;
    m_searchBoxEndY = endY;
}

void ImageDisplayLabel::setTemplateSelection(int startX, int startY, int endX, int endY)
{
    m_templateSelectionStartX = startX;
    m_templateSelectionStartY = startY;
    m_templateSelectionEndX = endX;
    m_templateSelectionEndY = endY;
}

void ImageDisplayLabel::getTemplateSelection(int& startX, int& startY, int& endX, int& endY) const
{
    startX = m_templateSelectionStartX;
    startY = m_templateSelectionStartY;
    endX = m_templateSelectionEndX;
    endY = m_templateSelectionEndY;
}

QPoint ImageDisplayLabel::pixmapOffset() const
{
    // Calculate offset of pixmap within the label (due to center alignment)
    int offsetX = 0;
    int offsetY = 0;

    if (m_pixmapWidth > 0 && m_pixmapHeight > 0) {
        offsetX = (width() - m_pixmapWidth) / 2;
        offsetY = (height() - m_pixmapHeight) / 2;
    }

    return QPoint(std::max(0, offsetX), std::max(0, offsetY));
}

QPoint ImageDisplayLabel::toPixmapCoords(const QPoint& widgetPos) const
{
    QPoint offset = pixmapOffset();
    return widgetPos - offset;
}

ImageDisplayLabel::DragCorner ImageDisplayLabel::hitTestCorner(const QPoint& pos, int startX, int startY, int endX, int endY)
{
    // Convert widget position to pixmap coordinates
    QPoint pixmapPos = toPixmapCoords(pos);

    const int hitRadius = 20;  // pixels for hit detection (increased for easier clicking)

    // Calculate scaled corner positions (in pixmap coordinates)
    int scaledStartX = static_cast<int>(startX * m_scaleX);
    int scaledStartY = static_cast<int>(startY * m_scaleY);
    int scaledEndX = static_cast<int>(endX * m_scaleX);
    int scaledEndY = static_cast<int>(endY * m_scaleY);

    // Check each corner
    QPoint topLeft(scaledStartX, scaledStartY);
    QPoint topRight(scaledEndX, scaledStartY);
    QPoint bottomLeft(scaledStartX, scaledEndY);
    QPoint bottomRight(scaledEndX, scaledEndY);

    if ((pixmapPos - topLeft).manhattanLength() < hitRadius) return DragCorner::TopLeft;
    if ((pixmapPos - topRight).manhattanLength() < hitRadius) return DragCorner::TopRight;
    if ((pixmapPos - bottomLeft).manhattanLength() < hitRadius) return DragCorner::BottomLeft;
    if ((pixmapPos - bottomRight).manhattanLength() < hitRadius) return DragCorner::BottomRight;

    return DragCorner::None;
}

ImageDisplayLabel::DragCorner ImageDisplayLabel::hitTestROICorner(const QPoint& pos)
{
    if (!m_roiEnabled) return DragCorner::None;
    return hitTestCorner(pos, m_roiStartX, m_roiStartY, m_roiEndX, m_roiEndY);
}

ImageDisplayLabel::DragCorner ImageDisplayLabel::hitTestSearchBoxCorner(const QPoint& pos)
{
    if (!m_searchBoxEnabled) return DragCorner::None;
    return hitTestCorner(pos, m_searchBoxStartX, m_searchBoxStartY, m_searchBoxEndX, m_searchBoxEndY);
}

ImageDisplayLabel::DragCorner ImageDisplayLabel::hitTestTemplateSelectionCorner(const QPoint& pos)
{
    if (!m_templateSelectionEnabled) return DragCorner::None;
    return hitTestCorner(pos, m_templateSelectionStartX, m_templateSelectionStartY,
                        m_templateSelectionEndX, m_templateSelectionEndY);
}

void ImageDisplayLabel::updateCursor(const QPoint& pos)
{
    // Check template selection first (highest priority when teaching)
    DragCorner corner = hitTestTemplateSelectionCorner(pos);
    if (corner == DragCorner::None) {
        // Check ROI corners (higher priority since it's usually inside search box)
        corner = hitTestROICorner(pos);
    }
    if (corner == DragCorner::None) {
        corner = hitTestSearchBoxCorner(pos);
    }

    switch (corner) {
        case DragCorner::TopLeft:
        case DragCorner::BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case DragCorner::TopRight:
        case DragCorner::BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        default:
            setCursor(Qt::ArrowCursor);
            break;
    }
}

void ImageDisplayLabel::mousePressEvent(QMouseEvent* event)
{
    if ((!m_roiEnabled && !m_searchBoxEnabled && !m_templateSelectionEnabled) || event->button() != Qt::LeftButton) {
        QLabel::mousePressEvent(event);
        return;
    }

    // Check template selection first (highest priority)
    m_draggingCorner = hitTestTemplateSelectionCorner(event->pos());
    if (m_draggingCorner != DragCorner::None) {
        m_draggingTarget = DragTarget::TemplateSelection;
        m_isDragging = true;
        event->accept();
        return;
    }

    // Check ROI corners (higher priority)
    m_draggingCorner = hitTestROICorner(event->pos());
    if (m_draggingCorner != DragCorner::None) {
        m_draggingTarget = DragTarget::ROI;
        m_isDragging = true;
        event->accept();
        return;
    }

    // Check Search Box corners
    m_draggingCorner = hitTestSearchBoxCorner(event->pos());
    if (m_draggingCorner != DragCorner::None) {
        m_draggingTarget = DragTarget::SearchBox;
        m_isDragging = true;
        event->accept();
        return;
    }

    QLabel::mousePressEvent(event);
}

void ImageDisplayLabel::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_roiEnabled && !m_searchBoxEnabled && !m_templateSelectionEnabled) {
        QLabel::mouseMoveEvent(event);
        return;
    }

    if (m_isDragging && m_draggingCorner != DragCorner::None) {
        // Convert widget position to pixmap coordinates first
        QPoint pixmapPos = toPixmapCoords(event->pos());

        // Convert pixmap position back to original image coordinates
        int imageX = static_cast<int>(pixmapPos.x() / m_scaleX);
        int imageY = static_cast<int>(pixmapPos.y() / m_scaleY);

        // Clamp to reasonable bounds (0 to 10000)
        imageX = std::max(0, std::min(10000, imageX));
        imageY = std::max(0, std::min(10000, imageY));

        if (m_draggingTarget == DragTarget::TemplateSelection) {
            // Update Template Selection corners
            switch (m_draggingCorner) {
                case DragCorner::TopLeft:
                    m_templateSelectionStartX = imageX;
                    m_templateSelectionStartY = imageY;
                    break;
                case DragCorner::TopRight:
                    m_templateSelectionEndX = imageX;
                    m_templateSelectionStartY = imageY;
                    break;
                case DragCorner::BottomLeft:
                    m_templateSelectionStartX = imageX;
                    m_templateSelectionEndY = imageY;
                    break;
                case DragCorner::BottomRight:
                    m_templateSelectionEndX = imageX;
                    m_templateSelectionEndY = imageY;
                    break;
                default:
                    break;
            }

            // Emit signal with new template selection (ensure start < end)
            int finalStartX = std::min(m_templateSelectionStartX, m_templateSelectionEndX);
            int finalStartY = std::min(m_templateSelectionStartY, m_templateSelectionEndY);
            int finalEndX = std::max(m_templateSelectionStartX, m_templateSelectionEndX);
            int finalEndY = std::max(m_templateSelectionStartY, m_templateSelectionEndY);

            emit templateSelectionCornerDragged(finalStartX, finalStartY, finalEndX, finalEndY);
        } else if (m_draggingTarget == DragTarget::ROI) {
            // Update ROI corners
            switch (m_draggingCorner) {
                case DragCorner::TopLeft:
                    m_roiStartX = imageX;
                    m_roiStartY = imageY;
                    break;
                case DragCorner::TopRight:
                    m_roiEndX = imageX;
                    m_roiStartY = imageY;
                    break;
                case DragCorner::BottomLeft:
                    m_roiStartX = imageX;
                    m_roiEndY = imageY;
                    break;
                case DragCorner::BottomRight:
                    m_roiEndX = imageX;
                    m_roiEndY = imageY;
                    break;
                default:
                    break;
            }

            // Emit signal with new ROI (ensure start < end)
            int finalStartX = std::min(m_roiStartX, m_roiEndX);
            int finalStartY = std::min(m_roiStartY, m_roiEndY);
            int finalEndX = std::max(m_roiStartX, m_roiEndX);
            int finalEndY = std::max(m_roiStartY, m_roiEndY);

            emit roiCornerDragged(finalStartX, finalStartY, finalEndX, finalEndY);
        } else if (m_draggingTarget == DragTarget::SearchBox) {
            // Update Search Box corners
            switch (m_draggingCorner) {
                case DragCorner::TopLeft:
                    m_searchBoxStartX = imageX;
                    m_searchBoxStartY = imageY;
                    break;
                case DragCorner::TopRight:
                    m_searchBoxEndX = imageX;
                    m_searchBoxStartY = imageY;
                    break;
                case DragCorner::BottomLeft:
                    m_searchBoxStartX = imageX;
                    m_searchBoxEndY = imageY;
                    break;
                case DragCorner::BottomRight:
                    m_searchBoxEndX = imageX;
                    m_searchBoxEndY = imageY;
                    break;
                default:
                    break;
            }

            // Emit signal with new Search Box (ensure start < end)
            int finalStartX = std::min(m_searchBoxStartX, m_searchBoxEndX);
            int finalStartY = std::min(m_searchBoxStartY, m_searchBoxEndY);
            int finalEndX = std::max(m_searchBoxStartX, m_searchBoxEndX);
            int finalEndY = std::max(m_searchBoxStartY, m_searchBoxEndY);

            emit searchBoxCornerDragged(finalStartX, finalStartY, finalEndX, finalEndY);
        }

        event->accept();
    } else {
        updateCursor(event->pos());
        QLabel::mouseMoveEvent(event);
    }
}

void ImageDisplayLabel::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_isDragging) {
        m_isDragging = false;
        m_draggingCorner = DragCorner::None;
        m_draggingTarget = DragTarget::None;
        event->accept();
    } else {
        QLabel::mouseReleaseEvent(event);
    }
}

// ============ VisionImageWidget Implementation ============

VisionImageWidget::VisionImageWidget(QWidget* parent)
    : QWidget(parent)
    , m_imageLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_captureBtn(nullptr)
    , m_clearBtn(nullptr)
    , m_crosshairToggle(nullptr)
    , m_showCrosshair(false)
    , m_roiEnabled(false)
    , m_roiStartX(0)
    , m_roiStartY(0)
    , m_roiEndX(640)
    , m_roiEndY(480)
    , m_searchBoxEnabled(false)
    , m_searchBoxStartX(0)
    , m_searchBoxStartY(0)
    , m_searchBoxEndX(800)
    , m_searchBoxEndY(600)
    , m_templateSelectionEnabled(false)
    , m_templateSelectionStartX(100)
    , m_templateSelectionStartY(100)
    , m_templateSelectionEndX(300)
    , m_templateSelectionEndY(300)
{
    setupUI();
}

void VisionImageWidget::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    // Image display area
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setStyleSheet("QScrollArea { background-color: #1E1E1E; border: 2px solid #3E3E3E; }");

    m_imageLabel = new ImageDisplayLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("QLabel { background-color: #1E1E1E; color: #808080; }");
    m_imageLabel->setText("No image captured\n\nClick 'Capture' to grab image from camera");
    m_imageLabel->setMinimumSize(1, 1);

    // Connect corner drag signals
    connect(m_imageLabel, &ImageDisplayLabel::roiCornerDragged,
            this, &VisionImageWidget::onROICornerDragged);
    connect(m_imageLabel, &ImageDisplayLabel::searchBoxCornerDragged,
            this, &VisionImageWidget::onSearchBoxCornerDragged);
    connect(m_imageLabel, &ImageDisplayLabel::templateSelectionCornerDragged,
            this, &VisionImageWidget::onTemplateSelectionCornerDragged);

    m_scrollArea->setWidget(m_imageLabel);
    layout->addWidget(m_scrollArea, 1);

    // Control buttons row
    QHBoxLayout* controlLayout = new QHBoxLayout();
    controlLayout->setSpacing(10);

    m_captureBtn = new QPushButton("Capture", this);
    m_captureBtn->setToolTip("Capture image from camera");
    m_captureBtn->setMinimumWidth(100);
    m_captureBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; padding: 8px; }");

    m_clearBtn = new QPushButton("Clear", this);
    m_clearBtn->setToolTip("Clear current image");
    m_clearBtn->setMinimumWidth(100);

    m_crosshairToggle = new QCheckBox("Crosshair", this);
    m_crosshairToggle->setToolTip("Show crosshair overlay on image");

    controlLayout->addWidget(m_captureBtn);
    controlLayout->addWidget(m_clearBtn);
    controlLayout->addWidget(m_crosshairToggle);
    controlLayout->addStretch();

    layout->addLayout(controlLayout);

    // Connect signals
    connect(m_captureBtn, &QPushButton::clicked, this, &VisionImageWidget::captureClicked);
    connect(m_clearBtn, &QPushButton::clicked, this, &VisionImageWidget::clearClicked);
    connect(m_clearBtn, &QPushButton::clicked, this, &VisionImageWidget::clearImage);
    connect(m_crosshairToggle, &QCheckBox::toggled, this, &VisionImageWidget::enableCrosshair);
}

void VisionImageWidget::onROICornerDragged(int startX, int startY, int endX, int endY)
{
    // Update internal ROI
    m_roiStartX = startX;
    m_roiStartY = startY;
    m_roiEndX = endX;
    m_roiEndY = endY;

    // Update display
    updateDisplayImage();

    // Forward signal to parent (VisionManagerPage)
    emit roiDragged(startX, startY, endX, endY);
}

void VisionImageWidget::setImage(const QPixmap& pixmap)
{
    // Store original image
    m_originalImage = pixmap;

    // Update display with scaling and overlays
    updateDisplayImage();
}

void VisionImageWidget::updateDisplayImage()
{
    if (m_originalImage.isNull()) {
        return;
    }

    // Scale image to fit the scroll area
    QPixmap scaledImage = scaleToFit(m_originalImage);

    // Calculate scale factor for overlay coordinates
    double scaleX = static_cast<double>(scaledImage.width()) / m_originalImage.width();
    double scaleY = static_cast<double>(scaledImage.height()) / m_originalImage.height();

    // Update label's scale factors and pixmap size for mouse tracking
    m_imageLabel->setScaleFactors(scaleX, scaleY);
    m_imageLabel->setPixmapSize(scaledImage.width(), scaledImage.height());
    m_imageLabel->setROI(m_roiStartX, m_roiStartY, m_roiEndX, m_roiEndY);
    m_imageLabel->setSearchBox(m_searchBoxStartX, m_searchBoxStartY, m_searchBoxEndX, m_searchBoxEndY);
    m_imageLabel->setTemplateSelection(m_templateSelectionStartX, m_templateSelectionStartY,
                                      m_templateSelectionEndX, m_templateSelectionEndY);

    // Draw overlays
    if (m_showCrosshair || m_roiEnabled || m_searchBoxEnabled || m_templateSelectionEnabled ||
        !m_resultMarkers.empty() || !m_overlays.empty()) {
        QPixmap withOverlay = scaledImage;
        QPainter painter(&withOverlay);
        painter.setRenderHint(QPainter::Antialiasing);

        // Draw Search Box rectangle if enabled (draw first so ROI appears on top)
        if (m_searchBoxEnabled) {
            int scaledStartX = static_cast<int>(m_searchBoxStartX * scaleX);
            int scaledStartY = static_cast<int>(m_searchBoxStartY * scaleY);
            int scaledEndX = static_cast<int>(m_searchBoxEndX * scaleX);
            int scaledEndY = static_cast<int>(m_searchBoxEndY * scaleY);

            // Search Box rectangle - use orange/magenta color to distinguish from ROI
            QPen searchBoxPen(QColor(255, 165, 0), 2, Qt::DashDotLine);  // Orange dashed
            painter.setPen(searchBoxPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(scaledStartX, scaledStartY,
                           scaledEndX - scaledStartX, scaledEndY - scaledStartY);

            // Corner markers (larger for easier grabbing)
            int markerSize = 12;
            painter.setBrush(QColor(255, 165, 0));  // Orange
            painter.drawRect(scaledStartX - markerSize/2, scaledStartY - markerSize/2, markerSize, markerSize);
            painter.drawRect(scaledEndX - markerSize/2, scaledStartY - markerSize/2, markerSize, markerSize);
            painter.drawRect(scaledStartX - markerSize/2, scaledEndY - markerSize/2, markerSize, markerSize);
            painter.drawRect(scaledEndX - markerSize/2, scaledEndY - markerSize/2, markerSize, markerSize);

            // Draw 3x3 grid lines inside search box
            QPen gridPen(QColor(255, 165, 0, 100), 1, Qt::DotLine);  // Semi-transparent orange
            painter.setPen(gridPen);

            int searchWidth = scaledEndX - scaledStartX;
            int searchHeight = scaledEndY - scaledStartY;

            // Vertical grid lines (divide into 3 columns)
            for (int i = 1; i < 3; ++i) {
                int x = scaledStartX + (searchWidth * i) / 3;
                painter.drawLine(x, scaledStartY, x, scaledEndY);
            }

            // Horizontal grid lines (divide into 3 rows)
            for (int i = 1; i < 3; ++i) {
                int y = scaledStartY + (searchHeight * i) / 3;
                painter.drawLine(scaledStartX, y, scaledEndX, y);
            }

            // Search Box label
            painter.setPen(QColor(255, 165, 0));
            painter.setFont(QFont("Arial", 10, QFont::Bold));
            QString searchText = QString("Search: %1x%2").arg(m_searchBoxEndX - m_searchBoxStartX).arg(m_searchBoxEndY - m_searchBoxStartY);
            painter.drawText(scaledStartX + 5, scaledStartY - 20, searchText);
        }

        // Draw Template Selection rectangle if enabled (RED for teaching template)
        if (m_templateSelectionEnabled) {
            int scaledStartX = static_cast<int>(m_templateSelectionStartX * scaleX);
            int scaledStartY = static_cast<int>(m_templateSelectionStartY * scaleY);
            int scaledEndX = static_cast<int>(m_templateSelectionEndX * scaleX);
            int scaledEndY = static_cast<int>(m_templateSelectionEndY * scaleY);

            // Template selection rectangle - RED for visibility
            QPen templatePen(QColor(255, 0, 0), 3, Qt::SolidLine);  // Thick red solid line
            painter.setPen(templatePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(scaledStartX, scaledStartY,
                           scaledEndX - scaledStartX, scaledEndY - scaledStartY);

            // Corner markers (larger for easier grabbing)
            int markerSize = 14;  // Slightly larger than ROI/SearchBox
            painter.setBrush(QColor(255, 0, 0));  // Red
            painter.drawRect(scaledStartX - markerSize/2, scaledStartY - markerSize/2, markerSize, markerSize);
            painter.drawRect(scaledEndX - markerSize/2, scaledStartY - markerSize/2, markerSize, markerSize);
            painter.drawRect(scaledStartX - markerSize/2, scaledEndY - markerSize/2, markerSize, markerSize);
            painter.drawRect(scaledEndX - markerSize/2, scaledEndY - markerSize/2, markerSize, markerSize);

            // Template selection label with outline for visibility
            painter.setFont(QFont("Arial", 12, QFont::Bold));  // Slightly larger font
            QString templateText = QString("Template: %1x%2 px")
                .arg(m_templateSelectionEndX - m_templateSelectionStartX)
                .arg(m_templateSelectionEndY - m_templateSelectionStartY);
            int textX = scaledStartX + 5;
            int textY = scaledStartY - 5;

            // Draw black outline for visibility
            painter.setPen(Qt::black);
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    if (dx != 0 || dy != 0) {
                        painter.drawText(textX + dx, textY + dy, templateText);
                    }
                }
            }

            // Draw red text on top
            painter.setPen(QColor(255, 0, 0));
            painter.drawText(textX, textY, templateText);
        }

        // Draw ROI rectangle if enabled
        if (m_roiEnabled) {
            int scaledStartX = static_cast<int>(m_roiStartX * scaleX);
            int scaledStartY = static_cast<int>(m_roiStartY * scaleY);
            int scaledEndX = static_cast<int>(m_roiEndX * scaleX);
            int scaledEndY = static_cast<int>(m_roiEndY * scaleY);

            // ROI rectangle
            QPen roiPen(Qt::cyan, 2, Qt::SolidLine);
            painter.setPen(roiPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(scaledStartX, scaledStartY,
                           scaledEndX - scaledStartX, scaledEndY - scaledStartY);

            // Corner markers (larger for easier grabbing)
            int markerSize = 12;
            painter.setBrush(Qt::cyan);
            painter.drawRect(scaledStartX - markerSize/2, scaledStartY - markerSize/2, markerSize, markerSize);
            painter.drawRect(scaledEndX - markerSize/2, scaledStartY - markerSize/2, markerSize, markerSize);
            painter.drawRect(scaledStartX - markerSize/2, scaledEndY - markerSize/2, markerSize, markerSize);
            painter.drawRect(scaledEndX - markerSize/2, scaledEndY - markerSize/2, markerSize, markerSize);

            // ROI crosshair (inside the ROI rectangle)
            int roiCenterX = (scaledStartX + scaledEndX) / 2;
            int roiCenterY = (scaledStartY + scaledEndY) / 2;

            QPen roiCrosshairPen(Qt::yellow, 1, Qt::DashLine);
            painter.setPen(roiCrosshairPen);

            // Horizontal line (within ROI bounds)
            painter.drawLine(scaledStartX, roiCenterY, scaledEndX, roiCenterY);
            // Vertical line (within ROI bounds)
            painter.drawLine(roiCenterX, scaledStartY, roiCenterX, scaledEndY);

            // Center marker
            painter.setPen(QPen(Qt::yellow, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QPoint(roiCenterX, roiCenterY), 5, 5);

            // ROI label with outline for visibility on both black and white backgrounds
            painter.setFont(QFont("Arial", 10, QFont::Bold));
            QString roiText = QString("ROI: %1x%2").arg(m_roiEndX - m_roiStartX).arg(m_roiEndY - m_roiStartY);
            int textX = scaledStartX + 5;
            int textY = scaledStartY - 5;

            // Draw black outline (draw text in black at 8 offset positions)
            painter.setPen(Qt::black);
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    if (dx != 0 || dy != 0) {
                        painter.drawText(textX + dx, textY + dy, roiText);
                    }
                }
            }

            // Draw main text in cyan (highly visible on both black and white)
            painter.setPen(QColor(0, 255, 255));  // Cyan
            painter.drawText(textX, textY, roiText);
        }

        // Draw overlay commands from VisionResult
        if (!m_overlays.empty()) {
            for (const auto& overlay : m_overlays) {
                // Convert OpenCV color (BGR) to QColor (RGB)
                QColor qcolor(
                    static_cast<int>(overlay.color[2]),  // R from B
                    static_cast<int>(overlay.color[1]),  // G
                    static_cast<int>(overlay.color[0])   // B from R
                );
                QPen overlayPen(qcolor, overlay.thickness);
                painter.setPen(overlayPen);
                painter.setBrush(Qt::NoBrush);

                switch (overlay.type) {
                    case OverlayCommand::DrawContour:
                        if (!overlay.contour.empty()) {
                            // Scale contour points and draw
                            QPolygon polygon;
                            for (const auto& pt : overlay.contour) {
                                int sx = static_cast<int>(pt.x * scaleX);
                                int sy = static_cast<int>(pt.y * scaleY);
                                polygon.append(QPoint(sx, sy));
                            }
                            painter.drawPolyline(polygon);
                        }
                        break;

                    case OverlayCommand::DrawLine:
                        painter.drawLine(
                            static_cast<int>(overlay.p1.x * scaleX),
                            static_cast<int>(overlay.p1.y * scaleY),
                            static_cast<int>(overlay.p2.x * scaleX),
                            static_cast<int>(overlay.p2.y * scaleY)
                        );
                        break;

                    case OverlayCommand::DrawCircle:
                        painter.drawEllipse(
                            QPointF(overlay.p1.x * scaleX, overlay.p1.y * scaleY),
                            overlay.radius * scaleX,
                            overlay.radius * scaleY
                        );
                        break;

                    case OverlayCommand::DrawEllipse:
                        {
                            // Draw rotated ellipse (for beam FWHM visualization)
                            painter.save();
                            // Translate to ellipse center, rotate, then draw
                            double cx = overlay.p1.x * scaleX;
                            double cy = overlay.p1.y * scaleY;
                            painter.translate(cx, cy);
                            painter.rotate(overlay.angle);  // angle in degrees
                            painter.drawEllipse(
                                QPointF(0, 0),
                                overlay.radius * scaleX,   // major semi-axis
                                overlay.radius2 * scaleY   // minor semi-axis
                            );
                            painter.restore();
                        }
                        break;

                    case OverlayCommand::DrawRectangle:
                        painter.drawRect(
                            static_cast<int>(overlay.rect.x * scaleX),
                            static_cast<int>(overlay.rect.y * scaleY),
                            static_cast<int>(overlay.rect.width * scaleX),
                            static_cast<int>(overlay.rect.height * scaleY)
                        );
                        break;

                    case OverlayCommand::DrawFilledRect:
                        {
                            // Draw semi-transparent filled rectangle
                            QColor fillColor(
                                static_cast<int>(overlay.color[2]),  // R (BGR to RGB)
                                static_cast<int>(overlay.color[1]),  // G
                                static_cast<int>(overlay.color[0])   // B
                            );
                            fillColor.setAlphaF(overlay.opacity);

                            painter.save();
                            painter.setBrush(QBrush(fillColor));
                            painter.setPen(Qt::NoPen);  // No outline for filled rect
                            painter.drawRect(
                                static_cast<int>(overlay.p1.x * scaleX),
                                static_cast<int>(overlay.p1.y * scaleY),
                                static_cast<int>((overlay.p2.x - overlay.p1.x) * scaleX),
                                static_cast<int>((overlay.p2.y - overlay.p1.y) * scaleY)
                            );
                            painter.restore();
                        }
                        break;

                    case OverlayCommand::DrawFilledPolygon:
                        {
                            // Draw semi-transparent filled polygon (rotated/tilted shape)
                            if (!overlay.contour.empty()) {
                                QColor fillColor(
                                    static_cast<int>(overlay.color[2]),  // R (BGR to RGB)
                                    static_cast<int>(overlay.color[1]),  // G
                                    static_cast<int>(overlay.color[0])   // B
                                );
                                fillColor.setAlphaF(overlay.opacity);

                                QPolygon polygon;
                                for (const auto& pt : overlay.contour) {
                                    int sx = static_cast<int>(pt.x * scaleX);
                                    int sy = static_cast<int>(pt.y * scaleY);
                                    polygon.append(QPoint(sx, sy));
                                }

                                painter.save();
                                painter.setBrush(QBrush(fillColor));
                                painter.setPen(Qt::NoPen);  // No outline
                                painter.drawPolygon(polygon);
                                painter.restore();
                            }
                        }
                        break;

                    case OverlayCommand::DrawCrosshair:
                        {
                            int cx = static_cast<int>(overlay.p1.x * scaleX);
                            int cy = static_cast<int>(overlay.p1.y * scaleY);
                            int size = static_cast<int>(overlay.radius);
                            painter.drawLine(cx - size, cy, cx + size, cy);
                            painter.drawLine(cx, cy - size, cx, cy + size);
                        }
                        break;

                    case OverlayCommand::DrawText:
                        painter.setFont(QFont("Arial", overlay.fontScale * 8));
                        painter.drawText(
                            static_cast<int>(overlay.p1.x * scaleX),
                            static_cast<int>(overlay.p1.y * scaleY),
                            QString::fromStdString(overlay.text)
                        );
                        break;

                    case OverlayCommand::DrawPoint:
                        painter.drawEllipse(
                            QPointF(overlay.p1.x * scaleX, overlay.p1.y * scaleY),
                            2.0, 2.0
                        );
                        break;
                }
            }
        }

        // Draw result markers (red mini crosshairs with labels)
        if (!m_resultMarkers.empty()) {
            for (const auto& marker : m_resultMarkers) {
                // Scale marker position to display coordinates
                int scaledX = static_cast<int>(marker.x * scaleX);
                int scaledY = static_cast<int>(marker.y * scaleY);
                int scaledRadius = static_cast<int>(marker.radius * scaleX);

                // Draw red crosshair at detected position
                QPen markerPen(Qt::red, 2, Qt::SolidLine);
                painter.setPen(markerPen);

                int crosshairSize = 15;  // Size of mini crosshair arms
                // Horizontal line
                painter.drawLine(scaledX - crosshairSize, scaledY, scaledX + crosshairSize, scaledY);
                // Vertical line
                painter.drawLine(scaledX, scaledY - crosshairSize, scaledX, scaledY + crosshairSize);

                // Draw circle outline if radius > 0
                if (scaledRadius > 0) {
                    painter.setBrush(Qt::NoBrush);
                    painter.drawEllipse(QPoint(scaledX, scaledY), scaledRadius, scaledRadius);
                }

                // Draw numbered label next to the marker
                painter.setPen(Qt::red);
                painter.setFont(QFont("Arial", 12, QFont::Bold));
                QString labelText = QString::number(marker.label);
                painter.drawText(scaledX + crosshairSize + 5, scaledY - 5, labelText);
            }
        }

        // Draw crosshair if enabled (full image crosshair)
        if (m_showCrosshair) {
            painter.setPen(QPen(Qt::green, 1, Qt::DashLine));

            int cx = withOverlay.width() / 2;
            int cy = withOverlay.height() / 2;

            // Horizontal line
            painter.drawLine(0, cy, withOverlay.width(), cy);
            // Vertical line
            painter.drawLine(cx, 0, cx, withOverlay.height());
        }

        m_imageLabel->setPixmap(withOverlay);
    } else {
        m_imageLabel->setPixmap(scaledImage);
    }

    // Don't call adjustSize() - let QScrollArea with widgetResizable(true)
    // handle the label sizing so the image stays centered at any window size
}

QPixmap VisionImageWidget::scaleToFit(const QPixmap& pixmap)
{
    if (pixmap.isNull()) {
        return pixmap;
    }

    // Get available size in scroll area
    QSize availableSize = m_scrollArea->viewport()->size();

    // Only shrink if image is larger than available space
    if (pixmap.width() <= availableSize.width() &&
        pixmap.height() <= availableSize.height()) {
        return pixmap;
    }

    // Scale to fit while maintaining aspect ratio
    return pixmap.scaled(availableSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void VisionImageWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Re-scale image when widget is resized
    if (!m_originalImage.isNull()) {
        updateDisplayImage();
    }
}

void VisionImageWidget::clearImage()
{
    m_originalImage = QPixmap();
    m_imageLabel->clear();
    m_imageLabel->setText("No image captured\n\nClick 'Capture' to grab image from camera");
}

void VisionImageWidget::enableCrosshair(bool enable)
{
    m_showCrosshair = enable;
    updateDisplayImage();
}

void VisionImageWidget::setROI(int startX, int startY, int endX, int endY)
{
    m_roiStartX = startX;
    m_roiStartY = startY;
    m_roiEndX = endX;
    m_roiEndY = endY;

    // Update label's ROI for mouse tracking
    m_imageLabel->setROI(startX, startY, endX, endY);

    // Update display if ROI is enabled
    if (m_roiEnabled && !m_originalImage.isNull()) {
        updateDisplayImage();
    }
}

void VisionImageWidget::setROIEnabled(bool enabled)
{
    m_roiEnabled = enabled;
    m_imageLabel->setROIEnabled(enabled);
    updateDisplayImage();
}

void VisionImageWidget::setResultMarkers(const std::vector<ResultMarker>& markers)
{
    m_resultMarkers = markers;
    updateDisplayImage();
}

void VisionImageWidget::clearResultMarkers()
{
    m_resultMarkers.clear();
    updateDisplayImage();
}

void VisionImageWidget::setOverlays(const std::vector<OverlayCommand>& overlays)
{
    m_overlays = overlays;
    updateDisplayImage();
}

void VisionImageWidget::clearOverlays()
{
    m_overlays.clear();
    updateDisplayImage();
}

void VisionImageWidget::onSearchBoxCornerDragged(int startX, int startY, int endX, int endY)
{
    // Update internal Search Box
    m_searchBoxStartX = startX;
    m_searchBoxStartY = startY;
    m_searchBoxEndX = endX;
    m_searchBoxEndY = endY;

    // Update display
    updateDisplayImage();

    // Forward signal to parent (VisionManagerPage)
    emit searchBoxDragged(startX, startY, endX, endY);
}

void VisionImageWidget::setSearchBox(int startX, int startY, int endX, int endY)
{
    m_searchBoxStartX = startX;
    m_searchBoxStartY = startY;
    m_searchBoxEndX = endX;
    m_searchBoxEndY = endY;

    // Update label's Search Box for mouse tracking
    m_imageLabel->setSearchBox(startX, startY, endX, endY);

    // Update display if Search Box is enabled
    if (m_searchBoxEnabled && !m_originalImage.isNull()) {
        updateDisplayImage();
    }
}

void VisionImageWidget::setSearchBoxEnabled(bool enabled)
{
    m_searchBoxEnabled = enabled;
    m_imageLabel->setSearchBoxEnabled(enabled);
    updateDisplayImage();
}

void VisionImageWidget::onTemplateSelectionCornerDragged(int startX, int startY, int endX, int endY)
{
    m_templateSelectionStartX = startX;
    m_templateSelectionStartY = startY;
    m_templateSelectionEndX = endX;
    m_templateSelectionEndY = endY;

    // Update display
    updateDisplayImage();

    // Forward signal to parent
    emit templateSelectionDragged(startX, startY, endX, endY);
}

void VisionImageWidget::setTemplateSelection(int startX, int startY, int endX, int endY)
{
    m_templateSelectionStartX = startX;
    m_templateSelectionStartY = startY;
    m_templateSelectionEndX = endX;
    m_templateSelectionEndY = endY;

    // Update label's template selection for mouse tracking
    m_imageLabel->setTemplateSelection(startX, startY, endX, endY);

    // Update display if template selection is enabled
    if (m_templateSelectionEnabled && !m_originalImage.isNull()) {
        updateDisplayImage();
    }
}

void VisionImageWidget::setTemplateSelectionEnabled(bool enabled)
{
    m_templateSelectionEnabled = enabled;
    m_imageLabel->setTemplateSelectionEnabled(enabled);
    updateDisplayImage();
}

void VisionImageWidget::getTemplateSelection(int& startX, int& startY, int& endX, int& endY) const
{
    startX = m_templateSelectionStartX;
    startY = m_templateSelectionStartY;
    endX = m_templateSelectionEndX;
    endY = m_templateSelectionEndY;
}
