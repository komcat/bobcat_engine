// PneumaticInterlockWidget.cpp
#include "PneumaticInterlockWidget.h"
#include "InterlockManager.h"
#include "unified_motion_manager.h"
#include "../../pneumatics/PneumaticManager.h"
#include "../../pneumatics/Pneumatic_Types.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

// Column indices
static constexpr int COL_ENABLE = 0;
static constexpr int COL_SLIDE = 1;
static constexpr int COL_STATE = 2;
static constexpr int COL_BLOCKED_DEVICE = 3;
static constexpr int COL_BLOCKED_AXES = 4;
static constexpr int COL_STATUS = 5;
static constexpr int COL_COUNT = 6;

PneumaticInterlockWidget::PneumaticInterlockWidget(UnifiedMotionManager* motionManager,
    PneumaticManager* pneumaticManager,
    InterlockManager* interlockManager,
    QWidget* parent)
    : QWidget(parent)
    , m_motionManager(motionManager)
    , m_pneumaticManager(pneumaticManager)
    , m_interlockManager(interlockManager)
{
    setupUI();
    populateTable();

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &PneumaticInterlockWidget::updateStatus);
    m_statusTimer->start(500);
}

PneumaticInterlockWidget::~PneumaticInterlockWidget() = default;

void PneumaticInterlockWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    QLabel* titleLabel = new QLabel("Pneumatic Interlock Rules");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    mainLayout->addWidget(titleLabel);

    QLabel* descLabel = new QLabel(
        "Block moves on a motion device while a pneumatic slide is not in the required state. "
        "Blocked Axes scopes the rule to specific axes (blank = all). Max 3 rules.");
    descLabel->setStyleSheet("color: #888888; font-size: 12px;");
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    m_table = new QTableWidget(0, COL_COUNT);
    m_table->setHorizontalHeaderLabels({
        "Enable", "Slide", "Required State", "Blocked Device", "Blocked Axes", "Status"
    });
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(COL_ENABLE, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_SLIDE, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_STATE, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_BLOCKED_DEVICE, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_BLOCKED_AXES, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_STATUS, QHeaderView::Fixed);
    m_table->setColumnWidth(COL_ENABLE, 60);
    m_table->setColumnWidth(COL_STATE, 140);
    m_table->setColumnWidth(COL_BLOCKED_AXES, 120);
    m_table->setColumnWidth(COL_STATUS, 100);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(40);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(m_table);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);

    m_addButton = new QPushButton("+ Add Rule");
    connect(m_addButton, &QPushButton::clicked, this, &PneumaticInterlockWidget::onAddRuleClicked);
    buttonLayout->addWidget(m_addButton);

    m_removeButton = new QPushButton("- Remove Rule");
    m_removeButton->setStyleSheet(
        "QPushButton { background-color: #e74c3c; border-color: #c0392b; }"
        "QPushButton:hover { background-color: #c0392b; }"
        "QPushButton:disabled { background-color: #555555; color: #888888; border-color: #3f3f46; }"
    );
    connect(m_removeButton, &QPushButton::clicked, this, &PneumaticInterlockWidget::onRemoveRuleClicked);
    buttonLayout->addWidget(m_removeButton);

    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
    mainLayout->addStretch();
}

void PneumaticInterlockWidget::populateTable() {
    m_updating = true;

    m_table->setRowCount(0);

    if (!m_interlockManager) {
        m_updating = false;
        return;
    }

    const auto& rules = m_interlockManager->GetPneumaticRules();
    const QStringList devices = getDeviceNames();
    const QStringList slides = getSlideNames();

    for (size_t i = 0; i < rules.size(); ++i) {
        const auto& rule = rules[i];
        int row = m_table->rowCount();
        m_table->insertRow(row);

        // Enable
        QWidget* checkWidget = new QWidget();
        QHBoxLayout* checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->setAlignment(Qt::AlignCenter);
        QCheckBox* enableCheck = new QCheckBox();
        enableCheck->setChecked(rule.enabled);
        connect(enableCheck, &QCheckBox::toggled, this, &PneumaticInterlockWidget::onRuleChanged);
        checkLayout->addWidget(enableCheck);
        m_table->setCellWidget(row, COL_ENABLE, checkWidget);

        // Slide
        QComboBox* slideCombo = new QComboBox();
        slideCombo->addItems(slides);
        slideCombo->setCurrentText(QString::fromStdString(rule.slideName));
        connect(slideCombo, &QComboBox::currentTextChanged, this, &PneumaticInterlockWidget::onRuleChanged);
        m_table->setCellWidget(row, COL_SLIDE, slideCombo);

        // Required state - only the two states that make sense as a requirement
        QComboBox* stateCombo = new QComboBox();
        stateCombo->addItems({ "Retracted", "Extended" });
        stateCombo->setCurrentText(QString::fromStdString(GetPositionString(rule.requiredState)));
        connect(stateCombo, &QComboBox::currentTextChanged, this, &PneumaticInterlockWidget::onRuleChanged);
        m_table->setCellWidget(row, COL_STATE, stateCombo);

        // Blocked device
        QComboBox* blockedCombo = new QComboBox();
        blockedCombo->addItems(devices);
        blockedCombo->setCurrentText(QString::fromStdString(rule.blockedDevice));
        connect(blockedCombo, &QComboBox::currentTextChanged, this, &PneumaticInterlockWidget::onRuleChanged);
        m_table->setCellWidget(row, COL_BLOCKED_DEVICE, blockedCombo);

        // Blocked axes - free-form, validated to upper-case XYZUVW characters
        QLineEdit* axesEdit = new QLineEdit();
        axesEdit->setPlaceholderText("blank = all");
        axesEdit->setText(QString::fromStdString(rule.blockedAxes));
        QRegularExpression axisRe("[XYZUVWxyzuvw]*");
        axesEdit->setValidator(new QRegularExpressionValidator(axisRe, axesEdit));
        connect(axesEdit, &QLineEdit::textEdited, this, &PneumaticInterlockWidget::onRuleChanged);
        m_table->setCellWidget(row, COL_BLOCKED_AXES, axesEdit);

        // Status (timer updates this)
        QLabel* statusLabel = new QLabel("--");
        statusLabel->setAlignment(Qt::AlignCenter);
        statusLabel->setStyleSheet("color: #888888; font-weight: bold;");
        m_table->setCellWidget(row, COL_STATUS, statusLabel);
    }

    m_addButton->setEnabled(m_interlockManager->GetPneumaticRuleCount() < MAX_PNEUMATIC_INTERLOCK_RULES);
    m_removeButton->setEnabled(m_interlockManager->GetPneumaticRuleCount() > 0);

    m_updating = false;
}

