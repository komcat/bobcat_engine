// EdgeImageGalleryWidget.cpp
#include "EdgeImageGalleryWidget.h"
#include "ApplicationContext.h"
#include "product_position_manager.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QPixmap>
#include <QPainter>
#include <filesystem>

EdgeImageGalleryWidget::EdgeImageGalleryWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
{
    setupUI();
}

void EdgeImageGalleryWidget::setupUI()
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

    QLabel* title = new QLabel("Captured Edge Images");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    QLabel* desc = new QLabel(
        "Images captured at each point. Center 25% crop with red crosshair.");
    desc->setStyleSheet("color: #aaa; font-size: 12px;");
    desc->setWordWrap(true);
    mainLayout->addWidget(desc);

    const QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    const QString imageStyle =
        "QLabel { border: 1px solid #3d5a8c; border-radius: 4px; background-color: #0a0f1e; "
        "color: #555; font-size: 11px; }";

    const QString infoStyle =
        "font-size: 11px; color: #aaa; font-family: monospace; padding: 2px;";

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
    grid->addWidget(makeCell("Target P1 (P3)", m_tgtP1Image, m_tgtP1Info), 1, 0);
    grid->addWidget(makeCell("Target P2 (P4)", m_tgtP2Image, m_tgtP2Info), 1, 1);

    mainLayout->addLayout(grid);
    mainLayout->addStretch();

    scroll->setWidget(content);
    outerLayout->addWidget(scroll);
}

void EdgeImageGalleryWidget::SetData(const EdgeTransferSet& set)
{
    m_set = set;

    loadImage(m_refP1Image, m_refP1Info, set.referenceEdge.p1ImageFile, "Ref P1",
              set.referenceEdge.p1x, set.referenceEdge.p1y, set.referenceEdge.p1z);
    loadImage(m_refP2Image, m_refP2Info, set.referenceEdge.p2ImageFile, "Ref P2",
              set.referenceEdge.p2x, set.referenceEdge.p2y, set.referenceEdge.p2z);
    loadImage(m_tgtP1Image, m_tgtP1Info, set.targetEdge.p1ImageFile, "Tgt P1",
              set.targetEdge.p1x, set.targetEdge.p1y, set.targetEdge.p1z);
    loadImage(m_tgtP2Image, m_tgtP2Info, set.targetEdge.p2ImageFile, "Tgt P2",
              set.targetEdge.p2x, set.targetEdge.p2y, set.targetEdge.p2z);
}

void EdgeImageGalleryWidget::loadImage(QLabel* imageLabel, QLabel* infoLabel,
                                       const std::string& imageFile,
                                       const QString& pointName,
                                       double px, double py, double pz)
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

    QPixmap scaled = cropped.scaled(imageLabel->size(),
                                    Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation);

    // Red crosshair
    QPainter painter(&scaled);
    painter.setPen(QPen(QColor(255, 0, 0), 1));
    int cx = scaled.width() / 2;
    int cy = scaled.height() / 2;
    painter.drawLine(cx, 0, cx, scaled.height());
    painter.drawLine(0, cy, scaled.width(), cy);
    painter.end();

    imageLabel->setText("");
    imageLabel->setPixmap(scaled);

    infoLabel->setText(QString("X=%1  Y=%2  Z=%3\n%4")
        .arg(px, 0, 'f', 4).arg(py, 0, 'f', 4).arg(pz, 0, 'f', 4)
        .arg(QString::fromStdString(imageFile)));
}

std::string EdgeImageGalleryWidget::getImageDirectory() const
{
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        std::filesystem::path productPath(
            m_context->productPositionManager->GetCurrentProductPath());
        return (productPath.parent_path() / "images" / "trainer").string();
    }
    return "config/images/trainer";
}
