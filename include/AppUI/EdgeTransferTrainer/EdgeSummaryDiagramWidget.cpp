// EdgeSummaryDiagramWidget.cpp
#include "EdgeSummaryDiagramWidget.h"
#include "ApplicationContext.h"
#include "product_position_manager.h"
#include "unified_motion_manager.h"
#include "SequenceEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMetaObject>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QGraphicsTextItem>
#include <QPainter>
#include <QPixmap>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Inner QGraphicsView: wheel-zoom + middle-mouse-pan
// ============================================================================
class EdgeSummaryDiagramWidget::View : public QGraphicsView {
public:
    explicit View(QWidget* parent = nullptr) : QGraphicsView(parent) {
        setRenderHint(QPainter::Antialiasing);
        setRenderHint(QPainter::SmoothPixmapTransform);
        setDragMode(QGraphicsView::NoDrag);
        setTransformationAnchor(AnchorUnderMouse);
        setStyleSheet("QGraphicsView { border: none; background-color: #0a0f1e; }");
    }
protected:
    void wheelEvent(QWheelEvent* event) override {
        constexpr double ZF = 1.15, MIN_Z = 0.1, MAX_Z = 10.0;
        if (event->angleDelta().y() > 0) {
            if (m_zoom * ZF <= MAX_Z) { scale(ZF, ZF); m_zoom *= ZF; }
        } else {
            if (m_zoom / ZF >= MIN_Z) { scale(1.0 / ZF, 1.0 / ZF); m_zoom /= ZF; }
        }
        event->accept();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton) {
            m_panning = true;
            m_lastPan = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
        } else {
            QGraphicsView::mousePressEvent(event);
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_panning) {
            QPoint d = event->pos() - m_lastPan;
            m_lastPan = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
            event->accept();
        } else {
            QGraphicsView::mouseMoveEvent(event);
        }
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton && m_panning) {
            m_panning = false;
            setCursor(Qt::ArrowCursor);
            event->accept();
        } else {
            QGraphicsView::mouseReleaseEvent(event);
        }
    }
private:
    bool m_panning = false;
    QPoint m_lastPan;
    double m_zoom = 1.0;
};

// ============================================================================
// EdgeSummaryDiagramWidget
// ============================================================================

EdgeSummaryDiagramWidget::EdgeSummaryDiagramWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_scene(new QGraphicsScene(this))
{
    setupUI();
}

void EdgeSummaryDiagramWidget::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // ---- Top toolbar: 4 auto-nav buttons (gantry-main @ 0.5 mm/s) ----
    const QString navBtnStyle =
        "QPushButton { background-color: #16a085; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 6px 12px; font-size: 12px; }"
        "QPushButton:hover { background-color: #1abc9c; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    QHBoxLayout* navRow = new QHBoxLayout();
    navRow->setContentsMargins(8, 8, 8, 0);
    navRow->setSpacing(6);

    m_navRefP1Btn = new QPushButton("Nav Ref P1");
    m_navRefP2Btn = new QPushButton("Nav Ref P2");
    m_navTgtP1Btn = new QPushButton("Nav Tgt P1 (P3)");
    m_navTgtP2Btn = new QPushButton("Nav Tgt P2 (P4)");
    for (auto* b : {m_navRefP1Btn, m_navRefP2Btn, m_navTgtP1Btn, m_navTgtP2Btn}) {
        b->setFixedHeight(28);
        b->setStyleSheet(navBtnStyle);
        b->setToolTip("Moves gantry-main to this captured point at 0.5 mm/s.");
        navRow->addWidget(b);
    }
    navRow->addStretch(1);

    m_stopNavBtn = new QPushButton("STOP");
    m_stopNavBtn->setFixedHeight(28);
    m_stopNavBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 6px 16px; font-size: 12px; }"
        "QPushButton:hover { background-color: #e74c3c; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }");
    m_stopNavBtn->setToolTip(
        "EmergencyStop gantry-main. Only active while an Auto-Nav is running.");
    m_stopNavBtn->setEnabled(false);
    navRow->addWidget(m_stopNavBtn);

    connect(m_navRefP1Btn, &QPushButton::clicked, this, &EdgeSummaryDiagramWidget::onNavRefP1);
    connect(m_navRefP2Btn, &QPushButton::clicked, this, &EdgeSummaryDiagramWidget::onNavRefP2);
    connect(m_navTgtP1Btn, &QPushButton::clicked, this, &EdgeSummaryDiagramWidget::onNavTgtP1);
    connect(m_navTgtP2Btn, &QPushButton::clicked, this, &EdgeSummaryDiagramWidget::onNavTgtP2);
    connect(m_stopNavBtn,  &QPushButton::clicked, this, &EdgeSummaryDiagramWidget::onStopNav);

    layout->addLayout(navRow);

    m_view = new View(this);
    m_view->setScene(m_scene);
    layout->addWidget(m_view);

    updateNavButtonsEnabled();
}

