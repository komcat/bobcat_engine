// EdgeSummaryDiagramWidget.h - Graphical summary for Edge Transfer
#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPushButton>
#include <future>
#include <string>
#include "EdgeTransferData.h"

struct ApplicationContext;

/**
 * @brief Visual summary: both edges drawn in gantry XY, midpoints, arrow
 * from reference midpoint to target midpoint, per-edge angle annotations,
 * captured-image thumbnails behind each point. Supports wheel-zoom and
 * middle-mouse-pan.
 */
class EdgeSummaryDiagramWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EdgeSummaryDiagramWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(const EdgeTransferSet& set);

    // Called by the page to gate nav while the Result widget is running Apply.
    void SetNavBlockedExternal(bool blocked);

signals:
    // Fires when a nav move starts (true) / ends (false). The page connects
    // this to EdgeResultWidget::SetApplyButtonsLocked so that corrections
    // can't be applied while the gantry is moving.
    void navBusyChanged(bool busy);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onNavRefP1();
    void onNavRefP2();
    void onNavTgtP1();
    void onNavTgtP2();
    void onStopNav();

private:
    class View;   // inner QGraphicsView subclass that handles wheel + pan

    void setupUI();
    void rebuildScene();
    void computeTransform();
    QPointF gantryToScene(double gx, double gy) const;
    void drawArrow(const QPointF& from, const QPointF& to, const QColor& color);
    QPixmap loadThumbnail(const std::string& imageFile);
    std::string getImageDirectory() const;

    // Async safe-move for point navigation: SetSystemVelocity(0.5)
    // -> MoveToAbs -> SetSystemVelocity(50). Mirrors EdgeTeachWidget pattern.
    void startNavAsync(double tx, double ty, double tz, const QString& which);
    void updateNavButtonsEnabled();

    ApplicationContext* m_context;
    EdgeTransferSet m_set;

    View* m_view = nullptr;
    QGraphicsScene* m_scene = nullptr;

    // Nav buttons (top toolbar)
    QPushButton* m_navRefP1Btn = nullptr;
    QPushButton* m_navRefP2Btn = nullptr;
    QPushButton* m_navTgtP1Btn = nullptr;
    QPushButton* m_navTgtP2Btn = nullptr;
    QPushButton* m_stopNavBtn  = nullptr;   // only enabled while nav is running

    // Nav state
    std::future<void> m_navFuture;
    std::string m_navActiveDevice;   // non-empty while a move is in progress
    bool m_externallyBlocked = false;

    double m_scale = 1.0;
    double m_originX = 0.0;
    double m_originY = 0.0;
    double m_sceneW = 800.0;
    double m_sceneH = 600.0;
};
