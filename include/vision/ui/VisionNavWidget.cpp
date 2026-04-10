// VisionNavWidget.cpp
#include "VisionNavWidget.h"
#include "VisionWizardState.h"
#include "ApplicationContext.h"
#include "product_position_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QInputDialog>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <iostream>

VisionNavWidget::VisionNavWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_watcher(new QFileSystemWatcher(this))
{
    setupUI();

    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &VisionNavWidget::onDirectoryChanged);
}

void VisionNavWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    setupProfileGroup(mainLayout);
    setupPluginSelector(mainLayout);
    setupStepGroup(mainLayout);
    setupImageModeGroup(mainLayout);

    // Status label
    m_statusLabel = new QLabel("No profile loaded");
    m_statusLabel->setStyleSheet("color: #888; font-size: 11px; padding: 5px;");
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    mainLayout->addStretch();
}

void VisionNavWidget::setupProfileGroup(QVBoxLayout* parentLayout)
{
    m_profileGroup = new QGroupBox("Profile");
    m_profileGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
    );
    QVBoxLayout* profileLayout = new QVBoxLayout(m_profileGroup);
    profileLayout->setSpacing(5);

    // Current profile name
    m_profileNameLabel = new QLabel("(none)");
    m_profileNameLabel->setStyleSheet(
        "QLabel { background-color: #1a1f2e; color: #ccc; border: 1px solid #3d5a8c; "
        "border-radius: 3px; padding: 5px 8px; font-size: 12px; }");
    profileLayout->addWidget(m_profileNameLabel);

    // Button style
    auto btnStyle = [](QPushButton* btn) {
        btn->setFixedHeight(28);
        btn->setStyleSheet(
            "QPushButton { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
            "border-radius: 3px; padding: 3px 8px; font-size: 11px; }"
            "QPushButton:hover { background-color: #3d5a8c; }"
            "QPushButton:disabled { background-color: #1a1f2e; color: #555; }"
        );
    };

    // Row 1: New, Load, Save, SaveAs
    QHBoxLayout* row1 = new QHBoxLayout();
    m_newBtn = new QPushButton("New");
    m_loadBtn = new QPushButton("Load");
    m_saveBtn = new QPushButton("Save");
    m_saveAsBtn = new QPushButton("Save As");
    btnStyle(m_newBtn);
    btnStyle(m_loadBtn);
    btnStyle(m_saveBtn);
    btnStyle(m_saveAsBtn);
    row1->addWidget(m_newBtn);
    row1->addWidget(m_loadBtn);
    row1->addWidget(m_saveBtn);
    row1->addWidget(m_saveAsBtn);
    profileLayout->addLayout(row1);

    // Row 2: Delete + Toggle list
    QHBoxLayout* row2 = new QHBoxLayout();
    m_deleteBtn = new QPushButton("Delete");
    m_toggleListBtn = new QPushButton("Show Profiles...");
    btnStyle(m_deleteBtn);
    btnStyle(m_toggleListBtn);
    row2->addWidget(m_deleteBtn);
    row2->addWidget(m_toggleListBtn);
    profileLayout->addLayout(row2);

    // Connect buttons
    connect(m_newBtn, &QPushButton::clicked, this, &VisionNavWidget::newProfileClicked);
    connect(m_loadBtn, &QPushButton::clicked, this, &VisionNavWidget::loadProfileClicked);
    connect(m_saveBtn, &QPushButton::clicked, this, &VisionNavWidget::saveProfileClicked);
    connect(m_saveAsBtn, &QPushButton::clicked, this, &VisionNavWidget::saveAsProfileClicked);
    connect(m_deleteBtn, &QPushButton::clicked, this, &VisionNavWidget::deleteProfileClicked);
    connect(m_toggleListBtn, &QPushButton::clicked, this, &VisionNavWidget::onToggleProfileList);

    // Collapsible profile list (hidden by default)
    m_profileList = new QListWidget();
    m_profileList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_profileList->setToolTip("Double-click to load profile\nRight-click for options");
    m_profileList->setMaximumHeight(200);
    m_profileList->setStyleSheet(
        "QListWidget { background-color: #2d2d2d; border: 1px solid #555; border-radius: 3px; }"
        "QListWidget::item { padding: 5px; border-bottom: 1px solid #444; }"
        "QListWidget::item:selected { background-color: #1565C0; }"
        "QListWidget::item:hover { background-color: #3d3d3d; }"
    );
    m_profileList->setVisible(false);
    profileLayout->addWidget(m_profileList);

    connect(m_profileList, &QListWidget::itemDoubleClicked,
            this, &VisionNavWidget::onProfileDoubleClicked);
    connect(m_profileList, &QListWidget::customContextMenuRequested,
            this, &VisionNavWidget::onProfileContextMenu);

    parentLayout->addWidget(m_profileGroup);
}

