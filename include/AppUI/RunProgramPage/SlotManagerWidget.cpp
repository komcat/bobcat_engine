// SlotManagerWidget.cpp
#include "SlotManagerWidget.h"

#include "ProgramManager.h"
#include "Theme.h"
#include "ThemeButtonStyles.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <iostream>

SlotManagerWidget::SlotManagerWidget(Mode mode, Deps deps, QWidget* parent)
    : QWidget(parent)
    , m_mode(mode)
    , m_deps(std::move(deps))
{
    if (m_mode == Mode::Run) {
        buildRunUi();
    } else {
        buildEditUi();
    }
    Reload();
}

SlotManagerWidget::~SlotManagerWidget() = default;

void SlotManagerWidget::buildRunUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* slotGroup = new QGroupBox("Active Slot");
    slotGroup->setStyleSheet(
        "QGroupBox { "
        "  background-color: #1e1e1e; "
        "  border: 1px solid #3f3f46; "
        "  border-radius: 4px; "
        "  margin-top: 10px; "
        "  padding-top: 15px; "
        "  color: #d4d4d4; "
        "  font-weight: bold; "
        "} "
        "QGroupBox::title { "
        "  subcontrol-origin: margin; "
        "  subcontrol-position: top left; "
        "  padding: 2px 5px; "
        "  color: #ffffff; "
        "}");
    auto* slotLayout = new QVBoxLayout(slotGroup);
    slotLayout->setSpacing(5);

    m_combo = new QComboBox();
    m_combo->setMinimumHeight(30);
    m_combo->setStyleSheet(
        "QComboBox { "
        "  background-color: #3c3c3c; "
        "  color: #d4d4d4; "
        "  border: 1px solid #555555; "
        "  border-radius: 3px; "
        "  padding: 5px 10px; "
        "  font-size: 13px; "
        "} "
        "QComboBox:hover { "
        "  border-color: #0e639c; "
        "} "
        "QComboBox::drop-down { "
        "  border: none; "
        "  width: 25px; "
        "} "
        "QComboBox::down-arrow { "
        "  image: none; "
        "  border-left: 5px solid transparent; "
        "  border-right: 5px solid transparent; "
        "  border-top: 5px solid #d4d4d4; "
        "} "
        "QComboBox QAbstractItemView { "
        "  background-color: #2d2d30; "
        "  color: #d4d4d4; "
        "  selection-background-color: #0e639c; "
        "  selection-color: white; "
        "  border: 1px solid #555555; "
        "}");
    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SlotManagerWidget::onComboBoxChanged);
    slotLayout->addWidget(m_combo);

    m_infoLabel = new QLabel("0 recipes in this slot");
    m_infoLabel->setStyleSheet("color: #888888; font-size: 11px; font-style: italic;");
    slotLayout->addWidget(m_infoLabel);

    outer->addWidget(slotGroup);
}

void SlotManagerWidget::buildEditUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* slotManageGroup = new QGroupBox("Recipe Slot");
    slotManageGroup->setStyleSheet(Theme::Boxes::groupBoxDark());
    auto* slotManageLayout = new QHBoxLayout(slotManageGroup);
    slotManageLayout->setSpacing(5);

    m_combo = new QComboBox();
    m_combo->setMinimumHeight(30);
    m_combo->setMinimumWidth(150);
    // No local override — global DarkTheme QSS handles QComboBox via Theme palette.
    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SlotManagerWidget::onComboBoxChanged);
    slotManageLayout->addWidget(m_combo, 1);

    const QString slotButtonStyle = Theme::Buttons::bezelNeutral();

    m_renameButton = new QPushButton("Rename");
    m_renameButton->setMinimumHeight(30);
    m_renameButton->setStyleSheet(slotButtonStyle);
    m_renameButton->setToolTip("Rename the current slot");
    connect(m_renameButton, &QPushButton::clicked, this, &SlotManagerWidget::onRenameClicked);
    slotManageLayout->addWidget(m_renameButton);

    m_addButton = new QPushButton("+ Add");
    m_addButton->setMinimumHeight(30);
    m_addButton->setStyleSheet(slotButtonStyle);
    m_addButton->setToolTip("Add a new slot (max 5)");
    connect(m_addButton, &QPushButton::clicked, this, &SlotManagerWidget::onAddClicked);
    slotManageLayout->addWidget(m_addButton);

    m_duplicateButton = new QPushButton("Copy");
    m_duplicateButton->setMinimumHeight(30);
    m_duplicateButton->setStyleSheet(slotButtonStyle);
    m_duplicateButton->setToolTip("Duplicate the current slot");
    connect(m_duplicateButton, &QPushButton::clicked, this, &SlotManagerWidget::onDuplicateClicked);
    slotManageLayout->addWidget(m_duplicateButton);

    m_deleteButton = new QPushButton("Delete");
    m_deleteButton->setMinimumHeight(30);
    m_deleteButton->setStyleSheet(Theme::Buttons::bezelDanger());
    m_deleteButton->setToolTip("Delete the current slot (cannot delete last slot)");
    connect(m_deleteButton, &QPushButton::clicked, this, &SlotManagerWidget::onDeleteClicked);
    slotManageLayout->addWidget(m_deleteButton);

    outer->addWidget(slotManageGroup);
}

