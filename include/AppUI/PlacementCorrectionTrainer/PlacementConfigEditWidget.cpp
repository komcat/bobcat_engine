// PlacementConfigEditWidget.cpp — see header.
#include "PlacementConfigEditWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QScrollArea>

namespace {

const char* kGroupStyle =
    "QGroupBox { color: #aabbcc; font-size: 13px; font-weight: bold; "
    "border: 1px solid #2d3e5f; border-radius: 4px; margin-top: 10px; padding-top: 6px; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }";

const char* kLineStyle =
    "QLineEdit { background-color: #1a1f2e; color: #eee; border: 1px solid #3d5a8c; "
    "border-radius: 3px; padding: 4px 6px; font-size: 12px; }";

const char* kSpinStyle =
    "QDoubleSpinBox { background-color: #1a1f2e; color: #eee; border: 1px solid #3d5a8c; "
    "border-radius: 3px; padding: 3px 6px; font-size: 12px; }";

// Read-only spins (master/target baseline) — visually dimmed, no spin buttons.
const char* kSpinReadOnlyStyle =
    "QDoubleSpinBox { background-color: #141926; color: #99a; border: 1px solid #2d3e5f; "
    "border-radius: 3px; padding: 3px 6px; font-size: 12px; }";

// Make a baseline spin read-only: derived from the named position, not editable here.
void markReadOnly(QDoubleSpinBox* spin) {
    spin->setReadOnly(true);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setFocusPolicy(Qt::NoFocus);
    spin->setStyleSheet(kSpinReadOnlyStyle);
}

const char* kCheckStyle = "QCheckBox { color: #eee; font-size: 12px; }";

const char* kLabelStyle = "color: #aaa; font-size: 12px;";

QLabel* fieldLabel(const QString& text) {
    QLabel* l = new QLabel(text);
    l->setStyleSheet(kLabelStyle);
    return l;
}

} // namespace

