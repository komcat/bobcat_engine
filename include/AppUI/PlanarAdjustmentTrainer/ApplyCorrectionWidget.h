// ApplyCorrectionWidget.h - Apply correction button + live hex readout
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include "PlanarAdjustmentData.h"

struct ApplicationContext;

/**
 * @brief Shows correction summary, live hex position, and Apply button.
 * Calls MoveRelative on the target hex device/axis.
 */
class ApplyCorrectionWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ApplyCorrectionWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetCorrection(const CorrectionConfig& cfg);

signals:
    void correctionApplied(double value);

private slots:
    void onApply();
    void updateHexPosition();

private:
    void setupUI();

    ApplicationContext* m_context;
    CorrectionConfig m_correction;

    QLabel* m_summaryLabel;
    QLabel* m_hexPositionLabel;
    QPushButton* m_applyBtn;
    QLabel* m_resultLabel;

    QTimer* m_positionTimer;
};
