// CreateSystemWidget.cpp
#include "CreateSystemWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QLayout>
#include <QSettings>

CreateSystemWidget::CreateSystemWidget(
    CoordinateSystemManager* coordinateSystemManager,
    UnifiedMotionManager* motionManager,
    ProductPositionManager* productManager,
    QWidget* parent)
    : QWidget(parent)
    , m_coordinateSystemManager(coordinateSystemManager)
    , m_motionManager(motionManager)
    , m_productManager(productManager)
    , m_currentStep(0)
    , m_totalSteps(4)
    , m_point1Captured(false)
    , m_point2Captured(false)
    , m_point3Captured(false)
{
    setupUI();
    updateStepIndicator();
}

void CreateSystemWidget::setupUI()
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ============================================================================
    // LEFT PANEL - Step Indicator
    // ============================================================================
    setupStepPanel();
    mainLayout->addWidget(m_stepPanel);

    // ============================================================================
    // RIGHT PANEL - Content Area + Navigation
    // ============================================================================
    QWidget* rightPanel = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(30, 30, 30, 30);
    rightLayout->setSpacing(20);

    // Content stack
    m_contentStack = new QStackedWidget();
    setupStepPages();
    rightLayout->addWidget(m_contentStack, 1);

    // Navigation buttons at bottom
    QHBoxLayout* navLayout = new QHBoxLayout();
    navLayout->setSpacing(15);

    m_cancelButton = new QPushButton("Cancel");
    m_cancelButton->setMinimumHeight(45);
    m_cancelButton->setMinimumWidth(120);
    m_cancelButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #95a5a6;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 6px;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "  padding: 10px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #7f8c8d;"
        "}"
    );
    navLayout->addWidget(m_cancelButton);
    navLayout->addStretch();

    m_previousButton = new QPushButton("← Previous");
    m_previousButton->setMinimumHeight(45);
    m_previousButton->setMinimumWidth(120);
    m_previousButton->setEnabled(false);
    m_previousButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #3498db;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 6px;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "  padding: 10px;"
        "}"
        "QPushButton:hover:enabled {"
        "  background-color: #2980b9;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #bdc3c7;"
        "}"
    );
    navLayout->addWidget(m_previousButton);

    m_nextButton = new QPushButton("Next →");
    m_nextButton->setMinimumHeight(45);
    m_nextButton->setMinimumWidth(120);
    m_nextButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #27ae60;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 6px;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "  padding: 10px;"
        "}"
        "QPushButton:hover:enabled {"
        "  background-color: #229954;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #bdc3c7;"
        "}"
    );
    navLayout->addWidget(m_nextButton);

    rightLayout->addLayout(navLayout);

    mainLayout->addWidget(rightPanel, 1);

    // Connections
    connect(m_previousButton, &QPushButton::clicked, this, &CreateSystemWidget::onPreviousClicked);
    connect(m_nextButton, &QPushButton::clicked, this, &CreateSystemWidget::onNextClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &CreateSystemWidget::onCancelClicked);
}

