// ProgramEditPanel.cpp
#include "ProgramEditPanel.h"

#include "ApplicationContext.h"
#include "DependencyGraphWindow.h"
#include "FileSelectorDialog.h"
#include "ProgramManager.h"
#include "RecipeManager.h"
#include "Theme.h"
#include "ThemeButtonStyles.h"
#include "product_position_manager.h"

#include <QAction>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QVBoxLayout>

#include <iostream>

ProgramEditPanel::ProgramEditPanel(Deps deps, QWidget* parent)
    : QWidget(parent)
    , m_deps(std::move(deps))
{
    buildUi();
}

ProgramEditPanel::~ProgramEditPanel() = default;

void ProgramEditPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Add Recipe / Add Script buttons (above the table).
    auto* addButtonsLayout = new QHBoxLayout();
    addButtonsLayout->setSpacing(6);

    m_addRecipeButton = new QPushButton("+ Add Recipe");
    m_addRecipeButton->setMinimumHeight(40);
    m_addRecipeButton->setStyleSheet(Theme::Buttons::bezelSuccess());
    addButtonsLayout->addWidget(m_addRecipeButton);

    m_addScriptButton = new QPushButton("+ Add Script");
    m_addScriptButton->setMinimumHeight(40);
    m_addScriptButton->setStyleSheet(Theme::Buttons::bezelPrimary());
    addButtonsLayout->addWidget(m_addScriptButton);

    layout->addLayout(addButtonsLayout);

    // Recipe table + preview (side by side)
    auto* recipeSplitter = new QSplitter(Qt::Horizontal);

    // Left: Recipe table
    auto* tableContainer = new QWidget();
    auto* tableLayout = new QVBoxLayout(tableContainer);
    tableLayout->setContentsMargins(0, 0, 0, 0);

    auto* tableLabel = new QLabel("Recipes in Program");
    tableLabel->setStyleSheet(QString("font-weight: bold; font-size: 13px; color: %1;")
                                  .arg(QLatin1String(Theme::Color::textPrimary)));
    tableLayout->addWidget(tableLabel);

    m_recipeTable = new QTableWidget();
    m_recipeTable->setColumnCount(5);
    m_recipeTable->setHorizontalHeaderLabels({"#", "Recipe Name", "File", "Dependencies", "Status"});
    m_recipeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_recipeTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recipeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_recipeTable->setAlternatingRowColors(true);
    m_recipeTable->verticalHeader()->setVisible(false);
    m_recipeTable->horizontalHeader()->setStretchLastSection(false);
    m_recipeTable->setColumnWidth(0, 40);
    m_recipeTable->setColumnWidth(1, 200);
    m_recipeTable->setColumnWidth(2, 180);
    m_recipeTable->setColumnWidth(3, 120);
    m_recipeTable->setColumnWidth(4, 80);
    m_recipeTable->setStyleSheet(QString(
        "QTableWidget { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 4px; "
        "  gridline-color: %2; "
        "  font-size: 16px; "
        "  color: %3; "
        "  show-decoration-selected: 1; "
        "} "
        "QTableWidget::item { "
        "  border-bottom: 1px solid %2; "
        "  padding: 10px 8px; "
        "} "
        "QTableWidget::item:hover { "
        "  background-color: %4; "
        "} "
        "QTableWidget::item:selected { "
        "  background-color: %5; "
        "  color: %6; "
        "} "
        "QHeaderView::section { "
        "  background-color: %7; "
        "  color: %3; "
        "  padding: 8px; "
        "  border: 1px solid %2; "
        "  font-size: 15px; "
        "  font-weight: bold; "
        "}"
    ).arg(QLatin1String(Theme::Color::bgDeep),
          QLatin1String(Theme::Color::borderSubtle),
          QLatin1String(Theme::Color::textPrimary),
          QLatin1String(Theme::Color::bgHover),
          QLatin1String(Theme::Color::primary),
          QLatin1String(Theme::Color::textOnPrimary),
          QLatin1String(Theme::Color::bgPrimary)));

    m_recipeTable->verticalHeader()->setDefaultSectionSize(42);
    m_recipeTable->verticalHeader()->setMinimumSectionSize(38);
    m_recipeTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_recipeTable, &QTableWidget::customContextMenuRequested,
            this, &ProgramEditPanel::onContextMenuRequested);

    tableLayout->addWidget(m_recipeTable);
    recipeSplitter->addWidget(tableContainer);

    // Right: Preview pane
    auto* previewContainer = new QWidget();
    auto* previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(0, 0, 0, 0);

    auto* previewLabel = new QLabel("Recipe Preview");
    previewLabel->setStyleSheet(QString("font-weight: bold; font-size: 13px; color: %1;")
                                    .arg(QLatin1String(Theme::Color::textPrimary)));
    previewLayout->addWidget(previewLabel);

    m_previewText = new QTextEdit();
    m_previewText->setReadOnly(true);
    m_previewText->setPlaceholderText("Select a recipe to see its steps...");
    m_previewText->setStyleSheet(QString(
        "QTextEdit { "
        "  font-family: 'Consolas', 'Courier New', monospace; "
        "  font-size: 12px; "
        "  background-color: %1; "
        "  color: %2; "
        "  border: 1px solid %3; "
        "  border-radius: 4px; "
        "}"
    ).arg(QLatin1String(Theme::Color::bgDeep),
          QLatin1String(Theme::Color::textPrimary),
          QLatin1String(Theme::Color::borderSubtle)));
    previewLayout->addWidget(m_previewText);

    recipeSplitter->addWidget(previewContainer);
    recipeSplitter->setStretchFactor(0, 6);
    recipeSplitter->setStretchFactor(1, 4);

    layout->addWidget(recipeSplitter, 1);

    // Bottom controls
    auto* controlLayout = new QHBoxLayout();
    m_removeRecipeButton = new QPushButton("Remove");
    m_removeRecipeButton->setStyleSheet(Theme::Buttons::bezelDanger());

    m_moveUpButton = new QPushButton(QString::fromUtf8("\xE2\x86\x91 Move Up"));
    m_moveUpButton->setStyleSheet(Theme::Buttons::bezelNeutral());

    m_moveDownButton = new QPushButton(QString::fromUtf8("\xE2\x86\x93 Move Down"));
    m_moveDownButton->setStyleSheet(Theme::Buttons::bezelNeutral());

    m_toggleEnableButton = new QPushButton(QString::fromUtf8("\xE2\x87\x84 Toggle Enable/Disable"));
    m_toggleEnableButton->setStyleSheet(Theme::Buttons::bezelWarning());
    m_toggleEnableButton->setEnabled(false);

    m_manageDepsButton = new QPushButton(QString::fromUtf8("\xF0\x9F\x94\x97 Manage Dependencies"));
    m_manageDepsButton->setStyleSheet(Theme::Buttons::bezelPrimary());

    controlLayout->addWidget(m_removeRecipeButton);
    controlLayout->addWidget(m_moveUpButton);
    controlLayout->addWidget(m_moveDownButton);
    controlLayout->addSpacing(10);
    controlLayout->addWidget(m_toggleEnableButton);
    controlLayout->addWidget(m_manageDepsButton);
    controlLayout->addStretch();

    layout->addLayout(controlLayout);

    // Wire signals
    connect(m_addRecipeButton,    &QPushButton::clicked, this, &ProgramEditPanel::onAddRecipe);
    connect(m_addScriptButton,    &QPushButton::clicked, this, &ProgramEditPanel::onAddScript);
    connect(m_removeRecipeButton, &QPushButton::clicked, this, &ProgramEditPanel::onRemoveRecipe);
    connect(m_moveUpButton,       &QPushButton::clicked, this, &ProgramEditPanel::onMoveRecipeUp);
    connect(m_moveDownButton,     &QPushButton::clicked, this, &ProgramEditPanel::onMoveRecipeDown);
    connect(m_toggleEnableButton, &QPushButton::clicked, this, &ProgramEditPanel::onToggleRecipeEnabled);
    connect(m_manageDepsButton,   &QPushButton::clicked, this, &ProgramEditPanel::onManageDependencies);
    connect(m_recipeTable, &QTableWidget::itemSelectionChanged,
            this, &ProgramEditPanel::onSelectionChanged);
}

