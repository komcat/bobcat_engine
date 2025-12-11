// ContextImageViewer.cpp
#include "ContextImageViewer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QPixmap>
#include <QScreen>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QShowEvent>
#include <iostream>

#ifdef HAS_OPENCV
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#endif

ContextImageViewer::ContextImageViewer(const QString& title, QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowStaysOnTopHint)
    , m_title(title)
    , m_autoCloseTimer(nullptr)
{
    setWindowTitle(title.isEmpty() ? "Context Image" : title);
    setModal(false);  // Non-modal (non-blocking)
    setAttribute(Qt::WA_DeleteOnClose);  // Auto-delete when closed

    setupUI();

    // Default size
    resize(800, 600);

    // Center on screen
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        QRect screenGeometry = screen->availableGeometry();
        int x = (screenGeometry.width() - width()) / 2;
        int y = (screenGeometry.height() - height()) / 2;
        move(x, y);
    }
}

void ContextImageViewer::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // Scroll area for image (allows viewing large images)
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setStyleSheet("QScrollArea { background-color: #1e1e1e; border: 1px solid #3f3f46; }");

    // Image label
    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("QLabel { background-color: #1e1e1e; color: #d4d4d4; }");
    m_imageLabel->setText("No image loaded");
    m_imageLabel->setMinimumSize(100, 100);
    m_scrollArea->setWidget(m_imageLabel);

    mainLayout->addWidget(m_scrollArea, 1);

    // Bottom button bar
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_closeButton = new QPushButton("Close", this);
    m_closeButton->setStyleSheet(
        "QPushButton { background-color: #0e639c; color: white; border: none; "
        "padding: 8px 20px; font-weight: bold; border-radius: 4px; }"
        "QPushButton:hover { background-color: #1177bb; }"
    );
    connect(m_closeButton, &QPushButton::clicked, this, &ContextImageViewer::onCloseClicked);
    buttonLayout->addWidget(m_closeButton);

    mainLayout->addLayout(buttonLayout);

    // Auto-close timer
    m_autoCloseTimer = new QTimer(this);
    m_autoCloseTimer->setSingleShot(true);
    connect(m_autoCloseTimer, &QTimer::timeout, this, &ContextImageViewer::onAutoClose);
}

#ifdef HAS_OPENCV
void ContextImageViewer::setImage(const cv::Mat& image)
{
    if (image.empty()) {
        m_imageLabel->setText("Empty image");
        return;
    }

    // Convert cv::Mat to QImage
    cv::Mat rgbImage;
    if (image.channels() == 1) {
        cv::cvtColor(image, rgbImage, cv::COLOR_GRAY2RGB);
    } else if (image.channels() == 3) {
        cv::cvtColor(image, rgbImage, cv::COLOR_BGR2RGB);
    } else if (image.channels() == 4) {
        cv::cvtColor(image, rgbImage, cv::COLOR_BGRA2RGBA);
    } else {
        rgbImage = image.clone();
    }

    QImage qImage(rgbImage.data, rgbImage.cols, rgbImage.rows,
                  static_cast<int>(rgbImage.step), QImage::Format_RGB888);

    m_pixmap = QPixmap::fromImage(qImage.copy());  // Deep copy

    // Resize window to fit image (with reasonable bounds)
    int imgWidth = image.cols;
    int imgHeight = image.rows;

    // Get screen size for max bounds
    QSize maxSize(1200, 900);
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        QRect screenGeometry = screen->availableGeometry();
        maxSize = QSize(screenGeometry.width() * 0.8, screenGeometry.height() * 0.8);
    }

    // Calculate window size to fit image with some padding for buttons
    int winWidth = std::min(std::max(imgWidth + 40, 400), maxSize.width());
    int winHeight = std::min(std::max(imgHeight + 100, 300), maxSize.height());

    resize(winWidth, winHeight);
    updateImageDisplay();

    std::cout << "[ContextImageViewer] Image set: " << image.cols << "x" << image.rows
              << ", window: " << winWidth << "x" << winHeight << std::endl;
}
#endif

void ContextImageViewer::setImageFromFile(const QString& filePath)
{
    if (filePath.isEmpty()) {
        m_imageLabel->setText("No file path provided");
        return;
    }

#ifdef HAS_OPENCV
    cv::Mat image = cv::imread(filePath.toStdString());
    if (!image.empty()) {
        setImage(image);
        return;
    }
#endif

    // Fallback to Qt image loading
    QImage qImage(filePath);
    if (qImage.isNull()) {
        m_imageLabel->setText("Failed to load image: " + filePath);
        return;
    }

    m_pixmap = QPixmap::fromImage(qImage);
    updateImageDisplay();

    std::cout << "[ContextImageViewer] Image loaded from file: " << filePath.toStdString() << std::endl;
}

void ContextImageViewer::setAutoCloseMs(int ms)
{
    if (ms > 0) {
        m_autoCloseTimer->start(ms);
    } else {
        m_autoCloseTimer->stop();
    }
}

void ContextImageViewer::updateImageDisplay()
{
    if (m_pixmap.isNull()) {
        m_imageLabel->setText("No image");
        return;
    }

    // Scale to fit window while maintaining aspect ratio
    QSize availableSize = m_scrollArea->viewport()->size() - QSize(20, 20);

    // Ensure minimum display size
    if (availableSize.width() < 400) availableSize.setWidth(400);
    if (availableSize.height() < 300) availableSize.setHeight(300);

    // Always scale to fit available area (both up and down)
    QPixmap scaledPixmap = m_pixmap.scaled(availableSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    m_imageLabel->setPixmap(scaledPixmap);
    m_imageLabel->setMinimumSize(scaledPixmap.size());
    m_imageLabel->adjustSize();
}

void ContextImageViewer::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (!m_pixmap.isNull()) {
        updateImageDisplay();
    }
}

void ContextImageViewer::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (!m_pixmap.isNull()) {
        // Delay update slightly to ensure layout is complete
        QTimer::singleShot(50, this, &ContextImageViewer::updateImageDisplay);
    }
}

void ContextImageViewer::onCloseClicked()
{
    close();
}

void ContextImageViewer::onAutoClose()
{
    std::cout << "[ContextImageViewer] Auto-closing window" << std::endl;
    close();
}

#ifdef HAS_OPENCV
void ContextImageViewer::ShowImage(const cv::Mat& image,
                                    const QString& title,
                                    int autoCloseMs,
                                    QWidget* parent)
{
    ContextImageViewer* viewer = new ContextImageViewer(title, parent);
    viewer->setImage(image);
    if (autoCloseMs > 0) {
        viewer->setAutoCloseMs(autoCloseMs);
    }
    viewer->show();
    viewer->raise();
    viewer->activateWindow();
}
#endif

void ContextImageViewer::ShowContextImage(const QString& contextKey,
                                           const QString& title,
                                           int autoCloseMs,
                                           QWidget* parent)
{
    // This is a placeholder - actual implementation will be called from VisionCommandHandler
    // which has access to the SequenceContext
    std::cout << "[ContextImageViewer] ShowContextImage called for key: "
              << contextKey.toStdString() << std::endl;
}
