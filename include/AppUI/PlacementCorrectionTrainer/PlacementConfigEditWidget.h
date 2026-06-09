// PlacementConfigEditWidget.h — raw editor for the training-set `config` block.
//
// Workflow step 10 ("Edit Config") of the Placement Correction Trainer.
// Lets an operator inspect and hand-edit every field persisted under the
// set JSON "config" object (device / position names, master & target XYZUVW
// baselines, sign/U-correction flags, and the manual tune offsets) without
// going through the teach/retrace flow. On Save it emits the edited config
// back to the page, which applies it to the current set and persists it.
#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include "PlacementCorrectionData.h"

struct ApplicationContext;

class PlacementConfigEditWidget : public QWidget {
    Q_OBJECT

public:
    explicit PlacementConfigEditWidget(ApplicationContext* context, QWidget* parent = nullptr);

    // Load the config fields from the given set into the editor controls.
    void SetData(const PlacementCorrectionSet& set);

signals:
    // Emitted when the user clicks Save. Carries the edited config; the parent
    // page applies it to the current set and persists via the store.
    void configSaved(const PlacementCorrectionConfig& config);

private slots:
    void onSave();
    void onRevert();

private:
    // Build a labeled QDoubleSpinBox configured for position/offset values.
    QDoubleSpinBox* makeSpin();

    // Pull the editor controls into a config struct (preserving fields the
    // editor does not expose by copying from m_loaded first).
    PlacementCorrectionConfig collect() const;

    ApplicationContext* m_context;
    PlacementCorrectionConfig m_loaded;   // last value loaded via SetData

    // Identity / names
    QLineEdit* m_targetDevice = nullptr;
    QLineEdit* m_cameraId = nullptr;
    QLineEdit* m_masterPositionName = nullptr;
    QLineEdit* m_targetPositionName = nullptr;
    QLineEdit* m_normalPositionName = nullptr;

    // Flags
    QCheckBox* m_reverseSign = nullptr;
    QCheckBox* m_applyUCorrection = nullptr;

    // Master baseline XYZUVW
    QDoubleSpinBox* m_masterX = nullptr;
    QDoubleSpinBox* m_masterY = nullptr;
    QDoubleSpinBox* m_masterZ = nullptr;
    QDoubleSpinBox* m_masterU = nullptr;
    QDoubleSpinBox* m_masterV = nullptr;
    QDoubleSpinBox* m_masterW = nullptr;

    // Target baseline XYZUVW
    QDoubleSpinBox* m_targetX = nullptr;
    QDoubleSpinBox* m_targetY = nullptr;
    QDoubleSpinBox* m_targetZ = nullptr;
    QDoubleSpinBox* m_targetU = nullptr;
    QDoubleSpinBox* m_targetV = nullptr;
    QDoubleSpinBox* m_targetW = nullptr;

    // Manual tune offsets XYZUVW
    QDoubleSpinBox* m_tuneX = nullptr;
    QDoubleSpinBox* m_tuneY = nullptr;
    QDoubleSpinBox* m_tuneZ = nullptr;
    QDoubleSpinBox* m_tuneU = nullptr;
    QDoubleSpinBox* m_tuneV = nullptr;
    QDoubleSpinBox* m_tuneW = nullptr;

    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_revertBtn = nullptr;
    QLabel* m_statusLabel = nullptr;
};
