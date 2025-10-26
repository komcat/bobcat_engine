// CollapsiblePanel.cpp
#include "CollapsiblePanel.h"
#include <QHBoxLayout>
#include <QVBoxLayout>

CollapsiblePanel::CollapsiblePanel(const QString& title, QWidget* contentWidget, QWidget* parent)
    : QWidget(parent)
    , m_title(title)
    , m_contentWidget(contentWidget)
    , m_collapseButton(nullptr)
    , m_titleLabel(nullptr)
    , m_headerWidget(nullptr)
    , m_isCollapsed(false)
{
    setupUI();
    // Set initial size policy (expanded by default)
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void CollapsiblePanel::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header widget
    m_headerWidget = new QWidget();
    m_headerWidget->setStyleSheet(
        "QWidget {"
        "  background-color: #34495e;"
        "  border-bottom: 2px solid #2c3e50;"
        "}"
    );
    m_headerWidget->setFixedHeight(35);
    m_headerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QHBoxLayout* headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(10, 5, 10, 5);
    headerLayout->setSpacing(5);

    // Title label
    m_titleLabel = new QLabel(m_title);
    m_titleLabel->setStyleSheet(
        "QLabel {"
        "  color: white;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "  background-color: transparent;"
        "  border: none;"
        "}"
    );
    headerLayout->addWidget(m_titleLabel, 1);

    // Collapse button
    m_collapseButton = new QPushButton();
    m_collapseButton->setFixedWidth(25);
    m_collapseButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_collapseButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #3498db;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 3px;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #2980b9;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #21618c;"
        "}"
    );
    updateCollapseButton();
    connect(m_collapseButton, &QPushButton::clicked, this, &CollapsiblePanel::toggleCollapse);
    headerLayout->addWidget(m_collapseButton);

    mainLayout->addWidget(m_headerWidget);

    // Content widget - add with stretch factor to expand
    if (m_contentWidget) {
        m_contentWidget->setParent(this);
        m_contentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        mainLayout->addWidget(m_contentWidget, 1);
    }
}

void CollapsiblePanel::setCollapsed(bool collapsed)
{
    if (m_isCollapsed == collapsed) {
        return;
    }

    m_isCollapsed = collapsed;

    if (m_contentWidget) {
        m_contentWidget->setVisible(!collapsed);
    }

    // Show/hide title label based on collapse state
    if (m_titleLabel) {
        m_titleLabel->setVisible(!collapsed);
    }

    // Adjust header widget layout for collapsed state
    if (m_headerWidget) {
        if (collapsed) {
            m_headerWidget->setFixedWidth(35);
            m_headerWidget->setMinimumHeight(0);
            m_headerWidget->setMaximumHeight(QWIDGETSIZE_MAX);
            m_headerWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        } else {
            m_headerWidget->setMinimumWidth(0);
            m_headerWidget->setMaximumWidth(QWIDGETSIZE_MAX);
            m_headerWidget->setFixedHeight(35);
            m_headerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        }
    }

    // When collapsed, minimize width (show as vertical strip)
    // When expanded, use expanding policy to fill available space
    if (collapsed) {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setMaximumWidth(35);  // Width of just the collapse button
    } else {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMaximumWidth(QWIDGETSIZE_MAX);  // Remove width restriction
    }

    updateCollapseButton();
    emit collapsedChanged(collapsed);
}

void CollapsiblePanel::toggleCollapse()
{
    setCollapsed(!m_isCollapsed);
}

void CollapsiblePanel::updateCollapseButton()
{
    if (m_collapseButton) {
        // Use arrow symbols for horizontal collapse
        m_collapseButton->setText(m_isCollapsed ? "◀" : "▶");
        m_collapseButton->setToolTip(m_isCollapsed ? "Expand" : "Collapse");
    }
}

void CollapsiblePanel::setTitle(const QString& title)
{
    m_title = title;
    if (m_titleLabel) {
        m_titleLabel->setText(title);
    }
}

QString CollapsiblePanel::title() const
{
    return m_title;
}
