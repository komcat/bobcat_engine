// DataBackupPage.cpp - Product data backup and restore management
#include "DataBackupPage.h"
#include "ApplicationContext.h"
#include "product_position_manager.h"
#include "LogManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTextEdit>
#include <QTimer>
#include <QFrame>
#include <QMessageBox>
#include <QSettings>
#include <QCoreApplication>
#include <QDateTime>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>

#include <filesystem>
#include <algorithm>
#include <string>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal helpers (file-local)
// ---------------------------------------------------------------------------
namespace {

// Prefix path with \\?\ for Windows long path support (>260 chars)
fs::path longPath(const fs::path& p)
{
    std::wstring ws = p.wstring();
    if (ws.size() >= 4 && ws.substr(0, 4) == L"\\\\?\\") {
        return p;
    }
    // Convert forward slashes to backslashes for \\?\ prefix
    fs::path canonical = p.lexically_normal();
    if (canonical.is_absolute()) {
        return fs::path(L"\\\\?\\" + canonical.wstring());
    }
    // Relative paths: make absolute first
    std::error_code ec;
    fs::path abs = fs::absolute(canonical, ec);
    if (ec) {
        return p;
    }
    return fs::path(L"\\\\?\\" + abs.wstring());
}

// Sanitise a user-supplied name so it is safe as a directory component.
QString sanitizeName(const QString& name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar ch : name) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_')) {
            out.append(ch);
        } else if (ch == QLatin1Char(' ')) {
            out.append(QLatin1Char('_'));
        }
    }
    return out;
}

// Badge colour for each backup type.
QString typeColor(const QString& type)
{
    if (type == QStringLiteral("save_point")) return QStringLiteral("#22c55e");  // green
    if (type == QStringLiteral("auto"))       return QStringLiteral("#3b82f6");  // blue
    if (type == QStringLiteral("pre_revert")) return QStringLiteral("#eab308");  // yellow
    return QStringLiteral("#6b7280");
}

// Human-readable label for backup type.
QString typeLabel(const QString& type)
{
    if (type == QStringLiteral("save_point")) return QStringLiteral("Save Point");
    if (type == QStringLiteral("auto"))       return QStringLiteral("Auto");
    if (type == QStringLiteral("pre_revert")) return QStringLiteral("Pre-Revert");
    return type;
}

// Shared dialog stylesheet (light – same pattern used across the codebase).
const char* DIALOG_CSS =
    "QMessageBox { background-color: #f0f0f0; color: #000000; } "
    "QLabel { color: #000000; font-size: 12px; background-color: transparent; } "
    "QPushButton { background-color: #e0e0e0; color: #000000; border: 1px solid #999999; "
    "border-radius: 4px; padding: 6px 20px; font-size: 12px; min-width: 80px; } "
    "QPushButton:hover { background-color: #d0d0d0; } "
    "QPushButton:default { background-color: #5a8fd6; color: white; border-color: #4a7fc6; }";

}  // namespace

// ===========================================================================
// Constructor
// ===========================================================================