void VisionNavWidget::setupPluginSelector(QVBoxLayout* parentLayout)
{
    QGroupBox* pluginGroup = new QGroupBox("Plugin");
    pluginGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
    );
    QVBoxLayout* pluginLayout = new QVBoxLayout(pluginGroup);
    pluginLayout->setSpacing(5);

    m_selectedPluginLabel = new QLabel("(none selected)");
    m_selectedPluginLabel->setStyleSheet(
        "QLabel { background-color: #1a1f2e; color: #ccc; border: 1px solid #3d5a8c; "
        "border-radius: 3px; padding: 5px 8px; font-size: 12px; }");
    pluginLayout->addWidget(m_selectedPluginLabel);

    m_selectPluginBtn = new QPushButton("Select Plugin...");
    m_selectPluginBtn->setFixedHeight(28);
    m_selectPluginBtn->setStyleSheet(
        "QPushButton { background-color: #1565C0; color: white; border: 1px solid #1976D2; "
        "border-radius: 3px; padding: 3px 8px; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background-color: #1976D2; }"
    );
    connect(m_selectPluginBtn, &QPushButton::clicked,
            this, &VisionNavWidget::onSelectPluginClicked);
    pluginLayout->addWidget(m_selectPluginBtn);

    parentLayout->addWidget(pluginGroup);
}

void VisionNavWidget::onSelectPluginClicked()
{
    PluginSelectionDialog dialog(this);
    dialog.setPluginsByCategory(m_prPlugins, m_measurePlugins, m_filterPlugins, m_calibrationPlugins);

    if (dialog.exec() == QDialog::Accepted) {
        QString selected = dialog.getSelectedPlugin();
        if (!selected.isEmpty()) {
            m_selectedPluginLabel->setText(selected);
            emit pluginSelected(selected);
        }
    }
}

void VisionNavWidget::setSelectedPluginName(const QString& name)
{
    if (name.isEmpty()) {
        m_selectedPluginLabel->setText("(none selected)");
    } else {
        m_selectedPluginLabel->setText(name);
    }
}

void VisionNavWidget::updatePluginsByCategory(const QStringList& prPlugins,
                                               const QStringList& measurePlugins,
                                               const QStringList& filterPlugins,
                                               const QStringList& calibrationPlugins)
{
    m_prPlugins = prPlugins;
    m_measurePlugins = measurePlugins;
    m_filterPlugins = filterPlugins;
    m_calibrationPlugins = calibrationPlugins;
}

void VisionNavWidget::setupStepGroup(QVBoxLayout* parentLayout)
{
    QGroupBox* stepGroup = new QGroupBox("Steps");
    stepGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
    );
    QVBoxLayout* stepLayout = new QVBoxLayout(stepGroup);

    m_navList = new QListWidget();
    m_navList->setStyleSheet(
        "QListWidget { background-color: #1a1f2e; color: white; border: none; font-size: 13px; }"
        "QListWidget::item { padding: 10px 8px; border-bottom: 1px solid #2d3e5f; }"
        "QListWidget::item:selected { background-color: #3d5a8c; }"
        "QListWidget::item:hover { background-color: #2d3e5f; }"
    );
    m_navList->addItem("1. Plugin Selection");
    m_navList->addItem("2. Camera & Lighting");
    m_navList->addItem("3. Plugin Parameters");
    m_navList->addItem("4. Execute & Tune");

    connect(m_navList, &QListWidget::currentRowChanged,
            this, &VisionNavWidget::onNavItemChanged);

    stepLayout->addWidget(m_navList);
    parentLayout->addWidget(stepGroup);
}

