// VisionToolbarWidget.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLabel>

class VisionToolbarWidget : public QWidget {
    Q_OBJECT

public:
    explicit VisionToolbarWidget(QWidget* parent = nullptr);

    void setTitle(const QString& title);

signals:
    void newRecipeClicked();
    void loadRecipeClicked();
    void saveRecipeClicked();
    void saveAsRecipeClicked();
    void runRecipeClicked();
    void stopClicked();

private:
    void setupUI();

    QLabel* m_titleLabel;
    QPushButton* m_newBtn;
    QPushButton* m_loadBtn;
    QPushButton* m_saveBtn;
    QPushButton* m_saveAsBtn;
    QPushButton* m_runBtn;
    QPushButton* m_stopBtn;
};