DataBackupPage::DataBackupPage(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_productLabel(nullptr)
    , m_descriptionEdit(nullptr)
    , m_createBtn(nullptr)
    , m_showChangesBtn(nullptr)
    , m_backBtn(nullptr)
    , m_autoBackupCheck(nullptr)
    , m_autoIntervalSpin(nullptr)
    , m_autoStatusLabel(nullptr)
    , m_backupList(nullptr)
    , m_backupCountLabel(nullptr)
    , m_detailTimestamp(nullptr)
    , m_detailType(nullptr)
    , m_detailDescription(nullptr)
    , m_detailFileCount(nullptr)
    , m_detailFileList(nullptr)
    , m_restoreBtn(nullptr)
    , m_deleteBtn(nullptr)
    , m_verifyBtn(nullptr)
    , m_statusLabel(nullptr)
    , m_autoBackupTimer(new QTimer(this))
    , m_selectedIndex(-1)
{
    setupUI();
    loadAutoBackupSettings();
    refreshBackupList();
}

// ===========================================================================
// UI construction
// ===========================================================================

void DataBackupPage::setupUI()
{
    setStyleSheet("QWidget { background-color: #1a1f2e; color: #ffffff; }");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // -----------------------------------------------------------------------
    // TOP BAR
    // -----------------------------------------------------------------------
    QWidget* topBar = new QWidget();
    topBar->setStyleSheet("background-color: #141824; border-bottom: 2px solid #3d5a8c;");
    topBar->setFixedHeight(50);
    QHBoxLayout* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(10, 5, 10, 5);
    topLayout->setSpacing(12);

    m_backBtn = new QPushButton("Back to Main Menu");
    m_backBtn->setFixedHeight(32);
    m_backBtn->setStyleSheet(
        "QPushButton { background-color: #3d5a8c; color: white; font-weight: bold; "
        "padding: 5px 15px; border: 1px solid #4d6e9f; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4d6e9f; }"
        "QPushButton:pressed { background-color: #2d4a7c; }");
    connect(m_backBtn, &QPushButton::clicked, this, &DataBackupPage::backToMainMenu);
    topLayout->addWidget(m_backBtn);

    QLabel* titleLabel = new QLabel("Data Backup");
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #ffffff; padding: 0 10px;");
    topLayout->addWidget(titleLabel);

    QFrame* vSep = new QFrame();
    vSep->setFrameShape(QFrame::VLine);
    vSep->setStyleSheet("color: #3d5a8c;");
    topLayout->addWidget(vSep);

    m_productLabel = new QLabel("Product: None");
    m_productLabel->setStyleSheet("font-size: 14px; color: #aaccff; padding: 0 5px;");
    topLayout->addWidget(m_productLabel);

    topLayout->addStretch();
    mainLayout->addWidget(topBar);

    // -----------------------------------------------------------------------
    // CREATE + AUTO-BACKUP SECTION
    // -----------------------------------------------------------------------
    QWidget* createWidget = new QWidget();
    createWidget->setStyleSheet(
        "QWidget { background-color: #1e2840; border-bottom: 1px solid #2d3e5f; }");
    createWidget->setFixedHeight(56);
    QHBoxLayout* createLayout = new QHBoxLayout(createWidget);
    createLayout->setContentsMargins(12, 8, 12, 8);
    createLayout->setSpacing(10);

    QLabel* descLabel = new QLabel("Description:");
    descLabel->setStyleSheet("color: #aaaaaa; font-size: 12px;");
    createLayout->addWidget(descLabel);

    m_descriptionEdit = new QLineEdit();
    m_descriptionEdit->setPlaceholderText("Optional note for this save point...");
    m_descriptionEdit->setMaximumWidth(380);
    m_descriptionEdit->setStyleSheet(
        "QLineEdit { background-color: #2d3748; color: #ffffff; border: 1px solid #3d5a8c; "
        "border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QLineEdit:focus { border-color: #5a8fc4; }");
    createLayout->addWidget(m_descriptionEdit);

    m_createBtn = new QPushButton("  Create Save Point");
    m_createBtn->setFixedHeight(34);
    m_createBtn->setMinimumWidth(170);
    m_createBtn->setStyleSheet(
        "QPushButton { "
        "  background-color: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #2ecc71,stop:1 #27ae60);"
        "  color: white; font-weight: bold; font-size: 13px; border-radius: 4px;"
        "  border-top: 2px solid rgba(255,255,255,0.5);"
        "  border-left: 2px solid rgba(255,255,255,0.5);"
        "  border-bottom: 2px solid rgba(0,0,0,0.5);"
        "  border-right: 2px solid rgba(0,0,0,0.5);"
        "}"
        "QPushButton:hover { background-color: #2ecc71; }"
        "QPushButton:pressed {"
        "  background-color: #1e7e34;"
        "  border-top: 2px solid rgba(0,0,0,0.5);"
        "  border-left: 2px solid rgba(0,0,0,0.5);"
        "  border-bottom: 2px solid rgba(255,255,255,0.3);"
        "  border-right: 2px solid rgba(255,255,255,0.3);"
        "}");
    connect(m_createBtn, &QPushButton::clicked, this, &DataBackupPage::onCreateSavePoint);
    createLayout->addWidget(m_createBtn);

    m_showChangesBtn = new QPushButton("  Show Changes");
    m_showChangesBtn->setFixedHeight(34);
    m_showChangesBtn->setMinimumWidth(140);
    m_showChangesBtn->setStyleSheet(
        "QPushButton { "
        "  background-color: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #6366f1,stop:1 #4f46e5);"
        "  color: white; font-weight: bold; font-size: 13px; border-radius: 4px;"
        "  border-top: 2px solid rgba(255,255,255,0.5);"
        "  border-left: 2px solid rgba(255,255,255,0.5);"
        "  border-bottom: 2px solid rgba(0,0,0,0.5);"
        "  border-right: 2px solid rgba(0,0,0,0.5);"
        "}"
        "QPushButton:hover { background-color: #6366f1; }"
        "QPushButton:pressed {"
        "  background-color: #3730a3;"
        "  border-top: 2px solid rgba(0,0,0,0.5);"
        "  border-left: 2px solid rgba(0,0,0,0.5);"
        "  border-bottom: 2px solid rgba(255,255,255,0.3);"
        "  border-right: 2px solid rgba(255,255,255,0.3);"
        "}");
    connect(m_showChangesBtn, &QPushButton::clicked, this, &DataBackupPage::onShowChanges);
    createLayout->addWidget(m_showChangesBtn);

    createLayout->addSpacing(20);

    QFrame* sep2 = new QFrame();
    sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet("color: #3d5a8c;");
    createLayout->addWidget(sep2);

    m_autoBackupCheck = new QCheckBox("Auto-backup every");
    m_autoBackupCheck->setStyleSheet("color: #cccccc; font-size: 12px;");
    connect(m_autoBackupCheck, &QCheckBox::toggled, this, &DataBackupPage::onAutoBackupToggled);
    createLayout->addWidget(m_autoBackupCheck);

    m_autoIntervalSpin = new QSpinBox();
    m_autoIntervalSpin->setRange(5, 480);
    m_autoIntervalSpin->setValue(DEFAULT_AUTO_INTERVAL_MIN);
    m_autoIntervalSpin->setSuffix(" min");
    m_autoIntervalSpin->setFixedWidth(90);
    m_autoIntervalSpin->setStyleSheet(
        "QSpinBox { background-color: #2d3748; color: #ffffff; border: 1px solid #3d5a8c; "
        "border-radius: 4px; padding: 3px; font-size: 12px; }"
        "QSpinBox::up-button, QSpinBox::down-button { width: 18px; background-color: #3d5a8c; }"
        "QSpinBox::up-arrow { width: 8px; height: 8px; }"
        "QSpinBox::down-arrow { width: 8px; height: 8px; }");
    connect(m_autoIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int /*val*/) {
                if (m_autoBackupCheck->isChecked()) {
                    saveAutoBackupSettings();
                    m_autoBackupTimer->setInterval(m_autoIntervalSpin->value() * 60 * 1000);
                }
            });
    createLayout->addWidget(m_autoIntervalSpin);

    m_autoStatusLabel = new QLabel();
    m_autoStatusLabel->setStyleSheet("color: #888888; font-size: 11px; padding: 0 5px;");
    createLayout->addWidget(m_autoStatusLabel);

    createLayout->addStretch();
    mainLayout->addWidget(createWidget);

    // -----------------------------------------------------------------------
    // MAIN BODY: splitter (list | detail)
    // -----------------------------------------------------------------------
    QSplitter* splitter = new QSplitter(Qt::Horizontal);
    splitter->setStyleSheet(
        "QSplitter::handle { background-color: #3d5a8c; width: 2px; }");
    splitter->setHandleWidth(2);

    // --- LEFT: backup list ---
    QWidget* listWidget = new QWidget();
    listWidget->setStyleSheet("background-color: #1a1f2e;");
    QVBoxLayout* listLayout = new QVBoxLayout(listWidget);
    listLayout->setContentsMargins(8, 8, 4, 8);
    listLayout->setSpacing(6);

    QHBoxLayout* listHeaderRow = new QHBoxLayout();
    QLabel* listTitle = new QLabel("Backups");
    listTitle->setStyleSheet("font-size: 13px; font-weight: bold; color: #aaccff;");
    listHeaderRow->addWidget(listTitle);
    listHeaderRow->addStretch();
    m_backupCountLabel = new QLabel("0 backups");
    m_backupCountLabel->setStyleSheet("font-size: 11px; color: #666666;");
    listHeaderRow->addWidget(m_backupCountLabel);
    listLayout->addLayout(listHeaderRow);

    m_backupList = new QListWidget();
    m_backupList->setStyleSheet(
        "QListWidget {"
        "  background-color: #141824;"
        "  border: 1px solid #2d3e5f;"
        "  border-radius: 4px;"
        "  outline: none;"
        "}"
        "QListWidget::item {"
        "  padding: 8px 6px;"
        "  border-bottom: 1px solid #1e2840;"
        "  color: #cccccc;"
        "}"
        "QListWidget::item:hover { background-color: #1e2840; }"
        "QListWidget::item:selected {"
        "  background-color: #2d3e5f;"
        "  color: #ffffff;"
        "  border-left: 3px solid #3b82f6;"
        "}");
    m_backupList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_backupList->setAlternatingRowColors(false);
    connect(m_backupList, &QListWidget::currentRowChanged,
            this, &DataBackupPage::onBackupSelected);
    listLayout->addWidget(m_backupList, 1);

    splitter->addWidget(listWidget);

    // --- RIGHT: detail panel ---
    QWidget* detailWidget = new QWidget();
    detailWidget->setStyleSheet("background-color: #1a1f2e;");
    QVBoxLayout* detailLayout = new QVBoxLayout(detailWidget);
    detailLayout->setContentsMargins(8, 8, 8, 8);
    detailLayout->setSpacing(8);

    QLabel* detailTitle = new QLabel("Backup Details");
    detailTitle->setStyleSheet("font-size: 13px; font-weight: bold; color: #aaccff; padding-bottom: 4px;");
    detailLayout->addWidget(detailTitle);

    // Info card
    QFrame* infoCard = new QFrame();
    infoCard->setStyleSheet(
        "QFrame { background-color: #1e2840; border: 1px solid #2d3e5f; border-radius: 6px; padding: 6px; }");
    QVBoxLayout* infoLayout = new QVBoxLayout(infoCard);
    infoLayout->setSpacing(5);
    infoLayout->setContentsMargins(10, 8, 10, 8);

    auto makeInfoRow = [&](const QString& labelText, QLabel*& valueLabel) {
        QHBoxLayout* row = new QHBoxLayout();
        row->setSpacing(8);
        QLabel* lbl = new QLabel(labelText);
        lbl->setStyleSheet("color: #666666; font-size: 11px; min-width: 90px;");
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueLabel = new QLabel();
        valueLabel->setStyleSheet("color: #cccccc; font-size: 12px;");
        valueLabel->setWordWrap(true);
        row->addWidget(lbl);
        row->addWidget(valueLabel, 1);
        infoLayout->addLayout(row);
    };

    makeInfoRow("Timestamp:", m_detailTimestamp);
    makeInfoRow("Type:", m_detailType);
    makeInfoRow("Description:", m_detailDescription);
    makeInfoRow("Files:", m_detailFileCount);

    detailLayout->addWidget(infoCard);

    QLabel* filesTitle = new QLabel("Backed-up files:");
    filesTitle->setStyleSheet("color: #888888; font-size: 11px;");
    detailLayout->addWidget(filesTitle);

    m_detailFileList = new QTextEdit();
    m_detailFileList->setReadOnly(true);
    m_detailFileList->setStyleSheet(
        "QTextEdit {"
        "  background-color: #0d1520;"
        "  color: #88aacc;"
        "  font-family: 'Consolas', 'Courier New', monospace;"
        "  font-size: 11px;"
        "  border: 1px solid #2d3e5f;"
        "  border-radius: 4px;"
        "}");
    detailLayout->addWidget(m_detailFileList, 1);

    // Action buttons
    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->setSpacing(8);

    m_verifyBtn = new QPushButton("Verify Integrity");
    m_verifyBtn->setFixedHeight(32);
    m_verifyBtn->setEnabled(false);
    m_verifyBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #2563eb,stop:1 #1e40af);"
        "  color: white; font-weight: bold; font-size: 12px; border-radius: 4px;"
        "  border-top: 2px solid rgba(255,255,255,0.4);"
        "  border-left: 2px solid rgba(255,255,255,0.4);"
        "  border-bottom: 2px solid rgba(0,0,0,0.5);"
        "  border-right: 2px solid rgba(0,0,0,0.5);"
        "}"
        "QPushButton:hover { background-color: #2563eb; }"
        "QPushButton:pressed { background-color: #1e3a8a; }"
        "QPushButton:disabled { background-color: #374151; color: #6b7280; border: 1px solid #4b5563; }");
    connect(m_verifyBtn, &QPushButton::clicked, this, &DataBackupPage::onVerifyClicked);
    actionLayout->addWidget(m_verifyBtn);

    m_restoreBtn = new QPushButton("Restore");
    m_restoreBtn->setFixedHeight(32);
    m_restoreBtn->setEnabled(false);
    m_restoreBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #d97706,stop:1 #92400e);"
        "  color: white; font-weight: bold; font-size: 12px; border-radius: 4px;"
        "  border-top: 2px solid rgba(255,255,255,0.4);"
        "  border-left: 2px solid rgba(255,255,255,0.4);"
        "  border-bottom: 2px solid rgba(0,0,0,0.5);"
        "  border-right: 2px solid rgba(0,0,0,0.5);"
        "}"
        "QPushButton:hover { background-color: #d97706; }"
        "QPushButton:pressed { background-color: #78350f; }"
        "QPushButton:disabled { background-color: #374151; color: #6b7280; border: 1px solid #4b5563; }");
    connect(m_restoreBtn, &QPushButton::clicked, this, &DataBackupPage::onRestoreClicked);
    actionLayout->addWidget(m_restoreBtn);

    m_deleteBtn = new QPushButton("Delete");
    m_deleteBtn->setFixedHeight(32);
    m_deleteBtn->setEnabled(false);
    m_deleteBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #dc2626,stop:1 #991b1b);"
        "  color: white; font-weight: bold; font-size: 12px; border-radius: 4px;"
        "  border-top: 2px solid rgba(255,255,255,0.4);"
        "  border-left: 2px solid rgba(255,255,255,0.4);"
        "  border-bottom: 2px solid rgba(0,0,0,0.5);"
        "  border-right: 2px solid rgba(0,0,0,0.5);"
        "}"
        "QPushButton:hover { background-color: #dc2626; }"
        "QPushButton:pressed { background-color: #7f1d1d; }"
        "QPushButton:disabled { background-color: #374151; color: #6b7280; border: 1px solid #4b5563; }");
    connect(m_deleteBtn, &QPushButton::clicked, this, &DataBackupPage::onDeleteClicked);
    actionLayout->addWidget(m_deleteBtn);

    detailLayout->addLayout(actionLayout);
    splitter->addWidget(detailWidget);

    // Set initial split sizes (~55 / 45)
    splitter->setSizes({ 550, 450 });

    mainLayout->addWidget(splitter, 1);

    // -----------------------------------------------------------------------
    // STATUS BAR
    // -----------------------------------------------------------------------
    QWidget* statusBar = new QWidget();
    statusBar->setFixedHeight(28);
    statusBar->setStyleSheet("background-color: #141824; border-top: 1px solid #2d3e5f;");
    QHBoxLayout* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(10, 2, 10, 2);

    m_statusLabel = new QLabel("Ready.");
    m_statusLabel->setStyleSheet("color: #888888; font-size: 11px;");
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();

    auto folderBtnStyle =
        "QPushButton { background-color: #2d3e5f; color: #aaccff; font-size: 11px; "
        "padding: 2px 10px; border: 1px solid #3d5a8c; border-radius: 3px; }"
        "QPushButton:hover { background-color: #3d5a8c; color: #ffffff; }";

    QPushButton* openLocalBtn = new QPushButton("Open Local Folder");
    openLocalBtn->setFixedHeight(20);
    openLocalBtn->setStyleSheet(folderBtnStyle);
    connect(openLocalBtn, &QPushButton::clicked, this, [this]() {
        fs::path p = getBackupBasePath();
        if (p.empty()) return;
        std::error_code ec;
        fs::create_directories(p, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(p.string())));
    });
    statusLayout->addWidget(openLocalBtn);

    QPushButton* openAppDataBtn = new QPushButton("Open AppData Folder");
    openAppDataBtn->setFixedHeight(20);
    openAppDataBtn->setStyleSheet(folderBtnStyle);
    connect(openAppDataBtn, &QPushButton::clicked, this, [this]() {
        fs::path p = getAppDataBackupPath();
        if (p.empty()) return;
        std::error_code ec;
        fs::create_directories(p, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(p.string())));
    });
    statusLayout->addWidget(openAppDataBtn);

    mainLayout->addWidget(statusBar);

    // -----------------------------------------------------------------------
    // Auto-backup timer
    // -----------------------------------------------------------------------
    connect(m_autoBackupTimer, &QTimer::timeout,
            this, &DataBackupPage::onAutoBackupTimer);

    // Populate product label
    onProductChanged();
}