void ProgramEditPanel::Reload()
{
    if (!m_deps.manager || !m_recipeTable) return;

    m_recipeTable->setRowCount(0);

    int rowIndex = 0;
    for (const auto& recipe : m_deps.manager->getAllRecipes()) {
        m_recipeTable->insertRow(rowIndex);

        auto* numItem = new QTableWidgetItem(QString::number(rowIndex + 1));
        numItem->setTextAlignment(Qt::AlignCenter);
        m_recipeTable->setItem(rowIndex, 0, numItem);

        auto* nameItem = new QTableWidgetItem(QString::fromStdString(recipe->displayName));
        m_recipeTable->setItem(rowIndex, 1, nameItem);

        QFileInfo recipeFileInfo(QString::fromStdString(recipe->recipeFilePath));
        QString fileDisplay = recipeFileInfo.fileName();
        if (recipe->type == RecipeReference::SCRIPT) {
            fileDisplay = "[LUA] " + fileDisplay;
        }
        auto* fileItem = new QTableWidgetItem(fileDisplay);
        fileItem->setForeground(recipe->type == RecipeReference::SCRIPT
                                    ? QColor("#60a5fa") : QColor("#a1a1aa"));
        fileItem->setToolTip(QString::fromStdString(recipe->recipeFilePath));
        m_recipeTable->setItem(rowIndex, 2, fileItem);

        std::vector<DependencyLink> deps = m_deps.manager->getRecipeDependencies(rowIndex);
        QString depsText;
        if (deps.empty()) {
            depsText = QString::fromUtf8("\xE2\x80\x94");
        } else {
            for (size_t i = 0; i < deps.size(); ++i) {
                if (i > 0) depsText += ", ";
                depsText += QString::number(deps[i].predecessorIndex + 1);
            }
        }
        auto* depsItem = new QTableWidgetItem(depsText);
        depsItem->setTextAlignment(Qt::AlignCenter);
        depsItem->setForeground(QColor(155, 135, 245));
        m_recipeTable->setItem(rowIndex, 3, depsItem);

        QString status = recipe->enabled ? "Enabled" : "Disabled";
        auto* statusItem = new QTableWidgetItem(status);
        statusItem->setTextAlignment(Qt::AlignCenter);
        if (!recipe->enabled) {
            statusItem->setForeground(QColor(150, 150, 150));
            numItem->setForeground(QColor(150, 150, 150));
            nameItem->setForeground(QColor(150, 150, 150));
            fileItem->setForeground(QColor(150, 150, 150));
            depsItem->setForeground(QColor(150, 150, 150));
        }
        m_recipeTable->setItem(rowIndex, 4, statusItem);

        rowIndex++;
    }
}