void EdgeSummaryDiagramWidget::SetData(const EdgeTransferSet& set)
{
    m_set = set;
    rebuildScene();
    updateNavButtonsEnabled();
}

void EdgeSummaryDiagramWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_scene->items().isEmpty()) {
        QRectF bounds = m_scene->itemsBoundingRect().adjusted(-30, -30, 30, 30);
        m_view->fitInView(bounds, Qt::KeepAspectRatio);
    }
}

void EdgeSummaryDiagramWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!m_scene->items().isEmpty()) {
        QRectF bounds = m_scene->itemsBoundingRect().adjusted(-30, -30, 30, 30);
        m_view->fitInView(bounds, Qt::KeepAspectRatio);
    }
}

void EdgeSummaryDiagramWidget::computeTransform()
{
    double xs[4] = { m_set.referenceEdge.p1x, m_set.referenceEdge.p2x,
                     m_set.targetEdge.p1x,    m_set.targetEdge.p2x };
    double ys[4] = { m_set.referenceEdge.p1y, m_set.referenceEdge.p2y,
                     m_set.targetEdge.p1y,    m_set.targetEdge.p2y };

    double minX = *std::min_element(xs, xs + 4);
    double maxX = *std::max_element(xs, xs + 4);
    double minY = *std::min_element(ys, ys + 4);
    double maxY = *std::max_element(ys, ys + 4);

    double rangeX = std::max(0.5, maxX - minX);
    double rangeY = std::max(0.5, maxY - minY);

    double padX = rangeX * 0.4;
    double padY = rangeY * 0.4;

    m_originX = minX - padX;
    m_originY = minY - padY;

    double totalX = rangeX + 2.0 * padX;
    double totalY = rangeY + 2.0 * padY;

    double sx = m_sceneW / totalX;
    double sy = m_sceneH / totalY;
    m_scale = std::min(sx, sy);
}

QPointF EdgeSummaryDiagramWidget::gantryToScene(double gx, double gy) const
{
    return QPointF((gx - m_originX) * m_scale, (gy - m_originY) * m_scale);
}

