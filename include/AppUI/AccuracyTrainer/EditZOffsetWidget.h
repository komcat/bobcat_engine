// EditZOffsetWidget.h - Edit hex device Z offset parameters
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <vector>
#include "AccuracyTrainerData.h"

struct ApplicationContext;

class EditZOffsetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EditZOffsetWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetTrainingSet(TrainingSet* ts);

signals:
    void zOffsetsUpdated();

private slots:
    void onRecalculate();
    void onApply();

private:
    void rebuildUI();

    struct DeviceRow {
        std::string deviceName;
        QDoubleSpinBox* zCompSpin = nullptr;
        QDoubleSpinBox* zPlaceSpin = nullptr;
        QDoubleSpinBox* thicknessSpin = nullptr;
        QDoubleSpinBox* bondlineSpin = nullptr;
        QLabel* resultLabel = nullptr;
    };

    ApplicationContext* m_context;
    TrainingSet* m_trainingSet = nullptr;
    QVBoxLayout* m_mainLayout = nullptr;
    QWidget* m_deviceContainer = nullptr;
    std::vector<DeviceRow> m_rows;
};