// ===========================================================================
// Slots
// ===========================================================================

void DataBackupPage::onProductChanged()
{
    QString name = getCurrentProductName();
    if (name.isEmpty()) {
        m_productLabel->setText("Product: None");
        m_createBtn->setEnabled(false);
        m_showChangesBtn->setEnabled(false);
        m_autoBackupCheck->setEnabled(false);
        m_autoBackupTimer->stop();
    } else {
        m_productLabel->setText("Product: " + name);
        m_createBtn->setEnabled(true);
        m_showChangesBtn->setEnabled(true);
        m_autoBackupCheck->setEnabled(true);
        if (m_autoBackupCheck->isChecked()) {
            m_autoBackupTimer->start(m_autoIntervalSpin->value() * 60 * 1000);
        }
    }
    clearDetailPanel();
    refreshBackupList();
}

void DataBackupPage::onCreateSavePoint()
{
    if (m_backupInProgress) {
        m_statusLabel->setText("Backup already in progress...");
        return;
    }
    QString name = m_descriptionEdit->text().trimmed();
    startBackupAsync(QStringLiteral("save_point"), name, name);
    m_descriptionEdit->clear();
}

void DataBackupPage::onAutoBackupTimer()
{
    if (getCurrentProductName().isEmpty() || m_backupInProgress) {
        return;
    }
    // Change detection + backup all happen on a worker thread to keep the UI responsive.
    startAutoBackupAsync();
}

