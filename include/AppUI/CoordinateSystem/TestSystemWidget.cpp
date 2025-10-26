// TestSystemWidget.cpp
#include "TestSystemWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>

TestSystemWidget::TestSystemWidget(
    CoordinateSystemManager* coordinateSystemManager,
    UnifiedMotionManager* motionManager,
    QWidget* parent)
    : QWidget(parent)
    , m_coordinateSystemManager(coordinateSystemManager)
    , m_motionManager(motionManager)
    , m_systemLoaded(false)
{
    setupUI();
}

void TestSystemWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // Title
    QLabel* titleLabel = new QLabel("Test Coordinate System");
    titleLabel->setStyleSheet(
        "QLabel {"
        "  font-size: 18px;"
        "  font-weight: bold;"
        "  color: #2c3e50;"
        "  padding: 10px;"
        "}"
    );
    mainLayout->addWidget(titleLabel);

    // System Selection Group
    QGroupBox* selectionGroup = new QGroupBox("System Selection");
    QVBoxLayout* selectionLayout = new QVBoxLayout(selectionGroup);

    QHBoxLayout* selectorLayout = new QHBoxLayout();
    m_systemSelector = new QComboBox();
    m_systemSelector->setMinimumWidth(300);
    selectorLayout->addWidget(m_systemSelector, 1);

    m_refreshButton = new QPushButton("Refresh");
    m_refreshButton->setFixedHeight(35);
    selectorLayout->addWidget(m_refreshButton);

    m_loadButton = new QPushButton("Load System");
    m_loadButton->setFixedHeight(35);
    m_loadButton->setEnabled(false);
    m_loadButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #3498db;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 8px 20px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #2980b9;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #95a5a6;"
        "}"
    );
    selectorLayout->addWidget(m_loadButton);

    selectionLayout->addLayout(selectorLayout);
    mainLayout->addWidget(selectionGroup);

    // System Info Group
    QGroupBox* infoGroup = new QGroupBox("System Information");
    QVBoxLayout* infoLayout = new QVBoxLayout(infoGroup);

    m_systemInfoLabel = new QLabel("No system loaded");
    m_systemInfoLabel->setStyleSheet(
        "QLabel {"
        "  padding: 8px;"
        "  background-color: #ecf0f1;"
        "  border-radius: 4px;"
        "  color: #7f8c8d;"
        "}"
    );
    infoLayout->addWidget(m_systemInfoLabel);

    m_deviceLabel = new QLabel();
    m_deviceLabel->setVisible(false);
    infoLayout->addWidget(m_deviceLabel);

    m_originLabel = new QLabel();
    m_originLabel->setVisible(false);
    infoLayout->addWidget(m_originLabel);

    m_axesLabel = new QLabel();
    m_axesLabel->setVisible(false);
    m_axesLabel->setWordWrap(true);
    infoLayout->addWidget(m_axesLabel);

    mainLayout->addWidget(infoGroup);

    // Movement Control Group
    QGroupBox* moveGroup = new QGroupBox("Movement Controls");
    QVBoxLayout* moveLayout = new QVBoxLayout(moveGroup);

    // Move to Origin button
    m_moveToOriginButton = new QPushButton("Move to Origin (0, 0, 0)");
    m_moveToOriginButton->setEnabled(false);
    m_moveToOriginButton->setFixedHeight(40);
    m_moveToOriginButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #9b59b6;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 10px;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #8e44ad;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #95a5a6;"
        "}"
    );
    moveLayout->addWidget(m_moveToOriginButton);

    // Local position input
    QGroupBox* localPosGroup = new QGroupBox("Move to Local Position");
    QFormLayout* localPosLayout = new QFormLayout(localPosGroup);

    m_localXSpinBox = new QDoubleSpinBox();
    m_localXSpinBox->setRange(-1000.0, 1000.0);
    m_localXSpinBox->setValue(0.0);
    m_localXSpinBox->setSuffix(" mm");
    m_localXSpinBox->setDecimals(3);
    m_localXSpinBox->setEnabled(false);
    localPosLayout->addRow("Local X:", m_localXSpinBox);

    m_localYSpinBox = new QDoubleSpinBox();
    m_localYSpinBox->setRange(-1000.0, 1000.0);
    m_localYSpinBox->setValue(0.0);
    m_localYSpinBox->setSuffix(" mm");
    m_localYSpinBox->setDecimals(3);
    m_localYSpinBox->setEnabled(false);
    localPosLayout->addRow("Local Y:", m_localYSpinBox);

    m_localZSpinBox = new QDoubleSpinBox();
    m_localZSpinBox->setRange(-1000.0, 1000.0);
    m_localZSpinBox->setValue(0.0);
    m_localZSpinBox->setSuffix(" mm");
    m_localZSpinBox->setDecimals(3);
    m_localZSpinBox->setEnabled(false);
    localPosLayout->addRow("Local Z:", m_localZSpinBox);

    m_moveToLocalPosButton = new QPushButton("Move to Local Position");
    m_moveToLocalPosButton->setEnabled(false);
    m_moveToLocalPosButton->setFixedHeight(40);
    m_moveToLocalPosButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #e67e22;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 10px;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #d35400;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #95a5a6;"
        "}"
    );
    localPosLayout->addRow(m_moveToLocalPosButton);

    moveLayout->addWidget(localPosGroup);
    mainLayout->addWidget(moveGroup);

    mainLayout->addStretch();

    // Connections
    connect(m_refreshButton, &QPushButton::clicked,
            this, &TestSystemWidget::onRefreshSystemsClicked);
    connect(m_loadButton, &QPushButton::clicked,
            this, &TestSystemWidget::onLoadSystemClicked);
    connect(m_systemSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestSystemWidget::onSystemSelected);
    connect(m_moveToOriginButton, &QPushButton::clicked,
            this, &TestSystemWidget::onMoveToOriginClicked);
    connect(m_moveToLocalPosButton, &QPushButton::clicked,
            this, &TestSystemWidget::onMoveToLocalPosClicked);

    // Initial refresh
    refreshSystemList();
}

