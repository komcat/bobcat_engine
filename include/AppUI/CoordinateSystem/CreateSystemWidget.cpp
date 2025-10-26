// CreateSystemWidget.cpp
#include "CreateSystemWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QSettings>

CreateSystemWidget::CreateSystemWidget(
    CoordinateSystemManager* coordinateSystemManager,
    UnifiedMotionManager* motionManager,
    QWidget* parent)
    : QWidget(parent)
    , m_coordinateSystemManager(coordinateSystemManager)
    , m_motionManager(motionManager)
    , m_point1Captured(false)
    , m_point2Captured(false)
    , m_point3ACaptured(false)
    , m_point3BCaptured(false)
    , m_point3CCaptured(false)
{
    setupUI();
}

void CreateSystemWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // Title
    QLabel* titleLabel = new QLabel("Create Coordinate System");
    titleLabel->setStyleSheet(
        "QLabel {"
        "  font-size: 18px;"
        "  font-weight: bold;"
        "  color: #2c3e50;"
        "  padding: 10px;"
        "}"
    );
    mainLayout->addWidget(titleLabel);

    // System Configuration Group
    QGroupBox* configGroup = new QGroupBox("System Configuration");
    QFormLayout* configLayout = new QFormLayout(configGroup);

    m_systemNameEdit = new QLineEdit();
    m_systemNameEdit->setPlaceholderText("Enter system name (e.g., PartA_System)");
    configLayout->addRow("System Name:", m_systemNameEdit);

    m_deviceSelector = new QComboBox();
    // Populate with ACS devices only
    if (m_motionManager) {
        std::vector<std::string> devices = m_motionManager->GetDeviceNamesByType("ACS");
        for (const auto& device : devices) {
            m_deviceSelector->addItem(QString::fromStdString(device));
        }
    }
    configLayout->addRow("Device:", m_deviceSelector);

    m_methodSelector = new QComboBox();
    m_methodSelector->addItem("2-Point + Offset Method");
    m_methodSelector->addItem("3-Point Rectangle Method");
    configLayout->addRow("Creation Method:", m_methodSelector);

    mainLayout->addWidget(configGroup);

    // Method-specific UI (stacked widget)
    m_methodStack = new QStackedWidget();

    setupTwoPointMethod();
    setupThreePointMethod();

    m_methodStack->addWidget(m_twoPointWidget);
    m_methodStack->addWidget(m_threePointWidget);

    mainLayout->addWidget(m_methodStack);

    // Status Label
    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet(
        "QLabel {"
        "  padding: 10px;"
        "  background-color: #ecf0f1;"
        "  border-radius: 4px;"
        "  color: #34495e;"
        "}"
    );
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_resetButton = new QPushButton("Reset");
    m_resetButton->setFixedHeight(40);
    m_resetButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #95a5a6;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 10px 20px;"
        "  font-size: 14px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #7f8c8d;"
        "}"
    );
    buttonLayout->addWidget(m_resetButton);

    m_createButton = new QPushButton("Create System");
    m_createButton->setEnabled(false);
    m_createButton->setFixedHeight(40);
    m_createButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #27ae60;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 10px 30px;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #229954;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #95a5a6;"
        "}"
    );
    buttonLayout->addWidget(m_createButton);

    mainLayout->addLayout(buttonLayout);
    mainLayout->addStretch();

    // Connections
    connect(m_methodSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CreateSystemWidget::onMethodChanged);
    connect(m_createButton, &QPushButton::clicked,
            this, &CreateSystemWidget::onCreateSystemClicked);
    connect(m_resetButton, &QPushButton::clicked,
            this, &CreateSystemWidget::onResetClicked);

    // Initialize
    updateStepStatus();
}

