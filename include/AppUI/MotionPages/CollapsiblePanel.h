// CollapsiblePanel.h
#pragma once
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>

class CollapsiblePanel : public QWidget
{
    Q_OBJECT

public:
    explicit CollapsiblePanel(const QString& title, QWidget* contentWidget, QWidget* parent = nullptr);
    ~CollapsiblePanel() = default;

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_isCollapsed; }

    void setTitle(const QString& title);
    QString title() const;

    QWidget* contentWidget() const { return m_contentWidget; }

signals:
    void collapsedChanged(bool collapsed);

private slots:
    void toggleCollapse();

private:
    void setupUI();
    void updateCollapseButton();

    QString m_title;
    QWidget* m_contentWidget;
    QPushButton* m_collapseButton;
    QLabel* m_titleLabel;
    QWidget* m_headerWidget;
    bool m_isCollapsed;
};
