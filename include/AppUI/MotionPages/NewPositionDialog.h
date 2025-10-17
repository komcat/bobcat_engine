// NewPositionDialog.h
#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>

/**
 * @brief Dialog for creating a new position from current device position
 * - Enter position name
 * - Shows current device position (X, Y, Z, U, V, W)
 * - Option to use current values or edit them
 */
class NewPositionDialog : public QDialog
{
  Q_OBJECT

public:
  explicit NewPositionDialog(const QString& deviceName,
    double x, double y, double z,
    double u, double v, double w,
    QWidget* parent = nullptr);
  ~NewPositionDialog() = default;

  // Get entered values
  QString getPositionName() const;
  double getX() const;
  double getY() const;
  double getZ() const;
  double getU() const;
  double getV() const;
  double getW() const;

private slots:
  void onOkClicked();
  void onCancelClicked();
  void validateInput();

private:
  void setupUI();

  QString m_deviceName;

  QLineEdit* m_nameEdit;
  QDoubleSpinBox* m_xSpin;
  QDoubleSpinBox* m_ySpin;
  QDoubleSpinBox* m_zSpin;
  QDoubleSpinBox* m_uSpin;
  QDoubleSpinBox* m_vSpin;
  QDoubleSpinBox* m_wSpin;

  QPushButton* m_okButton;
  QPushButton* m_cancelButton;

  QLabel* m_errorLabel;
};