void PneumaticInterlockWidget::onAddRuleClicked() {
    if (!m_interlockManager) return;

    PneumaticInterlockRule rule;
    rule.enabled = true;
    rule.requiredState = PneumaticPosition::RETRACTED;

    const QStringList slides = getSlideNames();
    if (!slides.isEmpty()) {
        rule.slideName = slides.first().toStdString();
    }

    const QStringList devices = getDeviceNames();
    if (!devices.isEmpty()) {
        rule.blockedDevice = devices.first().toStdString();
    }

    rule.blockedAxes = "";  // default: all axes

    if (m_interlockManager->AddPneumaticRule(rule)) {
        saveRules();
        populateTable();
    }
}

void PneumaticInterlockWidget::onRemoveRuleClicked() {
    if (!m_interlockManager) return;

    int row = m_table->currentRow();
    if (row < 0) {
        if (m_interlockManager->GetPneumaticRuleCount() > 0) {
            row = static_cast<int>(m_interlockManager->GetPneumaticRuleCount()) - 1;
        } else {
            return;
        }
    }

    m_interlockManager->RemovePneumaticRule(static_cast<size_t>(row));
    saveRules();
    populateTable();
}

void PneumaticInterlockWidget::onRuleChanged() {
    if (m_updating || !m_interlockManager) return;

    for (int row = 0; row < m_table->rowCount(); ++row) {
        PneumaticInterlockRule rule;

        QWidget* checkWidget = m_table->cellWidget(row, COL_ENABLE);
        QCheckBox* enableCheck = checkWidget ? checkWidget->findChild<QCheckBox*>() : nullptr;
        rule.enabled = enableCheck ? enableCheck->isChecked() : true;

        QComboBox* slideCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, COL_SLIDE));
        rule.slideName = slideCombo ? slideCombo->currentText().toStdString() : "";

        QComboBox* stateCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, COL_STATE));
        const QString stateText = stateCombo ? stateCombo->currentText() : "Retracted";
        rule.requiredState = (stateText == "Extended") ? PneumaticPosition::EXTENDED
                                                       : PneumaticPosition::RETRACTED;

        QComboBox* blockedCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, COL_BLOCKED_DEVICE));
        rule.blockedDevice = blockedCombo ? blockedCombo->currentText().toStdString() : "";

        QLineEdit* axesEdit = qobject_cast<QLineEdit*>(m_table->cellWidget(row, COL_BLOCKED_AXES));
        rule.blockedAxes = axesEdit ? axesEdit->text().toUpper().toStdString() : "";

        m_interlockManager->UpdatePneumaticRule(static_cast<size_t>(row), rule);
    }

    saveRules();
}

void PneumaticInterlockWidget::updateStatus() {
    if (!m_interlockManager) return;

    for (int row = 0; row < m_table->rowCount(); ++row) {
        QLabel* statusLabel = qobject_cast<QLabel*>(m_table->cellWidget(row, COL_STATUS));
        if (!statusLabel) continue;

        QWidget* checkWidget = m_table->cellWidget(row, COL_ENABLE);
        QCheckBox* enableCheck = checkWidget ? checkWidget->findChild<QCheckBox*>() : nullptr;
        const bool enabled = enableCheck ? enableCheck->isChecked() : true;

        const bool triggered = m_interlockManager->IsPneumaticRuleTriggered(static_cast<size_t>(row));

        if (!enabled) {
            statusLabel->setText("OFF");
            statusLabel->setStyleSheet("color: #888888; font-weight: bold;");
        } else if (triggered) {
            statusLabel->setText("BLOCKED");
            statusLabel->setStyleSheet("color: #e74c3c; font-weight: bold;");
        } else {
            statusLabel->setText("OK");
            statusLabel->setStyleSheet("color: #27ae60; font-weight: bold;");
        }
    }
}

void PneumaticInterlockWidget::saveRules() {
    if (m_interlockManager) {
        m_interlockManager->SaveConfig("");
    }
}

QStringList PneumaticInterlockWidget::getDeviceNames() const {
    QStringList result;
    if (m_motionManager) {
        for (const auto& name : m_motionManager->GetAllDeviceNames()) {
            result.append(QString::fromStdString(name));
        }
    }
    return result;
}

QStringList PneumaticInterlockWidget::getSlideNames() const {
    QStringList result;
    if (m_pneumaticManager) {
        for (const auto& name : m_pneumaticManager->GetAllSlideNames()) {
            result.append(QString::fromStdString(name));
        }
    }
    return result;
}