void DataBackupPage::onBackupSelected(int row)
{
    if (row < 0 || row >= static_cast<int>(m_backups.size())) {
        m_selectedIndex = -1;
        clearDetailPanel();
        return;
    }
    m_selectedIndex = row;
    updateDetailPanel(row);
    m_restoreBtn->setEnabled(true);
    m_deleteBtn->setEnabled(true);
    m_verifyBtn->setEnabled(true);
}

void DataBackupPage::onRestoreClicked()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_backups.size())) {
        return;
    }
    const BackupEntry& entry = m_backups[static_cast<size_t>(m_selectedIndex)];

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Confirm Restore");
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText(
        QString("Restore from backup:\n\n%1\n\nThis will overwrite the current product files.\n"
                "A pre-revert backup will be created first.\n\nContinue?")
        .arg(entry.timestamp));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    msgBox.setStyleSheet(DIALOG_CSS);
    if (msgBox.exec() != QMessageBox::Yes) {
        return;
    }

    bool ok = restoreFromBackup(entry.folderPath);
    if (ok) {
        m_statusLabel->setText("Restore completed successfully.");
        m_statusLabel->setStyleSheet("color: #22c55e; font-size: 11px;");
    } else {
        m_statusLabel->setText("Restore failed — check logs.");
        m_statusLabel->setStyleSheet("color: #ef4444; font-size: 11px;");
    }
}

void DataBackupPage::onDeleteClicked()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_backups.size())) {
        return;
    }
    const BackupEntry& entry = m_backups[static_cast<size_t>(m_selectedIndex)];

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Confirm Delete");
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText(
        QString("Delete backup:\n\n%1\n\nThis action cannot be undone. Continue?")
        .arg(entry.timestamp));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    msgBox.setStyleSheet(DIALOG_CSS);
    if (msgBox.exec() != QMessageBox::Yes) {
        return;
    }

    std::error_code ec;
    fs::remove_all(fs::path(entry.folderPath.toStdString()), ec);
    if (ec) {
        m_statusLabel->setText("Delete failed: " + QString::fromStdString(ec.message()));
        m_statusLabel->setStyleSheet("color: #ef4444; font-size: 11px;");
        if (m_context && m_context->logManager) {
            m_context->logManager->Error("Failed to delete backup " +
                entry.folderPath.toStdString() + ": " + ec.message(), "DataBackupPage");
        }
    } else {
        m_statusLabel->setText("Backup deleted.");
        m_statusLabel->setStyleSheet("color: #aaaaaa; font-size: 11px;");
    }

    clearDetailPanel();
    refreshBackupList();
}

void DataBackupPage::onVerifyClicked()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_backups.size())) {
        return;
    }
    const BackupEntry& entry = m_backups[static_cast<size_t>(m_selectedIndex)];

    // Read manifest
    fs::path manifestPath = fs::path(entry.folderPath.toStdString()) / "manifest.json";
    QFile manifestFile(QString::fromStdString(manifestPath.string()));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        m_statusLabel->setText("Verify failed: cannot read manifest.");
        m_statusLabel->setStyleSheet("color: #ef4444; font-size: 11px;");
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll());
    manifestFile.close();

    if (doc.isNull() || !doc.isObject()) {
        m_statusLabel->setText("Verify failed: invalid manifest.");
        m_statusLabel->setStyleSheet("color: #ef4444; font-size: 11px;");
        return;
    }

    QJsonObject root    = doc.object();
    QJsonObject files   = root.value("files").toObject();
    QStringList failed;

    // Check each file
    int checked = 0;
    static constexpr int MAX_FILE_CHECKS = 10000;
    for (const QString& relPath : files.keys()) {
        if (checked >= MAX_FILE_CHECKS) {
            break;
        }
        ++checked;

        QJsonObject fileInfo = files.value(relPath).toObject();
        QString expectedHash = fileInfo.value("sha256").toString();

        fs::path backupFile = fs::path(entry.folderPath.toStdString()) /
                              fs::path(relPath.toStdString());
        QString actualHash  = computeFileHash(QString::fromStdString(backupFile.string()));

        if (actualHash != expectedHash) {
            failed.append(relPath);
        }
    }

    if (failed.isEmpty()) {
        m_statusLabel->setText(
            QString("Integrity OK — %1 files verified.").arg(checked));
        m_statusLabel->setStyleSheet("color: #22c55e; font-size: 11px;");
    } else {
        m_statusLabel->setText(
            QString("INTEGRITY FAILURE — %1 corrupted file(s)!").arg(failed.size()));
        m_statusLabel->setStyleSheet("color: #ef4444; font-size: 11px;");
        // List corrupted files in the detail text area
        m_detailFileList->setPlainText("CORRUPTED FILES:\n" + failed.join('\n'));
        if (m_context && m_context->logManager) {
            m_context->logManager->Warning(
                "Backup integrity check failed for " + entry.folderPath.toStdString() +
                " — " + std::to_string(failed.size()) + " corrupted file(s).",
                "DataBackupPage");
        }
    }
}

void DataBackupPage::onAutoBackupToggled(bool enabled)
{
    saveAutoBackupSettings();
    if (enabled && !getCurrentProductName().isEmpty()) {
        int intervalMs = m_autoIntervalSpin->value() * 60 * 1000;
        m_autoBackupTimer->start(intervalMs);
        m_autoStatusLabel->setText("Active");
        m_autoStatusLabel->setStyleSheet("color: #22c55e; font-size: 11px; padding: 0 5px;");
    } else {
        m_autoBackupTimer->stop();
        m_autoStatusLabel->setText("Off");
        m_autoStatusLabel->setStyleSheet("color: #666666; font-size: 11px; padding: 0 5px;");
    }
}

// ===========================================================================
// Backup creation (background worker)
// ===========================================================================

void DataBackupPage::startBackupAsync(const QString& type, const QString& name,
                                       const QString& description)
{
    QString productName = getCurrentProductName();
    if (productName.isEmpty()) {
        m_statusLabel->setText("No product loaded.");
        return;
    }

    fs::path productDir = getProductDirPath();
    fs::path localBase  = getBackupBasePath();
    fs::path remoteBase = getAppDataBackupPath();

    // Generate folder name on UI thread (uses QDateTime)
    QString timestamp  = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString folderName = timestamp + "_" + type;
    QString safeName   = sanitizeName(name);
    if (!safeName.isEmpty()) {
        folderName += "_" + safeName;
    }

    // Disable UI during backup
    m_backupInProgress = true;
    m_createBtn->setEnabled(false);
    m_showChangesBtn->setEnabled(false);
    m_statusLabel->setText("Backup in progress...");
    m_statusLabel->setStyleSheet("color: #eab308; font-size: 11px;");

    m_backupFuture = std::async(std::launch::async,
        [this, type, name, description, productDir, localBase, remoteBase, folderName]() {
            return performBackupWork(type, name, description,
                                     productDir, localBase, remoteBase, folderName);
        });

    // Poll for completion using a repeating QTimer (non-blocking)
    QTimer* pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, [this, pollTimer, type]() {
        if (!m_backupFuture.valid()) {
            pollTimer->stop();
            pollTimer->deleteLater();
            return;
        }
        auto status = m_backupFuture.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            pollTimer->stop();
            pollTimer->deleteLater();
            BackupResult result = m_backupFuture.get();
            onBackupComplete(result, type);
        }
    });
    pollTimer->start(100);
}

