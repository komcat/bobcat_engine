// MotionInspectorWidget.h
#pragma once

#include <QDialog>
#include <QWidget>

class QLabel;
class QLineEdit;
class QTableWidget;
class QComboBox;
class QDoubleSpinBox;
class QJsonObject;

/**
 * @brief Modal editor for a single motion-related step.
 *
 * Writes changes back to the recipe JSON file. Shows only the subset of
 * parameters that are meaningful for the step type (velocity / distance /
 * axis / position name).
 */
class MotionStepEditDialog : public QDialog
{
  Q_OBJECT

public:
  MotionStepEditDialog(const QString& recipeFilePath, int stepIndex, QWidget* parent = nullptr);
  ~MotionStepEditDialog() = default;

private slots:
  void onSave();

private:
  void buildFieldsForType(const QString& type, const QJsonObject& params);

  struct Field {
    QString key;            // Parameter key in JSON
    QLineEdit* line = nullptr;      // Used for strings
    QDoubleSpinBox* spin = nullptr; // Used for numbers
    QComboBox* combo = nullptr;     // Used for fixed choices
  };

  QString m_recipeFilePath;
  int m_stepIndex;
  QString m_type;
  QLabel* m_headerLabel = nullptr;
  QLabel* m_errorLabel = nullptr;
  QWidget* m_fieldsHost = nullptr;
  std::vector<Field> m_fields;
};

/**
 * @brief Read-only table view of a recipe's motion-related steps.
 *
 * Each row has an "Edit" button that opens a MotionStepEditDialog against
 * the underlying recipe JSON file.
 */
class MotionInspectorWidget : public QWidget
{
  Q_OBJECT

public:
  explicit MotionInspectorWidget(QWidget* parent = nullptr);
  ~MotionInspectorWidget() = default;

  // Load a recipe JSON file and populate the table with its motion steps.
  // Returns false if the file cannot be read or parsed.
  bool loadRecipe(const QString& recipeFilePath, const QString& displayName);

  // Clear the table and reset the header label.
  void clear();

private:
  void openEditDialog(int stepIndex);

  QLabel* m_titleLabel;
  QLabel* m_summaryLabel;
  QTableWidget* m_table;

  QString m_currentRecipePath;
  QString m_currentDisplayName;
};