void CreateSystemWidget::setupTwoPointMethod()
{
    m_twoPointWidget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(m_twoPointWidget);

    QGroupBox* instructionsGroup = new QGroupBox("Instructions");
    QVBoxLayout* instrLayout = new QVBoxLayout(instructionsGroup);
    QLabel* instrLabel = new QLabel(
        "1. Jog to first point and click 'Capture Point 1'\n"
        "2. Jog to second point and click 'Capture Point 2'\n"
        "3. Program will find midpoint and offset perpendicular\n"
        "4. Select which axis the line(P1-P2) represents"
    );
    instrLabel->setWordWrap(true);
    instrLayout->addWidget(instrLabel);
    layout->addWidget(instructionsGroup);

    // Point capture group
    QGroupBox* captureGroup = new QGroupBox("Point Capture");
    QVBoxLayout* captureLayout = new QVBoxLayout(captureGroup);

    // Point 1
    QHBoxLayout* p1Layout = new QHBoxLayout();
    m_capturePoint1Btn = new QPushButton("Capture Point 1");
    m_capturePoint1Btn->setFixedHeight(35);
    m_point1Label = new QLabel("Not captured");
    m_point1Label->setStyleSheet("QLabel { color: #e74c3c; }");
    p1Layout->addWidget(m_capturePoint1Btn);
    p1Layout->addWidget(m_point1Label);
    p1Layout->addStretch();
    captureLayout->addLayout(p1Layout);

    // Point 2
    QHBoxLayout* p2Layout = new QHBoxLayout();
    m_capturePoint2Btn = new QPushButton("Capture Point 2");
    m_capturePoint2Btn->setFixedHeight(35);
    m_capturePoint2Btn->setEnabled(false);
    m_point2Label = new QLabel("Not captured");
    m_point2Label->setStyleSheet("QLabel { color: #e74c3c; }");
    p2Layout->addWidget(m_capturePoint2Btn);
    p2Layout->addWidget(m_point2Label);
    p2Layout->addStretch();
    captureLayout->addLayout(p2Layout);

    layout->addWidget(captureGroup);

    // Parameters group
    QGroupBox* paramGroup = new QGroupBox("Parameters");
    QFormLayout* paramLayout = new QFormLayout(paramGroup);

    m_offsetSpinBox = new QDoubleSpinBox();
    m_offsetSpinBox->setRange(-1000.0, 1000.0);
    m_offsetSpinBox->setValue(0.0);
    m_offsetSpinBox->setSuffix(" mm");
    m_offsetSpinBox->setDecimals(3);
    paramLayout->addRow("Perpendicular Offset:", m_offsetSpinBox);

    m_axisSelector2Point = new QComboBox();
    m_axisSelector2Point->addItem("X (line P1-P2 is X-axis)");
    m_axisSelector2Point->addItem("Y (line P1-P2 is Y-axis)");
    paramLayout->addRow("Axis Selection:", m_axisSelector2Point);

    layout->addWidget(paramGroup);
    layout->addStretch();

    // Connections
    connect(m_capturePoint1Btn, &QPushButton::clicked,
            this, &CreateSystemWidget::onCapturePoint1Clicked);
    connect(m_capturePoint2Btn, &QPushButton::clicked,
            this, &CreateSystemWidget::onCapturePoint2Clicked);
}

void CreateSystemWidget::setupThreePointMethod()
{
    m_threePointWidget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(m_threePointWidget);

    QGroupBox* instructionsGroup = new QGroupBox("Instructions");
    QVBoxLayout* instrLayout = new QVBoxLayout(instructionsGroup);
    QLabel* instrLabel = new QLabel(
        "1. Jog to first corner and click 'Capture Point A'\n"
        "2. Jog to second corner and click 'Capture Point B'\n"
        "3. Jog to third corner and click 'Capture Point C'\n"
        "4. Program will calculate rectangle center as origin\n"
        "5. Select which edge should be the X-axis"
    );
    instrLabel->setWordWrap(true);
    instrLayout->addWidget(instrLabel);
    layout->addWidget(instructionsGroup);

    // Point capture group
    QGroupBox* captureGroup = new QGroupBox("Point Capture");
    QVBoxLayout* captureLayout = new QVBoxLayout(captureGroup);

    // Point A
    QHBoxLayout* paLayout = new QHBoxLayout();
    m_capturePoint3ABtn = new QPushButton("Capture Point A");
    m_capturePoint3ABtn->setFixedHeight(35);
    m_point3ALabel = new QLabel("Not captured");
    m_point3ALabel->setStyleSheet("QLabel { color: #e74c3c; }");
    paLayout->addWidget(m_capturePoint3ABtn);
    paLayout->addWidget(m_point3ALabel);
    paLayout->addStretch();
    captureLayout->addLayout(paLayout);

    // Point B
    QHBoxLayout* pbLayout = new QHBoxLayout();
    m_capturePoint3BBtn = new QPushButton("Capture Point B");
    m_capturePoint3BBtn->setFixedHeight(35);
    m_capturePoint3BBtn->setEnabled(false);
    m_point3BLabel = new QLabel("Not captured");
    m_point3BLabel->setStyleSheet("QLabel { color: #e74c3c; }");
    pbLayout->addWidget(m_capturePoint3BBtn);
    pbLayout->addWidget(m_point3BLabel);
    pbLayout->addStretch();
    captureLayout->addLayout(pbLayout);

    // Point C
    QHBoxLayout* pcLayout = new QHBoxLayout();
    m_capturePoint3CBtn = new QPushButton("Capture Point C");
    m_capturePoint3CBtn->setFixedHeight(35);
    m_capturePoint3CBtn->setEnabled(false);
    m_point3CLabel = new QLabel("Not captured");
    m_point3CLabel->setStyleSheet("QLabel { color: #e74c3c; }");
    pcLayout->addWidget(m_capturePoint3CBtn);
    pcLayout->addWidget(m_point3CLabel);
    pcLayout->addStretch();
    captureLayout->addLayout(pcLayout);

    layout->addWidget(captureGroup);

    // Parameters group
    QGroupBox* paramGroup = new QGroupBox("Parameters");
    QFormLayout* paramLayout = new QFormLayout(paramGroup);

    m_axisSelector3Point = new QComboBox();
    m_axisSelector3Point->addItem("X (edge A-B is X-axis)");
    m_axisSelector3Point->addItem("Y (edge A-B is Y-axis)");
    paramLayout->addRow("Axis Selection:", m_axisSelector3Point);

    layout->addWidget(paramGroup);
    layout->addStretch();

    // Connections
    connect(m_capturePoint3ABtn, &QPushButton::clicked,
            this, &CreateSystemWidget::onCapturePoint3Clicked);
    connect(m_capturePoint3BBtn, &QPushButton::clicked,
            this, &CreateSystemWidget::onCapturePoint3Clicked);
    connect(m_capturePoint3CBtn, &QPushButton::clicked,
            this, &CreateSystemWidget::onCapturePoint3Clicked);
}