void EdgeSummaryDiagramWidget::rebuildScene()
{
    m_scene->clear();

    if (!m_set.referenceEdge.valid || !m_set.targetEdge.valid) {
        auto* text = m_scene->addText(
            "Capture both edges to see the summary diagram.",
            QFont("sans-serif", 14));
        text->setDefaultTextColor(QColor("#888888"));
        text->setPos(50, 50);
        m_scene->setSceneRect(0, 0, m_sceneW, m_sceneH);
        m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
        return;
    }

    computeTransform();
    m_scene->setSceneRect(0, 0, m_sceneW, m_sceneH);

    QPointF refP1 = gantryToScene(m_set.referenceEdge.p1x, m_set.referenceEdge.p1y);
    QPointF refP2 = gantryToScene(m_set.referenceEdge.p2x, m_set.referenceEdge.p2y);
    QPointF tgtP1 = gantryToScene(m_set.targetEdge.p1x,    m_set.targetEdge.p1y);
    QPointF tgtP2 = gantryToScene(m_set.targetEdge.p2x,    m_set.targetEdge.p2y);

    QPointF refMid = (refP1 + refP2) / 2.0;
    QPointF tgtMid = (tgtP1 + tgtP2) / 2.0;

    const QColor refColor("#00ff88");
    const QColor tgtColor("#ff8844");
    const QColor arrowColor("#f0c040");

    // Thumbnails behind each point (z=0)
    std::string imageFiles[4] = {
        m_set.referenceEdge.p1ImageFile, m_set.referenceEdge.p2ImageFile,
        m_set.targetEdge.p1ImageFile,    m_set.targetEdge.p2ImageFile
    };
    QPointF pts[4] = { refP1, refP2, tgtP1, tgtP2 };
    for (int i = 0; i < 4; ++i) {
        QPixmap thumb = loadThumbnail(imageFiles[i]);
        auto* item = m_scene->addPixmap(thumb);
        item->setPos(pts[i].x() - thumb.width() / 2.0,
                     pts[i].y() - thumb.height() / 2.0);
        item->setZValue(0);
        item->setOpacity(0.7);
    }

    // Edges
    auto* refLine = m_scene->addLine(QLineF(refP1, refP2), QPen(refColor, 2.5));
    refLine->setZValue(2);
    auto* tgtLine = m_scene->addLine(QLineF(tgtP1, tgtP2), QPen(tgtColor, 2.5));
    tgtLine->setZValue(2);

    // Point markers + labels
    QColor ptColors[4] = { refColor, refColor, tgtColor, tgtColor };
    QString ptLabels[4] = { "Ref P1", "Ref P2", "Tgt P1 (P3)", "Tgt P2 (P4)" };
    for (int i = 0; i < 4; ++i) {
        auto* dot = m_scene->addEllipse(pts[i].x() - 6, pts[i].y() - 6, 12, 12,
                                         QPen(ptColors[i], 1.5), QBrush(ptColors[i]));
        dot->setZValue(3);
        auto* label = m_scene->addText(ptLabels[i], QFont("sans-serif", 9, QFont::Bold));
        label->setDefaultTextColor(QColor("#cccccc"));
        label->setPos(pts[i].x() + 10, pts[i].y() - 20);
        label->setZValue(7);
    }

    // Midpoint markers
    for (const auto& mid : { refMid, tgtMid }) {
        auto* circle = m_scene->addEllipse(mid.x() - 8, mid.y() - 8, 16, 16,
                                            QPen(Qt::white, 1.5), QBrush(Qt::transparent));
        circle->setZValue(4);
        m_scene->addLine(mid.x() - 4, mid.y(), mid.x() + 4, mid.y(),
                         QPen(Qt::white, 1))->setZValue(4);
        m_scene->addLine(mid.x(), mid.y() - 4, mid.x(), mid.y() + 4,
                         QPen(Qt::white, 1))->setZValue(4);
    }

    // Midpoint-to-midpoint arrow
    drawArrow(refMid, tgtMid, arrowColor);

    // Angle + delta annotations
    const QString deg = QString(QChar(0x00B0));

    auto* refAngle = m_scene->addText(
        QString("Ref: angleToX=%1%2   tilt=%3%4")
            .arg(m_set.referenceEdge.angleToX, 0, 'f', 3).arg(deg)
            .arg(m_set.referenceEdge.verticalTilt, 0, 'f', 3).arg(deg),
        QFont("sans-serif", 9));
    refAngle->setDefaultTextColor(refColor);
    refAngle->setPos(refMid.x() + 12, refMid.y() + 4);
    refAngle->setZValue(6);

    auto* tgtAngle = m_scene->addText(
        QString("Tgt: angleToX=%1%2   tilt=%3%4")
            .arg(m_set.targetEdge.angleToX, 0, 'f', 3).arg(deg)
            .arg(m_set.targetEdge.verticalTilt, 0, 'f', 3).arg(deg),
        QFont("sans-serif", 9));
    tgtAngle->setDefaultTextColor(tgtColor);
    tgtAngle->setPos(tgtMid.x() + 12, tgtMid.y() + 4);
    tgtAngle->setZValue(6);

    auto* deltaLabel = m_scene->addText(
        QString("deltaYaw=%1%2   deltaTilt=%3%4")
            .arg(m_set.deltaYawDeg, 0, 'f', 3).arg(deg)
            .arg(m_set.deltaTiltDeg, 0, 'f', 3).arg(deg),
        QFont("sans-serif", 10, QFont::Bold));
    deltaLabel->setDefaultTextColor(arrowColor);
    QPointF arrowMid = (refMid + tgtMid) / 2.0;
    deltaLabel->setPos(arrowMid.x() + 10, arrowMid.y() - 20);
    deltaLabel->setZValue(6);

    // Footer info
    QString info = QString("Target device: %1    U=%2 V=%3 W=%4")
        .arg(QString::fromStdString(m_set.config.targetDevice))
        .arg(EdgeAxisRoleToString(m_set.config.mapping.u))
        .arg(EdgeAxisRoleToString(m_set.config.mapping.v))
        .arg(EdgeAxisRoleToString(m_set.config.mapping.w));
    auto* infoText = m_scene->addText(info, QFont("sans-serif", 9));
    infoText->setDefaultTextColor(QColor("#666666"));
    infoText->setPos(5, m_sceneH - 25);
    infoText->setZValue(7);

    QRectF bounds = m_scene->itemsBoundingRect().adjusted(-30, -30, 30, 30);
    m_scene->setSceneRect(bounds);
    m_view->fitInView(bounds, Qt::KeepAspectRatio);
}

