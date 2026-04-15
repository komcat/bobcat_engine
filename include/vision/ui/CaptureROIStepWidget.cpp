// CaptureROIStepWidget.cpp
// Step 2: Image capture & ROI configuration for vision wizard
#include "CaptureROIStepWidget.h"
#include "VisionImageWidget.h"
#include "VisionWizardState.h"
#include <QFormLayout>
#include <QGridLayout>
#include <QFrame>
#include <QSizePolicy>
#include <iostream>

// ============================================================
// Constructor
// ============================================================

CaptureROIStepWidget::CaptureROIStepWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_state(nullptr)
    , m_imagePlaceholder(nullptr)
    , m_imagePlaceholderLayout(nullptr)
    , m_captureBtn(nullptr)
    , m_clearBtn(nullptr)
    , m_crosshairToggle(nullptr)
    , m_roiEnabledCheck(nullptr)
    , m_roiStartXSpin(nullptr)
    , m_roiStartYSpin(nullptr)
    , m_roiEndXSpin(nullptr)
    , m_roiEndYSpin(nullptr)
    , m_roiSizeLabel(nullptr)
    , m_searchBoxEnabledCheck(nullptr)
    , m_searchBoxStartXSpin(nullptr)
    , m_searchBoxStartYSpin(nullptr)
    , m_searchBoxEndXSpin(nullptr)
    , m_searchBoxEndYSpin(nullptr)
    , m_searchBoxSizeLabel(nullptr)
{
    setupUI();
}

// ============================================================
// Public API
// ============================================================

void CaptureROIStepWidget::setState(VisionWizardState* state)
{
    m_state = state;
}

void CaptureROIStepWidget::embedImageWidget(VisionImageWidget* imgWidget)
{
    if (!imgWidget || !m_imagePlaceholderLayout) {
        return;
    }
    m_imagePlaceholderLayout->addWidget(imgWidget);
    imgWidget->show();
}

void CaptureROIStepWidget::removeImageWidget()
{
    if (!m_imagePlaceholderLayout) {
        return;
    }
    // Remove all items without deleting — the image widget is owned by the parent page
    for (int i = m_imagePlaceholderLayout->count() - 1; i >= 0; --i) {
        QLayoutItem* item = m_imagePlaceholderLayout->takeAt(i);
        if (item) {
            QWidget* w = item->widget();
            if (w) {
                w->setParent(nullptr);
            }
            delete item;
        }
    }
}

// -------- ROI --------

void CaptureROIStepWidget::setROI(int startX, int startY, int endX, int endY)
{
    m_roiStartXSpin->blockSignals(true);
    m_roiStartYSpin->blockSignals(true);
    m_roiEndXSpin->blockSignals(true);
    m_roiEndYSpin->blockSignals(true);

    m_roiStartXSpin->setValue(startX);
    m_roiStartYSpin->setValue(startY);
    m_roiEndXSpin->setValue(endX);
    m_roiEndYSpin->setValue(endY);

    m_roiStartXSpin->blockSignals(false);
    m_roiStartYSpin->blockSignals(false);
    m_roiEndXSpin->blockSignals(false);
    m_roiEndYSpin->blockSignals(false);

    updateROISizeLabel();
}

void CaptureROIStepWidget::setROIEnabled(bool enabled)
{
    m_roiEnabledCheck->blockSignals(true);
    m_roiEnabledCheck->setChecked(enabled);
    m_roiEnabledCheck->blockSignals(false);
}

bool CaptureROIStepWidget::isROIEnabled() const
{
    return m_roiEnabledCheck ? m_roiEnabledCheck->isChecked() : false;
}

// -------- Search Box --------

void CaptureROIStepWidget::setSearchBox(int startX, int startY, int endX, int endY)
{
    m_searchBoxStartXSpin->blockSignals(true);
    m_searchBoxStartYSpin->blockSignals(true);
    m_searchBoxEndXSpin->blockSignals(true);
    m_searchBoxEndYSpin->blockSignals(true);

    m_searchBoxStartXSpin->setValue(startX);
    m_searchBoxStartYSpin->setValue(startY);
    m_searchBoxEndXSpin->setValue(endX);
    m_searchBoxEndYSpin->setValue(endY);

    m_searchBoxStartXSpin->blockSignals(false);
    m_searchBoxStartYSpin->blockSignals(false);
    m_searchBoxEndXSpin->blockSignals(false);
    m_searchBoxEndYSpin->blockSignals(false);

    updateSearchBoxSizeLabel();
}