void ProgramEditPanel::Clear()
{
    if (m_recipeTable) m_recipeTable->setRowCount(0);
    if (m_previewText) {
        m_previewText->clear();
        m_previewText->setPlaceholderText("Select a recipe to see its steps...");
    }
}

void ProgramEditPanel::onAddRecipe()
{
    if (!m_deps.context || !m_deps.context->productPositionManager ||
        !m_deps.context->productPositionManager->HasLoadedProduct()) {
        QMessageBox::warning(this, "No Product Loaded",
            "Please load a product first before adding recipes.\n\n"
            "Use the 'Load Product' button in the main menu.");
        return;
    }

    QString productName = QString::fromStdString(
        m_deps.context->productPositionManager->GetCurrentProductName());
    QString recipesPath = QString("config/%1/recipes").arg(productName);

    QString filename = FileSelectorDialog::getOpenFileName(
        this, recipesPath, "*.json", "Add Recipe", "recipe files", true);
    if (filename.isEmpty()) return;

    QFileInfo fileInfo(filename);
    QString displayName = fileInfo.baseName();

    m_deps.manager->addRecipe(filename.toStdString(), displayName.toStdString());
    emit recipeListChanged();
}

void ProgramEditPanel::onAddScript()
{
    if (!m_deps.context || !m_deps.context->productPositionManager ||
        !m_deps.context->productPositionManager->HasLoadedProduct()) {
        QMessageBox::warning(this, "No Product Loaded",
            "Please load a product first before adding scripts.");
        return;
    }

    QString productName = QString::fromStdString(
        m_deps.context->productPositionManager->GetCurrentProductName());
    QString scriptsPath = QString("config/%1/scripts").arg(productName);
    if (!QDir(scriptsPath).exists()) {
        scriptsPath = "scripts";
    }

    QString filename = QFileDialog::getOpenFileName(
        this, "Add Script", scriptsPath, "Lua Scripts (*.lua)");
    if (filename.isEmpty()) return;

    QFileInfo fileInfo(filename);
    QString displayName = fileInfo.baseName();

    m_deps.manager->addScript(filename.toStdString(), displayName.toStdString());
    emit recipeListChanged();
}