void CreateSystemWidget::setupStepPanel()
{
    m_stepPanel = new QWidget();
    m_stepPanel->setFixedWidth(250);
    m_stepPanel->setStyleSheet(
        "QWidget {"
        "  background-color: #2c3e50;"
        "  border-right: 2px solid #34495e;"
        "}"
    );

    QVBoxLayout* stepLayout = new QVBoxLayout(m_stepPanel);
    stepLayout->setContentsMargins(20, 40, 20, 40);
    stepLayout->setSpacing(30);

    // Title
    QLabel* titleLabel = new QLabel("Create System");
    titleLabel->setStyleSheet(
        "QLabel {"
        "  color: white;"
        "  font-size: 20px;"
        "  font-weight: bold;"
        "  padding-bottom: 10px;"
        "}"
    );
    stepLayout->addWidget(titleLabel);

    // Current Product Display
    QString productName = "No Product";
    if (m_productManager && m_productManager->HasLoadedProduct()) {
        productName = QString::fromStdString(m_productManager->GetCurrentProductName());
    }
    QLabel* productLabel = new QLabel("Product: " + productName);
    productLabel->setStyleSheet(
        "QLabel {"
        "  color: #3498db;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "  padding: 8px;"
        "  background-color: #34495e;"
        "  border-radius: 4px;"
        "  margin-bottom: 10px;"
        "}"
    );
    productLabel->setWordWrap(true);
    stepLayout->addWidget(productLabel);

    // Step indicators
    QStringList stepNames;
    stepNames << "Configuration" << "Capture Point 1" << "Capture Point 2" << "Parameters & Create";

    for (int i = 0; i < stepNames.size(); ++i) {
        QWidget* stepItem = new QWidget();
        QHBoxLayout* itemLayout = new QHBoxLayout(stepItem);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(15);

        // Step number circle
        QLabel* numberLabel = new QLabel(QString::number(i + 1));
        numberLabel->setFixedSize(40, 40);
        numberLabel->setAlignment(Qt::AlignCenter);
        numberLabel->setStyleSheet(
            "QLabel {"
            "  background-color: #34495e;"
            "  color: #95a5a6;"
            "  border-radius: 20px;"
            "  font-size: 18px;"
            "  font-weight: bold;"
            "}"
        );
        itemLayout->addWidget(numberLabel);
        m_stepNumbers.append(numberLabel);

        // Step name
        QLabel* nameLabel = new QLabel(stepNames[i]);
        nameLabel->setWordWrap(true);
        nameLabel->setStyleSheet(
            "QLabel {"
            "  color: #95a5a6;"
            "  font-size: 14px;"
            "}"
        );
        itemLayout->addWidget(nameLabel, 1);
        m_stepLabels.append(nameLabel);

        stepLayout->addWidget(stepItem);
    }

    stepLayout->addStretch();
}

void CreateSystemWidget::setupStepPages()
{
    m_contentStack->addWidget(createConfigurationPage());
    m_contentStack->addWidget(createCapturePoint1Page());
    m_contentStack->addWidget(createCapturePoint2Page());
    m_contentStack->addWidget(createParametersPage());
}

QWidget* CreateSystemWidget::createConfigurationPage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setSpacing(20);

    // Title
    QLabel* title = new QLabel("Step 1: System Configuration");
    title->setStyleSheet(
        "QLabel {"
        "  font-size: 24px;"
        "  font-weight: bold;"
        "  color: #2c3e50;"
        "  padding-bottom: 10px;"
        "}"
    );
    layout->addWidget(title);

    // Description
    QLabel* desc = new QLabel("Enter the basic information for your coordinate system.");
    desc->setStyleSheet("QLabel { font-size: 14px; color: #7f8c8d; padding-bottom: 20px; }");
    layout->addWidget(desc);

    // Form
    QGroupBox* formGroup = new QGroupBox();
    formGroup->setStyleSheet(
        "QGroupBox {"
        "  border: 2px solid #ecf0f1;"
        "  border-radius: 8px;"
        "  margin-top: 10px;"
        "  padding: 20px;"
        "}"
    );
    QFormLayout* formLayout = new QFormLayout(formGroup);
    formLayout->setSpacing(20);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // System name
    m_systemNameEdit = new QLineEdit();
    m_systemNameEdit->setPlaceholderText("e.g., PartA_System");
    m_systemNameEdit->setMinimumHeight(40);
    m_systemNameEdit->setStyleSheet(
        "QLineEdit {"
        "  font-size: 14px;"
        "  padding: 8px;"
        "  border: 2px solid #bdc3c7;"
        "  border-radius: 4px;"
        "}"
        "QLineEdit:focus {"
        "  border: 2px solid #3498db;"
        "}"
    );
    QLabel* nameLabel = new QLabel("System Name:");
    nameLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; }");
    formLayout->addRow(nameLabel, m_systemNameEdit);

    // Device selector
    m_deviceSelector = new QComboBox();
    m_deviceSelector->setMinimumHeight(40);
    m_deviceSelector->setStyleSheet(
        "QComboBox {"
        "  font-size: 14px;"
        "  padding: 8px;"
        "  border: 2px solid #bdc3c7;"
        "  border-radius: 4px;"
        "}"
    );
    if (m_motionManager) {
        std::vector<std::string> devices = m_motionManager->GetDeviceNamesByType("ACS");
        for (const auto& device : devices) {
            m_deviceSelector->addItem(QString::fromStdString(device));
        }
    }
    QLabel* deviceLabel = new QLabel("Device:");
    deviceLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; }");
    formLayout->addRow(deviceLabel, m_deviceSelector);

    // Method selector
    m_methodSelector = new QComboBox();
    m_methodSelector->addItem("2-Point + Offset Method", 2);
    m_methodSelector->addItem("3-Point Rectangle Method", 3);
    m_methodSelector->setMinimumHeight(40);
    m_methodSelector->setStyleSheet(
        "QComboBox {"
        "  font-size: 14px;"
        "  padding: 8px;"
        "  border: 2px solid #bdc3c7;"
        "  border-radius: 4px;"
        "}"
    );
    QLabel* methodLabel = new QLabel("Creation Method:");
    methodLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; }");
    formLayout->addRow(methodLabel, m_methodSelector);

    layout->addWidget(formGroup);
    layout->addStretch();

    connect(m_methodSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CreateSystemWidget::onMethodChanged);

    return page;
}

