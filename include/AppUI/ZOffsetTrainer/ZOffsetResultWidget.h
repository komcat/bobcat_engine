// ZOffsetResultWidget.h - Z difference display + target device/Z diff selection
#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include "ZOffsetData.h"

struct ApplicationContext;

/**
 * @brief Displays measured Z difference and lets the user select
 * target device and desired Z offset.
 */
class ZOffsetResultWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ZOffsetResultWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(const ZOffsetSet& set);

signals:
    void correctionConfigured(const ZOffsetCorrectionConfig& cfg);

private slots:
    void updatePreview();

private:
    void setupUI();
    void populateHexDevices();

    ApplicationContext* m_context;
    ZOffsetSet m_set;

    // Section A: Z difference
    QLabel* m_pointSummaryLabel;
    QLabel* m_zDiffLabel;
    QLabel* m_zDiffUmLabel;

    // Section B: Correction selection
    QComboBox* m_targetDeviceCombo;
    QDoubleSpinBox* m_targetZDiffSpinBox;
    QCheckBox* m_reverseSignCheck;
    QLabel* m_previewLabel;
};
