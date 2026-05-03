// TemplateManagerDialog.cpp
#ifdef HAS_OPENCV

#include "TemplateManagerDialog.h"
#include "TemplateMatchingBase.h"
#include "../VisionManager.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QPen>
#include <QImage>
#include <QPixmap>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

// ---------- TemplateImageLabel ----------

TemplateImageLabel::TemplateImageLabel(QWidget* parent)
    : QLabel(parent)
{
    setAlignment(Qt::AlignTop | Qt::AlignLeft);
    setStyleSheet("QLabel { background-color: #2a2a2a; border: 1px solid #555; color: #cccccc; }");
    setMouseTracking(true);
    setText("No image. Capture a frame on the Vision page first, then re-open this dialog.");
    setMinimumSize(400, 300);
}

void TemplateImageLabel::setSourceImage(const cv::Mat& image, int maxDisplaySize)
{
    if (image.empty()) {
        m_imgWidth = 0;
        m_imgHeight = 0;
        m_scale = 1.0;
        clearSelection();
        setPixmap(QPixmap());
        setText("No image. Capture a frame on the Vision page first, then re-open this dialog.");
        return;
    }

    cv::Mat display;
    if (image.channels() == 1) {
        cv::cvtColor(image, display, cv::COLOR_GRAY2RGB);
    } else if (image.channels() == 4) {
        cv::cvtColor(image, display, cv::COLOR_BGRA2RGB);
    } else {
        cv::cvtColor(image, display, cv::COLOR_BGR2RGB);
    }

    m_imgWidth = image.cols;
    m_imgHeight = image.rows;

    int longest = std::max(m_imgWidth, m_imgHeight);
    m_scale = (longest > maxDisplaySize) ? (static_cast<double>(maxDisplaySize) / longest) : 1.0;
    int displayW = static_cast<int>(std::round(m_imgWidth * m_scale));
    int displayH = static_cast<int>(std::round(m_imgHeight * m_scale));

    QImage qimg(display.data, display.cols, display.rows,
                static_cast<int>(display.step), QImage::Format_RGB888);
    QPixmap pixmap = QPixmap::fromImage(qimg.copy())
                         .scaled(displayW, displayH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    setText(QString());
    setPixmap(pixmap);
    setFixedSize(displayW, displayH);
    clearSelection();
}

bool TemplateImageLabel::getSelection(int& startX, int& startY, int& endX, int& endY) const
{
    if (!m_hasSelection || m_imgWidth == 0 || m_imgHeight == 0) {
        return false;
    }
    startX = std::min(m_imgStart.x(), m_imgEnd.x());
    startY = std::min(m_imgStart.y(), m_imgEnd.y());
    endX = std::max(m_imgStart.x(), m_imgEnd.x());
    endY = std::max(m_imgStart.y(), m_imgEnd.y());

    startX = std::max(0, std::min(startX, m_imgWidth - 1));
    startY = std::max(0, std::min(startY, m_imgHeight - 1));
    endX = std::max(0, std::min(endX, m_imgWidth));
    endY = std::max(0, std::min(endY, m_imgHeight));
    return endX > startX && endY > startY;
}

void TemplateImageLabel::clearSelection()
{
    m_dragging = false;
    m_hasSelection = false;
    m_imgStart = QPoint();
    m_imgEnd = QPoint();
    update();
}

QPoint TemplateImageLabel::widgetToImage(const QPoint& w) const
{
    if (m_imgWidth == 0 || m_imgHeight == 0 || m_scale <= 0.0) {
        return QPoint();
    }
    int ix = static_cast<int>(std::round(w.x() / m_scale));
    int iy = static_cast<int>(std::round(w.y() / m_scale));
    ix = std::max(0, std::min(ix, m_imgWidth));
    iy = std::max(0, std::min(iy, m_imgHeight));
    return QPoint(ix, iy);
}

void TemplateImageLabel::mousePressEvent(QMouseEvent* event)
{
    if (m_imgWidth == 0 || m_imgHeight == 0 || event->button() != Qt::LeftButton) {
        return;
    }
    m_dragging = true;
    m_hasSelection = false;
    m_imgStart = widgetToImage(event->pos());
    m_imgEnd = m_imgStart;
    update();
}

void TemplateImageLabel::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging) {
        return;
    }
    m_imgEnd = widgetToImage(event->pos());
    update();
}

void TemplateImageLabel::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragging || event->button() != Qt::LeftButton) {
        return;
    }
    m_dragging = false;
    m_imgEnd = widgetToImage(event->pos());
    m_hasSelection = (std::abs(m_imgEnd.x() - m_imgStart.x()) > 2) &&
                     (std::abs(m_imgEnd.y() - m_imgStart.y()) > 2);
    update();
}

