// TemplateManagerDialog.h
// Modal dialog that consolidates template teach/clear/save/load/anchor controls
// for plugins inheriting from TemplateMatchingBase.
#pragma once

#ifdef HAS_OPENCV

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QPoint>
#include <opencv2/core.hpp>

class TemplateMatchingBase;

class TemplateImageLabel : public QLabel {
    Q_OBJECT
public:
    explicit TemplateImageLabel(QWidget* parent = nullptr);

    void setSourceImage(const cv::Mat& image, int maxDisplaySize = 700);
    bool hasSelection() const { return m_hasSelection; }
    bool getSelection(int& startX, int& startY, int& endX, int& endY) const;
    void clearSelection();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QPoint widgetToImage(const QPoint& w) const;

    int m_imgWidth = 0;
    int m_imgHeight = 0;
    double m_scale = 1.0;

    bool m_dragging = false;
    bool m_hasSelection = false;
    QPoint m_imgStart;
    QPoint m_imgEnd;
};

class TemplateManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit TemplateManagerDialog(TemplateMatchingBase* plugin, QWidget* parent = nullptr);

private:
    void onTeach();
    void onClear();
    void onSave();
    void onLoad();
    void onAnchor();
    void refreshPreview();
    void loadSourceImage();

    TemplateMatchingBase* m_plugin = nullptr;
    TemplateImageLabel* m_imageLabel = nullptr;
    QLabel* m_previewLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_teachBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_loadBtn = nullptr;
    QPushButton* m_anchorBtn = nullptr;
};

#endif // HAS_OPENCV
