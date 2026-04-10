// InterlockWidget.cpp
#include "InterlockWidget.h"
#include "InterlockManager.h"
#include "unified_motion_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>

// Column indices
static constexpr int COL_ENABLE = 0;
static constexpr int COL_MONITOR_DEVICE = 1;
static constexpr int COL_AXIS = 2;
static constexpr int COL_OPERATOR = 3;
static constexpr int COL_THRESHOLD = 4;
static constexpr int COL_BLOCKED_DEVICE = 5;
static constexpr int COL_STATUS = 6;
static constexpr int COL_COUNT = 7;

InterlockWidget::InterlockWidget(UnifiedMotionManager* motionManager,
    InterlockManager* interlockManager,
    QWidget* parent)
    : QWidget(parent)
    , m_motionManager(motionManager)
    , m_interlockManager(interlockManager)
{
    setupUI();
    populateTable();

    // Live status update every 500ms
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &InterlockWidget::updateStatus);
    m_statusTimer->start(500);
}

InterlockWidget::~InterlockWidget() = default;

void InterlockWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Title
    QLabel* titleLabel = new QLabel("Motion Interlock Rules");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    mainLayout->addWidget(titleLabel);

    // Description
    QLabel* descLabel = new QLabel(
        "Block moves on a device when another device's axis position meets a condition. Max 3 rules.");
    descLabel->setStyleSheet("color: #888888; font-size: 12px;");
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // Table - no inline stylesheet, DarkTheme handles it
    m_table = new QTableWidget(0, COL_COUNT);
    m_table->setHorizontalHeaderLabels({
        "Enable", "Monitor Device", "Axis", "Op", "Threshold", "Blocked Device", "Status"
    });
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(COL_ENABLE, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_MONITOR_DEVICE, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_AXIS, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_OPERATOR, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_THRESHOLD, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_BLOCKED_DEVICE, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_STATUS, QHeaderView::Fixed);
    m_table->setColumnWidth(COL_ENABLE, 60);
    m_table->setColumnWidth(COL_AXIS, 70);
    m_table->setColumnWidth(COL_OPERATOR, 60);
    m_table->setColumnWidth(COL_THRESHOLD, 100);
    m_table->setColumnWidth(COL_STATUS, 100);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(40);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(m_table);

    // Buttons - DarkTheme styles default buttons; only override remove button color
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);

    m_addButton = new QPushButton("+ Add Rule");
    connect(m_addButton, &QPushButton::clicked, this, &InterlockWidget::onAddRuleClicked);
    buttonLayout->addWidget(m_addButton);

    m_removeButton = new QPushButton("- Remove Rule");
    m_removeButton->setStyleSheet(
        "QPushButton { background-color: #e74c3c; border-color: #c0392b; }"
        "QPushButton:hover { background-color: #c0392b; }"
        "QPushButton:disabled { background-color: #555555; color: #888888; border-color: #3f3f46; }"
    );
    connect(m_removeButton, &QPushButton::clicked, this, &InterlockWidget::onRemoveRuleClicked);
    buttonLayout->addWidget(m_removeButton);

    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
    mainLayout->addStretch();
}