void CreateSystemWidget::onMethodChanged(int index)
{
    m_methodStack->setCurrentIndex(index);
    resetAll();
}

void CreateSystemWidget::onCapturePoint1Clicked()
{
    if (captureCurrentPosition(m_point1)) {
        m_point1Captured = true;
        m_point1Label->setText(QString("X:% Y:%2 Z:%3")
            .arg(m_point1.x, 0, 'f', 3)
            .arg(m_point1.y, 0, 'f', 3)
            .arg(m_point1.z, 0, 'f', 3));
        m_point1Label->setStyleSheet("QLabel { color: #27ae60; }");
        m_capturePoint2Btn->setEnabled(true);
        updateStepStatus();
    }
}

void CreateSystemWidget::onCapturePoint2Clicked()
{
    if (captureCurrentPosition(m_point2)) {
        m_point2Captured = true;
        m_point2Label->setText(QString("X:%1 Y:%2 Z:%3")
            .arg(m_point2.x, 0, 'f', 3)
            .arg(m_point2.y, 0, 'f', 3)
            .arg(m_point2.z, 0, 'f', 3));
        m_point2Label->setStyleSheet("QLabel { color: #27ae60; }");
        updateStepStatus();
    }
}

void CreateSystemWidget::onCapturePoint3Clicked()
{
    QPushButton* sender = qobject_cast<QPushButton*>(QObject::sender());

    if (sender == m_capturePoint3ABtn) {
        if (captureCurrentPosition(m_point3A)) {
            m_point3ACaptured = true;
            m_point3ALabel->setText(QString("X:%1 Y:%2 Z:%3")
                .arg(m_point3A.x, 0, 'f', 3)
                .arg(m_point3A.y, 0, 'f', 3)
                .arg(m_point3A.z, 0, 'f', 3));
            m_point3ALabel->setStyleSheet("QLabel { color: #27ae60; }");
            m_capturePoint3BBtn->setEnabled(true);
        }
    }
    else if (sender == m_capturePoint3BBtn) {
        if (captureCurrentPosition(m_point3B)) {
            m_point3BCaptured = true;
            m_point3BLabel->setText(QString("X:%1 Y:%2 Z:%3")
                .arg(m_point3B.x, 0, 'f', 3)
                .arg(m_point3B.y, 0, 'f', 3)
                .arg(m_point3B.z, 0, 'f', 3));
            m_point3BLabel->setStyleSheet("QLabel { color: #27ae60; }");
            m_capturePoint3CBtn->setEnabled(true);
        }
    }
    else if (sender == m_capturePoint3CBtn) {
        if (captureCurrentPosition(m_point3C)) {
            m_point3CCaptured = true;
            m_point3CLabel->setText(QString("X:%1 Y:%2 Z:%3")
                .arg(m_point3C.x, 0, 'f', 3)
                .arg(m_point3C.y, 0, 'f', 3)
                .arg(m_point3C.z, 0, 'f', 3));
            m_point3CLabel->setStyleSheet("QLabel { color: #27ae60; }");
        }
    }

    updateStepStatus();
}

