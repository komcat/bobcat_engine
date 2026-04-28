// QtCameraQuickMovePanel.h
// Self-contained 6×4 grid of "quick move" buttons that issue relative
// gantry moves via UnifiedMotionManager. Each button is independently
// configurable (label + ΔX/ΔY/ΔZ in mm) via right-click context menu;
// settings persist per-instance to QSettings.
//
// Extracted from QtCameraWindow so the window class can stay focused on
// camera display + side-panel orchestration. Behaviour is preserved
// exactly:
//   * Left click: emits the configured relative moves on "gantry-main"
//     (X first, then Y, then Z; non-blocking).
//   * Right click: Configure / Clear context menu.
//   * Settings key: `QtCameraWindow[/<instanceId>]/QuickMove/<index>/...`
//     unchanged so saved bindings survive the refactor.
#pragma once

#include <QGroupBox>
#include <QString>

#include <array>
#include <string>

class QPushButton;
class UnifiedMotionManager;

class QtCameraQuickMovePanel : public QGroupBox
{
  Q_OBJECT

public:
  QtCameraQuickMovePanel(UnifiedMotionManager* motionManager,
                         std::string instanceId,
                         QWidget* parent = nullptr);

  // The page wires the motion manager via dependency injection AFTER
  // the panel is constructed, so this setter must be reachable.
  void setMotionManager(UnifiedMotionManager* motionManager) {
    m_motionManager = motionManager;
  }

  // 6 rows × 4 cols = 24 configurable buttons.
  static constexpr int ROWS = 6;
  static constexpr int COLS = 4;
  static constexpr int COUNT = ROWS * COLS;

private slots:
  void onClicked(int index);
  void onRightClicked(int index);

private:
  struct ButtonData {
    bool assigned = false;
    QString label;            // max 10 chars
    double relX = 0.0;        // mm
    double relY = 0.0;        // mm
    double relZ = 0.0;        // mm
  };

  void setupUI();
  void showConfigDialog(int index);
  void updateButton(int index);
  void saveSettings();
  void loadSettings();
  QString settingsKey(const QString& key) const;

  UnifiedMotionManager* m_motionManager;
  std::string m_instanceId;

  std::array<QPushButton*, COUNT> m_buttons{};
  std::array<ButtonData, COUNT> m_data{};
};
