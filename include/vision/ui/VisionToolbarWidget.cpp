// VisionToolbarWidget.cpp
#include "VisionToolbarWidget.h"
#include <QFrame>

VisionToolbarWidget::VisionToolbarWidget(QWidget* parent)
    : QWidget(parent)
    , m_titleLabel(nullptr)
    , m_newBtn(nullptr)
    , m_loadBtn(nullptr)
    , m_saveBtn(nullptr)
    , m_saveAsBtn(nullptr)
    , m_runBtn(nullptr)
    , m_stopBtn(nullptr)
{
    setupUI();
}

void VisionToolbarWidget::setupUI()
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 5, 10, 5);
    layout->setSpacing(8);

    // Recipe management buttons
    m_newBtn = new QPushButton("New", this);
    m_newBtn->setToolTip("Create new vision recipe");
    m_newBtn->setMinimumWidth(80);

    m_loadBtn = new QPushButton("Load", this);
    m_loadBtn->setToolTip("Load vision recipe from file");
    m_loadBtn->setMinimumWidth(80);

    m_saveBtn = new QPushButton("Save", this);
    m_saveBtn->setToolTip("Save current recipe");
    m_saveBtn->setMinimumWidth(80);

    m_saveAsBtn = new QPushButton("Save As", this);
    m_saveAsBtn->setToolTip("Save recipe to new file");
    m_saveAsBtn->setMinimumWidth(80);

    // Separator
    QFrame* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);

    // Execution buttons
    m_runBtn = new QPushButton("Run Recipe", this);
    m_runBtn->setToolTip("Execute all recipe steps");
    m_runBtn->setMinimumWidth(100);
    m_runBtn->setStyleSheet("QPushButton { background-color: #2E7D32; color: white; font-weight: bold; }");

    m_stopBtn = new QPushButton("Stop", this);
    m_stopBtn->setToolTip("Stop recipe execution");
    m_stopBtn->setMinimumWidth(80);
    m_stopBtn->setStyleSheet("QPushButton { background-color: #C62828; color: white; font-weight: bold; }");
    m_stopBtn->setEnabled(false);  // Disabled until recipe is running

    // Title label (shows current profile/recipe name)
    m_titleLabel = new QLabel("", this);
    m_titleLabel->setStyleSheet("font-size: 12px; color: #90CAF9; font-style: italic;");

    // Add widgets to layout
    layout->addWidget(m_newBtn);
    layout->addWidget(m_loadBtn);
    layout->addWidget(m_saveBtn);
    layout->addWidget(m_saveAsBtn);
    layout->addWidget(separator);
    layout->addWidget(m_runBtn);
    layout->addWidget(m_stopBtn);
    layout->addSpacing(20);
    layout->addWidget(m_titleLabel);
    layout->addStretch();

    // Connect signals (mockup - just emit signals)
    connect(m_newBtn, &QPushButton::clicked, this, &VisionToolbarWidget::newRecipeClicked);
    connect(m_loadBtn, &QPushButton::clicked, this, &VisionToolbarWidget::loadRecipeClicked);
    connect(m_saveBtn, &QPushButton::clicked, this, &VisionToolbarWidget::saveRecipeClicked);
    connect(m_saveAsBtn, &QPushButton::clicked, this, &VisionToolbarWidget::saveAsRecipeClicked);
    connect(m_runBtn, &QPushButton::clicked, this, &VisionToolbarWidget::runRecipeClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &VisionToolbarWidget::stopClicked);

    // Set fixed height for toolbar
    setFixedHeight(50);
}

void VisionToolbarWidget::setTitle(const QString& title)
{
    if (m_titleLabel) {
        // Extract just the filename part if it's a full title
        if (title.contains(" - ")) {
            QString fileName = title.section(" - ", 1);
            m_titleLabel->setText("Current: " + fileName);
        } else if (title != "Vision Manager") {
            m_titleLabel->setText("Current: " + title);
        } else {
            m_titleLabel->setText("");
        }
    }
}
