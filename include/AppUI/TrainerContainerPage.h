// TrainerContainerPage.h - Container with side panel navigation for trainer tools
#pragma once

#include <QWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QVector>

struct ApplicationContext;
class AccuracyTrainerPage;

/**
 * @brief Container page with a narrow side panel for switching between trainer tools.
 *
 * Layout:
 *   [Top bar: Back button + "Trainer" title]
 *   [Side panel (60px) | Content area (QStackedWidget)]
 *
 * Side panel buttons:
 *   0: "Pos\nAccuracy" -> AccuracyTrainerPage
 *   1: "Camera\nOffset" -> placeholder
 *   2-4: reserved (disabled)
 */
class TrainerContainerPage : public QWidget
{
    Q_OBJECT

public:
    explicit TrainerContainerPage(ApplicationContext* context, QWidget* parent = nullptr);

signals:
    void backToMainMenu();

private slots:
    void onSideButtonClicked(int index);

private:
    void setupUI();
    void selectButton(int index);

    ApplicationContext* m_context;
    QStackedWidget* m_contentStack;
    QVector<QPushButton*> m_sideButtons;
    AccuracyTrainerPage* m_accuracyTrainerPage;
    QWidget* m_cameraOffsetPlaceholder;
    int m_activeIndex;
};