DataBackupPage::BackupResult DataBackupPage::performBackupWork(
    const QString& type, const QString& name, const QString& description,
    const fs::path& productDir, const fs::path& localBase,
    const fs::path& remoteBase, const QString& folderName)
{
    BackupResult result;
    std::error_code ec;

    // --- Local backup (config/{product}/backups/) ---
    fs::create_directories(localBase, ec);
    if (!ec) {
        fs::path localDir = localBase / folderName.toStdString();
        fs::create_directories(localDir, ec);
        if (!ec && copyDirectoryRecursive(productDir, localDir)) {
            QJsonObject hashes = computeDirectoryHashes(localDir, localDir);
            if (writeManifest(localDir, type, name, description, hashes)) {
                result.localOk   = true;
                result.localPath = QString::fromStdString(localDir.string());
            } else {
                fs::remove_all(localDir, ec);
            }
        } else {
            fs::remove_all(localDir, ec);
        }
    }

    // --- AppData redundant backup (%APPDATA%/BobcatEngine/backups/{product}/) ---
    if (!remoteBase.empty()) {
        fs::create_directories(remoteBase, ec);
        if (!ec) {
            fs::path remoteDir = remoteBase / folderName.toStdString();
            fs::create_directories(remoteDir, ec);
            if (!ec && copyDirectoryRecursive(productDir, remoteDir)) {
                QJsonObject hashes = computeDirectoryHashes(remoteDir, remoteDir);
                if (writeManifest(remoteDir, type, name, description, hashes)) {
                    result.remoteOk   = true;
                    result.remotePath = QString::fromStdString(remoteDir.string());
                } else {
                    fs::remove_all(remoteDir, ec);
                }
            } else {
                fs::remove_all(remoteDir, ec);
            }
        }
    }

    return result;
}

void DataBackupPage::startAutoBackupAsync()
{
    QString productName = getCurrentProductName();
    if (productName.isEmpty()) {
        return;
    }

    fs::path productDir = getProductDirPath();
    fs::path localBase  = getBackupBasePath();
    fs::path remoteBase = getAppDataBackupPath();

    QString timestamp  = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString folderName = timestamp + "_auto";

    // m_backups is sorted newest-first; latest manifest is cheap to grab on the UI thread.
    fs::path latestManifest;
    if (!m_backups.empty()) {
        latestManifest = fs::path(m_backups[0].folderPath.toStdString()) / "manifest.json";
    }

    m_backupInProgress = true;
    m_createBtn->setEnabled(false);
    m_showChangesBtn->setEnabled(false);
    m_autoStatusLabel->setText("Checking for changes...");
    m_autoStatusLabel->setStyleSheet("color: #888888; font-size: 11px; padding: 0 5px;");

    m_backupFuture = std::async(std::launch::async,
        [this, productDir, localBase, remoteBase, folderName, latestManifest]() {
            return performAutoBackupWork(productDir, localBase, remoteBase,
                                         folderName, latestManifest);
        });

    const QString type = QStringLiteral("auto");
    QTimer* pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, [this, pollTimer, type]() {
        if (!m_backupFuture.valid()) {
            pollTimer->stop();
            pollTimer->deleteLater();
            return;
        }
        auto status = m_backupFuture.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            pollTimer->stop();
            pollTimer->deleteLater();
            BackupResult result = m_backupFuture.get();
            onBackupComplete(result, type);
        }
    });
    pollTimer->start(250);
}

DataBackupPage::BackupResult DataBackupPage::performAutoBackupWork(
    const fs::path& productDir, const fs::path& localBase,
    const fs::path& remoteBase, const QString& folderName,
    const fs::path& latestManifestPath)
{
    BackupResult result;

    // --- Change detection (SHA-256 work happens here on the worker thread) ---
    if (!latestManifestPath.empty()) {
        std::error_code ec;
        if (fs::exists(latestManifestPath, ec) && !ec) {
            QFile f(QString::fromStdString(latestManifestPath.string()));
            if (f.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                f.close();

                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject backupFiles = doc.object().value("files").toObject();
                    QJsonObject currentFiles = computeDirectoryHashes(productDir, productDir);

                    // Strip out any backups/ entries from the current scan
                    const QStringList curKeys = currentFiles.keys();
                    for (const QString& key : curKeys) {
                        if (key.startsWith("backups/")) {
                            currentFiles.remove(key);
                        }
                    }

                    bool unchanged = (currentFiles.size() == backupFiles.size());
                    if (unchanged) {
                        const QStringList keys = currentFiles.keys();
                        for (const QString& key : keys) {
                            if (!backupFiles.contains(key)) {
                                unchanged = false;
                                break;
                            }
                            const QString cur =
                                currentFiles.value(key).toObject().value("sha256").toString();
                            const QString bak =
                                backupFiles.value(key).toObject().value("sha256").toString();
                            if (cur != bak) {
                                unchanged = false;
                                break;
                            }
                        }
                    }

                    if (unchanged) {
                        result.skipped = true;
                        return result;
                    }
                }
            }
        }
    }

    // Changes detected (or no prior backup) — perform the actual backup
    return performBackupWork(QStringLiteral("auto"), QString(), QString(),
                              productDir, localBase, remoteBase, folderName);
}

void DataBackupPage::onBackupComplete(const BackupResult& result, const QString& type)
{
    m_backupInProgress = false;
    m_createBtn->setEnabled(!getCurrentProductName().isEmpty());
    m_showChangesBtn->setEnabled(!getCurrentProductName().isEmpty());

    if (result.skipped) {
        const QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
        m_autoStatusLabel->setText("No changes — skipped " + ts);
        m_autoStatusLabel->setStyleSheet("color: #888888; font-size: 11px; padding: 0 5px;");
        m_statusLabel->setText("Auto-backup: no changes.");
        m_statusLabel->setStyleSheet("color: #aaaaaa; font-size: 11px;");
        return;
    }

    if (result.localOk && result.remoteOk) {
        m_statusLabel->setText("Backup saved (local + AppData).");
        m_statusLabel->setStyleSheet("color: #22c55e; font-size: 11px;");
    } else if (result.localOk) {
        m_statusLabel->setText("Backup saved locally. AppData copy failed.");
        m_statusLabel->setStyleSheet("color: #eab308; font-size: 11px;");
    } else if (result.remoteOk) {
        m_statusLabel->setText("Local backup failed. AppData copy saved.");
        m_statusLabel->setStyleSheet("color: #eab308; font-size: 11px;");
    } else {
        m_statusLabel->setText("Backup failed on both destinations.");
        m_statusLabel->setStyleSheet("color: #ef4444; font-size: 11px;");
    }

    if (m_context && m_context->logManager) {
        std::string msg = "Backup complete — local:" +
            std::string(result.localOk ? "OK" : "FAIL") +
            " appdata:" + std::string(result.remoteOk ? "OK" : "FAIL");
        m_context->logManager->Info(msg, "DataBackupPage");
    }

    if (type == QStringLiteral("auto")) {
        QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
        m_autoStatusLabel->setText("Last auto-backup: " + ts);
    }

    // Prune if auto/pre_revert
    if (type == QStringLiteral("auto") || type == QStringLiteral("pre_revert")) {
        pruneAutoBackups();
    }

    refreshBackupList();
}