void VisionNavWidget::setupImageModeGroup(QVBoxLayout* parentLayout)
{
    QGroupBox* modeGroup = new QGroupBox("Image Mode");
    modeGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
    );
    QHBoxLayout* modeLayout = new QHBoxLayout(modeGroup);

    m_frameBtn = new QPushButton("Frame");
    m_liveBtn = new QPushButton("Live");

    auto activeStyle =
        "QPushButton { background-color: #2E7D32; color: white; font-weight: bold; "
        "padding: 6px 12px; border-radius: 3px; border: 1px solid #4CAF50; }"
        "QPushButton:hover { background-color: #388E3C; }";
    auto inactiveStyle =
        "QPushButton { background-color: #2d3e5f; color: #aaa; "
        "padding: 6px 12px; border-radius: 3px; border: 1px solid #4d6e9f; }"
        "QPushButton:hover { background-color: #3d5a8c; }";

    // Default: Frame mode active
    m_frameBtn->setStyleSheet(activeStyle);
    m_liveBtn->setStyleSheet(inactiveStyle);

    connect(m_frameBtn, &QPushButton::clicked, this, [this, activeStyle, inactiveStyle]() {
        if (m_isLiveMode) {
            m_isLiveMode = false;
            m_frameBtn->setStyleSheet(activeStyle);
            m_liveBtn->setStyleSheet(inactiveStyle);
            emit imageModeChanged(false);
        }
    });

    connect(m_liveBtn, &QPushButton::clicked, this, [this, activeStyle, inactiveStyle]() {
        if (!m_isLiveMode) {
            m_isLiveMode = true;
            m_liveBtn->setStyleSheet(activeStyle);
            m_frameBtn->setStyleSheet(inactiveStyle);
            emit imageModeChanged(true);
        }
    });

    modeLayout->addWidget(m_frameBtn);
    modeLayout->addWidget(m_liveBtn);
    parentLayout->addWidget(modeGroup);
}

// ========== Public Methods ==========

void VisionNavWidget::updateCautionIcons(const VisionWizardState& state)
{
    // Unicode caution/check icons
    const QString caution = QString::fromUtf8("\xe2\x9a\xa0 ");   // warning triangle
    const QString check = QString::fromUtf8("\xe2\x9c\x93 ");     // checkmark

    m_navList->item(0)->setText(
        (state.isPluginCautionActive() ? caution : check) + "1. Plugin Selection");
    m_navList->item(1)->setText(
        (state.isCameraCautionActive() ? caution : check) + "2. Camera & Lighting");
    m_navList->item(2)->setText(
        (state.isCaptureCautionActive() ? caution : check) + "3. Plugin Parameters");
    m_navList->item(3)->setText(
        (state.isExecuteCautionActive() ? caution : check) + "4. Execute & Tune");
}

void VisionNavWidget::setCurrentStep(int step)
{
    if (step >= 0 && step < m_navList->count()) {
        m_navList->setCurrentRow(step);
    }
}

void VisionNavWidget::setProfileName(const QString& name)
{
    if (name.isEmpty()) {
        m_profileNameLabel->setText("(none)");
    } else {
        m_profileNameLabel->setText(name);
    }
}

void VisionNavWidget::setStatusText(const QString& text)
{
    m_statusLabel->setText(text);
}

void VisionNavWidget::setVisionDirectory(const QString& dir)
{
    if (!m_visionDir.isEmpty()) {
        m_watcher->removePath(m_visionDir);
    }

    m_visionDir = dir;

    if (!m_visionDir.isEmpty() && QDir(m_visionDir).exists()) {
        m_watcher->addPath(m_visionDir);
    }

    refreshProfileList();
}

void VisionNavWidget::refreshProfileList()
{
    m_profileList->clear();

    QString visionDir = m_visionDir;
    if (visionDir.isEmpty()) {
        if (m_context && m_context->productPositionManager &&
            m_context->productPositionManager->HasLoadedProduct()) {
            QString productFilePath = QString::fromStdString(
                m_context->productPositionManager->GetCurrentProductPath());
            QString productDir = QFileInfo(productFilePath).path();
            visionDir = productDir + "/vision";
        } else {
            visionDir = "config/vision";
        }
    }

    QDir dir(visionDir);
    if (!dir.exists()) {
        m_profileList->addItem("(No profiles found)");
        m_profileList->item(0)->setFlags(Qt::NoItemFlags);
        return;
    }

    QStringList filters;
    filters << "*.json";
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::Name);

    QFileInfoList files = dir.entryInfoList();
    int count = 0;
    for (const QFileInfo& fileInfo : files) {
        if (fileInfo.fileName().endsWith(".recipe.json")) {
            continue;
        }

        auto* item = new QListWidgetItem(fileInfo.baseName());
        item->setData(Qt::UserRole, fileInfo.absoluteFilePath());
        item->setToolTip(fileInfo.absoluteFilePath());
        m_profileList->addItem(item);
        count++;
    }

    if (count == 0) {
        m_profileList->addItem("(No profiles found)");
        m_profileList->item(0)->setFlags(Qt::NoItemFlags);
    }

    std::cout << "[VisionNavWidget] Loaded " << count << " profiles from: "
              << visionDir.toStdString() << std::endl;
}

// ========== Private Slots ==========