void CaptureROIStepWidget::setSearchBoxEnabled(bool enabled)
{
    m_searchBoxEnabledCheck->blockSignals(true);
    m_searchBoxEnabledCheck->setChecked(enabled);
    m_searchBoxEnabledCheck->blockSignals(false);
}

bool CaptureROIStepWidget::isSearchBoxEnabled() const
{
    return m_searchBoxEnabledCheck ? m_searchBoxEnabledCheck->isChecked() : false;
}

void CaptureROIStepWidget::getSearchBox(int& startX, int& startY, int& endX, int& endY) const
{
    startX = m_searchBoxStartXSpin->value();
    startY = m_searchBoxStartYSpin->value();
    endX   = m_searchBoxEndXSpin->value();
    endY   = m_searchBoxEndYSpin->value();
}

// ============================================================
// UI Construction
// ============================================================

void CaptureROIStepWidget::setupUI()
{
    // Outer layout: left config panel | right image area
    QHBoxLayout* outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // -------- Left config panel (fixed ~300px) --------
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFixedWidth(480);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { background-color: #2D2D2D; }");

    QWidget* panelWidget = new QWidget();
    panelWidget->setStyleSheet("background-color: #2D2D2D;");

    QVBoxLayout* panelLayout = new QVBoxLayout(panelWidget);
    panelLayout->setContentsMargins(8, 8, 8, 8);
    panelLayout->setSpacing(10);

    setupCaptureSection(panelLayout);

    // Separator line
    QFrame* sep1 = new QFrame(panelWidget);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFrameShadow(QFrame::Sunken);
    sep1->setStyleSheet("color: #555555;");
    panelLayout->addWidget(sep1);

    setupROISection(panelLayout);

    // Separator line
    QFrame* sep2 = new QFrame(panelWidget);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFrameShadow(QFrame::Sunken);
    sep2->setStyleSheet("color: #555555;");
    panelLayout->addWidget(sep2);

    setupSearchBoxSection(panelLayout);

    panelLayout->addStretch();

    scrollArea->setWidget(panelWidget);
    outerLayout->addWidget(scrollArea, 0);

    // -------- Right image area (flex) --------
    m_imagePlaceholder = new QWidget(this);
    m_imagePlaceholder->setStyleSheet("background-color: #1E1E1E;");
    m_imagePlaceholderLayout = new QVBoxLayout(m_imagePlaceholder);
    m_imagePlaceholderLayout->setContentsMargins(0, 0, 0, 0);
    m_imagePlaceholderLayout->setSpacing(0);

    outerLayout->addWidget(m_imagePlaceholder, 1);
}

void CaptureROIStepWidget::setupCaptureSection(QVBoxLayout* parentLayout)
{
    // Test Execute button
    m_captureBtn = new QPushButton("Test Execute Profile");
    m_captureBtn->setToolTip("Execute the vision profile with current parameters");
    m_captureBtn->setStyleSheet(
        "QPushButton { background-color: #2E7D32; color: white; font-weight: bold; "
        "padding: 10px; border-radius: 4px; font-size: 13px; }"
        "QPushButton:hover { background-color: #388E3C; }"
        "QPushButton:pressed { background-color: #1B5E20; }"
    );
    connect(m_captureBtn, &QPushButton::clicked, this, &CaptureROIStepWidget::captureClicked);

    parentLayout->addWidget(m_captureBtn);

    // These are unused in step 3 but kept to avoid null pointer issues
    m_clearBtn = nullptr;
    m_crosshairToggle = nullptr;
}

void CaptureROIStepWidget::setupROISection(QVBoxLayout* parentLayout)
{
    static const char* compactGroupStyle =
        "QGroupBox { color: #CCCCCC; border: 1px solid #444; border-radius: 3px; "
        "margin-top: 8px; padding: 4px 4px 2px 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 6px; font-weight: bold; font-size: 11px; }";

    // ---- ROI group (enable + coordinates in one box) ----
    QGroupBox* roiGroup = new QGroupBox("ROI");
    roiGroup->setStyleSheet(compactGroupStyle);
    QVBoxLayout* roiLayout = new QVBoxLayout(roiGroup);
    roiLayout->setContentsMargins(4, 2, 4, 2);
    roiLayout->setSpacing(3);

    m_roiEnabledCheck = new QCheckBox("Enable ROI");
    m_roiEnabledCheck->setToolTip("Enable cropping to Region of Interest before processing");
    connect(m_roiEnabledCheck, &QCheckBox::checkStateChanged,
            this, &CaptureROIStepWidget::onROIEnabledChanged);
    roiLayout->addWidget(m_roiEnabledCheck);

    // Two-column grid for X/Y fields
    auto makeSpinBox = [this](int defaultVal) {
        auto* spin = new QSpinBox();
        spin->setRange(0, 10000);
        spin->setSingleStep(10);
        spin->setValue(defaultVal);
        spin->setMaximumWidth(70);
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &CaptureROIStepWidget::onROIValueChanged);
        return spin;
    };

    QGridLayout* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);
    // Row 0: X1 | Y1
    grid->addWidget(new QLabel("X1:"), 0, 0);
    m_roiStartXSpin = makeSpinBox(0);
    grid->addWidget(m_roiStartXSpin, 0, 1);
    grid->addWidget(new QLabel("Y1:"), 0, 2);
    m_roiStartYSpin = makeSpinBox(0);
    grid->addWidget(m_roiStartYSpin, 0, 3);
    // Row 1: X2 | Y2
    grid->addWidget(new QLabel("X2:"), 1, 0);
    m_roiEndXSpin = makeSpinBox(640);
    grid->addWidget(m_roiEndXSpin, 1, 1);
    grid->addWidget(new QLabel("Y2:"), 1, 2);
    m_roiEndYSpin = makeSpinBox(480);
    grid->addWidget(m_roiEndYSpin, 1, 3);

    roiLayout->addLayout(grid);

    m_roiSizeLabel = new QLabel("640 x 480");
    m_roiSizeLabel->setStyleSheet("font-family: monospace; color: #888; font-size: 10px;");
    roiLayout->addWidget(m_roiSizeLabel);

    parentLayout->addWidget(roiGroup);
    updateROISizeLabel();
}

