// SummaryDiagramWidget.cpp - Visual summary diagram for placement correction
#include "SummaryDiagramWidget.h"
#include "ApplicationContext.h"
#include "product_position_manager.h"
#include <QVBoxLayout>
#include <QShowEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QGraphicsTextItem>
#include <QPainter>
#include <QPixmap>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// DiagramGraphicsView
// ============================================================================

DiagramGraphicsView::DiagramGraphicsView(QWidget* parent)
    : QGraphicsView(parent)
{
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setViewportUpdateMode(FullViewportUpdate);
    setStyleSheet("QGraphicsView { border: none; background-color: #0a0f1e; }");
}

void DiagramGraphicsView::wheelEvent(QWheelEvent* event)
{
    constexpr double ZOOM_FACTOR = 1.15;
    constexpr double MIN_ZOOM = 0.1;
    constexpr double MAX_ZOOM = 10.0;

    if (event->angleDelta().y() > 0) {
        if (m_currentZoom * ZOOM_FACTOR <= MAX_ZOOM) {
            scale(ZOOM_FACTOR, ZOOM_FACTOR);
            m_currentZoom *= ZOOM_FACTOR;
        }
    } else {
        if (m_currentZoom / ZOOM_FACTOR >= MIN_ZOOM) {
            scale(1.0 / ZOOM_FACTOR, 1.0 / ZOOM_FACTOR);
            m_currentZoom /= ZOOM_FACTOR;
        }
    }
    event->accept();
}

void DiagramGraphicsView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        m_isPanning = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        QGraphicsView::mousePressEvent(event);
    }
}

void DiagramGraphicsView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
    } else {
        QGraphicsView::mouseMoveEvent(event);
    }
}

void DiagramGraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton && m_isPanning) {
        m_isPanning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
    } else {
        QGraphicsView::mouseReleaseEvent(event);
    }
}

// ============================================================================
// DraggableThumbnailItem
// ============================================================================

DraggableThumbnailItem::DraggableThumbnailItem(const QPixmap& pixmap, int thumbIndex,
                                               QGraphicsLineItem* leaderLine,
                                               const QPointF& anchorScenePos,
                                               QGraphicsItem* parent)
    : QGraphicsPixmapItem(pixmap, parent)
    , m_thumbIndex(thumbIndex)
    , m_leaderLine(leaderLine)
    , m_anchorScenePos(anchorScenePos)
{
    setFlags(ItemIsMovable | ItemSendsGeometryChanges);
    setCursor(Qt::OpenHandCursor);
    setZValue(10);
}

void DraggableThumbnailItem::setMoveCallback(std::function<void(int, double, double)> cb)
{
    m_moveCallback = cb;
}

QVariant DraggableThumbnailItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionHasChanged && m_leaderLine) {
        QPointF newPos = value.toPointF();
        QPointF thumbCenter = newPos + QPointF(pixmap().width() / 2.0, pixmap().height() / 2.0);
        m_leaderLine->setLine(QLineF(m_anchorScenePos, thumbCenter));

        double dx = newPos.x() - m_anchorScenePos.x();
        double dy = newPos.y() - m_anchorScenePos.y();
        if (m_moveCallback) {
            m_moveCallback(m_thumbIndex, dx, dy);
        }
    }
    return QGraphicsPixmapItem::itemChange(change, value);
}

// ============================================================================
// SummaryDiagramWidget
// ============================================================================

SummaryDiagramWidget::SummaryDiagramWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_scene(new QGraphicsScene(this))
{
    setupUI();
}

void SummaryDiagramWidget::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_view = new DiagramGraphicsView(this);
    m_view->setScene(m_scene);
    layout->addWidget(m_view);
}

void SummaryDiagramWidget::SetData(const PlacementCorrectionSet& set)
{
    m_set = set;
    rebuildScene();
}

void SummaryDiagramWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_scene->items().isEmpty()) {
        QRectF bounds = m_scene->itemsBoundingRect().adjusted(-30, -30, 30, 30);
        m_view->fitInView(bounds, Qt::KeepAspectRatio);
    }
}

void SummaryDiagramWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!m_scene->items().isEmpty()) {
        QRectF bounds = m_scene->itemsBoundingRect().adjusted(-30, -30, 30, 30);
        m_view->fitInView(bounds, Qt::KeepAspectRatio);
    }
}

void SummaryDiagramWidget::computeTransform()
{
    double xs[4] = { m_set.referenceLine.p1x, m_set.referenceLine.p2x,
                     m_set.targetLine.p1x,    m_set.targetLine.p2x };
    double ys[4] = { m_set.referenceLine.p1y, m_set.referenceLine.p2y,
                     m_set.targetLine.p1y,    m_set.targetLine.p2y };

    double minX = *std::min_element(xs, xs + 4);
    double maxX = *std::max_element(xs, xs + 4);
    double minY = *std::min_element(ys, ys + 4);
    double maxY = *std::max_element(ys, ys + 4);

    double rangeX = maxX - minX;
    double rangeY = maxY - minY;
    if (rangeX < 0.5) rangeX = 0.5;
    if (rangeY < 0.5) rangeY = 0.5;

    // Add 40% padding for thumbnails
    double padX = rangeX * 0.4;
    double padY = rangeY * 0.4;

    m_originX = minX - padX;
    m_originY = minY - padY;

    double totalX = rangeX + 2.0 * padX;
    double totalY = rangeY + 2.0 * padY;

    // Uniform scale
    double scaleX = m_sceneW / totalX;
    double scaleY = m_sceneH / totalY;
    m_scale = std::min(scaleX, scaleY);
}

QPointF SummaryDiagramWidget::gantryToScene(double gx, double gy) const
{
    double sx = (gx - m_originX) * m_scale;
    double sy = (gy - m_originY) * m_scale;  // Y down matches physical hardware
    return QPointF(sx, sy);
}