void TestSystemWidget::onRefreshSystemsClicked()
{
    refreshSystemList();
}

void TestSystemWidget::onLoadSystemClicked()
{
    QString systemName = m_systemSelector->currentText();
    if (systemName.isEmpty()) {
        return;
    }

    m_currentSystemName = systemName.toStdString();

    CoordinateSystem system;
    if (m_coordinateSystemManager->GetSystem(m_currentSystemName, system)) {
        m_systemLoaded = true;
        updateSystemInfo();

        // Enable controls
        m_moveToOriginButton->setEnabled(true);
        m_moveToLocalPosButton->setEnabled(true);
        m_localXSpinBox->setEnabled(true);
        m_localYSpinBox->setEnabled(true);
        m_localZSpinBox->setEnabled(true);

        QMessageBox::information(this, "System Loaded",
            QString("System '%1' loaded successfully!").arg(systemName));
    }
    else {
        QMessageBox::critical(this, "Error",
            QString("Failed to load system '%1'").arg(systemName));
    }
}

void TestSystemWidget::onMoveToOriginClicked()
{
    if (!m_systemLoaded) {
        return;
    }

    // Convert local (0,0,0) to global coordinates
    Point3D localOrigin(0, 0, 0);
    Point3D globalPos;

    if (!m_coordinateSystemManager->LocalToGlobal(m_currentSystemName, localOrigin, globalPos)) {
        QMessageBox::critical(this, "Error", "Failed to transform coordinates");
        return;
    }

    // Get device name from system
    CoordinateSystem system;
    if (!m_coordinateSystemManager->GetSystem(m_currentSystemName, system)) {
        return;
    }

    // Move to global position
    bool success = m_motionManager->MoveToAbs(
        system.deviceName,
        globalPos.x,
        globalPos.y,
        globalPos.z,
        100.0,  // velocity
        false   // non-blocking
    );

    if (success) {
        QMessageBox::information(this, "Moving",
            QString("Moving to origin (0,0,0) in local coordinates\n"
                    "Global coordinates: X:%1 Y:%2 Z:%3")
                .arg(globalPos.x, 0, 'f', 3)
                .arg(globalPos.y, 0, 'f', 3)
                .arg(globalPos.z, 0, 'f', 3));
    }
    else {
        QMessageBox::critical(this, "Error", "Failed to move to origin");
    }
}