void InterlockWidget::populateTable() {
    m_updating = true;

    m_table->setRowCount(0);

    if (!m_interlockManager) {
        m_updating = false;
        return;
    }

    const auto& rules = m_interlockManager->GetRules();
    QStringList devices = getDeviceNames();

    for (size_t i = 0; i < rules.size(); ++i) {
        const auto& rule = rules[i];
        int row = m_table->rowCount();
        m_table->insertRow(row);

        // Enable checkbox
        QWidget* checkWidget = new QWidget();
        QHBoxLayout* checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->setAlignment(Qt::AlignCenter);
        QCheckBox* enableCheck = new QCheckBox();
        enableCheck->setChecked(rule.enabled);
        connect(enableCheck, &QCheckBox::toggled, this, &InterlockWidget::onRuleChanged);
        checkLayout->addWidget(enableCheck);
        m_table->setCellWidget(row, COL_ENABLE, checkWidget);

        // Monitor device combo
        QComboBox* monitorDeviceCombo = new QComboBox();
        monitorDeviceCombo->addItems(devices);
        monitorDeviceCombo->setCurrentText(QString::fromStdString(rule.monitorDevice));
        connect(monitorDeviceCombo, &QComboBox::currentTextChanged, this, [this, row](const QString& deviceName) {
            QComboBox* axisCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, COL_AXIS));
            if (axisCombo) {
                axisCombo->blockSignals(true);
                axisCombo->clear();
                axisCombo->addItems(getAxesForDevice(deviceName));
                axisCombo->blockSignals(false);
            }
            onRuleChanged();
        });
        m_table->setCellWidget(row, COL_MONITOR_DEVICE, monitorDeviceCombo);

        // Axis combo
        QComboBox* axisCombo = new QComboBox();
        QStringList axes = getAxesForDevice(QString::fromStdString(rule.monitorDevice));
        axisCombo->addItems(axes);
        axisCombo->setCurrentText(QString::fromStdString(rule.monitorAxis));
        connect(axisCombo, &QComboBox::currentTextChanged, this, &InterlockWidget::onRuleChanged);
        m_table->setCellWidget(row, COL_AXIS, axisCombo);

        // Operator combo
        QComboBox* opCombo = new QComboBox();
        opCombo->addItems({ "<", ">", "<=", ">=" });
        opCombo->setCurrentText(QString::fromStdString(rule.op));
        connect(opCombo, &QComboBox::currentTextChanged, this, &InterlockWidget::onRuleChanged);
        m_table->setCellWidget(row, COL_OPERATOR, opCombo);

        // Threshold spinbox
        QDoubleSpinBox* thresholdSpin = new QDoubleSpinBox();
        thresholdSpin->setRange(-10000.0, 10000.0);
        thresholdSpin->setDecimals(3);
        thresholdSpin->setValue(rule.threshold);
        connect(thresholdSpin, &QDoubleSpinBox::valueChanged, this, &InterlockWidget::onRuleChanged);
        m_table->setCellWidget(row, COL_THRESHOLD, thresholdSpin);

        // Blocked device combo
        QComboBox* blockedDeviceCombo = new QComboBox();
        blockedDeviceCombo->addItems(devices);
        blockedDeviceCombo->setCurrentText(QString::fromStdString(rule.blockedDevice));
        connect(blockedDeviceCombo, &QComboBox::currentTextChanged, this, &InterlockWidget::onRuleChanged);
        m_table->setCellWidget(row, COL_BLOCKED_DEVICE, blockedDeviceCombo);

        // Status label (updated by timer)
        QLabel* statusLabel = new QLabel("--");
        statusLabel->setAlignment(Qt::AlignCenter);
        statusLabel->setStyleSheet("color: #888888; font-weight: bold;");
        m_table->setCellWidget(row, COL_STATUS, statusLabel);
    }

    // Update button states
    m_addButton->setEnabled(m_interlockManager->GetRuleCount() < MAX_INTERLOCK_RULES);
    m_removeButton->setEnabled(m_interlockManager->GetRuleCount() > 0);

    m_updating = false;
}

void InterlockWidget::onAddRuleClicked() {
    if (!m_interlockManager) return;

    InterlockRule rule;
    rule.enabled = true;

    // Default to first available devices
    QStringList devices = getDeviceNames();
    if (devices.size() >= 2) {
        rule.monitorDevice = devices[0].toStdString();
        rule.blockedDevice = devices[1].toStdString();
    } else if (devices.size() == 1) {
        rule.monitorDevice = devices[0].toStdString();
        rule.blockedDevice = devices[0].toStdString();
    }

    QStringList axes = getAxesForDevice(QString::fromStdString(rule.monitorDevice));
    if (!axes.isEmpty()) {
        rule.monitorAxis = axes[0].toStdString();
    }
    rule.op = "<";
    rule.threshold = 0.0;

    if (m_interlockManager->AddRule(rule)) {
        saveRules();
        populateTable();
    }
}