void SummaryDiagramWidget::rebuildScene()
{
    m_scene->clear();

    if (!m_set.referenceLine.valid || !m_set.targetLine.valid) {
        auto* text = m_scene->addText("Teach both lines to see the summary diagram.",
            QFont("sans-serif", 14));
        text->setDefaultTextColor(QColor("#888888"));
        text->setPos(50, 50);
        m_scene->setSceneRect(0, 0, m_sceneW, m_sceneH);
        m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
        return;
    }

    computeTransform();
    m_scene->setSceneRect(0, 0, m_sceneW, m_sceneH);

    // Map all points
    QPointF refP1 = gantryToScene(m_set.referenceLine.p1x, m_set.referenceLine.p1y);
    QPointF refP2 = gantryToScene(m_set.referenceLine.p2x, m_set.referenceLine.p2y);
    QPointF tgtP1 = gantryToScene(m_set.targetLine.p1x, m_set.targetLine.p1y);
    QPointF tgtP2 = gantryToScene(m_set.targetLine.p2x, m_set.targetLine.p2y);

    QPointF refMid = (refP1 + refP2) / 2.0;
    QPointF tgtMid = (tgtP1 + tgtP2) / 2.0;

    QColor refColor("#00ff88");
    QColor tgtColor("#ff8844");
    QColor arrowColor("#f0c040");
    QColor leaderColor("#888888");

    // --- Reference line ---
    auto* refLine = m_scene->addLine(QLineF(refP1, refP2), QPen(refColor, 2.5));
    refLine->setZValue(2);

    // --- Target line ---
    auto* tgtLine = m_scene->addLine(QLineF(tgtP1, tgtP2), QPen(tgtColor, 2.5));
    tgtLine->setZValue(2);

    // --- Point markers ---
    QPointF pts[4] = { refP1, refP2, tgtP1, tgtP2 };
    QColor ptColors[4] = { refColor, refColor, tgtColor, tgtColor };
    QString ptLabels[4] = { "Ref P1", "Ref P2", "Tgt P1", "Tgt P2" };

    for (int i = 0; i < 4; ++i) {
        auto* dot = m_scene->addEllipse(pts[i].x() - 6, pts[i].y() - 6, 12, 12,
                                         QPen(ptColors[i], 1.5), QBrush(ptColors[i]));
        dot->setZValue(3);

        auto* label = m_scene->addText(ptLabels[i], QFont("sans-serif", 9, QFont::Bold));
        label->setDefaultTextColor(QColor("#cccccc"));
        label->setPos(pts[i].x() + 10, pts[i].y() - 20);
        label->setZValue(7);
    }

    // --- Midpoint markers ---
    for (const auto& mid : { refMid, tgtMid }) {
        auto* circle = m_scene->addEllipse(mid.x() - 8, mid.y() - 8, 16, 16,
                                            QPen(Qt::white, 1.5), QBrush(Qt::transparent));
        circle->setZValue(4);
        // Cross inside
        m_scene->addLine(mid.x() - 4, mid.y(), mid.x() + 4, mid.y(),
                         QPen(Qt::white, 1))->setZValue(4);
        m_scene->addLine(mid.x(), mid.y() - 4, mid.x(), mid.y() + 4,
                         QPen(Qt::white, 1))->setZValue(4);
    }

    // --- Master vector arrow ---
    drawArrow(refMid, tgtMid, arrowColor);

    // --- Angle annotations ---
    {
        QString deg = QString(QChar(0x00B0));
        auto* refAngle = m_scene->addText(
            QString("Ref: %1%2").arg(m_set.refAngleDeg, 0, 'f', 2).arg(deg),
            QFont("sans-serif", 9));
        refAngle->setDefaultTextColor(refColor);
        refAngle->setPos(refMid.x() + 12, refMid.y() + 4);
        refAngle->setZValue(6);

        auto* tgtAngle = m_scene->addText(
            QString("Tgt: %1%2").arg(m_set.targetAngleDeg, 0, 'f', 2).arg(deg),
            QFont("sans-serif", 9));
        tgtAngle->setDefaultTextColor(tgtColor);
        tgtAngle->setPos(tgtMid.x() + 12, tgtMid.y() + 4);
        tgtAngle->setZValue(6);

        auto* deltaLabel = m_scene->addText(
            QString("Delta: %1%2").arg(m_set.deltaAngleDeg, 0, 'f', 3).arg(deg),
            QFont("sans-serif", 10, QFont::Bold));
        deltaLabel->setDefaultTextColor(arrowColor);
        QPointF arrowMid = (refMid + tgtMid) / 2.0;
        deltaLabel->setPos(arrowMid.x() + 10, arrowMid.y() - 20);
        deltaLabel->setZValue(6);
    }

    // --- Info text ---
    {
        QString info = QString("Device: %1  |  Master: %2  |  Target: %3")
            .arg(QString::fromStdString(m_set.config.targetDevice))
            .arg(QString::fromStdString(m_set.config.masterPositionName))
            .arg(QString::fromStdString(m_set.config.targetPositionName));
        auto* infoText = m_scene->addText(info, QFont("sans-serif", 9));
        infoText->setDefaultTextColor(QColor("#666666"));
        infoText->setPos(5, m_sceneH - 25);
        infoText->setZValue(7);
    }

    // --- Background thumbnails centered on each point ---
    std::string imageFiles[4] = {
        m_set.referenceLine.p1ImageFile, m_set.referenceLine.p2ImageFile,
        m_set.targetLine.p1ImageFile,    m_set.targetLine.p2ImageFile
    };

    for (int i = 0; i < 4; ++i) {
        QPixmap thumb = loadThumbnail(imageFiles[i]);
        auto* thumbItem = m_scene->addPixmap(thumb);
        thumbItem->setPos(pts[i].x() - thumb.width() / 2.0,
                          pts[i].y() - thumb.height() / 2.0);
        thumbItem->setZValue(0);  // behind everything
        thumbItem->setOpacity(0.7);
    }

    // Fit to all items including thumbnails at their offset positions
    QRectF bounds = m_scene->itemsBoundingRect().adjusted(-30, -30, 30, 30);
    m_scene->setSceneRect(bounds);
    m_view->fitInView(bounds, Qt::KeepAspectRatio);
}

