// ManualAlignPointWidget.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <string>

class UnifiedMotionManager;
struct ApplicationContext;
class CameraManager;

/**
 * @brief Manual alignment point widget for execution panel
 *
 * Allows user to manually align a point using camera click-to-move
 * during sequence execution. Shows reference image and captures on save.
 * Saves the aligned XYZ position to context.
 */
class ManualAlignPointWidget : public QWidget
{
  Q_OBJECT

public:
  explicit ManualAlignPointWidget(
    UnifiedMotionManager* motionManager,
    ApplicationContext* context,
    QWidget* parent = nullptr
  );
  ~ManualAlignPointWidget() = default;

  // Show alignment dialog
  void showAlignment(
    const QString& pointName,
    const QString& message,
    bool enableX,
    bool enableY,
    bool enableZ
  );

  // Hide alignment dialog
  void hideAlignment();

signals:
  // Emitted when user makes a choice
  void responseReceived(bool completed);  // true = Continue, false = Cancel

private slots:
  void onContinueClicked();
  void onCancelClicked();
  void onUpdatePosition();

private:
  void setupUI();
  void updatePositionDisplay();
  void enableClickToMove(bool enable);
  void enableCrosshair(bool enable);
  void loadReferenceImage();
  void captureAndSaveImage();
  void saveNavigationData(double x, double y, double z);
  bool loadNavigationData(double& x, double& y, double& z);
  QString getReferenceImagePath(bool createNew = false);
  QString getNavigationFilePath();
  QString getProductName();

  UnifiedMotionManager* m_motionManager;
  ApplicationContext* m_context;

  // UI Components
  QLabel* m_titleLabel;
  QLabel* m_messageLabel;
  QLabel* m_positionLabel;
  QLabel* m_instructionLabel;
  QLabel* m_referenceImageLabel;  // 200x200 reference image display

  // Action buttons
  QPushButton* m_continueButton;
  QPushButton* m_cancelButton;

  // State
  std::string m_pointName;
  std::string m_currentDevice;
  bool m_enableX;
  bool m_enableY;
  bool m_enableZ;

  // Position update timer
  QTimer* m_positionTimer;
};