void CreateSystemWidget::onCreateSystemClicked()
{
    QString systemName = m_systemNameEdit->text().trimmed();
    QString deviceName = m_deviceSelector->currentText();

    if (systemName.isEmpty()) {
        QMessageBox::warning(this, "Invalid Input", "Please enter a system name.");
        return;
    }

    if (deviceName.isEmpty()) {
        QMessageBox::warning(this, "Invalid Input", "Please select a device.");
        return;
    }

    bool success = false;
    int method = m_methodSelector->currentIndex();

    if (method == 0) {
        // 2-Point method
        double offset = m_offsetSpinBox->value();
        QString axis = m_axisSelector2Point->currentIndex() == 0 ? "X" : "Y";

        success = m_coordinateSystemManager->CreateSystemTwoPoint(
            systemName.toStdString(),
            deviceName.toStdString(),
            m_point1,
            m_point2,
            offset,
            axis.toStdString()
        );
    }
    else {
        // 3-Point method
        QString axis = m_axisSelector3Point->currentIndex() == 0 ? "X" : "Y";

        success = m_coordinateSystemManager->CreateSystemThreePoint(
            systemName.toStdString(),
            deviceName.toStdString(),
            m_point3A,
            m_point3B,
            m_point3C,
            axis.toStdString()
        );
    }

    if (success) {
        // Save to file
        // For now, save to a default product folder
        QString productName = "Default";
        m_coordinateSystemManager->SaveSystem(systemName.toStdString(), productName.toStdString());

        QMessageBox::information(this, "Success",
            QString("Coordinate system '%1' created successfully!").arg(systemName));

        emit systemCreated(systemName);
        resetAll();
    }
    else {
        QMessageBox::critical(this, "Error",
            "Failed to create coordinate system. Please check your inputs.");
    }
}

void CreateSystemWidget::onResetClicked()
{
    resetAll();
}

bool CreateSystemWidget::captureCurrentPosition(Point3D& point)
{
    QString deviceName = m_deviceSelector->currentText();
    if (deviceName.isEmpty()) {
        QMessageBox::warning(this, "No Device", "Please select a device first.");
        return false;
    }

    if (!m_motionManager) {
        QMessageBox::critical(this, "Error", "Motion manager not available.");
        return false;
    }

    PositionStruct pos = m_motionManager->GetCurrentPosition(deviceName.toStdString());
    point.x = pos.x;
    point.y = pos.y;
    point.z = pos.z;

    return true;
}

void CreateSystemWidget::updateStepStatus()
{
    int method = m_methodSelector->currentIndex();

    if (method == 0) {
        // 2-Point method
        if (!m_point1Captured) {
            m_statusLabel->setText("Step 1: Jog to first point and capture it");
            m_createButton->setEnabled(false);
        }
        else if (!m_point2Captured) {
            m_statusLabel->setText("Step 2: Jog to second point and capture it");
            m_createButton->setEnabled(false);
        }
        else {
            m_statusLabel->setText("All points captured! Review parameters and click 'Create System'");
            m_createButton->setEnabled(!m_systemNameEdit->text().trimmed().isEmpty());
        }
    }
    else {
        // 3-Point method
        if (!m_point3ACaptured) {
            m_statusLabel->setText("Step 1: Jog to point A and capture it");
            m_createButton->setEnabled(false);
        }
        else if (!m_point3BCaptured) {
            m_statusLabel->setText("Step 2: Jog to point B and capture it");
            m_createButton->setEnabled(false);
        }
        else if (!m_point3CCaptured) {
            m_statusLabel->setText("Step 3: Jog to point C and capture it");
            m_createButton->setEnabled(false);
        }
        else {
            m_statusLabel->setText("All points captured! Review parameters and click 'Create System'");
            m_createButton->setEnabled(!m_systemNameEdit->text().trimmed().isEmpty());
        }
    }
}

void CreateSystemWidget::resetAll()
{
    m_point1Captured = false;
    m_point2Captured = false;
    m_point3ACaptured = false;
    m_point3BCaptured = false;
    m_point3CCaptured = false;

    m_point1Label->setText("Not captured");
    m_point1Label->setStyleSheet("QLabel { color: #e74c3c; }");
    m_point2Label->setText("Not captured");
    m_point2Label->setStyleSheet("QLabel { color: #e74c3c; }");

    m_point3ALabel->setText("Not captured");
    m_point3ALabel->setStyleSheet("QLabel { color: #e74c3c; }");
    m_point3BLabel->setText("Not captured");
    m_point3BLabel->setStyleSheet("QLabel { color: #e74c3c; }");
    m_point3CLabel->setText("Not captured");
    m_point3CLabel->setStyleSheet("QLabel { color: #e74c3c; }");

    m_capturePoint2Btn->setEnabled(false);
    m_capturePoint3BBtn->setEnabled(false);
    m_capturePoint3CBtn->setEnabled(false);

    m_offsetSpinBox->setValue(0.0);

    updateStepStatus();
}