void SummaryDiagramWidget::drawArrow(const QPointF& from, const QPointF& to, const QColor& color)
{
    auto* shaft = m_scene->addLine(QLineF(from, to), QPen(color, 2.5));
    shaft->setZValue(5);

    double angle = std::atan2(to.y() - from.y(), to.x() - from.x());
    double arrowSize = 12.0;

    QPointF p1 = to - QPointF(arrowSize * std::cos(angle - M_PI / 6),
                               arrowSize * std::sin(angle - M_PI / 6));
    QPointF p2 = to - QPointF(arrowSize * std::cos(angle + M_PI / 6),
                               arrowSize * std::sin(angle + M_PI / 6));

    QPolygonF arrowHead;
    arrowHead << to << p1 << p2;
    auto* headItem = m_scene->addPolygon(arrowHead, QPen(color, 1), QBrush(color));
    headItem->setZValue(5);
}

QPixmap SummaryDiagramWidget::loadThumbnail(const std::string& imageFile)
{
    constexpr int THUMB_W = 120;
    constexpr int THUMB_H = 90;

    if (imageFile.empty()) {
        // Gray placeholder
        QPixmap placeholder(THUMB_W, THUMB_H);
        placeholder.fill(QColor("#1a2640"));
        QPainter p(&placeholder);
        p.setPen(QColor("#555555"));
        p.setFont(QFont("sans-serif", 8));
        p.drawText(placeholder.rect(), Qt::AlignCenter, "No Image");
        p.end();
        return placeholder;
    }

    std::string fullPath = getImageDirectory() + "/" + imageFile;
    QPixmap pixmap(QString::fromStdString(fullPath));
    if (pixmap.isNull()) {
        QPixmap placeholder(THUMB_W, THUMB_H);
        placeholder.fill(QColor("#1a2640"));
        QPainter p(&placeholder);
        p.setPen(QColor("#555555"));
        p.setFont(QFont("sans-serif", 8));
        p.drawText(placeholder.rect(), Qt::AlignCenter, "Missing");
        p.end();
        return placeholder;
    }

    // Crop center 25%
    int cropW = pixmap.width() / 2;
    int cropH = pixmap.height() / 2;
    int cropX = (pixmap.width() - cropW) / 2;
    int cropY = (pixmap.height() - cropH) / 2;
    QPixmap cropped = pixmap.copy(cropX, cropY, cropW, cropH);

    QPixmap scaled = cropped.scaled(THUMB_W, THUMB_H, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Draw crosshair (cyan)
    QPainter painter(&scaled);
    painter.setPen(QPen(QColor(0, 255, 255), 1));
    int cx = scaled.width() / 2;
    int cy = scaled.height() / 2;
    painter.drawLine(cx, 0, cx, scaled.height());
    painter.drawLine(0, cy, scaled.width(), cy);
    // Border
    painter.setPen(QPen(QColor("#3d5a8c"), 1));
    painter.drawRect(0, 0, scaled.width() - 1, scaled.height() - 1);
    painter.end();

    return scaled;
}

std::string SummaryDiagramWidget::getImageDirectory() const
{
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        std::filesystem::path productPath(
            m_context->productPositionManager->GetCurrentProductPath());
        return (productPath.parent_path() / "images" / "trainer").string();
    }
    return "config/images/trainer";
}