void ProgramEditPanel::onRemoveRecipe()
{
    int selectedRow = m_recipeTable->currentRow();
    if (selectedRow < 0) {
        QMessageBox::warning(this, "No Selection", "Please select a recipe to remove");
        return;
    }
    m_deps.manager->removeRecipe(selectedRow);
    emit recipeListChanged();
}

void ProgramEditPanel::onMoveRecipeUp()
{
    int selectedRow = m_recipeTable->currentRow();
    if (selectedRow <= 0) return;
    if (m_deps.manager->moveRecipeUp(selectedRow)) {
        emit recipeListChanged();
        m_recipeTable->selectRow(selectedRow - 1);
    }
}

void ProgramEditPanel::onMoveRecipeDown()
{
    int selectedRow = m_recipeTable->currentRow();
    if (selectedRow < 0 || selectedRow >= m_deps.manager->getRecipeCount() - 1) return;
    if (m_deps.manager->moveRecipeDown(selectedRow)) {
        emit recipeListChanged();
        m_recipeTable->selectRow(selectedRow + 1);
    }
}

void ProgramEditPanel::onToggleRecipeEnabled()
{
    int selectedRow = m_recipeTable->currentRow();
    if (selectedRow < 0) return;
    const RecipeReference* recipe = m_deps.manager->getRecipe(selectedRow);
    if (!recipe) return;

    bool newState = !recipe->enabled;
    m_deps.manager->setRecipeEnabled(selectedRow, newState);
    emit recipeListChanged();
    m_recipeTable->selectRow(selectedRow);
}

void ProgramEditPanel::onManageDependencies()
{
    int recipeCount = m_deps.manager->getRecipeCount();
    if (recipeCount == 0) {
        QMessageBox::information(this, "No Recipes",
            "Please add recipes first before managing dependencies");
        return;
    }

    auto* graphWindow = new DependencyGraphWindow(m_deps.manager, this);
    graphWindow->setAttribute(Qt::WA_DeleteOnClose);
    graphWindow->exec();

    // Auto-save program when dialog closes (if we have a save path).
    if (m_deps.currentProgramPath) {
        QString programPath = m_deps.currentProgramPath();
        if (!programPath.isEmpty()) {
            if (m_deps.manager->saveProgram(programPath.toStdString())) {
                std::cout << "[ProgramEditPanel] Auto-saved program after dependency changes"
                          << std::endl;
            }
        }
    }

    emit recipeListChanged();
}

void ProgramEditPanel::onSelectionChanged()
{
    int currentRow = m_recipeTable->currentRow();
    bool hasSelection = currentRow >= 0;

    m_removeRecipeButton->setEnabled(hasSelection);
    m_moveUpButton->setEnabled(hasSelection && currentRow > 0);
    m_moveDownButton->setEnabled(
        hasSelection && currentRow < m_deps.manager->getRecipeCount() - 1);
    m_toggleEnableButton->setEnabled(hasSelection);

    if (hasSelection) {
        const RecipeReference* recipe = m_deps.manager->getRecipe(currentRow);
        if (recipe) {
            loadRecipePreview(recipe->recipeFilePath);
        }
    } else {
        m_previewText->clear();
        m_previewText->setPlaceholderText("Select a recipe to see its steps...");
    }
}