QWidget* CreateSystemWidget::createCapturePoint1Page()
{
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setSpacing(20);

    // Title
    QLabel* title = new QLabel("Step 2: Capture Point 1");
    title->setStyleSheet(
        "QLabel {"
        "  font-size: 24px;"
        "  font-weight: bold;"
        "  color: #2c3e50;"
        "  padding-bottom: 10px;"
        "}"
    );
    layout->addWidget(title);

    // Description
    QLabel* desc = new QLabel("Jog the device to the first point and click 'Capture' to record its position.");
    desc->setStyleSheet("QLabel { font-size: 14px; color: #7f8c8d; padding-bottom: 20px; }");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // Capture area
    QWidget* captureArea = new QWidget();
    captureArea->setStyleSheet(
        "QWidget {"
        "  background-color: #ecf0f1;"
        "  border-radius: 8px;"
        "  padding: 40px;"
        "}"
    );
    QVBoxLayout* captureLayout = new QVBoxLayout(captureArea);
    captureLayout->setSpacing(20);
    captureLayout->setAlignment(Qt::AlignCenter);

    QPushButton* captureBtn = new QPushButton("📍 Capture Point 1");
    captureBtn->setMinimumHeight(60);
    captureBtn->setMinimumWidth(250);
    captureBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #3498db;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 8px;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #2980b9;"
        "}"
    );
    captureLayout->addWidget(captureBtn, 0, Qt::AlignCenter);

    m_point1Label = new QLabel("Not captured");
    m_point1Label->setAlignment(Qt::AlignCenter);
    m_point1Label->setMinimumHeight(40);
    m_point1Label->setStyleSheet(
        "QLabel {"
        "  color: #e74c3c;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "  padding: 10px;"
        "}"
    );
    captureLayout->addWidget(m_point1Label);

    layout->addWidget(captureArea);
    layout->addStretch();

    connect(captureBtn, &QPushButton::clicked, this, &CreateSystemWidget::onCapturePoint1Clicked);

    return page;
}

QWidget* CreateSystemWidget::createCapturePoint2Page()
{
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setSpacing(20);

    // Title
    QLabel* title = new QLabel("Step 3: Capture Point 2");
    title->setStyleSheet(
        "QLabel {"
        "  font-size: 24px;"
        "  font-weight: bold;"
        "  color: #2c3e50;"
        "  padding-bottom: 10px;"
        "}"
    );
    layout->addWidget(title);

    // Description
    QLabel* desc = new QLabel("Jog the device to the second point and click 'Capture' to record its position.");
    desc->setStyleSheet("QLabel { font-size: 14px; color: #7f8c8d; padding-bottom: 20px; }");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // Capture area
    QWidget* captureArea = new QWidget();
    captureArea->setStyleSheet(
        "QWidget {"
        "  background-color: #ecf0f1;"
        "  border-radius: 8px;"
        "  padding: 40px;"
        "}"
    );
    QVBoxLayout* captureLayout = new QVBoxLayout(captureArea);
    captureLayout->setSpacing(20);
    captureLayout->setAlignment(Qt::AlignCenter);

    QPushButton* captureBtn = new QPushButton("📍 Capture Point 2");
    captureBtn->setMinimumHeight(60);
    captureBtn->setMinimumWidth(250);
    captureBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #3498db;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 8px;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #2980b9;"
        "}"
    );
    captureLayout->addWidget(captureBtn, 0, Qt::AlignCenter);

    m_point2Label = new QLabel("Not captured");
    m_point2Label->setAlignment(Qt::AlignCenter);
    m_point2Label->setMinimumHeight(40);
    m_point2Label->setStyleSheet(
        "QLabel {"
        "  color: #e74c3c;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "  padding: 10px;"
        "}"
    );
    captureLayout->addWidget(m_point2Label);

    layout->addWidget(captureArea);
    layout->addStretch();

    connect(captureBtn, &QPushButton::clicked, this, &CreateSystemWidget::onCapturePoint2Clicked);

    return page;
}

