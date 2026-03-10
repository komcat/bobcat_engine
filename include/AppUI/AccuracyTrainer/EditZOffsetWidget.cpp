// EditZOffsetWidget.cpp - Edit hex device Z offset parameters
#include "EditZOffsetWidget.h"
#include "ApplicationContext.h"
#include <QGroupBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <iostream>

EditZOffsetWidget::EditZOffsetWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(15, 15, 15, 15);
    m_mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Edit Z Offset Parameters");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff; padding-bottom: 5px;");
    m_mainLayout->addWidget(title);

    QLabel* desc = new QLabel(
        "Adjust Z placement parameters for each hex device.\n"
        "Formula: hexZAdjustment = -(zPlace - zComp - (Tcomp + BLT))");
    desc->setStyleSheet("color: #aaa; font-size: 12px; padding-bottom: 10px;");
    desc->setWordWrap(true);
    m_mainLayout->addWidget(desc);

    // Scrollable area for device rows
    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; }");
    m_deviceContainer = new QWidget();
    m_deviceContainer->setLayout(new QVBoxLayout());
    m_deviceContainer->layout()->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(m_deviceContainer);
    m_mainLayout->addWidget(scroll, 1);

    // Bottom buttons
    QHBoxLayout* btnRow = new QHBoxLayout();
    QPushButton* recalcBtn = new QPushButton("Recalculate");
    QPushButton* applyBtn = new QPushButton("Apply && Save");

    for (auto* btn : {recalcBtn, applyBtn}) {
        btn->setFixedHeight(34);
        btn->setStyleSheet(
            "QPushButton { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
            "border-radius: 4px; padding: 5px 20px; font-size: 12px; font-weight: bold; }"
            "QPushButton:hover { background-color: #3d5a8c; }"
        );
    }
    applyBtn->setStyleSheet(
        "QPushButton { background-color: #2d6e3f; color: white; border: 1px solid #4d9f6e; "
        "border-radius: 4px; padding: 5px 20px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background-color: #3d8c5a; }"
    );

    connect(recalcBtn, &QPushButton::clicked, this, &EditZOffsetWidget::onRecalculate);
    connect(applyBtn, &QPushButton::clicked, this, &EditZOffsetWidget::onApply);

    btnRow->addStretch();
    btnRow->addWidget(recalcBtn);
    btnRow->addWidget(applyBtn);
    m_mainLayout->addLayout(btnRow);
}

void EditZOffsetWidget::SetTrainingSet(TrainingSet* ts)
{
    m_trainingSet = ts;
    rebuildUI();
}

