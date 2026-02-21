// TemplateMatchingBase.cpp
// Common base class for template-based pattern matching plugins

#ifdef HAS_OPENCV

#include "TemplateMatchingBase.h"
#include "../ui/VisionManagerPage.h"
#include "../VisionManager.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QPixmap>
#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QPainter>
#include <QUuid>
#include <iostream>
#include <cmath>
#include <algorithm>

// TemplateAnchorPointWidget implementation
TemplateAnchorPointWidget::TemplateAnchorPointWidget(const QPixmap& templateImage,
                                                     int originalWidth, int originalHeight,
                                                     double initialOffsetX, double initialOffsetY,
                                                     QWidget* parent)
    : QLabel(parent)
    , m_templatePixmap(templateImage)
    , m_originalWidth(originalWidth)
    , m_originalHeight(originalHeight)
    , m_anchorOffsetX(initialOffsetX)
    , m_anchorOffsetY(initialOffsetY)
{
    // Calculate scale factors (preview size / original size)
    m_scaleX = (m_originalWidth > 0) ? static_cast<double>(m_templatePixmap.width()) / m_originalWidth : 1.0;
    m_scaleY = (m_originalHeight > 0) ? static_cast<double>(m_templatePixmap.height()) / m_originalHeight : 1.0;

    setAlignment(Qt::AlignCenter);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);

    // Set fixed size to match pixmap to avoid alignment offset issues
    setFixedSize(m_templatePixmap.size());

    updateDisplay();
}

void TemplateAnchorPointWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        updateAnchorFromMouse(event->pos());
    }
}

void TemplateAnchorPointWidget::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        updateAnchorFromMouse(event->pos());
    }
}

void TemplateAnchorPointWidget::updateAnchorFromMouse(const QPoint& pos) {
    // Get the offset of the pixmap within the label (in case of alignment padding)
    QPoint pixmapOffset = getPixmapOffset();

    // Convert mouse position to pixmap coordinates
    int pixmapX = pos.x() - pixmapOffset.x();
    int pixmapY = pos.y() - pixmapOffset.y();

    // Clamp to pixmap bounds
    pixmapX = std::max(0, std::min(pixmapX, m_templatePixmap.width() - 1));
    pixmapY = std::max(0, std::min(pixmapY, m_templatePixmap.height() - 1));

    // Convert from preview coordinates to original template coordinates
    double originalX = pixmapX / m_scaleX;
    double originalY = pixmapY / m_scaleY;

    // Calculate offset from center of ORIGINAL template
    double originalCenterX = m_originalWidth / 2.0;
    double originalCenterY = m_originalHeight / 2.0;

    m_anchorOffsetX = originalX - originalCenterX;
    m_anchorOffsetY = originalY - originalCenterY;

    updateDisplay();
    if (onAnchorChanged) {
        onAnchorChanged(m_anchorOffsetX, m_anchorOffsetY);
    }
}

QPoint TemplateAnchorPointWidget::getPixmapOffset() const {
    // Calculate the offset of the pixmap within the label due to alignment
    int offsetX = 0;
    int offsetY = 0;

    Qt::Alignment align = alignment();
    QSize labelSize = size();
    QSize pixmapSize = m_templatePixmap.size();

    if (align & Qt::AlignHCenter) {
        offsetX = (labelSize.width() - pixmapSize.width()) / 2;
    } else if (align & Qt::AlignRight) {
        offsetX = labelSize.width() - pixmapSize.width();
    }

    if (align & Qt::AlignVCenter) {
        offsetY = (labelSize.height() - pixmapSize.height()) / 2;
    } else if (align & Qt::AlignBottom) {
        offsetY = labelSize.height() - pixmapSize.height();
    }

    return QPoint(std::max(0, offsetX), std::max(0, offsetY));
}