void TestSystemWidget::onMoveToLocalPosClicked()
{
    if (!m_systemLoaded) {
        return;
    }

    // Get local coordinates from spinboxes
    Point3D localPos(
        m_localXSpinBox->value(),
        m_localYSpinBox->value(),
        m_localZSpinBox->value()
    );

    // Convert to global coordinates
    Point3D globalPos;
    if (!m_coordinateSystemManager->LocalToGlobal(m_currentSystemName, localPos, globalPos)) {
        QMessageBox::critical(this, "Error", "Failed to transform coordinates");
        return;
    }

    // Get device name from system
    CoordinateSystem system;
    if (!m_coordinateSystemManager->GetSystem(m_currentSystemName, system)) {
        return;
    }

    // Move to global position
    bool success = m_motionManager->MoveToAbs(
        system.deviceName,
        globalPos.x,
        globalPos.y,
        globalPos.z,
        100.0,  // velocity
        false   // non-blocking
    );

    if (success) {
        QMessageBox::information(this, "Moving",
            QString("Moving to local position X:%1 Y:%2 Z:%3\n"
                    "Global coordinates: X:%4 Y:%5 Z:%6")
                .arg(localPos.x, 0, 'f', 3)
                .arg(localPos.y, 0, 'f', 3)
                .arg(localPos.z, 0, 'f', 3)
                .arg(globalPos.x, 0, 'f', 3)
                .arg(globalPos.y, 0, 'f', 3)
                .arg(globalPos.z, 0, 'f', 3));
    }
    else {
        QMessageBox::critical(this, "Error", "Failed to move to position");
    }
}

void TestSystemWidget::onSystemSelected(int index)
{
    m_loadButton->setEnabled(index >= 0 && !m_systemSelector->currentText().isEmpty());
}

void TestSystemWidget::refreshSystemList()
{
    m_systemSelector->clear();

    if (!m_coordinateSystemManager) {
        return;
    }

    // Get all system names
    std::vector<std::string> systems = m_coordinateSystemManager->GetAllSystemNames();

    for (const auto& systemName : systems) {
        m_systemSelector->addItem(QString::fromStdString(systemName));
    }

    if (m_systemSelector->count() == 0) {
        m_systemSelector->addItem("(No systems available)");
        m_loadButton->setEnabled(false);
    }
    else {
        m_loadButton->setEnabled(true);
    }
}

void TestSystemWidget::updateSystemInfo()
{
    if (!m_systemLoaded) {
        return;
    }

    CoordinateSystem system;
    if (!m_coordinateSystemManager->GetSystem(m_currentSystemName, system)) {
        return;
    }

    m_systemInfoLabel->setText(QString("System: %1").arg(QString::fromStdString(system.name)));
    m_systemInfoLabel->setStyleSheet(
        "QLabel {"
        "  padding: 8px;"
        "  background-color: #d5f4e6;"
        "  border-radius: 4px;"
        "  color: #27ae60;"
        "  font-weight: bold;"
        "}"
    );

    m_deviceLabel->setText(QString("Device: %1").arg(QString::fromStdString(system.deviceName)));
    m_deviceLabel->setVisible(true);

    m_originLabel->setText(QString("Origin (global): X:%1 Y:%2 Z:%3")
        .arg(system.origin_global.x, 0, 'f', 3)
        .arg(system.origin_global.y, 0, 'f', 3)
        .arg(system.origin_global.z, 0, 'f', 3));
    m_originLabel->setVisible(true);

    QString axesInfo = QString(
        "X-axis: (%1, %2, %3)\n"
        "Y-axis: (%4, %5, %6)\n"
        "Z-axis: (%7, %8, %9)")
        .arg(system.x_axis_unit.x, 0, 'f', 4)
        .arg(system.x_axis_unit.y, 0, 'f', 4)
        .arg(system.x_axis_unit.z, 0, 'f', 4)
        .arg(system.y_axis_unit.x, 0, 'f', 4)
        .arg(system.y_axis_unit.y, 0, 'f', 4)
        .arg(system.y_axis_unit.z, 0, 'f', 4)
        .arg(system.z_axis_unit.x, 0, 'f', 4)
        .arg(system.z_axis_unit.y, 0, 'f', 4)
        .arg(system.z_axis_unit.z, 0, 'f', 4);

    m_axesLabel->setText(axesInfo);
    m_axesLabel->setVisible(true);
}