void EditZOffsetWidget::rebuildUI()
{
    m_rows.clear();

    // Clear old device container content
    QLayout* oldLayout = m_deviceContainer->layout();
    if (oldLayout) {
        QLayoutItem* child = nullptr;
        for (int i = 0; (child = oldLayout->takeAt(0)) != nullptr; ) {
            if (child->widget()) {
                delete child->widget();
            }
            delete child;
        }
    }

    if (!m_trainingSet) {
        QLabel* emptyLabel = new QLabel("No training set loaded.");
        emptyLabel->setStyleSheet("color: #888; font-size: 13px; padding: 20px;");
        emptyLabel->setAlignment(Qt::AlignCenter);
        m_deviceContainer->layout()->addWidget(emptyLabel);
        return;
    }

    if (m_trainingSet->hexDevicePositions.empty()) {
        QLabel* emptyLabel = new QLabel("No hex devices taught yet.\nTeach multi-device positions first.");
        emptyLabel->setStyleSheet("color: #888; font-size: 13px; padding: 20px;");
        emptyLabel->setAlignment(Qt::AlignCenter);
        m_deviceContainer->layout()->addWidget(emptyLabel);
        return;
    }

    for (const auto& hd : m_trainingSet->hexDevicePositions) {
        QGroupBox* group = new QGroupBox(QString::fromStdString(hd.deviceName));
        group->setStyleSheet(
            "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
            "border-radius: 4px; margin-top: 10px; padding-top: 18px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
        );
        QFormLayout* form = new QFormLayout(group);
        form->setSpacing(8);
        form->setContentsMargins(12, 12, 12, 12);

        QString spinStyle =
            "QDoubleSpinBox { background-color: #1a1f2e; color: white; border: 1px solid #4d6e9f; "
            "border-radius: 3px; padding: 4px 8px; font-size: 12px; }"
            "QDoubleSpinBox:focus { border: 1px solid #6d9ecf; }";

        DeviceRow row;
        row.deviceName = hd.deviceName;

        // Z Comp position
        row.zCompSpin = new QDoubleSpinBox();
        row.zCompSpin->setRange(-500.0, 500.0);
        row.zCompSpin->setDecimals(4);
        row.zCompSpin->setSuffix(" mm");
        row.zCompSpin->setValue(hd.zComp);
        row.zCompSpin->setStyleSheet(spinStyle);
        form->addRow("Z Component Top:", row.zCompSpin);

        // Z Place position
        row.zPlaceSpin = new QDoubleSpinBox();
        row.zPlaceSpin->setRange(-500.0, 500.0);
        row.zPlaceSpin->setDecimals(4);
        row.zPlaceSpin->setSuffix(" mm");
        row.zPlaceSpin->setValue(hd.zPlace);
        row.zPlaceSpin->setStyleSheet(spinStyle);
        form->addRow("Z Placement Surface:", row.zPlaceSpin);

        // Component thickness
        row.thicknessSpin = new QDoubleSpinBox();
        row.thicknessSpin->setRange(0.0, 50.0);
        row.thicknessSpin->setDecimals(3);
        row.thicknessSpin->setSingleStep(0.1);
        row.thicknessSpin->setSuffix(" mm");
        row.thicknessSpin->setValue(hd.componentThickness);
        row.thicknessSpin->setStyleSheet(spinStyle);
        form->addRow("Component Thickness:", row.thicknessSpin);

        // Bondline thickness
        row.bondlineSpin = new QDoubleSpinBox();
        row.bondlineSpin->setRange(0.0, 5.0);
        row.bondlineSpin->setDecimals(3);
        row.bondlineSpin->setSingleStep(0.01);
        row.bondlineSpin->setSuffix(" mm");
        row.bondlineSpin->setValue(hd.bondlineThickness);
        row.bondlineSpin->setStyleSheet(spinStyle);
        form->addRow("Bondline Thickness:", row.bondlineSpin);

        // Computed result
        double adj = -(hd.zPlace - hd.zComp - (hd.componentThickness + hd.bondlineThickness));
        row.resultLabel = new QLabel(QString("hexZAdjustment = %1 mm").arg(adj, 0, 'f', 4));
        row.resultLabel->setStyleSheet(
            "QLabel { color: #55ff55; font-size: 13px; font-weight: bold; "
            "background-color: #1a2e1f; border: 1px solid #2d6e3f; "
            "border-radius: 3px; padding: 6px 10px; }");
        form->addRow("Computed Z Offset:", row.resultLabel);

        // Auto-recalculate on any spinbox change
        auto recalcLambda = [this]() { onRecalculate(); };
        connect(row.zCompSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, recalcLambda);
        connect(row.zPlaceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, recalcLambda);
        connect(row.thicknessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, recalcLambda);
        connect(row.bondlineSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, recalcLambda);

        m_rows.push_back(row);
        m_deviceContainer->layout()->addWidget(group);
    }

    // Add stretch at bottom
    static_cast<QVBoxLayout*>(m_deviceContainer->layout())->addStretch();
}

void EditZOffsetWidget::onRecalculate()
{
    for (auto& row : m_rows) {
        double zComp = row.zCompSpin->value();
        double zPlace = row.zPlaceSpin->value();
        double tComp = row.thicknessSpin->value();
        double blt = row.bondlineSpin->value();
        double adj = -(zPlace - zComp - (tComp + blt));
        row.resultLabel->setText(QString("hexZAdjustment = %1 mm").arg(adj, 0, 'f', 4));
    }
}

void EditZOffsetWidget::onApply()
{
    if (!m_trainingSet) return;

    for (size_t i = 0; i < m_rows.size() && i < m_trainingSet->hexDevicePositions.size(); ++i) {
        auto& hd = m_trainingSet->hexDevicePositions[i];
        auto& row = m_rows[i];

        hd.zComp = row.zCompSpin->value();
        hd.zPlace = row.zPlaceSpin->value();
        hd.componentThickness = row.thicknessSpin->value();
        hd.bondlineThickness = row.bondlineSpin->value();
        hd.hexZAdjustment = -(hd.zPlace - hd.zComp - (hd.componentThickness + hd.bondlineThickness));

        std::cout << "[EditZOffset] " << hd.deviceName
                  << ": zComp=" << hd.zComp
                  << ", zPlace=" << hd.zPlace
                  << ", Tcomp=" << hd.componentThickness
                  << ", BLT=" << hd.bondlineThickness
                  << ", hexZAdj=" << hd.hexZAdjustment << std::endl;
    }

    emit zOffsetsUpdated();
    std::cout << "[EditZOffset] Applied Z offset changes." << std::endl;
}