void TemplateAnchorPointWidget::updateDisplay() {
    QPixmap display = m_templatePixmap.copy();
    QPainter painter(&display);

    // Convert anchor offset from ORIGINAL coordinates to PREVIEW coordinates
    // Anchor offset is relative to center of original template
    double originalCenterX = m_originalWidth / 2.0;
    double originalCenterY = m_originalHeight / 2.0;

    // Anchor position in original coordinates
    double anchorOriginalX = originalCenterX + m_anchorOffsetX;
    double anchorOriginalY = originalCenterY + m_anchorOffsetY;

    // Convert to preview coordinates
    int anchorX = static_cast<int>(anchorOriginalX * m_scaleX);
    int anchorY = static_cast<int>(anchorOriginalY * m_scaleY);

    // Clamp to preview bounds
    anchorX = std::max(0, std::min(anchorX, display.width() - 1));
    anchorY = std::max(0, std::min(anchorY, display.height() - 1));

    // Draw crosshair
    painter.setPen(QPen(Qt::green, 2));
    painter.drawLine(anchorX - 10, anchorY, anchorX + 10, anchorY);
    painter.drawLine(anchorX, anchorY - 10, anchorX, anchorY + 10);
    painter.drawEllipse(QPoint(anchorX, anchorY), 5, 5);

    setPixmap(display);
}

// TemplateMatchingBase implementation
TemplateMatchingBase::TemplateMatchingBase(const std::string& name, const std::string& category, const std::string& description)
    : VisionPluginBase(name, category, description)
{
    m_templateConfigWidget = nullptr;
    m_previewLabel = nullptr;
    m_templateInfoLabel = nullptr;
    m_templatePathLabel = nullptr;
    m_teachBtn = nullptr;
    m_saveTemplateBtn = nullptr;
    m_loadTemplateBtn = nullptr;
    m_clearTemplateBtn = nullptr;
    m_setAnchorBtn = nullptr;
}

TemplateMatchingBase::~TemplateMatchingBase() {
    // Widgets are owned by Qt parent, no need to delete
}

void TemplateMatchingBase::clearTemplate() {
    m_template = cv::Mat();
    m_templateFilePath.clear();
    m_anchorOffsetX = 0.0;
    m_anchorOffsetY = 0.0;
    m_useCustomAnchor = false;
    updatePreviewLabel();
}

cv::Mat TemplateMatchingBase::getTemplatePreview(int maxSize) const {
    if (!hasTemplate()) {
        return cv::Mat();
    }

    cv::Mat preview = m_template.clone();
    if (preview.cols > maxSize || preview.rows > maxSize) {
        double scale = std::min(maxSize / (double)preview.cols, maxSize / (double)preview.rows);
        cv::resize(preview, preview, cv::Size(), scale, scale);
    }

    return preview;
}

bool TemplateMatchingBase::saveTemplateToFile(const std::string& filePath) {
    if (!hasTemplate()) {
        return false;
    }

    bool success = cv::imwrite(filePath, m_template);
    if (success) {
        m_templateFilePath = filePath;
        std::cout << "[" << m_name << "] Template saved to: " << filePath << std::endl;
    }
    return success;
}

bool TemplateMatchingBase::loadTemplateFromFile(const std::string& filePath) {
    cv::Mat loadedTemplate = cv::imread(filePath, cv::IMREAD_UNCHANGED);
    if (loadedTemplate.empty()) {
        std::cout << "[" << m_name << "] Failed to load template from: " << filePath << std::endl;
        return false;
    }

    m_templateFilePath = filePath;
    return teachFromImage(loadedTemplate);
}

void TemplateMatchingBase::setAnchorOffset(double x, double y) {
    m_anchorOffsetX = x;
    m_anchorOffsetY = y;
    m_useCustomAnchor = (std::abs(x) > 0.1 || std::abs(y) > 0.1);
}

void TemplateMatchingBase::resetTemplateWidgetPointers() {
    m_templateConfigWidget = nullptr;
    m_previewLabel = nullptr;
    m_templateInfoLabel = nullptr;
    m_templatePathLabel = nullptr;
    m_teachBtn = nullptr;
    m_saveTemplateBtn = nullptr;
    m_loadTemplateBtn = nullptr;
    m_clearTemplateBtn = nullptr;
    m_setAnchorBtn = nullptr;
}

