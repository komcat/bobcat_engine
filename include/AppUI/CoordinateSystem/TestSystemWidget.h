// TestSystemWidget.h
#pragma once

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include "CoordinateSystemManager.h"
#include "unified_motion_manager.h"

/**
 * @brief Widget for testing coordinate systems
 *
 * Features:
 * - Load existing coordinate systems
 * - Move to origin
 * - Move to local coordinates (X, Y, Z)
 */
class TestSystemWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TestSystemWidget(
        CoordinateSystemManager* coordinateSystemManager,
        UnifiedMotionManager* motionManager,
        QWidget* parent = nullptr);
    ~TestSystemWidget() = default;

private slots:
    void onRefreshSystemsClicked();
    void onLoadSystemClicked();
    void onMoveToOriginClicked();
    void onMoveToLocalPosClicked();
    void onSystemSelected(int index);

private:
    void setupUI();
    void refreshSystemList();
    void updateSystemInfo();

    CoordinateSystemManager* m_coordinateSystemManager;
    UnifiedMotionManager* m_motionManager;

    // UI Components
    QComboBox* m_systemSelector;
    QPushButton* m_refreshButton;
    QPushButton* m_loadButton;

    // System Info
    QLabel* m_systemInfoLabel;
    QLabel* m_deviceLabel;
    QLabel* m_originLabel;
    QLabel* m_axesLabel;

    // Movement controls
    QDoubleSpinBox* m_localXSpinBox;
    QDoubleSpinBox* m_localYSpinBox;
    QDoubleSpinBox* m_localZSpinBox;
    QPushButton* m_moveToOriginButton;
    QPushButton* m_moveToLocalPosButton;

    // Current system
    std::string m_currentSystemName;
    bool m_systemLoaded;
};