void EdgeSummaryDiagramWidget::drawArrow(const QPointF& from, const QPointF& to,
                                         const QColor& color)
{
    auto* shaft = m_scene->addLine(QLineF(from, to), QPen(color, 2.5));
    shaft->setZValue(5);

    double angle = std::atan2(to.y() - from.y(), to.x() - from.x());
    double arrowSize = 12.0;
    QPointF p1 = to - QPointF(arrowSize * std::cos(angle - M_PI / 6),
                               arrowSize * std::sin(angle - M_PI / 6));
    QPointF p2 = to - QPointF(arrowSize * std::cos(angle + M_PI / 6),
                               arrowSize * std::sin(angle + M_PI / 6));
    QPolygonF head; head << to << p1 << p2;
    auto* headItem = m_scene->addPolygon(head, QPen(color, 1), QBrush(color));
    headItem->setZValue(5);
}

QPixmap EdgeSummaryDiagramWidget::loadThumbnail(const std::string& imageFile)
{
    constexpr int TW = 120, TH = 90;

    auto makePlaceholder = [&](const QString& text) {
        QPixmap ph(TW, TH);
        ph.fill(QColor("#1a2640"));
        QPainter p(&ph);
        p.setPen(QColor("#555555"));
        p.setFont(QFont("sans-serif", 8));
        p.drawText(ph.rect(), Qt::AlignCenter, text);
        p.end();
        return ph;
    };

    if (imageFile.empty()) return makePlaceholder("No Image");

    std::string fullPath = getImageDirectory() + "/" + imageFile;
    QPixmap pixmap(QString::fromStdString(fullPath));
    if (pixmap.isNull()) return makePlaceholder("Missing");

    int cropW = pixmap.width() / 2;
    int cropH = pixmap.height() / 2;
    int cropX = (pixmap.width() - cropW) / 2;
    int cropY = (pixmap.height() - cropH) / 2;
    QPixmap cropped = pixmap.copy(cropX, cropY, cropW, cropH);

    QPixmap scaled = cropped.scaled(TW, TH, Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation);

    QPainter painter(&scaled);
    painter.setPen(QPen(QColor(0, 255, 255), 1));
    int cx = scaled.width() / 2;
    int cy = scaled.height() / 2;
    painter.drawLine(cx, 0, cx, scaled.height());
    painter.drawLine(0, cy, scaled.width(), cy);
    painter.setPen(QPen(QColor("#3d5a8c"), 1));
    painter.drawRect(0, 0, scaled.width() - 1, scaled.height() - 1);
    painter.end();
    return scaled;
}

// ============================================================================
// Auto-nav buttons (one per captured point)
// ============================================================================

void EdgeSummaryDiagramWidget::SetNavBlockedExternal(bool blocked)
{
    m_externallyBlocked = blocked;
    updateNavButtonsEnabled();
}

void EdgeSummaryDiagramWidget::updateNavButtonsEnabled()
{
    const bool busy = !m_navActiveDevice.empty();
    const bool refValid = m_set.referenceEdge.valid;
    const bool tgtValid = m_set.targetEdge.valid;
    const bool canClick = !busy && !m_externallyBlocked;

    if (m_navRefP1Btn) m_navRefP1Btn->setEnabled(canClick && refValid);
    if (m_navRefP2Btn) m_navRefP2Btn->setEnabled(canClick && refValid);
    if (m_navTgtP1Btn) m_navTgtP1Btn->setEnabled(canClick && tgtValid);
    if (m_navTgtP2Btn) m_navTgtP2Btn->setEnabled(canClick && tgtValid);
    if (m_stopNavBtn)  m_stopNavBtn->setEnabled(busy);
}