void InterlockWidget::onRemoveRuleClicked() {
    if (!m_interlockManager) return;

    int row = m_table->currentRow();
    if (row < 0) {
        // Remove last if none selected
        if (m_interlockManager->GetRuleCount() > 0) {
            row = static_cast<int>(m_interlockManager->GetRuleCount()) - 1;
        } else {
            return;
        }
    }

    m_interlockManager->RemoveRule(static_cast<size_t>(row));
    saveRules();
    populateTable();
}

void InterlockWidget::onRuleChanged() {
    if (m_updating || !m_interlockManager) return;

    // Read all rows back into rules
    for (int row = 0; row < m_table->rowCount(); ++row) {
        InterlockRule rule;

        // Enable
        QWidget* checkWidget = m_table->cellWidget(row, COL_ENABLE);
        QCheckBox* enableCheck = checkWidget ? checkWidget->findChild<QCheckBox*>() : nullptr;
        rule.enabled = enableCheck ? enableCheck->isChecked() : true;

        // Monitor device
        QComboBox* monitorCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, COL_MONITOR_DEVICE));
        rule.monitorDevice = monitorCombo ? monitorCombo->currentText().toStdString() : "";

        // Axis
        QComboBox* axisCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, COL_AXIS));
        rule.monitorAxis = axisCombo ? axisCombo->currentText().toStdString() : "";

        // Operator
        QComboBox* opCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, COL_OPERATOR));
        rule.op = opCombo ? opCombo->currentText().toStdString() : "<";

        // Threshold
        QDoubleSpinBox* thresholdSpin = qobject_cast<QDoubleSpinBox*>(m_table->cellWidget(row, COL_THRESHOLD));
        rule.threshold = thresholdSpin ? thresholdSpin->value() : 0.0;

        // Blocked device
        QComboBox* blockedCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, COL_BLOCKED_DEVICE));
        rule.blockedDevice = blockedCombo ? blockedCombo->currentText().toStdString() : "";

        m_interlockManager->UpdateRule(static_cast<size_t>(row), rule);
    }

    saveRules();
}

void InterlockWidget::updateStatus() {
    if (!m_interlockManager) return;

    for (int row = 0; row < m_table->rowCount(); ++row) {
        QLabel* statusLabel = qobject_cast<QLabel*>(m_table->cellWidget(row, COL_STATUS));
        if (!statusLabel) continue;

        bool triggered = m_interlockManager->IsRuleTriggered(static_cast<size_t>(row));

        // Check if rule is enabled
        QWidget* checkWidget = m_table->cellWidget(row, COL_ENABLE);
        QCheckBox* enableCheck = checkWidget ? checkWidget->findChild<QCheckBox*>() : nullptr;
        bool enabled = enableCheck ? enableCheck->isChecked() : true;

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

void InterlockWidget::saveRules() {
    if (m_interlockManager) {
        m_interlockManager->SaveConfig("");
    }
}

QStringList InterlockWidget::getDeviceNames() const {
    QStringList result;
    if (m_motionManager) {
        for (const auto& name : m_motionManager->GetAllDeviceNames()) {
            result.append(QString::fromStdString(name));
        }
    }
    return result;
}

QStringList InterlockWidget::getAxesForDevice(const QString& deviceName) const {
    QStringList result;
    if (m_motionManager) {
        std::string axes = m_motionManager->GetInstalledAxes(deviceName.toStdString());
        for (char c : axes) {
            result.append(QString(1, c));
        }
    }
    if (result.isEmpty()) {
        result << "X" << "Y" << "Z" << "U" << "V" << "W";
    }
    return result;
}