void VisionNavWidget::onNavItemChanged(int currentRow)
{
    if (currentRow >= 0 && currentRow < 4) {
        emit navItemChanged(currentRow);
    }
}

void VisionNavWidget::onProfileDoubleClicked(QListWidgetItem* item)
{
    if (!item || item->flags() == Qt::NoItemFlags) {
        return;
    }

    QString filePath = item->data(Qt::UserRole).toString();
    if (!filePath.isEmpty()) {
        emit profileLoadRequested(filePath);
    }
}

void VisionNavWidget::onProfileContextMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_profileList->itemAt(pos);
    if (!item || item->flags() == Qt::NoItemFlags) {
        return;
    }

    QString filePath = item->data(Qt::UserRole).toString();

    QMenu contextMenu(this);
    QAction* loadAction = contextMenu.addAction("Load");
    contextMenu.addSeparator();
    QAction* renameAction = contextMenu.addAction("Rename");
    QAction* deleteAction = contextMenu.addAction("Delete");

    QAction* selectedAction = contextMenu.exec(m_profileList->mapToGlobal(pos));

    if (selectedAction == loadAction) {
        emit profileLoadRequested(filePath);
    } else if (selectedAction == renameAction) {
        renameProfile(filePath);
    } else if (selectedAction == deleteAction) {
        deleteProfile(filePath);
    }
}

void VisionNavWidget::onDirectoryChanged(const QString& /*path*/)
{
    refreshProfileList();
}

void VisionNavWidget::onToggleProfileList()
{
    m_profileListVisible = !m_profileListVisible;
    m_profileList->setVisible(m_profileListVisible);
    m_toggleListBtn->setText(m_profileListVisible ? "Hide Profiles" : "Show Profiles...");

    if (m_profileListVisible) {
        refreshProfileList();
    }
}

void VisionNavWidget::deleteProfile(const QString& filePath)
{
    QFileInfo fileInfo(filePath);

    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Delete Profile",
        QString("Are you sure you want to delete?\n\n%1").arg(fileInfo.fileName()),
        QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    QFile file(filePath);
    if (file.remove()) {
        std::cout << "[VisionNavWidget] Deleted: " << filePath.toStdString() << std::endl;
        emit profileDeleted(filePath);
        refreshProfileList();
    } else {
        QMessageBox::critical(this, "Error",
            QString("Failed to delete file:\n%1").arg(filePath));
    }
}

void VisionNavWidget::renameProfile(const QString& filePath)
{
    QFileInfo fileInfo(filePath);
    QString oldName = fileInfo.baseName();

    bool ok = false;
    QString newName = QInputDialog::getText(this, "Rename Profile",
        "Enter new name:", QLineEdit::Normal, oldName, &ok);

    if (!ok || newName.isEmpty() || newName == oldName) {
        return;
    }

    if (newName.contains(QRegularExpression("[\\\\/:*?\"<>|]"))) {
        QMessageBox::warning(this, "Invalid Name",
            "Profile name cannot contain: \\ / : * ? \" < > |");
        return;
    }

    QString newPath = fileInfo.path() + "/" + newName + ".json";

    if (QFile::exists(newPath)) {
        QMessageBox::warning(this, "Name Exists",
            QString("A profile named '%1' already exists.").arg(newName));
        return;
    }

    QFile file(filePath);
    if (file.rename(newPath)) {
        std::cout << "[VisionNavWidget] Renamed: " << oldName.toStdString()
                  << " -> " << newName.toStdString() << std::endl;
        emit profileRenamed(filePath, newPath);
        refreshProfileList();
    } else {
        QMessageBox::critical(this, "Error",
            QString("Failed to rename file:\n%1").arg(file.errorString()));
    }
}

// ========== PluginSelectionDialog ==========

