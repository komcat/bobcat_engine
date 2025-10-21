// EditPositionDialog.h
#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>

/**
 * @brief Dialog for editing an existing position's XYZ UVW values
 * - Shows position name (read-only)
 * - Edit X, Y, Z, U, V, W values
 */
class EditPositionDialog : public QDialog
{
  Q_OBJECT

public:
  explicit EditPositionDialog(const QString& positionName,
    double x, double y, double z,
    double u, double v, double w,
    QWidget* parent = nullptr);
  ~EditPositionDialog() = default;

  // Get edited values
  double getX() const;
  double getY() const;
  double getZ() const;
  double getU() const;
  double getV() const;
  double getW() const;

private slots:
  void onSaveClicked();

private:
  void setupUI();
  QDoubleSpinBox* createSpinBox(double value);

  QString m_positionName;
  double m_x, m_y, m_z, m_u, m_v, m_w;

  QDoubleSpinBox* m_xSpin;
  QDoubleSpinBox* m_ySpin;
  QDoubleSpinBox* m_zSpin;
  QDoubleSpinBox* m_uSpin;
  QDoubleSpinBox* m_vSpin;
  QDoubleSpinBox* m_wSpin;
};