// ===========================================================================
// Restore
// ===========================================================================

bool DataBackupPage::restoreFromBackup(const QString& backupPath)
{
    // Synchronous pre-revert backup before restore
    fs::path productDir = getProductDirPath();
    fs::path localBase  = getBackupBasePath();
    fs::path remoteBase = getAppDataBackupPath();
    QString timestamp   = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString folderName  = timestamp + "_pre_revert";

    BackupResult preRevert = performBackupWork(
        QStringLiteral("pre_revert"), QString(), QString(),
        productDir, localBase, remoteBase, folderName);

    if (!preRevert.localOk && !preRevert.remoteOk) {
        if (m_context && m_context->logManager) {
            m_context->logManager->Warning(
                "Pre-revert backup failed — aborting restore.", "DataBackupPage");
        }
        m_statusLabel->setText("Pre-revert backup failed — restore aborted.");
        return false;
    }

    fs::path srcDir = fs::path(backupPath.toStdString());

    std::error_code ec;
    if (!fs::exists(srcDir, ec) || ec) {
        return false;
    }

    // Walk backup folder and copy files to product dir (skip manifest.json itself)
    int copied  = 0;
    int skipped = 0;
    static constexpr int MAX_RESTORE_FILES = 10000;

    for (const auto& entry : fs::recursive_directory_iterator(srcDir, ec)) {
        if (ec) {
            break;
        }
        if (copied + skipped >= MAX_RESTORE_FILES) {
            break;
        }

        if (!entry.is_regular_file(ec) || ec) {
            ++skipped;
            continue;
        }

        fs::path rel      = fs::relative(entry.path(), srcDir, ec);
        if (ec) {
            ++skipped;
            continue;
        }

        // Skip the manifest itself
        if (rel == fs::path("manifest.json")) {
            continue;
        }

        fs::path destFile = productDir / rel;
        fs::create_directories(longPath(destFile.parent_path()), ec);
        if (ec) {
            ++skipped;
            continue;
        }

        fs::copy_file(longPath(entry.path()), longPath(destFile),
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (m_context && m_context->logManager) {
                m_context->logManager->Error(
                    "Restore copy failed for " + rel.string() + ": " + ec.message(),
                    "DataBackupPage");
            }
            ++skipped;
        } else {
            ++copied;
        }
    }

    refreshBackupList();

    if (m_context && m_context->logManager) {
        m_context->logManager->Info(
            "Restore complete — " + std::to_string(copied) + " files restored, " +
            std::to_string(skipped) + " skipped.",
            "DataBackupPage");
    }

    return (skipped == 0);
}

// ===========================================================================
// Directory copy (skips backups/ subdirectory)
// ===========================================================================

bool DataBackupPage::copyDirectoryRecursive(const fs::path& src, const fs::path& dst)
{
    std::error_code ec;
    if (!fs::exists(src, ec) || ec) {
        return false;
    }

    // Name of the backups sub-folder — we skip it to avoid recursion
    const fs::path backupsSubdir = fs::path("backups");

    int copied  = 0;
    int errors  = 0;
    static constexpr int MAX_COPY_FILES = 10000;

    for (const auto& entry : fs::recursive_directory_iterator(src, ec)) {
        if (ec) {
            break;
        }
        if (copied + errors >= MAX_COPY_FILES) {
            break;
        }

        fs::path rel = fs::relative(entry.path(), src, ec);
        if (ec) {
            ++errors;
            continue;
        }

        // Skip the backups/ subtree
        if (!rel.empty() && *rel.begin() == backupsSubdir) {
            continue;
        }

        if (entry.is_directory(ec) && !ec) {
            fs::create_directories(longPath(dst / rel), ec);
            if (ec) {
                ++errors;
            }
            continue;
        }

        if (!entry.is_regular_file(ec) || ec) {
            ++errors;
            continue;
        }

        fs::path destFile = dst / rel;
        fs::create_directories(longPath(destFile.parent_path()), ec);
        if (ec) {
            ++errors;
            continue;
        }

        fs::copy_file(longPath(entry.path()), longPath(destFile),
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (m_context && m_context->logManager) {
                m_context->logManager->Error(
                    "Copy failed: " + rel.string() + ": " + ec.message(), "DataBackupPage");
            }
            ++errors;
        } else {
            ++copied;
        }
    }

    return (errors == 0);
}

// ===========================================================================
// SHA-256 helpers
// ===========================================================================

QString DataBackupPage::computeFileHash(const QString& filePath)
{
    QString longFilePath = filePath;
    if (!longFilePath.startsWith("\\\\?\\") && longFilePath.length() > 200) {
        longFilePath = "\\\\?\\" + QDir::toNativeSeparators(longFilePath);
    }
    QFile file(longFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);

    // Read in 64 KB chunks (bounded to avoid infinite loop on malformed streams)
    static constexpr int CHUNK      = 65536;
    static constexpr int MAX_CHUNKS = 100000;
    for (int chunk = 0; chunk < MAX_CHUNKS && !file.atEnd(); ++chunk) {
        QByteArray data = file.read(CHUNK);
        if (data.isEmpty()) {
            break;
        }
        hash.addData(data);
    }
    file.close();

    return QString::fromLatin1(hash.result().toHex());
}

QJsonObject DataBackupPage::computeDirectoryHashes(const fs::path& dir,
                                                    const fs::path& baseDir)
{
    QJsonObject result;
    std::error_code ec;

    static constexpr int MAX_HASH_FILES = 10000;
    int count = 0;

    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (ec || count >= MAX_HASH_FILES) {
            break;
        }
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }

        fs::path rel = fs::relative(entry.path(), baseDir, ec);
        if (ec) {
            continue;
        }

        // Skip manifest
        if (rel == fs::path("manifest.json")) {
            continue;
        }

        QString filePath = QString::fromStdString(entry.path().string());
        QString hashHex  = computeFileHash(filePath);

        QJsonObject fileObj;
        fileObj["sha256"] = hashHex;
        fileObj["size"]   = static_cast<qint64>(fs::file_size(entry.path(), ec));

        result[QString::fromStdString(rel.generic_string())] = fileObj;
        ++count;
    }

    return result;
}

// ===========================================================================
// Manifest writing
// ===========================================================================