void ProgramEditPanel::onContextMenuRequested(const QPoint& pos)
{
    QTableWidgetItem* item = m_recipeTable->itemAt(pos);
    if (!item) return;

    int row = item->row();
    if (row < 0 || row >= m_deps.manager->getRecipeCount()) return;

    QMenu contextMenu(this);
    const QString menuStyle =
        "QMenu { "
        "  background-color: #2d2d30; "
        "  color: white; "
        "  border: 1px solid #3c3c3c; "
        "  padding: 5px; "
        "} "
        "QMenu::item { "
        "  padding: 8px 20px; "
        "} "
        "QMenu::item:selected { "
        "  background-color: #0e639c; "
        "} "
        "QMenu::item:disabled { "
        "  color: #666666; "
        "}";
    contextMenu.setStyleSheet(menuStyle);

    QAction* renameAction        = contextMenu.addAction("Rename Recipe...");
    QAction* inspectMotionAction = contextMenu.addAction("Inspect Motion");
    inspectMotionAction->setToolTip(
        "Show motion-related steps (velocity, axis, distance, position) in a table");

    // "Move to Slot" submenu when there are multiple slots.
    QMenu* moveToSlotMenu = nullptr;
    std::vector<QAction*> slotActions;
    if (m_deps.manager->getSlotCount() > 1) {
        contextMenu.addSeparator();
        moveToSlotMenu = contextMenu.addMenu("Move to Slot");
        moveToSlotMenu->setStyleSheet(menuStyle);

        int currentSlot = m_deps.manager->getActiveSlotIndex();
        auto slotNames = m_deps.manager->getSlotNames();
        for (int i = 0; i < static_cast<int>(slotNames.size()); ++i) {
            if (i != currentSlot) {
                QAction* slotAction = moveToSlotMenu->addAction(QString::fromStdString(slotNames[i]));
                slotAction->setData(i);
                slotActions.push_back(slotAction);
            }
        }
    }

    // Retrace edit only when not currently executing.
    QAction* retraceAction = nullptr;
    bool isExec = m_deps.isExecuting && m_deps.isExecuting();
    if (!isExec) {
        contextMenu.addSeparator();
        retraceAction = contextMenu.addAction("Retrace Edit This Recipe...");
        retraceAction->setToolTip(
            "Run through recipe and pause at each motion for position adjustment");
    }

    QAction* selectedAction = contextMenu.exec(
        m_recipeTable->viewport()->mapToGlobal(pos));

    if (selectedAction == renameAction) {
        renameRecipe(row);
    } else if (selectedAction == inspectMotionAction) {
        emit inspectMotionRequested(row);
    } else if (retraceAction && selectedAction == retraceAction) {
        emit retraceEditRequested(row);
    } else if (moveToSlotMenu) {
        for (QAction* slotAction : slotActions) {
            if (selectedAction == slotAction) {
                int targetSlotIndex = slotAction->data().toInt();
                QString recipeName = QString::fromStdString(
                    m_deps.manager->getRecipe(row)->displayName);
                QString targetSlotName = QString::fromStdString(
                    m_deps.manager->getSlotName(targetSlotIndex));

                if (m_deps.manager->moveRecipeToSlot(row, targetSlotIndex)) {
                    emit recipeListChanged();
                    std::cout << "[ProgramEditPanel] Moved recipe '"
                              << recipeName.toStdString() << "' to slot '"
                              << targetSlotName.toStdString() << "'" << std::endl;
                } else {
                    QMessageBox::warning(this, "Move Failed",
                        QString("Failed to move recipe '%1' to slot '%2'.")
                            .arg(recipeName).arg(targetSlotName));
                }
                break;
            }
        }
    }
}

