// VisionHistoryWidget.cpp
#include "VisionHistoryWidget.h"

#include <QFile>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <iostream>
#include <string>

#ifdef HAS_OPENCV
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace {
constexpr const char* kTempImagePath = "logs/vision_result_temp.png";
}

VisionHistoryWidget::VisionHistoryWidget(QWidget* parent)
    : QGroupBox("Vision Result", parent)
{
    setStyleSheet(
        "QGroupBox { "
        "  font-weight: bold; "
        "  color: #FFA500; "
        "  border: 1px solid #3f3f46; "
        "  border-radius: 4px; "
        "  margin-top: 10px; "
        "  padding-top: 10px; "
        "} "
        "QGroupBox::title { "
        "  subcontrol-origin: margin; "
        "  left: 10px; "
        "  padding: 0 5px; "
        "}");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 15, 5, 5);

    m_imageLabel = new QLabel("No vision result");
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setMinimumHeight(300);
    m_imageLabel->setStyleSheet(
        "QLabel { "
        "  background-color: #0f0f0f; "
        "  border: 1px solid #3f3f46; "
        "  color: #808080; "
        "  font-style: italic; "
        "}");
    layout->addWidget(m_imageLabel, 1);

    auto* navWidget = new QWidget(this);
    auto* navLayout = new QHBoxLayout(navWidget);
    navLayout->setContentsMargins(0, 5, 0, 0);
    navLayout->setSpacing(10);

    m_prevButton = new QPushButton(QStringLiteral("◀ Prev"), navWidget);
    m_prevButton->setFixedWidth(70);
    m_prevButton->setEnabled(false);
    m_prevButton->setStyleSheet(
        "QPushButton { background-color: #2d2d30; color: #cccccc; border: 1px solid #3f3f46; padding: 4px 8px; }"
        "QPushButton:hover { background-color: #3d3d40; }"
        "QPushButton:disabled { color: #606060; background-color: #1e1e1e; }");
    connect(m_prevButton, &QPushButton::clicked, this, &VisionHistoryWidget::onPrevClicked);

    m_indexLabel = new QLabel("- / -", navWidget);
    m_indexLabel->setAlignment(Qt::AlignCenter);
    m_indexLabel->setMinimumWidth(60);
    m_indexLabel->setStyleSheet("QLabel { color: #808080; }");

    m_nextButton = new QPushButton(QStringLiteral("Next ▶"), navWidget);
    m_nextButton->setFixedWidth(70);
    m_nextButton->setEnabled(false);
    m_nextButton->setStyleSheet(
        "QPushButton { background-color: #2d2d30; color: #cccccc; border: 1px solid #3f3f46; padding: 4px 8px; }"
        "QPushButton:hover { background-color: #3d3d40; }"
        "QPushButton:disabled { color: #606060; background-color: #1e1e1e; }");
    connect(m_nextButton, &QPushButton::clicked, this, &VisionHistoryWidget::onNextClicked);

    navLayout->addStretch();
    navLayout->addWidget(m_prevButton);
    navLayout->addWidget(m_indexLabel);
    navLayout->addWidget(m_nextButton);
    navLayout->addStretch();

    layout->addWidget(navWidget);
}

void VisionHistoryWidget::AddImage(const QString& imagePath)
{
    if (m_history.size() >= static_cast<size_t>(kMaxHistory)) {
        m_history.erase(m_history.begin());
    }
    m_history.push_back(imagePath);
    m_viewIndex = -1;  // Show latest.
    updateNavigation();
    loadImage(static_cast<int>(m_history.size()) - 1);
}

void VisionHistoryWidget::Clear()
{
    m_history.clear();
    m_viewIndex = -1;
    updateNavigation();
}

void VisionHistoryWidget::RefreshFromTempFile()
{
    if (!m_imageLabel) return;
    renderPixmapFromPath(QString::fromUtf8(kTempImagePath), /*isHistoryView=*/false);
}

void VisionHistoryWidget::ClearAndResetDisplay()
{
    // Remove the temp file the worker writes to, then put the placeholder
    // text back and drop the history.
    const QString tempPath = QString::fromUtf8(kTempImagePath);
    if (QFile::exists(tempPath)) {
        QFile::remove(tempPath);
    }
    if (m_imageLabel) {
        m_imageLabel->clear();
        m_imageLabel->setText("No vision result");
        m_imageLabel->setStyleSheet(
            "QLabel { "
            "  background-color: #0f0f0f; "
            "  border: 1px solid #3f3f46; "
            "  color: #808080; "
            "  font-style: italic; "
            "}");
    }
    Clear();
}

void VisionHistoryWidget::onPrevClicked()
{
    if (m_history.empty()) return;
    int idx = m_viewIndex;
    if (idx == -1) {
        idx = static_cast<int>(m_history.size()) - 2;  // second-to-last
    } else if (idx > 0) {
        --idx;
    }
    if (idx >= 0) {
        m_viewIndex = idx;
        loadImage(idx);
        updateNavigation();
    }
}

void VisionHistoryWidget::onNextClicked()
{
    if (m_history.empty()) return;
    int idx = m_viewIndex;
    if (idx == -1) return;  // already latest

    if (idx < static_cast<int>(m_history.size()) - 1) {
        ++idx;
        m_viewIndex = (idx == static_cast<int>(m_history.size()) - 1) ? -1 : idx;
        loadImage(idx);
        updateNavigation();
    }
}

void VisionHistoryWidget::updateNavigation()
{
    const int total = static_cast<int>(m_history.size());
    int displayIdx = m_viewIndex;
    if (displayIdx == -1) displayIdx = total - 1;

    if (total > 0) {
        m_indexLabel->setText(QString("%1 / %2").arg(displayIdx + 1).arg(total));
        m_indexLabel->setStyleSheet("QLabel { color: #FFA500; font-weight: bold; }");
    } else {
        m_indexLabel->setText("- / -");
        m_indexLabel->setStyleSheet("QLabel { color: #808080; }");
    }

    const bool canPrev = total > 1 && (m_viewIndex != 0);
    const bool canNext = total > 1 && (m_viewIndex != -1);
    m_prevButton->setEnabled(canPrev);
    m_nextButton->setEnabled(canNext);
}

void VisionHistoryWidget::loadImage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_history.size())) return;
    renderPixmapFromPath(m_history[index], /*isHistoryView=*/(m_viewIndex != -1));
}

void VisionHistoryWidget::renderPixmapFromPath(const QString& path, bool isHistoryView)
{
#ifdef HAS_OPENCV
    cv::Mat img = cv::imread(path.toStdString(), cv::IMREAD_COLOR);
    if (img.empty()) {
        std::cout << "[VisionHistoryWidget] Failed to load image: "
                  << path.toStdString() << std::endl;
        return;
    }
    cv::Mat rgb;
    cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);
    QImage qImage(rgb.data, rgb.cols, rgb.rows,
                   static_cast<int>(rgb.step), QImage::Format_RGB888);
    qImage = qImage.copy();  // detach from cv::Mat memory

    QPixmap pixmap = QPixmap::fromImage(qImage);
    pixmap = pixmap.scaled(m_imageLabel->size(),
                            Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(pixmap);

    const QString border = isHistoryView ? "#FF6B00" : "#FFA500";
    m_imageLabel->setStyleSheet(
        QString("QLabel { background-color: #0f0f0f; border: 2px solid %1; }").arg(border));
#else
    (void)path;
    (void)isHistoryView;
    if (m_imageLabel) m_imageLabel->setText("OpenCV not available");
#endif
}
