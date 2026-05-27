// PneumaticInterlockWidget.h
// UI for configuring pneumatic-state interlock rules (max 3 rules).
// Each rule blocks moves on a motion device whenever a named pneumatic
// slide is not in the required state.
#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QTimer>

class UnifiedMotionManager;
class InterlockManager;
class PneumaticManager;

class PneumaticInterlockWidget : public QWidget {
    Q_OBJECT

public:
    explicit PneumaticInterlockWidget(UnifiedMotionManager* motionManager,
        PneumaticManager* pneumaticManager,
        InterlockManager* interlockManager,
        QWidget* parent = nullptr);
    ~PneumaticInterlockWidget() override;

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
    QStringList getSlideNames() const;

    UnifiedMotionManager* m_motionManager;
    PneumaticManager* m_pneumaticManager;
    InterlockManager* m_interlockManager;

    QTableWidget* m_table;
    QPushButton* m_addButton;
    QPushButton* m_removeButton;
    QTimer* m_statusTimer;

    bool m_updating = false;
};
