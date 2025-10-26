// ManualAdjustWidget.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <string>

class UnifiedMotionManager;
class TransformationMatrixLoader;

/**
 * @brief Manual adjustment widget for execution panel
 *
 * Allows user to manually jog a motor device during sequence execution
 * with visual feedback of current position. Similar to UserPromptWidget
 * but with jog controls.
 */
class ManualAdjustWidget : public QWidget
{
  Q_OBJECT

public:
  explicit ManualAdjustWidget(
    UnifiedMotionManager* motionManager,
    TransformationMatrixLoader* matrixLoader,
    QWidget* parent = nullptr
  );
  ~ManualAdjustWidget() = default;

  // Show adjustment dialog
  void showAdjustment(
    const QString& message,
    const QString& deviceName,
    double stepSizeUm,
    bool enableX,
    bool enableY,
    bool enableZ
  );

  // Hide adjustment dialog
  void hideAdjustment();

signals:
  // Emitted when user makes a choice
  void responseReceived(bool completed);  // true = OK, false = Cancel

private slots:
  void onJogXMinus();
  void onJogXPlus();
  void onJogYMinus();
  void onJogYPlus();
  void onJogZMinus();
  void onJogZPlus();
  void onOkClicked();
  void onCancelClicked();
  void onUpdatePosition();

private:
  void setupUI();
  void sendJogCommand(const std::string& axis, double direction);
  void updatePositionDisplay();

  UnifiedMotionManager* m_motionManager;
  TransformationMatrixLoader* m_matrixLoader;

  // UI Components
  QLabel* m_messageLabel;
  QLabel* m_deviceLabel;
  QLabel* m_stepSizeLabel;
  QLabel* m_positionLabel;

  // Jog buttons
  QPushButton* m_btnXMinus;
  QPushButton* m_btnXPlus;
  QPushButton* m_btnYMinus;
  QPushButton* m_btnYPlus;
  QPushButton* m_btnZMinus;
  QPushButton* m_btnZPlus;

  // Action buttons
  QPushButton* m_okButton;
  QPushButton* m_cancelButton;

  // State
  std::string m_currentDevice;
  double m_stepSizeUm;
  bool m_enableX;
  bool m_enableY;
  bool m_enableZ;

  // Position update timer
  QTimer* m_positionTimer;
};