QWidget* CreateSystemWidget::createCapturePoint3Page()
{
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setSpacing(20);

    // Title
    QLabel* title = new QLabel("Step 3: Capture Point 3");
    title->setStyleSheet(
        "QLabel {"
        "  font-size: 24px;"
        "  font-weight: bold;"
        "  color: #2c3e50;"
        "  padding-bottom: 10px;"
        "}"
    );
    layout->addWidget(title);

    // Description
    QLabel* desc = new QLabel("Jog the device to the third point and click 'Capture' to record its position.");
    desc->setStyleSheet("QLabel { font-size: 14px; color: #7f8c8d; padding-bottom: 20px; }");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // Capture area
    QWidget* captureArea = new QWidget();
    captureArea->setStyleSheet(
        "QWidget {"
        "  background-color: #ecf0f1;"
        "  border-radius: 8px;"
        "  padding: 40px;"
        "}"
    );
    QVBoxLayout* captureLayout = new QVBoxLayout(captureArea);
    captureLayout->setSpacing(20);
    captureLayout->setAlignment(Qt::AlignCenter);

    QPushButton* captureBtn = new QPushButton("📍 Capture Point 3");
    captureBtn->setMinimumHeight(60);
    captureBtn->setMinimumWidth(250);
    captureBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #3498db;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 8px;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #2980b9;"
        "}"
    );
    captureLayout->addWidget(captureBtn, 0, Qt::AlignCenter);

    m_point3Label = new QLabel("Not captured");
    m_point3Label->setAlignment(Qt::AlignCenter);
    m_point3Label->setMinimumHeight(40);
    m_point3Label->setStyleSheet(
        "QLabel {"
        "  color: #e74c3c;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "  padding: 10px;"
        "}"
    );
    captureLayout->addWidget(m_point3Label);

    layout->addWidget(captureArea);
    layout->addStretch();

    connect(captureBtn, &QPushButton::clicked, this, &CreateSystemWidget::onCapturePoint3Clicked);

    return page;
}

QWidget* CreateSystemWidget::createParametersPage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setSpacing(20);

    // Title
    QLabel* title = new QLabel("Step 4: Set Parameters");
    title->setStyleSheet(
        "QLabel {"
        "  font-size: 24px;"
        "  font-weight: bold;"
        "  color: #2c3e50;"
        "  padding-bottom: 10px;"
        "}"
    );
    layout->addWidget(title);

    // Description
    QLabel* desc = new QLabel("Configure the final parameters for your coordinate system.");
    desc->setStyleSheet("QLabel { font-size: 14px; color: #7f8c8d; padding-bottom: 20px; }");
    layout->addWidget(desc);

    // Form
    QGroupBox* formGroup = new QGroupBox();
    formGroup->setStyleSheet(
        "QGroupBox {"
        "  border: 2px solid #ecf0f1;"
        "  border-radius: 8px;"
        "  margin-top: 10px;"
        "  padding: 20px;"
        "}"
    );
    QFormLayout* formLayout = new QFormLayout(formGroup);
    formLayout->setSpacing(20);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Offset (for 2-point method)
    m_offsetSpinBox = new QDoubleSpinBox();
    m_offsetSpinBox->setRange(-1000.0, 1000.0);
    m_offsetSpinBox->setValue(0.0);
    m_offsetSpinBox->setSuffix(" mm");
    m_offsetSpinBox->setDecimals(3);
    m_offsetSpinBox->setMinimumHeight(40);
    m_offsetSpinBox->setStyleSheet(
        "QDoubleSpinBox {"
        "  font-size: 14px;"
        "  padding: 8px;"
        "  border: 2px solid #bdc3c7;"
        "  border-radius: 4px;"
        "}"
    );
    QLabel* offsetLabel = new QLabel("Perpendicular Offset:");
    offsetLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; }");
    formLayout->addRow(offsetLabel, m_offsetSpinBox);

    // Axis selector
    m_axisSelector = new QComboBox();
    m_axisSelector->addItem("X (line P1-P2 is X-axis)");
    m_axisSelector->addItem("Y (line P1-P2 is Y-axis)");
    m_axisSelector->setMinimumHeight(40);
    m_axisSelector->setStyleSheet(
        "QComboBox {"
        "  font-size: 14px;"
        "  padding: 8px;"
        "  border: 2px solid #bdc3c7;"
        "  border-radius: 4px;"
        "}"
    );
    QLabel* axisLabel = new QLabel("Axis Selection:");
    axisLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; }");
    formLayout->addRow(axisLabel, m_axisSelector);

    layout->addWidget(formGroup);
    layout->addStretch();

    return page;
}

