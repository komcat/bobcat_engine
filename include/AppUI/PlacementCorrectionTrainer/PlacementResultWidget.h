// PlacementResultWidget.h - Display master vector teach results (no correction)
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
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

private slots:
    void onToggleTransformDetails();

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
};