bool DataBackupPage::writeManifest(const fs::path& backupDir,
                                    const QString& type,
                                    const QString& name,
                                    const QString& description,
                                    const QJsonObject& fileHashes)
{
    QJsonObject root;
    root["version"]     = 1;
    root["type"]        = type;
    root["name"]        = name;
    root["description"] = description;
    root["created"]     = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["productName"] = getCurrentProductName();
    root["fileCount"]   = fileHashes.size();
    root["files"]       = fileHashes;

    QJsonDocument doc(root);

    fs::path manifestPath = backupDir / "manifest.json";
    QFile file(QString::fromStdString(manifestPath.string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

// ===========================================================================
// List management
// ===========================================================================

void DataBackupPage::refreshBackupList()
{
    m_backups.clear();
    m_backupList->clear();
    clearDetailPanel();

    fs::path backupBase = getBackupBasePath();
    std::error_code ec;

    if (!fs::exists(backupBase, ec) || ec) {
        m_backupCountLabel->setText("0 backups");
        return;
    }

    // Collect all backup folder paths
    std::vector<fs::path> dirs;
    dirs.reserve(64);

    static constexpr int MAX_DIRS = 10000;
    int dirCount = 0;
    for (const auto& entry : fs::directory_iterator(backupBase, ec)) {
        if (ec || dirCount >= MAX_DIRS) {
            break;
        }
        if (entry.is_directory(ec) && !ec) {
            dirs.push_back(entry.path());
            ++dirCount;
        }
    }

    // Sort newest first (folder names start with timestamp)
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
        return a.filename().string() > b.filename().string();
    });

    // Parse manifest for each directory
    for (const fs::path& dir : dirs) {
        fs::path manifestPath = dir / "manifest.json";
        QFile f(QString::fromStdString(manifestPath.string()));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();

        if (doc.isNull() || !doc.isObject()) {
            continue;
        }

        QJsonObject obj = doc.object();
        BackupEntry entry;
        entry.folderPath  = QString::fromStdString(dir.string());
        entry.timestamp   = obj.value("created").toString();
        entry.type        = obj.value("type").toString();
        entry.name        = obj.value("name").toString();
        entry.description = obj.value("description").toString();
        entry.fileCount   = obj.value("fileCount").toInt(0);
        m_backups.push_back(entry);

        // Build list item text
        QString displayText;
        displayText += typeLabel(entry.type) + "  |  ";
        displayText += entry.timestamp;
        if (!entry.name.isEmpty()) {
            displayText += "\n  " + entry.name;
        }
        displayText += QString("\n  %1 file(s)").arg(entry.fileCount);

        QListWidgetItem* item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, static_cast<int>(m_backups.size()) - 1);
        item->setForeground(QColor(typeColor(entry.type)));
        m_backupList->addItem(item);
    }

    QString countText = QString("%1 backup%2")
        .arg(static_cast<int>(m_backups.size()))
        .arg(m_backups.size() == 1 ? "" : "s");
    m_backupCountLabel->setText(countText);
}

void DataBackupPage::updateDetailPanel(int index)
{
    if (index < 0 || index >= static_cast<int>(m_backups.size())) {
        return;
    }
    const BackupEntry& entry = m_backups[static_cast<size_t>(index)];

    m_detailTimestamp->setText(entry.timestamp);
    m_detailType->setText(typeLabel(entry.type));
    m_detailType->setStyleSheet(
        QString("color: %1; font-size: 12px;").arg(typeColor(entry.type)));
    m_detailDescription->setText(
        entry.description.isEmpty() ? "(none)" : entry.description);
    m_detailFileCount->setText(QString::number(entry.fileCount));

    // Load file list from manifest
    fs::path manifestPath = fs::path(entry.folderPath.toStdString()) / "manifest.json";
    QFile f(QString::fromStdString(manifestPath.string()));
    if (!f.open(QIODevice::ReadOnly)) {
        m_detailFileList->setPlainText("(cannot read manifest)");
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    if (doc.isNull() || !doc.isObject()) {
        m_detailFileList->setPlainText("(invalid manifest)");
        return;
    }

    QJsonObject files = doc.object().value("files").toObject();
    QStringList fileList = files.keys();
    std::sort(fileList.begin(), fileList.end());
    m_detailFileList->setPlainText(fileList.join('\n'));
}

void DataBackupPage::clearDetailPanel()
{
    if (m_detailTimestamp)   { m_detailTimestamp->clear(); }
    if (m_detailType)        { m_detailType->clear(); }
    if (m_detailDescription) { m_detailDescription->clear(); }
    if (m_detailFileCount)   { m_detailFileCount->clear(); }
    if (m_detailFileList)    { m_detailFileList->clear(); }

    m_selectedIndex = -1;

    if (m_restoreBtn) { m_restoreBtn->setEnabled(false); }
    if (m_deleteBtn)  { m_deleteBtn->setEnabled(false); }
    if (m_verifyBtn)  { m_verifyBtn->setEnabled(false); }
}

// ===========================================================================
// Pruning
// ===========================================================================

void DataBackupPage::pruneAutoBackups()
{
    fs::path backupBase = getBackupBasePath();
    std::error_code ec;

    if (!fs::exists(backupBase, ec) || ec) {
        return;
    }

    // Collect (timestamp, path) for "auto" and "pre_revert" separately
    std::vector<std::pair<std::string, fs::path>> autoBackups;
    std::vector<std::pair<std::string, fs::path>> preRevertBackups;
    autoBackups.reserve(32);
    preRevertBackups.reserve(8);

    static constexpr int MAX_SCAN = 10000;
    int scanned = 0;

    for (const auto& entry : fs::directory_iterator(backupBase, ec)) {
        if (ec || scanned >= MAX_SCAN) {
            break;
        }
        if (!entry.is_directory(ec) || ec) {
            ++scanned;
            continue;
        }

        fs::path manifestPath = entry.path() / "manifest.json";
        QFile f(QString::fromStdString(manifestPath.string()));
        if (!f.open(QIODevice::ReadOnly)) {
            ++scanned;
            continue;
        }
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();

        if (doc.isNull() || !doc.isObject()) {
            ++scanned;
            continue;
        }

        QString type = doc.object().value("type").toString();
        std::string folderName = entry.path().filename().string();

        if (type == QStringLiteral("auto")) {
            autoBackups.emplace_back(folderName, entry.path());
        } else if (type == QStringLiteral("pre_revert")) {
            preRevertBackups.emplace_back(folderName, entry.path());
        }
        ++scanned;
    }

    // Sort oldest first (ascending folder name = ascending timestamp)
    auto ascendingByName = [](const std::pair<std::string, fs::path>& a,
                               const std::pair<std::string, fs::path>& b) {
        return a.first < b.first;
    };
    std::sort(autoBackups.begin(), autoBackups.end(), ascendingByName);
    std::sort(preRevertBackups.begin(), preRevertBackups.end(), ascendingByName);

    // Delete oldest auto backups beyond the limit
    for (int i = 0; i < MAX_AUTO_BACKUPS + MAX_SCAN &&
         static_cast<int>(autoBackups.size()) > MAX_AUTO_BACKUPS; ++i) {
        fs::remove_all(autoBackups.front().second, ec);
        autoBackups.erase(autoBackups.begin());
    }

    // Delete oldest pre-revert backups beyond the limit
    for (int i = 0; i < MAX_PRE_REVERT_BACKUPS + MAX_SCAN &&
         static_cast<int>(preRevertBackups.size()) > MAX_PRE_REVERT_BACKUPS; ++i) {
        fs::remove_all(preRevertBackups.front().second, ec);
        preRevertBackups.erase(preRevertBackups.begin());
    }
}

// ===========================================================================
// Path helpers
// ===========================================================================

fs::path DataBackupPage::getProductDirPath() const
{
    QString productName = getCurrentProductName();
    if (productName.isEmpty()) {
        return fs::path();
    }
    QString base = QCoreApplication::applicationDirPath() +
                   "/config/" + productName;
    return fs::path(base.toStdString());
}

fs::path DataBackupPage::getBackupBasePath() const
{
    fs::path productDir = getProductDirPath();
    if (productDir.empty()) {
        return fs::path();
    }
    return productDir / "backups";
}

fs::path DataBackupPage::getAppDataBackupPath() const
{
    QString productName = getCurrentProductName();
    if (productName.isEmpty()) {
        return fs::path();
    }
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appData.isEmpty()) {
        return fs::path();
    }
    return fs::path(appData.toStdString()) / "backups" / productName.toStdString();
}