void CreateSystemWidget::onMethodChanged(int index)
{
    int method = m_methodSelector->itemData(index).toInt();

    // Update total steps and step labels
    if (method == 2) {
        // 2-Point method: Config, Point1, Point2, Parameters
        m_totalSteps = 4;
        if (m_stepLabels.size() >= 4) {
            m_stepLabels[1]->setText("Capture Point 1");
            m_stepLabels[2]->setText("Capture Point 2");
            m_stepLabels[3]->setText("Parameters & Create");
        }
        // Hide/show appropriate pages
        if (m_contentStack->count() == 5) {
            m_contentStack->removeWidget(m_contentStack->widget(3)); // Remove Point 3 page
        }
        m_offsetSpinBox->setVisible(true);
        QFormLayout* formLayout = qobject_cast<QFormLayout*>(m_offsetSpinBox->parentWidget()->layout());
        if (formLayout) {
            QWidget* label = formLayout->labelForField(m_offsetSpinBox);
            if (label) label->setVisible(true);
        }
    } else {
        // 3-Point method: Config, Point1, Point2, Point3, Parameters
        m_totalSteps = 5;
        if (m_stepLabels.size() >= 4) {
            m_stepLabels[1]->setText("Capture Point A");
            m_stepLabels[2]->setText("Capture Point B");
            m_stepLabels[3]->setText("Capture Point C");
        }
        // Add Point 3 page if not present
        if (m_contentStack->count() == 4) {
            m_contentStack->insertWidget(3, createCapturePoint3Page());
        }
        m_offsetSpinBox->setVisible(false);
        QFormLayout* formLayout = qobject_cast<QFormLayout*>(m_offsetSpinBox->parentWidget()->layout());
        if (formLayout) {
            QWidget* label = formLayout->labelForField(m_offsetSpinBox);
            if (label) label->setVisible(false);
        }
    }

    // Update axis selector text
    if (method == 2) {
        m_axisSelector->setItemText(0, "X (line P1-P2 is X-axis)");
        m_axisSelector->setItemText(1, "Y (line P1-P2 is Y-axis)");
    } else {
        m_axisSelector->setItemText(0, "X (edge A-B is X-axis)");
        m_axisSelector->setItemText(1, "Y (edge A-B is Y-axis)");
    }

    resetAll();
}

void CreateSystemWidget::onCapturePoint1Clicked()
{
    if (captureCurrentPosition(m_point1)) {
        m_point1Captured = true;
        m_point1Label->setText(QString("✓ X:%1  Y:%2  Z:%3")
            .arg(m_point1.x, 0, 'f', 3)
            .arg(m_point1.y, 0, 'f', 3)
            .arg(m_point1.z, 0, 'f', 3));
        m_point1Label->setStyleSheet(
            "QLabel {"
            "  color: #27ae60;"
            "  font-size: 16px;"
            "  font-weight: bold;"
            "  padding: 10px;"
            "}"
        );
    }
}

