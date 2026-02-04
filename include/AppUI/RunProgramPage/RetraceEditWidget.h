// RetraceEditWidget.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QString>
#include <string>

class UnifiedMotionManager;
class TransformationMatrixLoader;
class QTimer;

/**
 * @brief Retrace Edit Widget for re-teaching position adjustments
 *
 * Displays during retrace edit mode when the sequence pauses after each
 * motion command. Shows:
 * - Current device and position name
 * - Original vs Current position table
 * - Jog controls for adjustment
 * - Save & Continue / Skip / Stop buttons
 */
class RetraceEditWidget : public QWidget
{
  Q_OBJECT

public:
  explicit RetraceEditWidget(
    UnifiedMotionManager* motionManager,
    TransformationMatrixLoader* matrixLoader,
    QWidget* parent = nullptr
  );
  ~RetraceEditWidget() = default;

  /**
   * @brief Show the widget with position data
   *
   * @param deviceName Motion device name
   * @param positionName Named position being adjusted
   * @param origX-origW Original target coordinates
   * @param currX-currW Current coordinates after move
   * @param positionIndex Current position number in sequence
   * @param totalPositions Total named positions in sequence (0 if unknown)
   */
  void showRetraceEdit(
    const QString& deviceName,
    const QString& positionName,
    double origX, double origY, double origZ,
    double origU, double origV, double origW,
    double currX, double currY, double currZ,
    double currU, double currV, double currW,
    int positionIndex = 0,
    int totalPositions = 0
  );

  void hideRetraceEdit();

  /**
   * @brief Set the default step size for jog controls
   */
  void setDefaultStepSize(double stepSizeUm) { m_stepSizeUm = stepSizeUm; }

signals:
  /**
   * @brief Emitted when user makes a choice
   * @param continueExecution true to continue, false to stop
   * @param saveRequested true if user wants to save the adjusted position
   */
  void responseReceived(bool continueExecution, bool saveRequested);

private slots:
  void onJogXMinus();
  void onJogXPlus();
  void onJogYMinus();
  void onJogYPlus();
  void onJogZMinus();
  void onJogZPlus();
  void onSaveAndContinue();
  void onSkip();
  void onStopRetrace();
  void onUpdatePosition();

private:
  void setupUI();
  void sendJogCommand(const std::string& axis, double direction);
  void updatePositionDisplay();
  void updateDeltaDisplay();

  UnifiedMotionManager* m_motionManager;
  TransformationMatrixLoader* m_matrixLoader;

  // UI Components
  QLabel* m_headerLabel;          // "Retrace Edit Mode - Adjusting Positions"
  QLabel* m_deviceLabel;          // "Device: gantry-main"
  QLabel* m_positionLabel;        // "Position: lensgrip"
  QLabel* m_progressLabel;        // "Position 3 of 12"
  QLabel* m_stepSizeLabel;        // "Step: 10 um"
  QTableWidget* m_positionTable;  // Original vs Current vs Delta

  // Jog buttons
  QPushButton* m_btnXMinus;
  QPushButton* m_btnXPlus;
  QPushButton* m_btnYMinus;
  QPushButton* m_btnYPlus;
  QPushButton* m_btnZMinus;
  QPushButton* m_btnZPlus;

  // Action buttons
  QPushButton* m_saveAndContinueButton;
  QPushButton* m_skipButton;
  QPushButton* m_stopButton;

  // State
  std::string m_currentDevice;
  std::string m_currentPositionName;
  double m_stepSizeUm;

  // Original position (for delta calculation)
  double m_originalX, m_originalY, m_originalZ;
  double m_originalU, m_originalV, m_originalW;

  // Current position (updated by timer)
  double m_currentX, m_currentY, m_currentZ;
  double m_currentU, m_currentV, m_currentW;

  // Position update timer
  QTimer* m_positionTimer;

  // Track if position was modified
  bool m_positionModified;
};