PlacementConfigEditWidget::PlacementConfigEditWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(15, 15, 15, 15);
    root->setSpacing(8);

    QLabel* title = new QLabel("Edit Config");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #fff;");
    root->addWidget(title);

    QLabel* hint = new QLabel(
        "Edit names, flags, and tune offsets. Position-name and device fields must "
        "match entries in motor_devices.json. The Master/Target baselines are "
        "read-only — they are re-read from the named position at retrace time. "
        "To nudge the saved result, use the Manual Tune Offsets (XYZ in mm, UVW in degrees).");
    hint->setStyleSheet("color: #888; font-size: 11px;");
    hint->setWordWrap(true);
    root->addWidget(hint);

    // Scrollable form (many fields)
    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    QWidget* form = new QWidget();
    QVBoxLayout* formLayout = new QVBoxLayout(form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(10);

    // ---- Identity / names ----
    QGroupBox* idGroup = new QGroupBox("Identity & Positions");
    idGroup->setStyleSheet(kGroupStyle);
    QGridLayout* idGrid = new QGridLayout(idGroup);
    idGrid->setHorizontalSpacing(10);
    idGrid->setVerticalSpacing(6);

    m_targetDevice = new QLineEdit();          m_targetDevice->setStyleSheet(kLineStyle);
    m_cameraId = new QLineEdit();              m_cameraId->setStyleSheet(kLineStyle);
    m_masterPositionName = new QLineEdit();    m_masterPositionName->setStyleSheet(kLineStyle);
    m_targetPositionName = new QLineEdit();    m_targetPositionName->setStyleSheet(kLineStyle);
    m_normalPositionName = new QLineEdit();    m_normalPositionName->setStyleSheet(kLineStyle);

    int r = 0;
    idGrid->addWidget(fieldLabel("Target Device"),        r, 0); idGrid->addWidget(m_targetDevice, r++, 1);
    idGrid->addWidget(fieldLabel("Camera ID"),            r, 0); idGrid->addWidget(m_cameraId, r++, 1);
    idGrid->addWidget(fieldLabel("Master Position Name"), r, 0); idGrid->addWidget(m_masterPositionName, r++, 1);
    idGrid->addWidget(fieldLabel("Target Position Name"), r, 0); idGrid->addWidget(m_targetPositionName, r++, 1);
    idGrid->addWidget(fieldLabel("Normal Position Name"), r, 0); idGrid->addWidget(m_normalPositionName, r++, 1);
    idGrid->setColumnStretch(1, 1);
    formLayout->addWidget(idGroup);

    // ---- Flags ----
    QGroupBox* flagGroup = new QGroupBox("Flags");
    flagGroup->setStyleSheet(kGroupStyle);
    QVBoxLayout* flagLayout = new QVBoxLayout(flagGroup);
    m_applyUCorrection = new QCheckBox("Apply U (rotation) correction");
    m_applyUCorrection->setStyleSheet(kCheckStyle);
    m_reverseSign = new QCheckBox("Reverse correction sign");
    m_reverseSign->setStyleSheet(kCheckStyle);
    flagLayout->addWidget(m_applyUCorrection);
    flagLayout->addWidget(m_reverseSign);
    formLayout->addWidget(flagGroup);

    // ---- Master baseline (read-only: derived from the named position) ----
    QGroupBox* masterGroup = new QGroupBox("Master Baseline (XYZUVW) — read-only, from named position");
    masterGroup->setStyleSheet(kGroupStyle);
    QGridLayout* mGrid = new QGridLayout(masterGroup);
    mGrid->setHorizontalSpacing(10);
    mGrid->setVerticalSpacing(6);
    m_masterX = makeSpin(); m_masterY = makeSpin(); m_masterZ = makeSpin();
    m_masterU = makeSpin(); m_masterV = makeSpin(); m_masterW = makeSpin();
    markReadOnly(m_masterX); markReadOnly(m_masterY); markReadOnly(m_masterZ);
    markReadOnly(m_masterU); markReadOnly(m_masterV); markReadOnly(m_masterW);
    mGrid->addWidget(fieldLabel("X (mm)"), 0, 0); mGrid->addWidget(m_masterX, 0, 1);
    mGrid->addWidget(fieldLabel("Y (mm)"), 1, 0); mGrid->addWidget(m_masterY, 1, 1);
    mGrid->addWidget(fieldLabel("Z (mm)"), 2, 0); mGrid->addWidget(m_masterZ, 2, 1);
    mGrid->addWidget(fieldLabel("U (deg)"), 0, 2); mGrid->addWidget(m_masterU, 0, 3);
    mGrid->addWidget(fieldLabel("V (deg)"), 1, 2); mGrid->addWidget(m_masterV, 1, 3);
    mGrid->addWidget(fieldLabel("W (deg)"), 2, 2); mGrid->addWidget(m_masterW, 2, 3);
    mGrid->setColumnStretch(1, 1);
    mGrid->setColumnStretch(3, 1);
    formLayout->addWidget(masterGroup);

    // ---- Target baseline (read-only: derived from the named position) ----
    QGroupBox* targetGroup = new QGroupBox("Target Baseline (XYZUVW) — read-only, from named position");
    targetGroup->setStyleSheet(kGroupStyle);
    QGridLayout* tGrid = new QGridLayout(targetGroup);
    tGrid->setHorizontalSpacing(10);
    tGrid->setVerticalSpacing(6);
    m_targetX = makeSpin(); m_targetY = makeSpin(); m_targetZ = makeSpin();
    m_targetU = makeSpin(); m_targetV = makeSpin(); m_targetW = makeSpin();
    markReadOnly(m_targetX); markReadOnly(m_targetY); markReadOnly(m_targetZ);
    markReadOnly(m_targetU); markReadOnly(m_targetV); markReadOnly(m_targetW);
    tGrid->addWidget(fieldLabel("X (mm)"), 0, 0); tGrid->addWidget(m_targetX, 0, 1);
    tGrid->addWidget(fieldLabel("Y (mm)"), 1, 0); tGrid->addWidget(m_targetY, 1, 1);
    tGrid->addWidget(fieldLabel("Z (mm)"), 2, 0); tGrid->addWidget(m_targetZ, 2, 1);
    tGrid->addWidget(fieldLabel("U (deg)"), 0, 2); tGrid->addWidget(m_targetU, 0, 3);
    tGrid->addWidget(fieldLabel("V (deg)"), 1, 2); tGrid->addWidget(m_targetV, 1, 3);
    tGrid->addWidget(fieldLabel("W (deg)"), 2, 2); tGrid->addWidget(m_targetW, 2, 3);
    tGrid->setColumnStretch(1, 1);
    tGrid->setColumnStretch(3, 1);
    formLayout->addWidget(targetGroup);

    // ---- Manual tune offsets ----
    QGroupBox* tuneGroup = new QGroupBox("Manual Tune Offsets (added at Save)");
    tuneGroup->setStyleSheet(kGroupStyle);
    QGridLayout* oGrid = new QGridLayout(tuneGroup);
    oGrid->setHorizontalSpacing(10);
    oGrid->setVerticalSpacing(6);
    m_tuneX = makeSpin(); m_tuneY = makeSpin(); m_tuneZ = makeSpin();
    m_tuneU = makeSpin(); m_tuneV = makeSpin(); m_tuneW = makeSpin();
    oGrid->addWidget(fieldLabel("X (mm)"), 0, 0); oGrid->addWidget(m_tuneX, 0, 1);
    oGrid->addWidget(fieldLabel("Y (mm)"), 1, 0); oGrid->addWidget(m_tuneY, 1, 1);
    oGrid->addWidget(fieldLabel("Z (mm)"), 2, 0); oGrid->addWidget(m_tuneZ, 2, 1);
    oGrid->addWidget(fieldLabel("U (deg)"), 0, 2); oGrid->addWidget(m_tuneU, 0, 3);
    oGrid->addWidget(fieldLabel("V (deg)"), 1, 2); oGrid->addWidget(m_tuneV, 1, 3);
    oGrid->addWidget(fieldLabel("W (deg)"), 2, 2); oGrid->addWidget(m_tuneW, 2, 3);
    oGrid->setColumnStretch(1, 1);
    oGrid->setColumnStretch(3, 1);
    formLayout->addWidget(tuneGroup);

    formLayout->addStretch();
    scroll->setWidget(form);
    root->addWidget(scroll, 1);

    // ---- Action row ----
    QHBoxLayout* actionRow = new QHBoxLayout();
    m_statusLabel = new QLabel("No set loaded.");
    m_statusLabel->setStyleSheet("color: #888; font-size: 11px;");
    actionRow->addWidget(m_statusLabel, 1);

    m_revertBtn = new QPushButton("Revert");
    m_revertBtn->setFixedSize(90, 30);
    m_revertBtn->setStyleSheet(
        "QPushButton { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; font-size: 12px; }"
        "QPushButton:hover { background-color: #3d5a8c; }");
    connect(m_revertBtn, &QPushButton::clicked, this, &PlacementConfigEditWidget::onRevert);
    actionRow->addWidget(m_revertBtn);

    m_saveBtn = new QPushButton("Save Config");
    m_saveBtn->setFixedSize(120, 30);
    m_saveBtn->setStyleSheet(
        "QPushButton { background-color: #3d5a8c; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; font-weight: bold; font-size: 12px; }"
        "QPushButton:hover { background-color: #4d6e9f; }");
    connect(m_saveBtn, &QPushButton::clicked, this, &PlacementConfigEditWidget::onSave);
    actionRow->addWidget(m_saveBtn);

    root->addLayout(actionRow);
}

