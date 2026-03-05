// TiltResultWidget.h - Angle display + device/axis selection for correction
#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include "PlanarAdjustmentData.h"

struct ApplicationContext;

/**
 * @brief Displays computed tilt angles and lets the user select
 * which angle to apply to which hex device/axis.
 */
class TiltResultWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TiltResultWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(const PlanarAdjustmentSet& set);

signals:
    void correctionConfigured(const CorrectionConfig& cfg);

private slots:
    void updatePreview();

private:
    void setupUI();
    void populateHexDevices();

    ApplicationContext* m_context;
    PlanarAdjustmentSet m_set;

    // Section A: Computed angles
    QLabel* m_pointSummaryLabel;
    QLabel* m_deltaLabel;
    QLabel* m_angleXLabel;
    QLabel* m_angleYLabel;

    // Section B: Correction selection
    QComboBox* m_sourceAngleCombo;
    QComboBox* m_targetDeviceCombo;
    QComboBox* m_targetAxisCombo;
    QCheckBox* m_reverseSignCheck;
    QLabel* m_previewLabel;
};