void CreateSystemWidget::onCapturePoint2Clicked()
{
    if (captureCurrentPosition(m_point2)) {
        m_point2Captured = true;
        m_point2Label->setText(QString("✓ X:%1  Y:%2  Z:%3")
            .arg(m_point2.x, 0, 'f', 3)
            .arg(m_point2.y, 0, 'f', 3)
            .arg(m_point2.z, 0, 'f', 3));
        m_point2Label->setStyleSheet(
            "QLabel {"
            "  color: #27ae60;"
            "  font-size: 16px;"
            "  font-weight: bold;"
            "  padding: 10px;"
            "}"
        );
    }
}

void CreateSystemWidget::onCapturePoint3Clicked()
{
    if (captureCurrentPosition(m_point3)) {
        m_point3Captured = true;
        m_point3Label->setText(QString("✓ X:%1  Y:%2  Z:%3")
            .arg(m_point3.x, 0, 'f', 3)
            .arg(m_point3.y, 0, 'f', 3)
            .arg(m_point3.z, 0, 'f', 3));
        m_point3Label->setStyleSheet(
            "QLabel {"
            "  color: #27ae60;"
            "  font-size: 16px;"
            "  font-weight: bold;"
            "  padding: 10px;"
            "}"
        );
    }
}

void CreateSystemWidget::onCreateSystemClicked()
{
    QString systemName = m_systemNameEdit->text().trimmed();
    QString deviceName = m_deviceSelector->currentText();
    int method = m_methodSelector->currentData().toInt();

    if (systemName.isEmpty()) {
        QMessageBox::warning(this, "Invalid Input", "Please enter a system name.");
        return;
    }

    if (deviceName.isEmpty()) {
        QMessageBox::warning(this, "Invalid Input", "Please select a device.");
        return;
    }

    bool success = false;

    if (method == 2) {
        // 2-Point method
        double offset = m_offsetSpinBox->value();
        QString axis = m_axisSelector->currentIndex() == 0 ? "X" : "Y";

        success = m_coordinateSystemManager->CreateSystemTwoPoint(
            systemName.toStdString(),
            deviceName.toStdString(),
            m_point1,
            m_point2,
            offset,
            axis.toStdString()
        );
    } else {
        // 3-Point method
        QString axis = m_axisSelector->currentIndex() == 0 ? "X" : "Y";

        success = m_coordinateSystemManager->CreateSystemThreePoint(
            systemName.toStdString(),
            deviceName.toStdString(),
            m_point1,
            m_point2,
            m_point3,
            axis.toStdString()
        );
    }

    if (success) {
        // Get current product name
        if (!m_productManager || !m_productManager->HasLoadedProduct()) {
            QMessageBox::critical(this, "Error", "No product loaded. Cannot save coordinate system.");
            return;
        }

        QString productName = QString::fromStdString(m_productManager->GetCurrentProductName());
        bool saved = m_coordinateSystemManager->SaveSystem(systemName.toStdString(), productName.toStdString());

        if (saved) {
            QMessageBox::information(this, "Success",
                QString("Coordinate system '%1' created successfully!\n\nSaved to: config/%2/%1.system.json")
                .arg(systemName)
                .arg(productName));

            emit systemCreated(systemName);
            resetAll();
        } else {
            QMessageBox::critical(this, "Error",
                QString("Failed to save coordinate system to product '%1'.").arg(productName));
        }
    } else {
        QMessageBox::critical(this, "Error",
            "Failed to create coordinate system. Please check your inputs.");
    }
}

void CreateSystemWidget::onPreviousClicked()
{
    if (m_currentStep > 0) {
        goToStep(m_currentStep - 1);
    }
}

void CreateSystemWidget::onNextClicked()
{
    if (validateCurrentStep()) {
        if (m_currentStep < m_totalSteps - 1) {
            goToStep(m_currentStep + 1);
        } else {
            // Last step - create the system
            onCreateSystemClicked();
        }
    }
}

void CreateSystemWidget::onCancelClicked()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Cancel",
        "Are you sure you want to cancel? All progress will be lost.",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        resetAll();
    }
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

