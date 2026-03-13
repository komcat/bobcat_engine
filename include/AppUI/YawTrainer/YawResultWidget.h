// YawResultWidget.h - Display ref/target angles, delta, and device/axis selection
#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include "YawTrainerData.h"

struct ApplicationContext;

/**
 * @brief Displays computed line angles, delta angle, and lets the user select
 * which hex device/axis to apply the yaw correction to.
 */
class YawResultWidget : public QWidget
{
    Q_OBJECT

public:
    explicit YawResultWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(const YawTrainerSet& set);

signals:
    void correctionConfigured(const YawCorrectionConfig& cfg);

private slots:
    void updatePreview();

private:
    void setupUI();
    void populateHexDevices();

    ApplicationContext* m_context;
    YawTrainerSet m_set;

    // Section A: Computed angles
    QLabel* m_lineSummaryLabel;
    QLabel* m_refAngleLabel;
    QLabel* m_tgtAngleLabel;
    QLabel* m_deltaAngleLabel;

    // Section B: Correction selection
    QComboBox* m_targetDeviceCombo;
    QComboBox* m_targetAxisCombo;
    QCheckBox* m_reverseSignCheck;
    QLabel* m_previewLabel;
};
