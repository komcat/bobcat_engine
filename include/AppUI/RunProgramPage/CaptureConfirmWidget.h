// CaptureConfirmWidget.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QPixmap>
#include <string>
#include "CameraFrameData.h"

/**
 * @brief Image capture confirmation widget for execution panel
 *
 * Displays a captured camera frame with a message and allows user to
 * confirm (continue) or cancel the sequence. Similar to UserPromptWidget
 * but with image display capability.
 *
 * Used by "CaptureAndConfirm" camera command.
 */
class CaptureConfirmWidget : public QWidget
{
  Q_OBJECT

public:
  explicit CaptureConfirmWidget(QWidget* parent = nullptr);
  ~CaptureConfirmWidget() = default;

  /**
   * @brief Show captured image with confirmation prompt
   * @param frameData Camera frame to display
   * @param message Message to show to user (e.g., "Check lens alignment")
   */
  void showConfirmation(const CameraFrameData& frameData, const QString& message);

  /**
   * @brief Hide confirmation dialog
   */
  void hideConfirmation();

signals:
  // Emitted when user makes a choice
  void responseReceived(bool confirmed);  // true = Continue, false = Cancel

private slots:
  void onContinueClicked();
  void onCancelClicked();

private:
  void setupUI();
  QPixmap convertFrameToPixmap(const CameraFrameData& frameData);

  // UI Components
  QLabel* m_messageLabel;
  QLabel* m_imageLabel;       // Displays the captured frame
  QLabel* m_imageInfoLabel;   // Shows image dimensions/camera info

  QPushButton* m_continueButton;
  QPushButton* m_cancelButton;

  // Display settings
  static constexpr int IMAGE_DISPLAY_WIDTH = 640;
  static constexpr int IMAGE_DISPLAY_HEIGHT = 480;
};