void TemplateImageLabel::paintEvent(QPaintEvent* event)
{
    QLabel::paintEvent(event);
    if ((!m_hasSelection && !m_dragging) || m_imgWidth == 0 || m_imgHeight == 0) {
        return;
    }

    int x1 = std::min(m_imgStart.x(), m_imgEnd.x());
    int y1 = std::min(m_imgStart.y(), m_imgEnd.y());
    int x2 = std::max(m_imgStart.x(), m_imgEnd.x());
    int y2 = std::max(m_imgStart.y(), m_imgEnd.y());

    QPainter painter(this);
    QPen pen(Qt::red, 2);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    int dx1 = static_cast<int>(std::round(x1 * m_scale));
    int dy1 = static_cast<int>(std::round(y1 * m_scale));
    int dx2 = static_cast<int>(std::round(x2 * m_scale));
    int dy2 = static_cast<int>(std::round(y2 * m_scale));
    painter.drawRect(QRect(QPoint(dx1, dy1), QPoint(dx2, dy2)));
}

// ---------- TemplateManagerDialog ----------

TemplateManagerDialog::TemplateManagerDialog(TemplateMatchingBase* plugin, QWidget* parent)
    : QDialog(parent)
    , m_plugin(plugin)
{
    setWindowTitle("Template Manager");
    setModal(true);
    setMinimumSize(900, 600);

    QHBoxLayout* root = new QHBoxLayout(this);
    root->setSpacing(8);

    m_imageLabel = new TemplateImageLabel(this);
    root->addWidget(m_imageLabel, 1);

    QVBoxLayout* rightCol = new QVBoxLayout();
    rightCol->setSpacing(6);

    m_teachBtn = new QPushButton("Teach", this);
    m_teachBtn->setStyleSheet(
        "QPushButton { background-color: #1565C0; color: white; font-weight: bold; padding: 6px; }");
    m_teachBtn->setToolTip("Capture the red rectangle as the template");
    rightCol->addWidget(m_teachBtn);

    m_clearBtn = new QPushButton("Clear", this);
    m_clearBtn->setStyleSheet("QPushButton { background-color: #C62828; color: white; padding: 6px; }");
    rightCol->addWidget(m_clearBtn);

    m_saveBtn = new QPushButton("Save", this);
    m_saveBtn->setToolTip("Save template to PNG file");
    rightCol->addWidget(m_saveBtn);

    m_loadBtn = new QPushButton("Load", this);
    m_loadBtn->setToolTip("Load template from PNG file");
    rightCol->addWidget(m_loadBtn);

    m_anchorBtn = new QPushButton("Anchor", this);
    m_anchorBtn->setStyleSheet("QPushButton { background-color: #388E3C; color: white; padding: 6px; }");
    m_anchorBtn->setToolTip("Set the anchor point on the current template");
    rightCol->addWidget(m_anchorBtn);

    rightCol->addSpacing(12);

    QLabel* previewTitle = new QLabel("Current template:", this);
    rightCol->addWidget(previewTitle);

    m_previewLabel = new QLabel("No template", this);
    m_previewLabel->setFixedSize(200, 200);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet("QLabel { background-color: #2a2a2a; border: 1px solid #555; }");
    rightCol->addWidget(m_previewLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    rightCol->addWidget(m_statusLabel);

    rightCol->addStretch();

    QPushButton* closeBtn = new QPushButton("Close", this);
    rightCol->addWidget(closeBtn);

    root->addLayout(rightCol, 0);

    connect(m_teachBtn, &QPushButton::clicked, this, &TemplateManagerDialog::onTeach);
    connect(m_clearBtn, &QPushButton::clicked, this, &TemplateManagerDialog::onClear);
    connect(m_saveBtn, &QPushButton::clicked, this, &TemplateManagerDialog::onSave);
    connect(m_loadBtn, &QPushButton::clicked, this, &TemplateManagerDialog::onLoad);
    connect(m_anchorBtn, &QPushButton::clicked, this, &TemplateManagerDialog::onAnchor);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    loadSourceImage();
    refreshPreview();
}

void TemplateManagerDialog::loadSourceImage()
{
    cv::Mat src = VisionManager::instance().getInputImage();
    m_imageLabel->setSourceImage(src);
    if (src.empty()) {
        m_statusLabel->setText("No source image. Capture a frame, then reopen.");
    } else {
        m_statusLabel->setText("Drag a red rectangle, then click Teach.");
    }
}

void TemplateManagerDialog::refreshPreview()
{
    if (!m_plugin || !m_plugin->hasTemplate()) {
        m_previewLabel->setPixmap(QPixmap());
        m_previewLabel->setText("No template");
        return;
    }
    cv::Mat preview = m_plugin->getTemplatePreview(200);
    if (preview.empty()) {
        m_previewLabel->setPixmap(QPixmap());
        m_previewLabel->setText("No template");
        return;
    }
    cv::Mat display;
    if (preview.channels() == 1) {
        cv::cvtColor(preview, display, cv::COLOR_GRAY2RGB);
    } else if (preview.channels() == 4) {
        cv::cvtColor(preview, display, cv::COLOR_BGRA2RGB);
    } else {
        cv::cvtColor(preview, display, cv::COLOR_BGR2RGB);
    }
    QImage qimg(display.data, display.cols, display.rows,
                static_cast<int>(display.step), QImage::Format_RGB888);
    QPixmap pix = QPixmap::fromImage(qimg.copy());
    m_previewLabel->setText(QString());
    m_previewLabel->setPixmap(pix.scaled(m_previewLabel->size(),
                                         Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void TemplateManagerDialog::onTeach()
{
    if (!m_plugin) return;

    int sx = 0;
    int sy = 0;
    int ex = 0;
    int ey = 0;
    if (!m_imageLabel->getSelection(sx, sy, ex, ey)) {
        QMessageBox::information(this, "Teach Template",
            "Drag a red rectangle on the image first.");
        return;
    }
    cv::Mat fullImage = VisionManager::instance().getInputImage();
    if (fullImage.empty()) {
        QMessageBox::warning(this, "Teach Template", "No source image available.");
        return;
    }

    cv::Rect roi(sx, sy, ex - sx, ey - sy);
    roi.x = std::max(0, roi.x);
    roi.y = std::max(0, roi.y);
    roi.width = std::min(fullImage.cols - roi.x, roi.width);
    roi.height = std::min(fullImage.rows - roi.y, roi.height);

    if (roi.width <= 0 || roi.height <= 0) {
        QMessageBox::warning(this, "Teach Template", "Invalid template region.");
        return;
    }

    cv::Mat templ = fullImage(roi).clone();
    if (m_plugin->teachFromImage(templ)) {
        // Mirror existing post-teach behaviour: anchor dialog + auto-save +
        // refresh of the main-page preview live in onTemplateTeachingComplete.
        m_plugin->onTemplateTeachingComplete();
        m_imageLabel->clearSelection();
        refreshPreview();
        m_statusLabel->setText("Template captured.");
    } else {
        QMessageBox::warning(this, "Teach Template",
            QString("Failed to teach template.\n%1").arg(QString::fromStdString(m_plugin->getLastError())));
    }
}

void TemplateManagerDialog::onClear()
{
    if (!m_plugin) return;
    m_plugin->clearTemplate();
    m_imageLabel->clearSelection();
    refreshPreview();
    m_statusLabel->setText("Template cleared.");
}

void TemplateManagerDialog::onSave()
{
    if (!m_plugin) return;
    if (!m_plugin->hasTemplate()) {
        QMessageBox::warning(this, "Save Error",
            "No template to save. Please teach a template first.");
        return;
    }
    QString fileName = QFileDialog::getSaveFileName(this,
        "Save Template", QString(), "PNG Images (*.png)");
    if (fileName.isEmpty()) return;

    if (m_plugin->saveTemplateToFile(fileName.toStdString())) {
        QMessageBox::information(this, "Template Saved", "Template saved successfully.");
    } else {
        QMessageBox::warning(this, "Save Error", "Failed to save template file.");
    }
}

void TemplateManagerDialog::onLoad()
{
    if (!m_plugin) return;
    QString fileName = QFileDialog::getOpenFileName(this,
        "Load Template", QString(), "PNG Images (*.png)");
    if (fileName.isEmpty()) return;

    if (m_plugin->loadTemplateFromFile(fileName.toStdString())) {
        refreshPreview();
        m_statusLabel->setText("Template loaded.");
    } else {
        QMessageBox::warning(this, "Load Error",
            QString("Failed to load template.\n%1").arg(QString::fromStdString(m_plugin->getLastError())));
    }
}

void TemplateManagerDialog::onAnchor()
{
    if (!m_plugin) return;
    if (!m_plugin->hasTemplate()) {
        QMessageBox::warning(this, "Anchor Point Error",
            "No template taught. Please teach a template first.");
        return;
    }
    m_plugin->showAnchorPointDialog();
}

#endif // HAS_OPENCV
