// InterlockWidget.h
// UI for configuring motion interlock rules (max 3 rules)
#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QTimer>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <vector>

class UnifiedMotionManager;
class InterlockManager;

class InterlockWidget : public QWidget {
    Q_OBJECT

public:
    explicit InterlockWidget(UnifiedMotionManager* motionManager,
        InterlockManager* interlockManager,
        QWidget* parent = nullptr);
    ~InterlockWidget() override;

private slots:
    void onAddRuleClicked();
    void onRemoveRuleClicked();
    void onRuleChanged();
    void updateStatus();

private:
    void setupUI();
    void populateTable();
    void saveRules();
    QStringList getDeviceNames() const;
    QStringList getAxesForDevice(const QString& deviceName) const;

    UnifiedMotionManager* m_motionManager;
    InterlockManager* m_interlockManager;

    QTableWidget* m_table;
    QPushButton* m_addButton;
    QPushButton* m_removeButton;
    QTimer* m_statusTimer;

    bool m_updating = false; // prevent re-entrant saves during populateTable
};
