// VisionImageWidget.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QScrollArea>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QString>
#include <vector>
#include "../VisionResult.h"

// Custom label for mouse tracking on image
class ImageDisplayLabel : public QLabel {
    Q_OBJECT

public:
    explicit ImageDisplayLabel(QWidget* parent = nullptr);

    void setROIEnabled(bool enabled) { m_roiEnabled = enabled; }
    void setROI(int startX, int startY, int endX, int endY);
    void setScaleFactors(double scaleX, double scaleY);
    void setPixmapSize(int width, int height);

    // Search Box support
    void setSearchBoxEnabled(bool enabled) { m_searchBoxEnabled = enabled; }
    void setSearchBox(int startX, int startY, int endX, int endY);

    // Template Selection support (red rectangle for teaching template)
    void setTemplateSelectionEnabled(bool enabled) { m_templateSelectionEnabled = enabled; }
    void setTemplateSelection(int startX, int startY, int endX, int endY);
    void getTemplateSelection(int& startX, int& startY, int& endX, int& endY) const;

signals:
    void roiCornerDragged(int startX, int startY, int endX, int endY);
    void searchBoxCornerDragged(int startX, int startY, int endX, int endY);
    void templateSelectionCornerDragged(int startX, int startY, int endX, int endY);
    // Viewport interaction
    void panDelta(const QPoint& delta);
    void resetViewRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class DragCorner { None, TopLeft, TopRight, BottomLeft, BottomRight };
    enum class DragTarget { None, ROI, SearchBox, TemplateSelection };
    DragCorner hitTestCorner(const QPoint& pos, int startX, int startY, int endX, int endY);
    DragCorner hitTestROICorner(const QPoint& pos);
    DragCorner hitTestSearchBoxCorner(const QPoint& pos);
    DragCorner hitTestTemplateSelectionCorner(const QPoint& pos);
    void updateCursor(const QPoint& pos);
    QPoint pixmapOffset() const;
    QPoint toPixmapCoords(const QPoint& widgetPos) const;

    bool m_roiEnabled = false;
    int m_roiStartX = 0;
    int m_roiStartY = 0;
    int m_roiEndX = 640;
    int m_roiEndY = 480;

    // Search Box
    bool m_searchBoxEnabled = false;
    int m_searchBoxStartX = 0;
    int m_searchBoxStartY = 0;
    int m_searchBoxEndX = 800;
    int m_searchBoxEndY = 600;

    // Template Selection (red rectangle for teaching template)
    bool m_templateSelectionEnabled = false;
    int m_templateSelectionStartX = 100;
    int m_templateSelectionStartY = 100;
    int m_templateSelectionEndX = 300;
    int m_templateSelectionEndY = 300;

    double m_scaleX = 1.0;
    double m_scaleY = 1.0;
    int m_pixmapWidth = 0;
    int m_pixmapHeight = 0;

    DragCorner m_draggingCorner = DragCorner::None;
    DragTarget m_draggingTarget = DragTarget::None;
    bool m_isDragging = false;

    // Pan state — left-click drag on the image (when no corner hit) scrolls
    // the viewport. Separate from ROI/corner drag handling above.
    bool m_isPanning = false;
    QPoint m_panLastPos;
};

class VisionImageWidget : public QWidget {
    Q_OBJECT

public:
    explicit VisionImageWidget(QWidget* parent = nullptr);

    void setImage(const QPixmap& pixmap);
    void clearImage();
    void enableCrosshair(bool enable);

    // ROI overlay
    void setROI(int startX, int startY, int endX, int endY);
    void setROIEnabled(bool enabled);

    // Search Box overlay (larger area where ROI can move)
    void setSearchBox(int startX, int startY, int endX, int endY);
    void setSearchBoxEnabled(bool enabled);

    // Template Selection overlay (red rectangle for teaching template)
    void setTemplateSelection(int startX, int startY, int endX, int endY);
    void setTemplateSelectionEnabled(bool enabled);
    void getTemplateSelection(int& startX, int& startY, int& endX, int& endY) const;

    // Result overlay - circles with crosshairs and labels
    struct ResultMarker {
        double x;
        double y;
        double radius;
        int label;
    };
    void setResultMarkers(const std::vector<ResultMarker>& markers);
    void clearResultMarkers();

    // Overlay commands from VisionResult
    void setOverlays(const std::vector<OverlayCommand>& overlays);
    void clearOverlays();

    // Test-mode image injection (load image from disk for vision profiling)
    bool isInjectionMode() const;
    void setInjectedFileName(const QString& fileName);

    // Viewport navigation
    void resetView();                       // Reset zoom to fit, scroll to top-left
    double zoomFactor() const { return m_zoomFactor; }

signals:
    void captureClicked();
    void clearClicked();
    void roiDragged(int startX, int startY, int endX, int endY);
    void searchBoxDragged(int startX, int startY, int endX, int endY);
    void templateSelectionDragged(int startX, int startY, int endX, int endY);
    void injectionModeToggled(bool enabled);
    void loadInjectedImageRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onROICornerDragged(int startX, int startY, int endX, int endY);
    void onSearchBoxCornerDragged(int startX, int startY, int endX, int endY);
    void onTemplateSelectionCornerDragged(int startX, int startY, int endX, int endY);
    void onPanDelta(const QPoint& delta);
    void onResetViewRequested();

private:
    void setupUI();
    void updateDisplayImage();
    QPixmap scaleToFit(const QPixmap& pixmap);

    ImageDisplayLabel* m_imageLabel;
    QScrollArea* m_scrollArea;
    QPushButton* m_captureBtn;
    QPushButton* m_clearBtn;
    QCheckBox* m_crosshairToggle;

    // Test-mode injection bar (above scroll area)
    QCheckBox* m_injectModeToggle = nullptr;
    QPushButton* m_loadInjectBtn = nullptr;
    QLabel* m_injectPathLabel = nullptr;
    QString m_injectFileNameFull;

    bool m_showCrosshair;

    // Original image storage for overlays
    QPixmap m_originalImage;

    // ROI overlay
    bool m_roiEnabled;
    int m_roiStartX;
    int m_roiStartY;
    int m_roiEndX;
    int m_roiEndY;

    // Search Box overlay
    bool m_searchBoxEnabled;
    int m_searchBoxStartX;
    int m_searchBoxStartY;
    int m_searchBoxEndX;
    int m_searchBoxEndY;

    // Template Selection overlay (red rectangle)
    bool m_templateSelectionEnabled;
    int m_templateSelectionStartX;
    int m_templateSelectionStartY;
    int m_templateSelectionEndX;
    int m_templateSelectionEndY;

    // Result markers
    std::vector<ResultMarker> m_resultMarkers;

    // Overlay commands
    std::vector<OverlayCommand> m_overlays;

    // Viewport zoom/pan state
    double m_zoomFactor = 1.0;      // 1.0 = fit-to-viewport; > 1 zoom in, < 1 zoom out
    double m_fitScale = 1.0;        // Image-to-viewport scale at zoom=1 (cached)
    static constexpr double kMinZoom = 0.25;
    static constexpr double kMaxZoom = 20.0;
    static constexpr double kZoomStep = 1.25;  // Multiplier per wheel notch
};
