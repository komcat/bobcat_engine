// TrainerContainerPage.cpp
#include "TrainerContainerPage.h"
#include "AccuracyTrainer/AccuracyTrainerPage.h"
#include "ApplicationContext.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <iostream>

TrainerContainerPage::TrainerContainerPage(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_contentStack(nullptr)
    , m_accuracyTrainerPage(nullptr)
    , m_cameraOffsetPlaceholder(nullptr)
    , m_activeIndex(0)
{
    setupUI();
}

void TrainerContainerPage::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ========================================
    // TOP BAR: Back button + title
    // ========================================
    QWidget* topBar = new QWidget();
    QHBoxLayout* topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(10, 5, 10, 5);

    QPushButton* backBtn = new QPushButton("Back to Main Menu");
    backBtn->setFixedHeight(32);
    backBtn->setStyleSheet(
        "QPushButton { background-color: #3d5a8c; color: white; font-weight: bold; "
        "padding: 5px 15px; border: 1px solid #4d6e9f; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4d6e9f; }"
    );
    connect(backBtn, &QPushButton::clicked, this, &TrainerContainerPage::backToMainMenu);
    topBarLayout->addWidget(backBtn);

    QLabel* titleLabel = new QLabel("Trainer");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff; padding: 0 15px;");
    topBarLayout->addWidget(titleLabel);
    topBarLayout->addStretch();

    mainLayout->addWidget(topBar);

    // Separator
    QFrame* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background-color: #3d5a8c;");
    sep->setFixedHeight(2);
    mainLayout->addWidget(sep);

    // ========================================
    // BODY: Side panel + content stack
    // ========================================
    QHBoxLayout* bodyLayout = new QHBoxLayout();
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    // --- Side panel ---
    QWidget* sidePanel = new QWidget();
    sidePanel->setFixedWidth(64);
    sidePanel->setStyleSheet("background-color: #141824;");
    QVBoxLayout* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(2, 4, 2, 4);
    sideLayout->setSpacing(4);

    // Button labels and enabled state
    struct ButtonDef {
        QString label;
        bool enabled;
    };
    const ButtonDef defs[] = {
        { "Pos\nAccuracy", true },
        { "Camera\nOffset", true },
        { "",               false },
        { "",               false },
        { "",               false },
    };

    QString btnStyle =
        "QPushButton {"
        "  background-color: #1a1f2e; color: #888; border: 1px solid #2d3e5f;"
        "  border-radius: 3px; font-size: 9px; padding: 6px 2px;"
        "}"
        "QPushButton:hover { background-color: #2d3e5f; color: #ccc; }"
        "QPushButton:checked { background-color: #3d5a8c; color: white; border-color: #5a8fc4; }"
        "QPushButton:disabled { background-color: #111520; color: #333; border-color: #1a1f2e; }";

    for (int i = 0; i < 5; ++i) {
        auto* btn = new QPushButton(defs[i].label);
        btn->setCheckable(true);
        btn->setEnabled(defs[i].enabled);
        btn->setFixedSize(58, 48);
        btn->setStyleSheet(btnStyle);
        int idx = i;
        connect(btn, &QPushButton::clicked, this, [this, idx]() {
            onSideButtonClicked(idx);
        });
        sideLayout->addWidget(btn);
        m_sideButtons.append(btn);
    }

    sideLayout->addStretch();
    bodyLayout->addWidget(sidePanel);

    // Vertical separator between side panel and content
    QFrame* vSep = new QFrame();
    vSep->setFrameShape(QFrame::VLine);
    vSep->setStyleSheet("background-color: #3d5a8c;");
    vSep->setFixedWidth(1);
    bodyLayout->addWidget(vSep);

    // --- Content stack ---
    m_contentStack = new QStackedWidget();

    // Index 0: AccuracyTrainerPage (with its own top bar hidden)
    m_accuracyTrainerPage = new AccuracyTrainerPage(m_context, this);
    m_accuracyTrainerPage->setTopBarVisible(false);
    m_contentStack->addWidget(m_accuracyTrainerPage);

    // Index 1: Camera Offset placeholder
    m_cameraOffsetPlaceholder = new QWidget();
    QVBoxLayout* placeholderLayout = new QVBoxLayout(m_cameraOffsetPlaceholder);
    placeholderLayout->setAlignment(Qt::AlignCenter);
    QLabel* placeholderLabel = new QLabel("Camera Offset Trainer\n\n(Coming soon)");
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setStyleSheet("color: #888; font-size: 16px;");
    placeholderLayout->addWidget(placeholderLabel);
    m_contentStack->addWidget(m_cameraOffsetPlaceholder);

    bodyLayout->addWidget(m_contentStack, 1);
    mainLayout->addLayout(bodyLayout, 1);

    // Select first button by default
    selectButton(0);
}

void TrainerContainerPage::onSideButtonClicked(int index)
{
    if (index < 0 || index >= m_sideButtons.size()) {
        return;
    }
    selectButton(index);
}

void TrainerContainerPage::selectButton(int index)
{
    m_activeIndex = index;

    // Update button checked states (exclusive toggle)
    for (int i = 0; i < m_sideButtons.size(); ++i) {
        m_sideButtons[i]->setChecked(i == index);
    }

    // Switch content
    if (index < m_contentStack->count()) {
        m_contentStack->setCurrentIndex(index);
    }

    std::cout << "[TrainerContainerPage] Switched to tab " << index << std::endl;
}
