// ZOffsetApplyWidget.h - Apply Z offset correction button + live hex readout
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include "ZOffsetData.h"

struct ApplicationContext;

/**
 * @brief Shows correction summary, live hex position, and Apply button.
 * Calls MoveRelative on the target hex device Z axis.
 */
class ZOffsetApplyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ZOffsetApplyWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetCorrection(const ZOffsetCorrectionConfig& cfg);

signals:
    void correctionApplied(double valueMm);

private slots:
    void onApply();
    void updateHexPosition();

private:
    void setupUI();

    ApplicationContext* m_context;
    ZOffsetCorrectionConfig m_correction;

    QLabel* m_summaryLabel;
    QLabel* m_hexPositionLabel;
    QPushButton* m_applyBtn;
    QLabel* m_resultLabel;

    QTimer* m_positionTimer;
};