QGroupBox* TemplateMatchingBase::createTemplateGroupWidget(QWidget* parent) {
    QGroupBox* templateGroup = new QGroupBox("Template", parent);
    m_templateConfigWidget = templateGroup;
    QVBoxLayout* templateLayout = new QVBoxLayout(templateGroup);

    // Connect to destroyed signal to reset our pointers
    QObject::connect(templateGroup, &QObject::destroyed, [this]() {
        resetTemplateWidgetPointers();
    });

    // Preview label
    m_previewLabel = new QLabel("No template", templateGroup);
    m_previewLabel->setMinimumSize(100, 100);
    m_previewLabel->setMaximumSize(150, 150);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet("QLabel { background-color: #2a2a2a; border: 1px solid #555; }");
    templateLayout->addWidget(m_previewLabel, 0, Qt::AlignCenter);

    // Template info label
    m_templateInfoLabel = new QLabel("Size: N/A", templateGroup);
    m_templateInfoLabel->setAlignment(Qt::AlignCenter);
    templateLayout->addWidget(m_templateInfoLabel);

    // Template path label
    m_templatePathLabel = new QLabel("File: Not saved", templateGroup);
    m_templatePathLabel->setAlignment(Qt::AlignCenter);
    m_templatePathLabel->setStyleSheet("QLabel { color: #888; font-size: 9px; }");
    templateLayout->addWidget(m_templatePathLabel);

    // Teach/Clear buttons row
    QHBoxLayout* teachButtonLayout = new QHBoxLayout();
    m_teachBtn = new QPushButton("Select Template Area", templateGroup);
    m_teachBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; }");
    m_teachBtn->setToolTip("Draw a red rectangle on the image to select template area");
    teachButtonLayout->addWidget(m_teachBtn);

    m_clearTemplateBtn = new QPushButton("Clear", templateGroup);
    m_clearTemplateBtn->setStyleSheet("QPushButton { background-color: #C62828; color: white; }");
    teachButtonLayout->addWidget(m_clearTemplateBtn);
    templateLayout->addLayout(teachButtonLayout);

    // Save/Load buttons row
    QHBoxLayout* fileButtonLayout = new QHBoxLayout();
    m_saveTemplateBtn = new QPushButton("Save...", templateGroup);
    m_saveTemplateBtn->setToolTip("Save template to PNG file");
    fileButtonLayout->addWidget(m_saveTemplateBtn);

    m_loadTemplateBtn = new QPushButton("Load...", templateGroup);
    m_loadTemplateBtn->setToolTip("Load template from PNG file");
    fileButtonLayout->addWidget(m_loadTemplateBtn);
    templateLayout->addLayout(fileButtonLayout);

    // Anchor point button
    m_setAnchorBtn = new QPushButton("Set Anchor Point...", templateGroup);
    m_setAnchorBtn->setToolTip("Set custom anchor point for pattern match reporting");
    m_setAnchorBtn->setStyleSheet("QPushButton { background-color: #388E3C; color: white; }");
    templateLayout->addWidget(m_setAnchorBtn);

    // Connect signals
    QObject::connect(m_teachBtn, &QPushButton::clicked, [this]() {
        startTemplateSelection();
    });

    QObject::connect(m_clearTemplateBtn, &QPushButton::clicked, [this]() {
        clearTemplate();
    });

    QObject::connect(m_saveTemplateBtn, &QPushButton::clicked, [this]() {
        if (!hasTemplate()) {
            if (m_templateConfigWidget) {
                QMessageBox::warning(m_templateConfigWidget, "Save Error",
                                     "No template to save. Please teach a template first.");
            }
            return;
        }

        QString fileName = QFileDialog::getSaveFileName(m_templateConfigWidget,
            "Save Template", "", "PNG Images (*.png)");

        if (!fileName.isEmpty()) {
            if (saveTemplateToFile(fileName.toStdString())) {
                QMessageBox::information(m_templateConfigWidget, "Template Saved", "Template saved successfully.");
                updatePreviewLabel();
            } else {
                QMessageBox::warning(m_templateConfigWidget, "Save Error", "Failed to save template file.");
            }
        }
    });

    QObject::connect(m_loadTemplateBtn, &QPushButton::clicked, [this]() {
        QString fileName = QFileDialog::getOpenFileName(m_templateConfigWidget,
            "Load Template", "", "PNG Images (*.png)");

        if (!fileName.isEmpty()) {
            if (loadTemplateFromFile(fileName.toStdString())) {
                QMessageBox::information(m_templateConfigWidget, "Template Loaded", "Template loaded successfully.");
            } else {
                QMessageBox::warning(m_templateConfigWidget, "Load Error",
                                     QString("Failed to load template.\n%1").arg(QString::fromStdString(m_lastError)));
            }
        }
    });

    QObject::connect(m_setAnchorBtn, &QPushButton::clicked, [this]() {
        if (!hasTemplate()) {
            if (m_templateConfigWidget) {
                QMessageBox::warning(m_templateConfigWidget, "Anchor Point Error",
                                     "No template taught. Please teach a template first.");
            }
            return;
        }
        showAnchorPointDialog();
    });

    // Update preview if we already have a template
    updatePreviewLabel();

    return templateGroup;
}