void CreateSystemWidget::updateStepIndicator()
{
    for (int i = 0; i < m_stepNumbers.size(); ++i) {
        if (i < m_currentStep) {
            // Completed steps - green
            m_stepNumbers[i]->setStyleSheet(
                "QLabel {"
                "  background-color: #27ae60;"
                "  color: white;"
                "  border-radius: 20px;"
                "  font-size: 18px;"
                "  font-weight: bold;"
                "}"
            );
            m_stepLabels[i]->setStyleSheet(
                "QLabel {"
                "  color: #27ae60;"
                "  font-size: 14px;"
                "  font-weight: bold;"
                "}"
            );
        } else if (i == m_currentStep) {
            // Current step - blue
            m_stepNumbers[i]->setStyleSheet(
                "QLabel {"
                "  background-color: #3498db;"
                "  color: white;"
                "  border-radius: 20px;"
                "  font-size: 18px;"
                "  font-weight: bold;"
                "}"
            );
            m_stepLabels[i]->setStyleSheet(
                "QLabel {"
                "  color: white;"
                "  font-size: 14px;"
                "  font-weight: bold;"
                "}"
            );
        } else {
            // Future steps - gray
            m_stepNumbers[i]->setStyleSheet(
                "QLabel {"
                "  background-color: #34495e;"
                "  color: #95a5a6;"
                "  border-radius: 20px;"
                "  font-size: 18px;"
                "  font-weight: bold;"
                "}"
            );
            m_stepLabels[i]->setStyleSheet(
                "QLabel {"
                "  color: #95a5a6;"
                "  font-size: 14px;"
                "}"
            );
        }
    }

    // Update navigation buttons
    m_previousButton->setEnabled(m_currentStep > 0);
    m_nextButton->setText(m_currentStep == m_totalSteps - 1 ? "Create System" : "Next →");
}

void CreateSystemWidget::goToStep(int step)
{
    if (step < 0 || step >= m_totalSteps) return;

    m_currentStep = step;
    m_contentStack->setCurrentIndex(step);
    updateStepIndicator();
}

bool CreateSystemWidget::validateCurrentStep()
{
    switch (m_currentStep) {
        case 0: // Configuration
            // Check if product is loaded
            if (!m_productManager || !m_productManager->HasLoadedProduct()) {
                QMessageBox::warning(this, "No Product Loaded",
                    "Please load a product first before creating coordinate systems.\n\n"
                    "Go to Motion Control tab and use 'Load Product' button.");
                return false;
            }
            if (m_systemNameEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, "Validation Error", "Please enter a system name.");
                return false;
            }
            if (m_deviceSelector->currentText().isEmpty()) {
                QMessageBox::warning(this, "Validation Error", "Please select a device.");
                return false;
            }
            return true;

        case 1: // Point 1
            if (!m_point1Captured) {
                QMessageBox::warning(this, "Validation Error", "Please capture Point 1 before proceeding.");
                return false;
            }
            return true;

        case 2: // Point 2
            if (!m_point2Captured) {
                QMessageBox::warning(this, "Validation Error", "Please capture Point 2 before proceeding.");
                return false;
            }
            return true;

        case 3: // Point 3 (for 3-point method) or Parameters (for 2-point method)
            if (m_methodSelector->currentData().toInt() == 3) {
                if (!m_point3Captured) {
                    QMessageBox::warning(this, "Validation Error", "Please capture Point 3 before proceeding.");
                    return false;
                }
            }
            return true;

        default:
            return true;
    }
}

void CreateSystemWidget::resetAll()
{
    m_point1Captured = false;
    m_point2Captured = false;
    m_point3Captured = false;

    m_point1Label->setText("Not captured");
    m_point1Label->setStyleSheet(
        "QLabel {"
        "  color: #e74c3c;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "  padding: 10px;"
        "}"
    );

    m_point2Label->setText("Not captured");
    m_point2Label->setStyleSheet(
        "QLabel {"
        "  color: #e74c3c;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "  padding: 10px;"
        "}"
    );

    m_point3Label->setText("Not captured");
    m_point3Label->setStyleSheet(
        "QLabel {"
        "  color: #e74c3c;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "  padding: 10px;"
        "}"
    );

    m_offsetSpinBox->setValue(0.0);
    m_systemNameEdit->clear();

    goToStep(0);
}
