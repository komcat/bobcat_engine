// PlacementResultWidget.h - Display master vector teach results (no correction)
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QScrollArea>
#include "PlacementCorrectionData.h"
#include "TransformationMatrixLoader.h"

struct ApplicationContext;

/**
 * @brief Displays the master vector teach results: angles, center points,
 * distance, master position data, and collapsible transform details.
 * No correction calculation — that happens during retrace.
 */
class PlacementResultWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlacementResultWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(const PlacementCorrectionSet& set);

signals:
    void recomputeRequested();
    // Emitted after the user re-saves the target position with a manual
    // offset. The updated set carries the new targetX..W values; the host
    // page should mirror them into its set and persist the training set JSON.
    void targetResaved(const PlacementCorrectionSet& updatedSet);

private slots:
    void onToggleTransformDetails();
    void onResaveTargetPosition();
    void onManualOffsetChanged();

private:
    void setupUI();
    void loadTransformationMatrix(const std::string& deviceName);

    ApplicationContext* m_context;
    PlacementCorrectionSet m_set;
    TransformationMatrixLoader m_matrixLoader;
    bool m_matrixLoaded = false;

    // Section A: Line summary & angles
    QLabel* m_lineSummaryLabel;
    QLabel* m_refAngleLabel;
    QLabel* m_tgtAngleLabel;
    QLabel* m_deltaAngleLabel;

    // Section B: Center points & master vector
    QLabel* m_refCenterLabel;
    QLabel* m_tgtCenterLabel;
    QLabel* m_distanceLabel;

    // Section C: Master position data (read-only)
    QLabel* m_masterPosLabel;

    // Section D: Transform details (collapsible, hidden by default)
    QPushButton* m_toggleTransformBtn;
    QGroupBox* m_transformGroup;
    QLabel* m_matrixLabel;
    QLabel* m_targetGlobalDeltaLabel;
    QLabel* m_targetLocalDeltaLabel;

    // Section E: Manual offset tuner (re-save target position)
    QLabel* m_currentTargetLabel;
    QDoubleSpinBox* m_offsetXSpin;
    QDoubleSpinBox* m_offsetYSpin;
    QDoubleSpinBox* m_offsetZSpin;
    QDoubleSpinBox* m_offsetUSpin;
    QDoubleSpinBox* m_offsetVSpin;
    QDoubleSpinBox* m_offsetWSpin;
    QLabel* m_offsetPreviewLabel;
    QPushButton* m_offsetClearBtn;
    QPushButton* m_resaveTargetBtn;
    QLabel* m_resaveStatusLabel;
};
