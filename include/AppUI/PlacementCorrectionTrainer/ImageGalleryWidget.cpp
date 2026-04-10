// ImageGalleryWidget.cpp - Gallery view for saved reference images
#include "ImageGalleryWidget.h"
#include "ApplicationContext.h"
#include "product_position_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QPixmap>
#include <QPainter>
#include <filesystem>

ImageGalleryWidget::ImageGalleryWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
{
    setupUI();
}

void ImageGalleryWidget::setupUI()
{
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    QWidget* content = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Reference Images");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    QLabel* desc = new QLabel("Captured reference images from teach steps. Center 25% crop with crosshair overlay.");
    desc->setStyleSheet("color: #aaa; font-size: 12px;");
    desc->setWordWrap(true);
    mainLayout->addWidget(desc);

    QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    QString imageStyle =
        "QLabel { border: 1px solid #3d5a8c; border-radius: 4px; background-color: #0a0f1e; "
        "color: #555; font-size: 11px; }";

    QString infoStyle = "font-size: 11px; color: #aaa; font-family: monospace; padding: 2px;";

    // 2x2 grid
    QGridLayout* grid = new QGridLayout();
    grid->setSpacing(10);

    auto makeCell = [&](const QString& title, QLabel*& imageLabel, QLabel*& infoLabel) {
        QGroupBox* group = new QGroupBox(title);
        group->setStyleSheet(groupStyle);
        QVBoxLayout* layout = new QVBoxLayout(group);

        imageLabel = new QLabel("No image");
        imageLabel->setFixedSize(360, 270);
        imageLabel->setAlignment(Qt::AlignCenter);
        imageLabel->setStyleSheet(imageStyle);
        layout->addWidget(imageLabel, 0, Qt::AlignCenter);

        infoLabel = new QLabel("---");
        infoLabel->setStyleSheet(infoStyle);
        infoLabel->setWordWrap(true);
        layout->addWidget(infoLabel);

        return group;
    };

    grid->addWidget(makeCell("Reference P1", m_refP1Image, m_refP1Info), 0, 0);
    grid->addWidget(makeCell("Reference P2", m_refP2Image, m_refP2Info), 0, 1);
    grid->addWidget(makeCell("Target P1", m_tgtP1Image, m_tgtP1Info), 1, 0);
    grid->addWidget(makeCell("Target P2", m_tgtP2Image, m_tgtP2Info), 1, 1);

    mainLayout->addLayout(grid);
    mainLayout->addStretch();

    scroll->setWidget(content);
    outerLayout->addWidget(scroll);
}

void ImageGalleryWidget::SetData(const PlacementCorrectionSet& set)
{
    m_set = set;

    loadImage(m_refP1Image, m_refP1Info, set.referenceLine.p1ImageFile, "Ref P1",
              set.referenceLine.p1x, set.referenceLine.p1y, set.referenceLine.p1z,
              set.referenceLine.p1Exposure);

    loadImage(m_refP2Image, m_refP2Info, set.referenceLine.p2ImageFile, "Ref P2",
              set.referenceLine.p2x, set.referenceLine.p2y, set.referenceLine.p2z,
              set.referenceLine.p2Exposure);

    loadImage(m_tgtP1Image, m_tgtP1Info, set.targetLine.p1ImageFile, "Tgt P1",
              set.targetLine.p1x, set.targetLine.p1y, set.targetLine.p1z,
              set.targetLine.p1Exposure);

    loadImage(m_tgtP2Image, m_tgtP2Info, set.targetLine.p2ImageFile, "Tgt P2",
              set.targetLine.p2x, set.targetLine.p2y, set.targetLine.p2z,
              set.targetLine.p2Exposure);
}

void ImageGalleryWidget::loadImage(QLabel* imageLabel, QLabel* infoLabel,
                                   const std::string& imageFile, const QString& pointName,
                                   double px, double py, double pz, double exposure)
{
    if (!imageLabel || !infoLabel) return;

    if (imageFile.empty()) {
        imageLabel->setText("No image captured");
        imageLabel->setPixmap(QPixmap());
        infoLabel->setText(QString("%1: no image").arg(pointName));
        return;
    }

    std::string fullPath = getImageDirectory() + "/" + imageFile;
    QPixmap pixmap(QString::fromStdString(fullPath));
    if (pixmap.isNull()) {
        imageLabel->setText("Image not found");
        infoLabel->setText(QString("%1: %2 (missing)")
            .arg(pointName).arg(QString::fromStdString(imageFile)));
        return;
    }

    // Crop center 25%
    int cropW = pixmap.width() / 2;
    int cropH = pixmap.height() / 2;
    int cropX = (pixmap.width() - cropW) / 2;
    int cropY = (pixmap.height() - cropH) / 2;
    QPixmap cropped = pixmap.copy(cropX, cropY, cropW, cropH);

    QPixmap scaled = cropped.scaled(imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Draw crosshair
    QPainter painter(&scaled);
    painter.setPen(QPen(QColor(255, 0, 0), 1));
    int cx = scaled.width() / 2;
    int cy = scaled.height() / 2;
    painter.drawLine(cx, 0, cx, scaled.height());
    painter.drawLine(0, cy, scaled.width(), cy);
    painter.end();

    imageLabel->setText("");
    imageLabel->setPixmap(scaled);

    infoLabel->setText(QString("X=%1  Y=%2  Z=%3  Exp=%4us\n%5")
        .arg(px, 0, 'f', 4).arg(py, 0, 'f', 4).arg(pz, 0, 'f', 4)
        .arg(static_cast<int>(exposure))
        .arg(QString::fromStdString(imageFile)));
}

std::string ImageGalleryWidget::getImageDirectory() const
{
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        std::filesystem::path productPath(
            m_context->productPositionManager->GetCurrentProductPath());
        return (productPath.parent_path() / "images" / "trainer").string();
    }
    return "config/images/trainer";
}
