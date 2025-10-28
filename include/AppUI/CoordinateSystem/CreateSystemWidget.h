// CreateSystemWidget.h
#pragma once

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QDoubleSpinBox>
#include "CoordinateSystemManager.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"

/**
 * @brief Widget for creating coordinate systems with stepped wizard interface
 *
 * Provides two methods:
 * 1. 2-Point + Perpendicular Offset
 * 2. 3-Point Rectangle
 */
class CreateSystemWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CreateSystemWidget(
        CoordinateSystemManager* coordinateSystemManager,
        UnifiedMotionManager* motionManager,
        ProductPositionManager* productManager,
        QWidget* parent = nullptr);
    ~CreateSystemWidget() = default;

signals:
    void systemCreated(const QString& systemName);

private slots:
    void onMethodChanged(int index);
    void onCapturePoint1Clicked();
    void onCapturePoint2Clicked();
    void onCapturePoint3Clicked();
    void onCreateSystemClicked();
    void onPreviousClicked();
    void onNextClicked();
    void onCancelClicked();

private:
    void setupUI();
    void setupStepPanel();
    void setupStepPages();

    // Step page creators
    QWidget* createConfigurationPage();
    QWidget* createCapturePoint1Page();
    QWidget* createCapturePoint2Page();
    QWidget* createCapturePoint3Page();
    QWidget* createParametersPage();

    bool captureCurrentPosition(Point3D& point);
    void updateStepIndicator();
    void goToStep(int step);
    bool validateCurrentStep();
    void resetAll();

    CoordinateSystemManager* m_coordinateSystemManager;
    UnifiedMotionManager* m_motionManager;
    ProductPositionManager* m_productManager;

    // Wizard state
    int m_currentStep;
    int m_totalSteps;

    // Step panel widgets
    QWidget* m_stepPanel;
    QVector<QLabel*> m_stepLabels;
    QVector<QLabel*> m_stepNumbers;

    // Content area
    QStackedWidget* m_contentStack;

    // Configuration (Step 1)
    QLineEdit* m_systemNameEdit;
    QComboBox* m_deviceSelector;
    QComboBox* m_methodSelector;

    // Point capture data
    Point3D m_point1;
    Point3D m_point2;
    Point3D m_point3;
    bool m_point1Captured;
    bool m_point2Captured;
    bool m_point3Captured;

    // Point capture widgets
    QLabel* m_point1Label;
    QLabel* m_point2Label;
    QLabel* m_point3Label;

    // Parameters
    QDoubleSpinBox* m_offsetSpinBox;
    QComboBox* m_axisSelector;

    // Navigation buttons
    QPushButton* m_previousButton;
    QPushButton* m_nextButton;
    QPushButton* m_cancelButton;
};