void EdgeSummaryDiagramWidget::onStopNav()
{
    if (!m_context || !m_context->motionManager) return;
    if (m_navActiveDevice.empty()) return;
    const std::string device = m_navActiveDevice;
    bool ok = m_context->motionManager->EmergencyStop(device);
    std::cout << "[EdgeSummary] STOP Auto-Nav on " << device
              << "  EmergencyStop=" << (ok ? "OK" : "FAIL") << std::endl;
    // The blocked MoveToAbs unblocks; the worker restores velocity and the
    // posted UI callback clears m_navActiveDevice + re-enables buttons.
}

void EdgeSummaryDiagramWidget::onNavRefP1() {
    if (!m_set.referenceEdge.valid) return;
    startNavAsync(m_set.referenceEdge.p1x, m_set.referenceEdge.p1y,
                  m_set.referenceEdge.p1z, "Ref P1");
}
void EdgeSummaryDiagramWidget::onNavRefP2() {
    if (!m_set.referenceEdge.valid) return;
    startNavAsync(m_set.referenceEdge.p2x, m_set.referenceEdge.p2y,
                  m_set.referenceEdge.p2z, "Ref P2");
}
void EdgeSummaryDiagramWidget::onNavTgtP1() {
    if (!m_set.targetEdge.valid) return;
    startNavAsync(m_set.targetEdge.p1x, m_set.targetEdge.p1y,
                  m_set.targetEdge.p1z, "Tgt P1");
}
void EdgeSummaryDiagramWidget::onNavTgtP2() {
    if (!m_set.targetEdge.valid) return;
    startNavAsync(m_set.targetEdge.p2x, m_set.targetEdge.p2y,
                  m_set.targetEdge.p2z, "Tgt P2");
}

void EdgeSummaryDiagramWidget::startNavAsync(double tx, double ty, double tz,
                                             const QString& which)
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Auto-Nav", "Motion manager not available.");
        return;
    }
    if (m_context->sequenceEngine && m_context->sequenceEngine->IsExecuting()) {
        QMessageBox::warning(this, "Auto-Nav Blocked",
            "A recipe is currently running. Stop the program before Auto-Nav.");
        return;
    }
    if (!m_navActiveDevice.empty()) return;   // double-click guard
    if (m_externallyBlocked) {
        QMessageBox::information(this, "Auto-Nav",
            "Blocked: a correction is currently being applied.");
        return;
    }

    constexpr double kSafeVel    = 0.5;   // mm/s during the move
    constexpr double kDefaultVel = 50.0;  // mm/s restored after

    const std::string device = "gantry-main";
    m_navActiveDevice = device;
    updateNavButtonsEnabled();
    emit navBusyChanged(true);

    auto* mm = m_context->motionManager;
    QString whichCopy = which;

    m_navFuture = std::async(std::launch::async,
        [this, mm, device, tx, ty, tz, whichCopy]() {
        bool velSlowOk    = mm->SetSystemVelocity(device, kSafeVel);
        bool moveOk       = mm->MoveToAbs(device, tx, ty, tz);
        bool velRestoreOk = mm->SetSystemVelocity(device, kDefaultVel);

        std::cout << "[EdgeSummary] Nav to " << whichCopy.toStdString()
                  << " (" << tx << ", " << ty << ", " << tz << ")"
                  << "  velSlow="    << (velSlowOk ? "OK" : "FAIL")
                  << "  move="       << (moveOk ? "OK" : "FAIL")
                  << "  velRestore=" << (velRestoreOk ? "OK" : "FAIL") << std::endl;

        QMetaObject::invokeMethod(this, [this, moveOk, whichCopy]() {
            m_navActiveDevice.clear();
            updateNavButtonsEnabled();
            emit navBusyChanged(false);
            if (!moveOk) {
                QMessageBox::warning(this, "Auto-Nav Failed",
                    QString("MoveToAbs to %1 failed.").arg(whichCopy));
            }
        }, Qt::QueuedConnection);
    });
}

std::string EdgeSummaryDiagramWidget::getImageDirectory() const
{
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        std::filesystem::path productPath(
            m_context->productPositionManager->GetCurrentProductPath());
        return (productPath.parent_path() / "images" / "trainer").string();
    }
    return "config/images/trainer";
}