void CaptureROIStepWidget::setupSearchBoxSection(QVBoxLayout* parentLayout)
{
    static const char* compactGroupStyle =
        "QGroupBox { color: #CCCCCC; border: 1px solid #444; border-radius: 3px; "
        "margin-top: 8px; padding: 4px 4px 2px 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 6px; font-weight: bold; font-size: 11px; }";

    QGroupBox* searchGroup = new QGroupBox("Search Box (3x3)");
    searchGroup->setStyleSheet(compactGroupStyle);
    QVBoxLayout* searchLayout = new QVBoxLayout(searchGroup);
    searchLayout->setContentsMargins(4, 2, 4, 2);
    searchLayout->setSpacing(3);

    m_searchBoxEnabledCheck = new QCheckBox("Enable Search Box");
    m_searchBoxEnabledCheck->setToolTip("Enable 3x3 grid search pattern");
    connect(m_searchBoxEnabledCheck, &QCheckBox::checkStateChanged,
            this, &CaptureROIStepWidget::onSearchBoxEnabledChanged);
    searchLayout->addWidget(m_searchBoxEnabledCheck);

    QLabel* searchInfo = new QLabel("Iterates ROI through 9 grid positions.");
    searchInfo->setStyleSheet("color: #FFA500; font-size: 10px;");
    searchInfo->setWordWrap(true);
    searchLayout->addWidget(searchInfo);

    // Two-column grid for X/Y fields
    auto makeSpinBox = [this](int defaultVal) {
        auto* spin = new QSpinBox();
        spin->setRange(0, 10000);
        spin->setSingleStep(10);
        spin->setValue(defaultVal);
        spin->setMaximumWidth(70);
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &CaptureROIStepWidget::onSearchBoxValueChanged);
        return spin;
    };

    QGridLayout* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);
    // Row 0: X1 | Y1
    grid->addWidget(new QLabel("X1:"), 0, 0);
    m_searchBoxStartXSpin = makeSpinBox(0);
    grid->addWidget(m_searchBoxStartXSpin, 0, 1);
    grid->addWidget(new QLabel("Y1:"), 0, 2);
    m_searchBoxStartYSpin = makeSpinBox(0);
    grid->addWidget(m_searchBoxStartYSpin, 0, 3);
    // Row 1: X2 | Y2
    grid->addWidget(new QLabel("X2:"), 1, 0);
    m_searchBoxEndXSpin = makeSpinBox(800);
    grid->addWidget(m_searchBoxEndXSpin, 1, 1);
    grid->addWidget(new QLabel("Y2:"), 1, 2);
    m_searchBoxEndYSpin = makeSpinBox(600);
    grid->addWidget(m_searchBoxEndYSpin, 1, 3);

    searchLayout->addLayout(grid);

    m_searchBoxSizeLabel = new QLabel("800 x 600");
    m_searchBoxSizeLabel->setStyleSheet("font-family: monospace; color: #FFA500; font-size: 10px;");
    searchLayout->addWidget(m_searchBoxSizeLabel);

    parentLayout->addWidget(searchGroup);
    updateSearchBoxSizeLabel();
}

