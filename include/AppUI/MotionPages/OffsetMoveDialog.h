// OffsetMoveDialog.h
#pragma once

#include <QDialog>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>

class OffsetMoveDialog : public QDialog
{
  Q_OBJECT

public:
  explicit OffsetMoveDialog(const QString& positionName,
    double baseX, double baseY, double baseZ,
    QWidget* parent = nullptr);
  ~OffsetMoveDialog() = default;

  // Get the formatted position name with offsets (e.g. "lensgrip+X1.5+Y-2")
  QString getOffsetPositionName() const;

  // Get individual offset values
  double getOffsetX() const { return m_xOffset->value(); }
  double getOffsetY() const { return m_yOffset->value(); }
  double getOffsetZ() const { return m_zOffset->value(); }

private slots:
  void updatePreview();

private:
  void setupUI();

  QString m_baseName;
  double m_baseX, m_baseY, m_baseZ;

  QDoubleSpinBox* m_xOffset;
  QDoubleSpinBox* m_yOffset;
  QDoubleSpinBox* m_zOffset;

  QLabel* m_previewLabel;
  QLabel* m_resultLabel;

  QPushButton* m_okButton;
  QPushButton* m_cancelButton;
};