PluginSelectionDialog::PluginSelectionDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Select Vision Plugin");
    setMinimumSize(450, 400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* titleLabel = new QLabel("Select a vision plugin:");
    titleLabel->setStyleSheet("font-weight: bold; font-size: 13px;");
    mainLayout->addWidget(titleLabel);

    m_tabs = new QTabWidget(this);

    m_prList = new QListWidget();
    m_tabs->addTab(m_prList, "PR");

    m_measureList = new QListWidget();
    m_tabs->addTab(m_measureList, "Measure");

    m_filterList = new QListWidget();
    m_tabs->addTab(m_filterList, "Filter");

    m_calibrationList = new QListWidget();
    m_tabs->addTab(m_calibrationList, "Calib");

    mainLayout->addWidget(m_tabs, 1);

    // Double-click to select
    auto connectList = [this](QListWidget* list) {
        connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
            m_selectedPlugin = item->text();
            accept();
        });
    };
    connectList(m_prList);
    connectList(m_measureList);
    connectList(m_filterList);
    connectList(m_calibrationList);

    // OK / Cancel buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* okBtn = new QPushButton("OK");
    okBtn->setStyleSheet(
        "QPushButton { background-color: #2E7D32; color: white; font-weight: bold; padding: 6px 20px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #388E3C; }"
    );
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        // Get selected from whichever tab is active
        QListWidget* currentList = nullptr;
        switch (m_tabs->currentIndex()) {
        case 0: currentList = m_prList; break;
        case 1: currentList = m_measureList; break;
        case 2: currentList = m_filterList; break;
        case 3: currentList = m_calibrationList; break;
        }
        if (currentList && currentList->currentItem()) {
            m_selectedPlugin = currentList->currentItem()->text();
            accept();
        }
    });

    QPushButton* cancelBtn = new QPushButton("Cancel");
    cancelBtn->setStyleSheet(
        "QPushButton { background-color: #555; color: white; padding: 6px 20px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #666; }"
    );
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}

void PluginSelectionDialog::setPluginsByCategory(const QStringList& prPlugins,
                                                   const QStringList& measurePlugins,
                                                   const QStringList& filterPlugins,
                                                   const QStringList& calibrationPlugins)
{
    m_prList->clear();
    m_measureList->clear();
    m_filterList->clear();
    m_calibrationList->clear();

    for (const QString& p : prPlugins) { m_prList->addItem(p); }
    for (const QString& p : measurePlugins) { m_measureList->addItem(p); }
    for (const QString& p : filterPlugins) { m_filterList->addItem(p); }
    for (const QString& p : calibrationPlugins) { m_calibrationList->addItem(p); }
}

// ========== ProfileSelectionDialog ==========

ProfileSelectionDialog::ProfileSelectionDialog(const QString& visionDir, QWidget* parent)
    : QDialog(parent)
    , m_profileList(nullptr)
    , m_visionDir(visionDir)
{
    setWindowTitle("Load Vision Profile");
    setMinimumSize(400, 350);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Directory label
    QLabel* dirLabel = new QLabel("Profiles in: " + visionDir);
    dirLabel->setStyleSheet("color: #888; font-size: 11px; font-style: italic;");
    dirLabel->setWordWrap(true);
    mainLayout->addWidget(dirLabel);

    // Profile list
    m_profileList = new QListWidget(this);
    m_profileList->setStyleSheet(
        "QListWidget { background-color: #2d2d2d; border: 1px solid #555; border-radius: 3px; font-size: 13px; }"
        "QListWidget::item { padding: 8px 10px; border-bottom: 1px solid #444; }"
        "QListWidget::item:selected { background-color: #1565C0; }"
        "QListWidget::item:hover { background-color: #3d3d3d; }"
    );
    mainLayout->addWidget(m_profileList, 1);

    // Populate list
    QDir dir(visionDir);
    if (dir.exists()) {
        QStringList filters;
        filters << "*.json";
        dir.setNameFilters(filters);
        dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
        dir.setSorting(QDir::Name);

        for (const QFileInfo& fi : dir.entryInfoList()) {
            if (fi.fileName().endsWith(".recipe.json")) {
                continue;
            }
            auto* item = new QListWidgetItem(fi.baseName());
            item->setData(Qt::UserRole, fi.absoluteFilePath());
            item->setToolTip(fi.absoluteFilePath());
            m_profileList->addItem(item);
        }
    }

    if (m_profileList->count() == 0) {
        auto* item = new QListWidgetItem("(No profiles found)");
        item->setFlags(Qt::NoItemFlags);
        m_profileList->addItem(item);
    }

    // Double-click to select
    connect(m_profileList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item && item->flags() != Qt::NoItemFlags) {
            m_selectedFilePath = item->data(Qt::UserRole).toString();
            accept();
        }
    });

    // OK / Cancel buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();

    QPushButton* okBtn = new QPushButton("Load");
    okBtn->setStyleSheet(
        "QPushButton { background-color: #2E7D32; color: white; font-weight: bold; padding: 6px 20px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #388E3C; }"
    );
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        auto* current = m_profileList->currentItem();
        if (current && current->flags() != Qt::NoItemFlags) {
            m_selectedFilePath = current->data(Qt::UserRole).toString();
            accept();
        }
    });

    QPushButton* cancelBtn = new QPushButton("Cancel");
    cancelBtn->setStyleSheet(
        "QPushButton { background-color: #555; color: white; padding: 6px 20px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #666; }"
    );
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}