void SlotManagerWidget::Reload()
{
    if (!m_combo || !m_deps.manager) return;

    auto names = m_deps.manager->getSlotNames();
    int activeIndex = m_deps.manager->getActiveSlotIndex();

    m_combo->blockSignals(true);
    m_combo->clear();
    for (const auto& name : names) {
        m_combo->addItem(QString::fromStdString(name));
    }
    if (activeIndex >= 0 && activeIndex < static_cast<int>(names.size())) {
        m_combo->setCurrentIndex(activeIndex);
    }
    m_combo->blockSignals(false);

    if (m_mode == Mode::Edit) {
        if (m_deleteButton) {
            m_deleteButton->setEnabled(names.size() > 1);
        }
        if (m_addButton) {
            m_addButton->setEnabled(static_cast<int>(names.size()) < MAX_RECIPE_SLOTS);
        }
        if (m_duplicateButton) {
            m_duplicateButton->setEnabled(static_cast<int>(names.size()) < MAX_RECIPE_SLOTS);
        }
    }
}

void SlotManagerWidget::SetSelectedSlot(int index)
{
    if (!m_combo) return;
    if (index < 0 || index >= m_combo->count()) return;
    m_combo->blockSignals(true);
    m_combo->setCurrentIndex(index);
    m_combo->blockSignals(false);
}

void SlotManagerWidget::SetRecipeCount(int count)
{
    if (!m_infoLabel) return;
    QString text = QString("%1 recipe%2 in this slot")
        .arg(count)
        .arg(count == 1 ? "" : "s");
    m_infoLabel->setText(text);
}

void SlotManagerWidget::onComboBoxChanged(int index)
{
    if (index < 0 || !m_deps.manager) return;
    m_deps.manager->setActiveSlot(index);
    std::cout << "[SlotManagerWidget] Switched to slot: "
              << m_deps.manager->getSlotName(index) << std::endl;
    emit activeSlotChanged(index);
}

void SlotManagerWidget::onRenameClicked()
{
    if (!m_deps.manager) return;

    int currentIndex = m_deps.manager->getActiveSlotIndex();
    QString currentName = QString::fromStdString(m_deps.manager->getSlotName(currentIndex));

    bool ok = false;
    QString newName = QInputDialog::getText(m_deps.dialogParent, "Rename Slot",
        "Enter new name for slot:", QLineEdit::Normal, currentName, &ok);

    if (ok && !newName.isEmpty() && newName != currentName) {
        m_deps.manager->renameSlot(currentIndex, newName.toStdString());
        Reload();
        emit slotsModified();
    }
}

void SlotManagerWidget::onAddClicked()
{
    if (!m_deps.manager) return;

    if (m_deps.manager->getSlotCount() >= MAX_RECIPE_SLOTS) {
        QMessageBox::warning(m_deps.dialogParent, "Maximum Slots Reached",
            QString("Cannot add more than %1 slots.").arg(MAX_RECIPE_SLOTS));
        return;
    }

    bool ok = false;
    QString name = QInputDialog::getText(m_deps.dialogParent, "Add Slot",
        "Enter name for new slot:", QLineEdit::Normal,
        QString("Slot %1").arg(m_deps.manager->getSlotCount() + 1), &ok);

    if (ok && !name.isEmpty()) {
        int newIndex = m_deps.manager->createSlot(name.toStdString());
        if (newIndex >= 0) {
            m_deps.manager->setActiveSlot(newIndex);
            Reload();
            emit slotsModified();
            emit activeSlotChanged(newIndex);
        }
    }
}

void SlotManagerWidget::onDeleteClicked()
{
    if (!m_deps.manager) return;

    if (m_deps.manager->getSlotCount() <= 1) {
        QMessageBox::warning(m_deps.dialogParent, "Cannot Delete",
            "Cannot delete the last slot. A program must have at least one slot.");
        return;
    }

    int currentIndex = m_deps.manager->getActiveSlotIndex();
    QString slotName = QString::fromStdString(m_deps.manager->getSlotName(currentIndex));

    QMessageBox::StandardButton reply = QMessageBox::question(m_deps.dialogParent,
        "Delete Slot",
        QString("Delete slot '%1' and all its recipes?\n\nThis cannot be undone.").arg(slotName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        if (m_deps.manager->deleteSlot(currentIndex)) {
            Reload();
            emit slotsModified();
            emit activeSlotChanged(m_deps.manager->getActiveSlotIndex());
        }
    }
}

void SlotManagerWidget::onDuplicateClicked()
{
    if (!m_deps.manager) return;

    if (m_deps.manager->getSlotCount() >= MAX_RECIPE_SLOTS) {
        QMessageBox::warning(m_deps.dialogParent, "Maximum Slots Reached",
            QString("Cannot add more than %1 slots.").arg(MAX_RECIPE_SLOTS));
        return;
    }

    int currentIndex = m_deps.manager->getActiveSlotIndex();
    QString currentName = QString::fromStdString(m_deps.manager->getSlotName(currentIndex));

    bool ok = false;
    QString name = QInputDialog::getText(m_deps.dialogParent, "Duplicate Slot",
        "Enter name for the copy:", QLineEdit::Normal,
        currentName + " (Copy)", &ok);

    if (ok && !name.isEmpty()) {
        int newIndex = m_deps.manager->duplicateSlot(currentIndex, name.toStdString());
        if (newIndex >= 0) {
            m_deps.manager->setActiveSlot(newIndex);
            Reload();
            emit slotsModified();
            emit activeSlotChanged(newIndex);
        }
    }
}
