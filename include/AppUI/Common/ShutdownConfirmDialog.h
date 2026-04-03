// ShutdownConfirmDialog.h
#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>

/**
 * @brief Modal confirmation dialog shown when the user closes the application.
 *
 * Displays a caution-styled checklist requiring the operator to confirm they
 * have saved their program and verified the machine before shutdown.
 * The "Close Application" button is disabled until all checklist items are checked.
 */
class ShutdownConfirmDialog : public QDialog
{
  Q_OBJECT

public:
  explicit ShutdownConfirmDialog(QWidget* parent = nullptr);
  ~ShutdownConfirmDialog() = default;

  /**
   * @brief Show the dialog and return true if the user confirmed shutdown.
   */
  static bool confirmShutdown(QWidget* parent = nullptr);

private:
  void setupUI();
  void applyStyle();
  void updateCloseButtonState();

  QLabel* m_iconLabel = nullptr;
  QLabel* m_titleLabel = nullptr;
  QLabel* m_messageLabel = nullptr;
  QCheckBox* m_saveProgramCheck = nullptr;
  QCheckBox* m_verifyMachineCheck = nullptr;
  QPushButton* m_closeButton = nullptr;
  QPushButton* m_cancelButton = nullptr;
};