void TemplateMatchingBase::updatePreviewLabel() {
    if (!m_previewLabel || !m_templateInfoLabel) {
        return;
    }

    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        return;
    }

    if (!hasTemplate()) {
        m_previewLabel->setText("No template");
        m_templateInfoLabel->setText("Size: N/A");
        if (m_templatePathLabel) m_templatePathLabel->setText("File: Not saved");
        return;
    }

    cv::Mat preview = getTemplatePreview(150);
    if (!preview.empty()) {
        cv::Mat display;
        if (preview.channels() == 1) {
            cv::cvtColor(preview, display, cv::COLOR_GRAY2RGB);
        } else if (preview.channels() == 4) {
            cv::cvtColor(preview, display, cv::COLOR_BGRA2RGB);
        } else {
            cv::cvtColor(preview, display, cv::COLOR_BGR2RGB);
        }

        QImage qImage(display.data, display.cols, display.rows, display.step, QImage::Format_RGB888);
        m_previewLabel->setPixmap(QPixmap::fromImage(qImage.copy()));
    }

    QString sizeText = QString("Size: %1 x %2").arg(m_template.cols).arg(m_template.rows);
    m_templateInfoLabel->setText(sizeText);

    if (m_templatePathLabel) {
        if (!m_templateFilePath.empty()) {
            QFileInfo fileInfo(QString::fromStdString(m_templateFilePath));
            m_templatePathLabel->setText("File: " + fileInfo.fileName());
        } else {
            m_templatePathLabel->setText("File: Not saved");
        }
    }
}

void TemplateMatchingBase::showAnchorPointDialog() {
    if (!hasTemplate()) {
        return;
    }

    // Store original template dimensions before scaling
    int originalWidth = m_template.cols;
    int originalHeight = m_template.rows;

    cv::Mat preview = getTemplatePreview(300);
    if (preview.empty()) {
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

    QImage qImage(display.data, display.cols, display.rows, display.step, QImage::Format_RGB888);
    QPixmap pixmap = QPixmap::fromImage(qImage.copy());

    QDialog dialog(m_templateConfigWidget);
    dialog.setWindowTitle("Set Anchor Point");
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* instructionLabel = new QLabel("Click on the template to set the anchor point.\n"
                                         "The anchor point is where the match position will be reported.", &dialog);
    instructionLabel->setWordWrap(true);
    layout->addWidget(instructionLabel);

    // Pass original template dimensions to widget for proper coordinate conversion
    TemplateAnchorPointWidget* anchorWidget = new TemplateAnchorPointWidget(
        pixmap, originalWidth, originalHeight, m_anchorOffsetX, m_anchorOffsetY, &dialog);
    layout->addWidget(anchorWidget);

    QLabel* coordLabel = new QLabel(QString::asprintf("Offset: (%.1f, %.1f)", m_anchorOffsetX, m_anchorOffsetY), &dialog);
    layout->addWidget(coordLabel);

    anchorWidget->onAnchorChanged = [coordLabel](double x, double y) {
        coordLabel->setText(QString::asprintf("Offset: (%.1f, %.1f)", x, y));
    };

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() == QDialog::Accepted) {
        m_anchorOffsetX = anchorWidget->getAnchorOffsetX();
        m_anchorOffsetY = anchorWidget->getAnchorOffsetY();
        m_useCustomAnchor = (std::abs(m_anchorOffsetX) > 0.1 || std::abs(m_anchorOffsetY) > 0.1);

        std::cout << "[" << m_name << "] Anchor point set to offset ("
                  << m_anchorOffsetX << ", " << m_anchorOffsetY << ")" << std::endl;
    }
}

void TemplateMatchingBase::startTemplateSelection() {
    if (!m_visionManagerPage) {
        if (m_templateConfigWidget) {
            QMessageBox::warning(m_templateConfigWidget, "Template Selection Error",
                                 "Vision system not available. Please try again.");
        }
        return;
    }

    auto* visionPage = static_cast<VisionManagerPage*>(m_visionManagerPage);

    if (m_inTemplateSelectionMode) {
        visionPage->onTemplateSelectionConfirmed();
        return;
    }

    m_inTemplateSelectionMode = true;
    if (m_teachBtn) {
        m_teachBtn->setText("OK - Confirm Template");
        m_teachBtn->setStyleSheet("QPushButton { background-color: #2E7D32; color: white; font-weight: bold; }");
    }

    visionPage->startTemplateSelectionMode(
        [this](int startX, int startY, int endX, int endY) {
            onTemplateSelectionComplete(startX, startY, endX, endY);
        },
        [this]() {
            onTemplateSelectionCancelled();
        }
    );
}

