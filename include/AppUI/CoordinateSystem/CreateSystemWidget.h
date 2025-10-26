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

/**
 * @brief Widget for creating coordinate systems
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
    void onResetClicked();

private:
    void setupUI();
    void setupTwoPointMethod();
    void setupThreePointMethod();

    bool captureCurrentPosition(Point3D& point);
    void updateStepStatus();
    void resetAll();

    CoordinateSystemManager* m_coordinateSystemManager;
    UnifiedMotionManager* m_motionManager;

    // UI Components
    QComboBox* m_methodSelector;
    QLineEdit* m_systemNameEdit;
    QComboBox* m_deviceSelector;
    QStackedWidget* m_methodStack;

    // 2-Point Method Widgets
    QWidget* m_twoPointWidget;
    QPushButton* m_capturePoint1Btn;
    QPushButton* m_capturePoint2Btn;
    QLabel* m_point1Label;
    QLabel* m_point2Label;
    QDoubleSpinBox* m_offsetSpinBox;
    QComboBox* m_axisSelector2Point;  // X or Y
    Point3D m_point1;
    Point3D m_point2;
    bool m_point1Captured;
    bool m_point2Captured;

    // 3-Point Method Widgets
    QWidget* m_threePointWidget;
    QPushButton* m_capturePoint3ABtn;
    QPushButton* m_capturePoint3BBtn;
    QPushButton* m_capturePoint3CBtn;
    QLabel* m_point3ALabel;
    QLabel* m_point3BLabel;
    QLabel* m_point3CLabel;
    QComboBox* m_axisSelector3Point;  // X or Y
    Point3D m_point3A;
    Point3D m_point3B;
    Point3D m_point3C;
    bool m_point3ACaptured;
    bool m_point3BCaptured;
    bool m_point3CCaptured;

    // Common widgets
    QPushButton* m_createButton;
    QPushButton* m_resetButton;
    QLabel* m_statusLabel;
};