// ============================================================
// Size label helpers
// ============================================================

void CaptureROIStepWidget::updateROISizeLabel()
{
    if (!m_roiSizeLabel) {
        return;
    }

    int startX = m_roiStartXSpin->value();
    int startY = m_roiStartYSpin->value();
    int endX   = m_roiEndXSpin->value();
    int endY   = m_roiEndYSpin->value();

    int w    = endX - startX;
    int h    = endY - startY;
    int area = w * h;

    m_roiSizeLabel->setText(QString("%1 x %2  (%L3 px)").arg(w).arg(h).arg(area));
}

void CaptureROIStepWidget::updateSearchBoxSizeLabel()
{
    if (!m_searchBoxSizeLabel) {
        return;
    }

    int startX = m_searchBoxStartXSpin->value();
    int startY = m_searchBoxStartYSpin->value();
    int endX   = m_searchBoxEndXSpin->value();
    int endY   = m_searchBoxEndYSpin->value();

    int w    = endX - startX;
    int h    = endY - startY;
    int area = w * h;

    m_searchBoxSizeLabel->setText(QString("%1 x %2  (%L3 px)").arg(w).arg(h).arg(area));
}

// ============================================================
// Slots
// ============================================================

void CaptureROIStepWidget::onROIValueChanged()
{
    updateROISizeLabel();

    int startX = m_roiStartXSpin->value();
    int startY = m_roiStartYSpin->value();
    int endX   = m_roiEndXSpin->value();
    int endY   = m_roiEndYSpin->value();

    std::cout << "[CaptureROIStepWidget] ROI changed: ("
              << startX << "," << startY << ") to ("
              << endX << "," << endY << ")" << std::endl;

    emit roiChanged(startX, startY, endX, endY);
}

void CaptureROIStepWidget::onROIEnabledChanged(Qt::CheckState state)
{
    bool enabled = (state == Qt::Checked);
    std::cout << "[CaptureROIStepWidget] ROI " << (enabled ? "enabled" : "disabled") << std::endl;
    emit roiEnabledChanged(enabled);
}

void CaptureROIStepWidget::onSearchBoxValueChanged()
{
    updateSearchBoxSizeLabel();

    emit searchBoxChanged(
        m_searchBoxStartXSpin->value(),
        m_searchBoxStartYSpin->value(),
        m_searchBoxEndXSpin->value(),
        m_searchBoxEndYSpin->value()
    );
}

void CaptureROIStepWidget::onSearchBoxEnabledChanged(Qt::CheckState state)
{
    bool enabled = (state == Qt::Checked);
    std::cout << "[CaptureROIStepWidget] Search box " << (enabled ? "enabled" : "disabled") << std::endl;
    emit searchBoxEnabledChanged(enabled);
}