QString DataBackupPage::getCurrentProductName() const
{
    if (!m_context || !m_context->productPositionManager) {
        return QString();
    }
    if (!m_context->productPositionManager->HasLoadedProduct()) {
        return QString();
    }
    return QString::fromStdString(
        m_context->productPositionManager->GetCurrentProductName());
}

// ===========================================================================
// Change detection
// ===========================================================================

DataBackupPage::FileChangeSet DataBackupPage::computeChangesVsBackup(int backupIndex) const
{
    FileChangeSet result;

    fs::path productDir = getProductDirPath();
    if (productDir.empty()) {
        return result;
    }

    // Compute current file hashes (skip backups/ directory)
    QJsonObject currentFiles = const_cast<DataBackupPage*>(this)->
        computeDirectoryHashes(productDir, productDir);

    // Remove any entries under "backups/" from current scan
    QStringList currentKeys = currentFiles.keys();
    for (const QString& key : currentKeys) {
        if (key.startsWith("backups/")) {
            currentFiles.remove(key);
        }
    }

    // If no backup to compare against, everything is "added"
    if (backupIndex < 0 || backupIndex >= static_cast<int>(m_backups.size())) {
        QStringList keys = currentFiles.keys();
        keys.sort();
        result.added = keys;
        return result;
    }

    // Read the backup manifest
    const BackupEntry& entry = m_backups[static_cast<size_t>(backupIndex)];
    fs::path manifestPath = fs::path(entry.folderPath.toStdString()) / "manifest.json";
    QFile f(QString::fromStdString(manifestPath.string()));
    if (!f.open(QIODevice::ReadOnly)) {
        QStringList keys = currentFiles.keys();
        keys.sort();
        result.added = keys;
        return result;
    }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    if (doc.isNull() || !doc.isObject()) {
        QStringList keys = currentFiles.keys();
        keys.sort();
        result.added = keys;
        return result;
    }

    QJsonObject backupFiles = doc.object().value("files").toObject();

    // Build sets for comparison
    QSet<QString> currentSet;
    for (const QString& k : currentFiles.keys()) {
        currentSet.insert(k);
    }
    QSet<QString> backupSet;
    for (const QString& k : backupFiles.keys()) {
        backupSet.insert(k);
    }

    // Added: in current but not in backup
    for (const QString& key : currentSet) {
        if (!backupSet.contains(key)) {
            result.added.append(key);
        }
    }

    // Deleted: in backup but not in current
    for (const QString& key : backupSet) {
        if (!currentSet.contains(key)) {
            result.deleted.append(key);
        }
    }

    // Modified: in both but hash differs
    for (const QString& key : currentSet) {
        if (backupSet.contains(key)) {
            QString currentHash = currentFiles.value(key).toObject().value("sha256").toString();
            QString backupHash  = backupFiles.value(key).toObject().value("sha256").toString();
            if (currentHash != backupHash) {
                result.modified.append(key);
            }
        }
    }

    result.added.sort();
    result.modified.sort();
    result.deleted.sort();

    return result;
}

void DataBackupPage::onShowChanges()
{
    if (getCurrentProductName().isEmpty()) {
        m_statusLabel->setText("No product loaded.");
        return;
    }

    m_statusLabel->setText("Computing changes...");
    m_statusLabel->setStyleSheet("color: #aaaaaa; font-size: 11px;");
    QCoreApplication::processEvents();

    // Compare against the selected backup, or the latest if none selected
    int compareIndex = (m_selectedIndex >= 0) ? m_selectedIndex : 0;
    FileChangeSet changes = computeChangesVsBackup(compareIndex);

    int totalChanges = changes.added.size() + changes.modified.size() + changes.deleted.size();

    // Build rich-text display
    QString report;

    if (m_backups.empty()) {
        report += "No backups exist yet — showing all current files.\n\n";
    } else {
        const BackupEntry& ref = m_backups[static_cast<size_t>(compareIndex)];
        report += "Comparing against: " + ref.timestamp;
        if (!ref.name.isEmpty()) {
            report += " (" + ref.name + ")";
        }
        report += "\n\n";
    }

    if (totalChanges == 0) {
        report += "No changes detected. All files match the backup.";
        m_statusLabel->setText("No changes since last backup.");
        m_statusLabel->setStyleSheet("color: #22c55e; font-size: 11px;");
    } else {
        m_statusLabel->setText(
            QString("%1 change(s) detected.").arg(totalChanges));
        m_statusLabel->setStyleSheet("color: #eab308; font-size: 11px;");

        if (!changes.modified.isEmpty()) {
            report += QString("--- MODIFIED (%1) ---\n").arg(changes.modified.size());
            for (const QString& f : changes.modified) {
                report += "  [M] " + f + "\n";
            }
            report += "\n";
        }

        if (!changes.added.isEmpty()) {
            report += QString("--- ADDED (%1) ---\n").arg(changes.added.size());
            for (const QString& f : changes.added) {
                report += "  [+] " + f + "\n";
            }
            report += "\n";
        }

        if (!changes.deleted.isEmpty()) {
            report += QString("--- DELETED (%1) ---\n").arg(changes.deleted.size());
            for (const QString& f : changes.deleted) {
                report += "  [-] " + f + "\n";
            }
        }
    }

    // Show in detail file list area
    m_detailFileList->setPlainText(report);

    // Update detail info labels for context
    if (!m_backups.empty() && compareIndex < static_cast<int>(m_backups.size())) {
        const BackupEntry& ref = m_backups[static_cast<size_t>(compareIndex)];
        m_detailTimestamp->setText("vs. " + ref.timestamp);
        m_detailType->setText("Change Report");
        m_detailType->setStyleSheet("color: #6366f1; font-size: 12px;");
        m_detailDescription->setText(
            QString("%1 modified, %2 added, %3 deleted")
            .arg(changes.modified.size())
            .arg(changes.added.size())
            .arg(changes.deleted.size()));
        m_detailFileCount->setText(QString::number(totalChanges) + " change(s)");
    }
}

// ===========================================================================
// Settings persistence
// ===========================================================================

void DataBackupPage::saveAutoBackupSettings()
{
    QSettings settings("Fabrinet", "BobcatEngine");
    settings.setValue("Backup/AutoEnabled",    m_autoBackupCheck->isChecked());
    settings.setValue("Backup/AutoIntervalMin", m_autoIntervalSpin->value());
}

void DataBackupPage::loadAutoBackupSettings()
{
    QSettings settings("Fabrinet", "BobcatEngine");
    bool  autoEnabled   = settings.value("Backup/AutoEnabled", false).toBool();
    int   intervalMin   = settings.value("Backup/AutoIntervalMin",
                                         DEFAULT_AUTO_INTERVAL_MIN).toInt();

    // Clamp to widget range
    intervalMin = qBound(m_autoIntervalSpin->minimum(),
                          intervalMin,
                          m_autoIntervalSpin->maximum());

    // Block signals while setting values to avoid triggering side effects
    m_autoIntervalSpin->blockSignals(true);
    m_autoIntervalSpin->setValue(intervalMin);
    m_autoIntervalSpin->blockSignals(false);

    m_autoBackupCheck->blockSignals(true);
    m_autoBackupCheck->setChecked(autoEnabled);
    m_autoBackupCheck->blockSignals(false);

    // Apply the loaded state
    onAutoBackupToggled(autoEnabled);
}