QDoubleSpinBox* PlacementConfigEditWidget::makeSpin()
{
    QDoubleSpinBox* spin = new QDoubleSpinBox();
    spin->setDecimals(6);
    spin->setRange(-100000.0, 100000.0);
    spin->setSingleStep(0.001);
    spin->setKeyboardTracking(false);
    spin->setStyleSheet(kSpinStyle);
    return spin;
}

void PlacementConfigEditWidget::SetData(const PlacementCorrectionSet& set)
{
    m_loaded = set.config;
    const PlacementCorrectionConfig& c = set.config;

    m_targetDevice->setText(QString::fromStdString(c.targetDevice));
    m_cameraId->setText(QString::fromStdString(c.cameraId));
    m_masterPositionName->setText(QString::fromStdString(c.masterPositionName));
    m_targetPositionName->setText(QString::fromStdString(c.targetPositionName));
    m_normalPositionName->setText(QString::fromStdString(c.normalPositionName));

    m_applyUCorrection->setChecked(c.applyUCorrection);
    m_reverseSign->setChecked(c.reverseSign);

    m_masterX->setValue(c.masterX); m_masterY->setValue(c.masterY); m_masterZ->setValue(c.masterZ);
    m_masterU->setValue(c.masterU); m_masterV->setValue(c.masterV); m_masterW->setValue(c.masterW);

    m_targetX->setValue(c.targetX); m_targetY->setValue(c.targetY); m_targetZ->setValue(c.targetZ);
    m_targetU->setValue(c.targetU); m_targetV->setValue(c.targetV); m_targetW->setValue(c.targetW);

    m_tuneX->setValue(c.tuneOffsetX); m_tuneY->setValue(c.tuneOffsetY); m_tuneZ->setValue(c.tuneOffsetZ);
    m_tuneU->setValue(c.tuneOffsetU); m_tuneV->setValue(c.tuneOffsetV); m_tuneW->setValue(c.tuneOffsetW);

    m_statusLabel->setText(QString("Editing: %1").arg(QString::fromStdString(set.name)));
}

PlacementCorrectionConfig PlacementConfigEditWidget::collect() const
{
    // Start from the loaded config so any field the editor does not expose is preserved.
    PlacementCorrectionConfig c = m_loaded;

    c.targetDevice = m_targetDevice->text().trimmed().toStdString();
    c.cameraId = m_cameraId->text().trimmed().toStdString();
    c.masterPositionName = m_masterPositionName->text().trimmed().toStdString();
    c.targetPositionName = m_targetPositionName->text().trimmed().toStdString();
    c.normalPositionName = m_normalPositionName->text().trimmed().toStdString();

    c.applyUCorrection = m_applyUCorrection->isChecked();
    c.reverseSign = m_reverseSign->isChecked();

    // Master/Target baselines are read-only and re-derived from the named
    // position at retrace time — preserve the loaded full-precision values
    // (copied from m_loaded above) rather than the spin's 6-decimal display.

    c.tuneOffsetX = m_tuneX->value(); c.tuneOffsetY = m_tuneY->value(); c.tuneOffsetZ = m_tuneZ->value();
    c.tuneOffsetU = m_tuneU->value(); c.tuneOffsetV = m_tuneV->value(); c.tuneOffsetW = m_tuneW->value();

    return c;
}

void PlacementConfigEditWidget::onSave()
{
    PlacementCorrectionConfig c = collect();
    m_loaded = c;
    emit configSaved(c);
    m_statusLabel->setText("Config saved.");
}

void PlacementConfigEditWidget::onRevert()
{
    // Re-apply the last loaded config to all controls.
    PlacementCorrectionSet tmp;
    tmp.config = m_loaded;
    SetData(tmp);
    m_statusLabel->setText("Reverted to last loaded values.");
}