void ProgramEditPanel::renameRecipe(int recipeIndex)
{
    const RecipeReference* recipe = m_deps.manager->getRecipe(recipeIndex);
    if (!recipe) return;

    QString currentName    = QString::fromStdString(recipe->displayName);
    QString recipeFilePath = QString::fromStdString(recipe->recipeFilePath);

    bool ok = false;
    QString newName = QInputDialog::getText(this, "Rename Recipe",
        "Enter new recipe name:", QLineEdit::Normal, currentName, &ok);
    if (!ok || newName.trimmed().isEmpty()) return;

    newName = newName.trimmed();
    if (newName == currentName) return;

    const QString invalidChars = "<>:\"/\\|?*";
    for (QChar c : invalidChars) {
        if (newName.contains(c)) {
            QMessageBox::warning(this, "Invalid Name",
                "Recipe name contains invalid characters.\n\n"
                "Invalid characters: < > : \" / \\ | ? *");
            return;
        }
    }

    QFileInfo oldFileInfo(recipeFilePath);
    QString newFilePath = oldFileInfo.absolutePath() + "/" + newName + ".json";

    if (newFilePath != recipeFilePath && QFile::exists(newFilePath)) {
        QMessageBox::warning(this, "Name Exists",
            QString("A recipe file named '%1.json' already exists.").arg(newName));
        return;
    }

    QFile file(recipeFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Error", "Failed to open recipe file for reading.");
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(fileData);
    if (!doc.isObject()) {
        QMessageBox::critical(this, "Error", "Recipe file is not valid JSON.");
        return;
    }

    QJsonObject obj = doc.object();
    obj["recipe_name"] = newName;

    QFile newFile(newFilePath);
    if (!newFile.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "Error", "Failed to write renamed recipe file.");
        return;
    }
    newFile.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    newFile.close();

    if (newFilePath != recipeFilePath) {
        QFile::remove(recipeFilePath);
    }

    m_deps.manager->setRecipeDisplayName(recipeIndex, newName.toStdString());
    RecipeReference* mutableRecipe = m_deps.manager->getRecipe(recipeIndex);
    if (mutableRecipe) {
        mutableRecipe->recipeFilePath = newFilePath.toStdString();
    }

    if (m_deps.currentProgramPath) {
        QString programPath = m_deps.currentProgramPath();
        if (!programPath.isEmpty()) {
            m_deps.manager->saveProgram(programPath.toStdString());
        }
    }

    QMessageBox::information(this, "Recipe Renamed",
        QString("Recipe renamed to \"%1\" and file saved.").arg(newName));

    emit recipeListChanged();
    m_recipeTable->selectRow(recipeIndex);
}

void ProgramEditPanel::loadRecipePreview(const std::string& recipeFilePath)
{
    RecipeManager tempRecipeManager;

    if (!tempRecipeManager.loadRecipe(recipeFilePath)) {
        m_previewText->setPlainText("Error: Failed to load recipe file");
        return;
    }

    QString previewText;
    previewText += QString("Recipe: %1\n")
        .arg(QString::fromStdString(tempRecipeManager.getRecipeName()));
    previewText += QString("Steps: %1\n").arg(tempRecipeManager.getStepCount());
    previewText += "\n";
    previewText += QString::fromUtf8(
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\n\n");

    int stepNumber = 1;
    for (const auto& step : tempRecipeManager.getAllSteps()) {
        if (step->enabled) {
            previewText += QString("%1. %2\n")
                .arg(stepNumber, 2, 10, QChar('0'))
                .arg(QString::fromStdString(step->userNickname));
            previewText += QString("   Type: %1\n")
                .arg(QString::fromStdString(step->processType));
        } else {
            previewText += QString("%1. %2 (DISABLED)\n")
                .arg(stepNumber, 2, 10, QChar('0'))
                .arg(QString::fromStdString(step->userNickname));
        }
        previewText += "\n";
        stepNumber++;
    }

    m_previewText->setPlainText(previewText);
}