void TemplateMatchingBase::onTemplateSelectionComplete(int startX, int startY, int endX, int endY) {
    cv::Mat fullImage = VisionManager::instance().getInputImage();
    if (fullImage.empty()) {
        if (m_templateConfigWidget) {
            QMessageBox::warning(m_templateConfigWidget, "Teaching Error",
                                 "No image available. Please capture an image first.");
        }
        m_inTemplateSelectionMode = false;
        if (m_teachBtn) {
            m_teachBtn->setText("Select Template Area");
            m_teachBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; }");
        }
        return;
    }

    cv::Rect templateROI(startX, startY, endX - startX, endY - startY);
    templateROI.x = std::max(0, templateROI.x);
    templateROI.y = std::max(0, templateROI.y);
    templateROI.width = std::min(fullImage.cols - templateROI.x, templateROI.width);
    templateROI.height = std::min(fullImage.rows - templateROI.y, templateROI.height);

    if (templateROI.width <= 0 || templateROI.height <= 0) {
        if (m_templateConfigWidget) {
            QMessageBox::warning(m_templateConfigWidget, "Teaching Error", "Invalid template region.");
        }
        m_inTemplateSelectionMode = false;
        if (m_teachBtn) {
            m_teachBtn->setText("Select Template Area");
            m_teachBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; }");
        }
        return;
    }

    cv::Mat templateImage = fullImage(templateROI).clone();

    if (teachFromImage(templateImage)) {
        onTemplateTeachingComplete();
    } else {
        if (m_templateConfigWidget) {
            QMessageBox::warning(m_templateConfigWidget, "Teaching Error",
                                 QString("Failed to create template.\n%1").arg(QString::fromStdString(m_lastError)));
        }
    }

    m_inTemplateSelectionMode = false;
    if (m_teachBtn) {
        m_teachBtn->setText("Select Template Area");
        m_teachBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; }");
    }
}

void TemplateMatchingBase::onTemplateSelectionCancelled() {
    m_inTemplateSelectionMode = false;
    if (m_teachBtn) {
        m_teachBtn->setText("Select Template Area");
        m_teachBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; }");
    }
}

void TemplateMatchingBase::onTemplateTeachingComplete() {
    // Show anchor point dialog first
    showAnchorPointDialog();

    // Auto-save if directory and profile name are set
    if (!m_templateDirectory.empty() && !m_profileName.empty()) {
        QString templatesDir = QString::fromStdString(m_templateDirectory);
        QDir dir(templatesDir);
        if (!dir.exists()) dir.mkpath(".");

        // Use UUID for unique template filenames (avoids overwriting on re-teach)
        QString uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString templateFileName = uid + QString::fromStdString(getTemplateFileSuffix());
        QString fullPath = templatesDir + "/" + templateFileName;

        if (saveTemplateToFile(fullPath.toStdString())) {
            std::cout << "[" << m_name << "] Template auto-saved to: " << fullPath.toStdString() << std::endl;
        }
    }

    // Update preview to show template thumbnail and file info
    updatePreviewLabel();
}

nlohmann::json TemplateMatchingBase::getTemplateParameters() const {
    nlohmann::json params;

    if (!m_templateFilePath.empty()) {
        params["templateFile"] = m_templateFilePath;
    }

    params["useCustomAnchor"] = m_useCustomAnchor;
    params["anchorOffsetX"] = m_anchorOffsetX;
    params["anchorOffsetY"] = m_anchorOffsetY;

    return params;
}

void TemplateMatchingBase::setTemplateParameters(const nlohmann::json& params) {
    if (params.contains("templateFile")) {
        std::string templatePath = params["templateFile"].get<std::string>();
        if (!templatePath.empty()) {
            loadTemplateFromFile(templatePath);
        }
    }

    if (params.contains("useCustomAnchor")) {
        m_useCustomAnchor = params["useCustomAnchor"].get<bool>();
    }
    if (params.contains("anchorOffsetX")) {
        m_anchorOffsetX = params["anchorOffsetX"].get<double>();
    }
    if (params.contains("anchorOffsetY")) {
        m_anchorOffsetY = params["anchorOffsetY"].get<double>();
    }
}

#endif // HAS_OPENCV
