// SummaryDiagramWidget.h - Visual summary diagram for placement correction
#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsLineItem>
#include <functional>
#include "PlacementCorrectionData.h"

struct ApplicationContext;

// ============================================================================
// DiagramGraphicsView — pan (middle mouse) + zoom (wheel)
// ============================================================================
class DiagramGraphicsView : public QGraphicsView
{
public:
    explicit DiagramGraphicsView(QWidget* parent = nullptr);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool m_isPanning = false;
    QPoint m_lastPanPoint;
    double m_currentZoom = 1.0;
};

// ============================================================================
// DraggableThumbnailItem — draggable pixmap with leader line to anchor point
// ============================================================================
class DraggableThumbnailItem : public QGraphicsPixmapItem
{
public:
    DraggableThumbnailItem(const QPixmap& pixmap, int thumbIndex,
                           QGraphicsLineItem* leaderLine,
                           const QPointF& anchorScenePos,
                           QGraphicsItem* parent = nullptr);

    void setMoveCallback(std::function<void(int, double, double)> cb);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    int m_thumbIndex;
    QGraphicsLineItem* m_leaderLine;
    QPointF m_anchorScenePos;
    std::function<void(int, double, double)> m_moveCallback;
};

// ============================================================================
// SummaryDiagramWidget — main widget
// ============================================================================
class SummaryDiagramWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SummaryDiagramWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(const PlacementCorrectionSet& set);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

signals:
    void thumbnailOffsetChanged(int index, double dx, double dy);

private:
    void setupUI();
    void rebuildScene();
    void computeTransform();
    QPointF gantryToScene(double gx, double gy) const;

    void drawArrow(const QPointF& from, const QPointF& to, const QColor& color);
    QPixmap loadThumbnail(const std::string& imageFile);
    std::string getImageDirectory() const;

    ApplicationContext* m_context;
    PlacementCorrectionSet m_set;

    DiagramGraphicsView* m_view = nullptr;
    QGraphicsScene* m_scene = nullptr;

    // Coordinate transform
    double m_scale = 1.0;
    double m_originX = 0.0;
    double m_originY = 0.0;
    double m_sceneW = 800.0;
    double m_sceneH = 600.0;
};
