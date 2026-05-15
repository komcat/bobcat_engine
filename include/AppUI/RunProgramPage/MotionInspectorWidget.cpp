// MotionInspectorWidget.cpp
#include "MotionInspectorWidget.h"

#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <iostream>

namespace {

const QStringList kMotionStepTypes = {
  "Move to Position",
  "Move to Position with Offset",
  "Move Relative",
  "Move Relative Multi-Axis",
  "Move Relative Theta",
  "Move to Graph Node",
  "Set Velocity",
  "Parallel Move",
  "Parallel Move Relative",
  "Move to Context XY",
};

QString paramValue(const QJsonObject& params, const QString& key)
{
  auto it = params.constFind(key);
  if (it == params.constEnd()) return QString();
  const QJsonValue& v = it.value();
  if (v.isString()) return v.toString();
  if (v.isDouble()) return QString::number(v.toDouble());
  if (v.isBool())   return v.toBool() ? "true" : "false";
  return QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
}

// Trim trailing zeros on numeric strings like "5.000000" → "5".
QString trimNumberString(const QString& s)
{
  if (s.isEmpty()) return s;
  bool ok = false;
  double d = s.toDouble(&ok);
  if (!ok) return s;
  QString out = QString::number(d, 'f', 6);
  while (out.contains('.') && out.endsWith('0')) out.chop(1);
  if (out.endsWith('.')) out.chop(1);
  return out;
}

struct MotionRow {
  QString axis;
  QString positionName;
  QString distance;
  QString velocity;
};

// Pick out the subset of parameters that are meaningful to display.
MotionRow extractMotionRow(const QString& type, const QJsonObject& params)
{
  MotionRow row;

  if (type == "Move to Position") {
    row.positionName = paramValue(params, "target_position");
    row.velocity = trimNumberString(paramValue(params, "velocity"));
  }
  else if (type == "Move to Position with Offset") {
    QString base = paramValue(params, "base_position");
    QString ox = trimNumberString(paramValue(params, "offset_x"));
    QString oy = trimNumberString(paramValue(params, "offset_y"));
    QString oz = trimNumberString(paramValue(params, "offset_z"));
    QString pos = base;
    auto nonZero = [](const QString& v) { return !v.isEmpty() && v != "0"; };
    if (nonZero(ox)) pos += QString(" +X%1").arg(ox);
    if (nonZero(oy)) pos += QString(" +Y%1").arg(oy);
    if (nonZero(oz)) pos += QString(" +Z%1").arg(oz);
    row.positionName = pos;
    row.velocity = trimNumberString(paramValue(params, "velocity"));
  }
  else if (type == "Move Relative") {
    row.axis = paramValue(params, "axis");
    row.distance = trimNumberString(paramValue(params, "distance"));
    row.velocity = trimNumberString(paramValue(params, "velocity"));
  }
  else if (type == "Move Relative Multi-Axis") {
    QStringList parts;
    auto addAxis = [&](const QString& label, const QString& key) {
      QString v = trimNumberString(paramValue(params, key));
      if (!v.isEmpty() && v != "0") parts << QString("%1=%2").arg(label, v);
    };
    addAxis("X", "distance_x");
    addAxis("Y", "distance_y");
    addAxis("Z", "distance_z");
    row.axis = "X/Y/Z";
    row.distance = parts.join(", ");
    row.velocity = trimNumberString(paramValue(params, "velocity"));
  }
  else if (type == "Move Relative Theta") {
    QStringList parts;
    auto addAxis = [&](const QString& label, const QString& key) {
      QString v = trimNumberString(paramValue(params, key));
      if (!v.isEmpty() && v != "0") parts << QString("%1=%2").arg(label, v);
    };
    addAxis("U", "distance_u");
    addAxis("V", "distance_v");
    addAxis("W", "distance_w");
    row.axis = "U/V/W";
    row.distance = parts.join(", ");
    row.velocity = trimNumberString(paramValue(params, "velocity"));
  }
  else if (type == "Move to Graph Node") {
    QString graph = paramValue(params, "graphName");
    QString end = paramValue(params, "endNodeName");
    row.positionName = graph.isEmpty() ? end : QString("%1 → %2").arg(graph, end);
    row.velocity = trimNumberString(paramValue(params, "velocity"));
  }
  else if (type == "Set Velocity") {
    row.velocity = trimNumberString(paramValue(params, "speed"));
  }
  else if (type == "Move to Context XY") {
    QString cx = paramValue(params, "context_x");
    QString cy = paramValue(params, "context_y");
    row.positionName = QString("ctx(%1, %2)").arg(cx, cy);
    row.velocity = trimNumberString(paramValue(params, "velocity"));
  }
  else {
    // Parallel Move / Parallel Move Relative and any future motion type —
    // fall back to a compact dump of all non-delay parameters so the user
    // can still see what was configured.
    QStringList parts;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
      const QString k = it.key();
      if (k == "device" || k.startsWith("delay_")) continue;
      parts << QString("%1=%2").arg(k, paramValue(params, k));
    }
    row.positionName = parts.join("; ");
  }

  return row;
}

// Describes one editable parameter: which JSON key it maps to, its UI label,
// and which input control to render.
struct FieldSpec {
  QString key;
  QString label;
  enum Kind { String, Number, AxisChoice } kind = String;
};

// Return the list of editable fields for a given motion step type. Order
// follows the user-visible priority: position/axis/distance first, then
// velocity.
std::vector<FieldSpec> fieldSpecsFor(const QString& type)
{
  if (type == "Move to Position") {
    return {
      {"target_position", "Target Position", FieldSpec::String},
      {"velocity",        "Velocity (mm/s, 0 = default)", FieldSpec::Number},
    };
  }
  if (type == "Move to Position with Offset") {
    return {
      {"base_position", "Base Position", FieldSpec::String},
      {"offset_x",      "X Offset (mm)", FieldSpec::Number},
      {"offset_y",      "Y Offset (mm)", FieldSpec::Number},
      {"offset_z",      "Z Offset (mm)", FieldSpec::Number},
      {"velocity",      "Velocity (mm/s, 0 = default)", FieldSpec::Number},
    };
  }
  if (type == "Move Relative") {
    return {
      {"axis",     "Axis", FieldSpec::AxisChoice},
      {"distance", "Distance (mm)", FieldSpec::Number},
      {"velocity", "Velocity (mm/s, 0 = default)", FieldSpec::Number},
    };
  }
  if (type == "Move Relative Multi-Axis") {
    return {
      {"distance_x", "X Distance (mm)", FieldSpec::Number},
      {"distance_y", "Y Distance (mm)", FieldSpec::Number},
      {"distance_z", "Z Distance (mm)", FieldSpec::Number},
      {"velocity",   "Velocity (mm/s, 0 = default)", FieldSpec::Number},
    };
  }
  if (type == "Move Relative Theta") {
    return {
      {"distance_u", "U Distance (deg)", FieldSpec::Number},
      {"distance_v", "V Distance (deg)", FieldSpec::Number},
      {"distance_w", "W Distance (deg)", FieldSpec::Number},
      {"velocity",   "Velocity (0 = default)", FieldSpec::Number},
    };
  }
  if (type == "Move to Graph Node") {
    return {
      {"graphName",   "Graph Name",    FieldSpec::String},
      {"endNodeName", "End Node Name", FieldSpec::String},
      {"velocity",    "Velocity (0 = default)", FieldSpec::Number},
    };
  }
  if (type == "Set Velocity") {
    return {
      {"speed", "Speed (mm/s)", FieldSpec::Number},
    };
  }
  if (type == "Move to Context XY") {
    return {
      {"context_x", "Context X variable", FieldSpec::String},
      {"context_y", "Context Y variable", FieldSpec::String},
      {"velocity",  "Velocity (0 = default)", FieldSpec::Number},
    };
  }
  return {};
}

}  // namespace

// ============================================================================
// MotionStepEditDialog
// ============================================================================

MotionStepEditDialog::MotionStepEditDialog(const QString& recipeFilePath, int stepIndex,
                                           QWidget* parent)
  : QDialog(parent),
    m_recipeFilePath(recipeFilePath),
    m_stepIndex(stepIndex)
{
  setWindowTitle("Edit Motion Step");
  setModal(true);
  setMinimumWidth(420);
  setStyleSheet(
    "QDialog { background-color: #2d2d30; color: #e0e0e0; } "
    "QLabel { color: #e0e0e0; } "
    "QLineEdit, QDoubleSpinBox, QComboBox { "
    "  background-color: #1e1e1e; color: #e0e0e0; "
    "  border: 1px solid #3f3f46; padding: 4px; } "
    "QPushButton { background-color: #0e639c; color: white; padding: 6px 14px; border: 0; } "
    "QPushButton:hover { background-color: #1177bb; }"
  );

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);

  m_headerLabel = new QLabel();
  m_headerLabel->setStyleSheet("font-weight: bold; font-size: 13px;");
  layout->addWidget(m_headerLabel);

  m_fieldsHost = new QWidget(this);
  layout->addWidget(m_fieldsHost);

  m_errorLabel = new QLabel();
  m_errorLabel->setStyleSheet("color: #e06c75;");
  m_errorLabel->setVisible(false);
  layout->addWidget(m_errorLabel);

  QDialogButtonBox* buttons = new QDialogButtonBox(
    QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &MotionStepEditDialog::onSave);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  // Load the current step and build fields.
  QFile file(m_recipeFilePath);
  if (!file.open(QIODevice::ReadOnly)) {
    m_headerLabel->setText(QString("Cannot open %1").arg(QFileInfo(m_recipeFilePath).fileName()));
    return;
  }
  QByteArray data = file.readAll();
  file.close();

  QJsonParseError err{};
  QJsonDocument doc = QJsonDocument::fromJson(data, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    m_headerLabel->setText("Invalid recipe JSON");
    return;
  }
  QJsonArray steps = doc.object().value("steps").toArray();
  if (m_stepIndex < 0 || m_stepIndex >= steps.size()) {
    m_headerLabel->setText("Step index out of range");
    return;
  }
  QJsonObject step = steps.at(m_stepIndex).toObject();
  m_type = step.value("type").toString();
  QJsonObject params = step.value("parameters").toObject();

  QString nickname = step.value("nickname").toString();
  QString device = paramValue(params, "device");
  m_headerLabel->setText(QString("Step %1 — %2\n%3 (device: %4)")
    .arg(m_stepIndex + 1)
    .arg(nickname.isEmpty() ? "(no nickname)" : nickname)
    .arg(m_type)
    .arg(device.isEmpty() ? "—" : device));

  buildFieldsForType(m_type, params);
}

void MotionStepEditDialog::buildFieldsForType(const QString& type, const QJsonObject& params)
{
  auto specs = fieldSpecsFor(type);
  QFormLayout* form = new QFormLayout(m_fieldsHost);
  form->setContentsMargins(0, 0, 0, 0);
  form->setSpacing(8);

  if (specs.empty()) {
    QLabel* note = new QLabel(QString("No editable fields available for type '%1'.").arg(type));
    note->setStyleSheet("color: #a0a0a0; font-style: italic;");
    form->addRow(note);
    return;
  }

  for (const auto& spec : specs) {
    Field field;
    field.key = spec.key;
    QString current = paramValue(params, spec.key);

    if (spec.kind == FieldSpec::AxisChoice) {
      field.combo = new QComboBox();
      field.combo->addItems({"X", "Y", "Z", "U", "V", "W"});
      int idx = field.combo->findText(current.isEmpty() ? "Z" : current);
      field.combo->setCurrentIndex(idx >= 0 ? idx : 0);
      form->addRow(spec.label, field.combo);
    }
    else if (spec.kind == FieldSpec::Number) {
      field.spin = new QDoubleSpinBox();
      field.spin->setRange(-1e6, 1e6);
      field.spin->setDecimals(6);
      field.spin->setSingleStep(0.1);
      bool ok = false;
      double val = current.toDouble(&ok);
      field.spin->setValue(ok ? val : 0.0);
      form->addRow(spec.label, field.spin);
    }
    else {
      field.line = new QLineEdit();
      field.line->setText(current);
      form->addRow(spec.label, field.line);
    }

    m_fields.push_back(field);
  }
}

void MotionStepEditDialog::onSave()
{
  m_errorLabel->setVisible(false);

  QFile file(m_recipeFilePath);
  if (!file.open(QIODevice::ReadOnly)) {
    m_errorLabel->setText("Failed to open recipe for reading.");
    m_errorLabel->setVisible(true);
    return;
  }
  QByteArray data = file.readAll();
  file.close();

  QJsonParseError err{};
  QJsonDocument doc = QJsonDocument::fromJson(data, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    m_errorLabel->setText(QString("Invalid JSON: %1").arg(err.errorString()));
    m_errorLabel->setVisible(true);
    return;
  }

  QJsonObject root = doc.object();
  QJsonArray steps = root.value("steps").toArray();
  if (m_stepIndex < 0 || m_stepIndex >= steps.size()) {
    m_errorLabel->setText("Step index no longer valid.");
    m_errorLabel->setVisible(true);
    return;
  }

  QJsonObject step = steps.at(m_stepIndex).toObject();
  QJsonObject params = step.value("parameters").toObject();

  for (const auto& field : m_fields) {
    QString value;
    if (field.combo) {
      value = field.combo->currentText();
    } else if (field.spin) {
      // Force any pending text in the spin box to be committed before we
      // read value() — without this, clicking Save without first leaving
      // focus can return the previous value.
      field.spin->interpretText();
      value = trimNumberString(QString::number(field.spin->value(), 'f', 6));
    } else if (field.line) {
      value = field.line->text();
    }
    // Recipe JSON convention: parameter values are stored as strings, even
    // for numeric fields. Preserve that convention here so diffs stay clean.
    params[field.key] = value;
    std::cout << "[MotionInspector] set " << field.key.toStdString()
              << " = " << value.toStdString() << std::endl;
  }

  step["parameters"] = params;
  steps[m_stepIndex] = step;
  root["steps"] = steps;

  QFile out(m_recipeFilePath);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    m_errorLabel->setText("Failed to open recipe for writing.");
    m_errorLabel->setVisible(true);
    return;
  }
  const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
  qint64 written = out.write(payload);
  out.close();

  if (written != payload.size()) {
    m_errorLabel->setText(QString("Wrote only %1 of %2 bytes.")
      .arg(written).arg(payload.size()));
    m_errorLabel->setVisible(true);
    return;
  }

  std::cout << "[MotionInspector] wrote " << written
            << " bytes to " << m_recipeFilePath.toStdString()
            << " (step " << m_stepIndex << ")" << std::endl;

  accept();
}

// ============================================================================
// MotionInspectorWidget
// ============================================================================

MotionInspectorWidget::MotionInspectorWidget(QWidget* parent)
  : QWidget(parent),
    m_titleLabel(nullptr),
    m_summaryLabel(nullptr),
    m_table(nullptr)
{
  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(6);

  QHBoxLayout* headerRow = new QHBoxLayout();
  m_titleLabel = new QLabel("Motion Inspector");
  m_titleLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #ffffff;");
  m_summaryLabel = new QLabel("No recipe selected");
  m_summaryLabel->setStyleSheet("color: #a0a0a0;");
  headerRow->addWidget(m_titleLabel);
  headerRow->addStretch();
  headerRow->addWidget(m_summaryLabel);
  layout->addLayout(headerRow);

  m_table = new QTableWidget(this);
  const QStringList headers = {
    "#", "Step", "Type", "Device", "Axis", "Position / Graph",
    "Rel. Distance", "Velocity", "Enabled", "Edit"
  };
  m_table->setColumnCount(headers.size());
  m_table->setHorizontalHeaderLabels(headers);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setAlternatingRowColors(true);
  m_table->verticalHeader()->setVisible(false);
  m_table->horizontalHeader()->setStretchLastSection(false);
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
  m_table->setStyleSheet(
    "QTableWidget { "
    "  background-color: #1e1e1e; "
    "  color: #d4d4d4; "
    "  gridline-color: #3f3f46; "
    "  alternate-background-color: #232326; "
    "  border: 1px solid #3f3f46; "
    "} "
    "QHeaderView::section { "
    "  background-color: #2d2d30; "
    "  color: #ffffff; "
    "  padding: 4px; "
    "  border: 0px; "
    "  border-right: 1px solid #3f3f46; "
    "} "
    "QTableWidget::item:selected { "
    "  background-color: #0e639c; "
    "  color: #ffffff; "
    "}"
  );
  layout->addWidget(m_table, 1);
}

void MotionInspectorWidget::clear()
{
  m_table->setRowCount(0);
  m_summaryLabel->setText("No recipe selected");
  m_currentRecipePath.clear();
  m_currentDisplayName.clear();
}

void MotionInspectorWidget::openEditDialog(int stepIndex)
{
  if (m_currentRecipePath.isEmpty()) return;
  MotionStepEditDialog dialog(m_currentRecipePath, stepIndex, this);
  if (dialog.exec() == QDialog::Accepted) {
    // Reload the recipe so the table reflects the persisted changes.
    loadRecipe(m_currentRecipePath, m_currentDisplayName);
  }
}

bool MotionInspectorWidget::loadRecipe(const QString& recipeFilePath, const QString& displayName)
{
  m_table->setRowCount(0);
  m_currentRecipePath = recipeFilePath;
  m_currentDisplayName = displayName;

  QFile file(recipeFilePath);
  if (!file.open(QIODevice::ReadOnly)) {
    m_summaryLabel->setText(QString("Failed to open: %1").arg(QFileInfo(recipeFilePath).fileName()));
    return false;
  }
  QByteArray data = file.readAll();
  file.close();

  QJsonParseError err{};
  QJsonDocument doc = QJsonDocument::fromJson(data, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    m_summaryLabel->setText(QString("Invalid JSON: %1").arg(err.errorString()));
    return false;
  }

  QJsonArray steps = doc.object().value("steps").toArray();
  int motionCount = 0;
  int totalCount = steps.size();

  for (int i = 0; i < steps.size(); ++i) {
    QJsonObject step = steps.at(i).toObject();
    QString type = step.value("type").toString();
    if (!kMotionStepTypes.contains(type)) continue;

    QJsonObject params = step.value("parameters").toObject();
    MotionRow row = extractMotionRow(type, params);

    QString nickname = step.value("nickname").toString();
    QString device = paramValue(params, "device");
    bool enabled = step.value("enabled").toBool(true);

    int r = m_table->rowCount();
    m_table->insertRow(r);
    auto addCell = [&](int col, const QString& text) {
      QTableWidgetItem* it = new QTableWidgetItem(text);
      if (!enabled) {
        it->setForeground(QColor("#808080"));
      }
      m_table->setItem(r, col, it);
    };
    addCell(0, QString::number(i + 1));
    addCell(1, nickname);
    addCell(2, type);
    addCell(3, device);
    addCell(4, row.axis);
    addCell(5, row.positionName);
    addCell(6, row.distance);
    addCell(7, row.velocity);
    addCell(8, enabled ? "Yes" : "No");

    // Edit button — opens MotionStepEditDialog for this step.
    QPushButton* editButton = new QPushButton("Edit");
    editButton->setStyleSheet(
      "QPushButton { background-color: #0e639c; color: white; "
      "              padding: 2px 10px; border: 0; } "
      "QPushButton:hover { background-color: #1177bb; }"
    );
    const int capturedStepIndex = i;
    connect(editButton, &QPushButton::clicked, this,
            [this, capturedStepIndex]() { openEditDialog(capturedStepIndex); });
    m_table->setCellWidget(r, 9, editButton);

    ++motionCount;
  }

  m_table->resizeColumnsToContents();
  m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);

  QString name = displayName.isEmpty() ? QFileInfo(recipeFilePath).fileName() : displayName;
  m_titleLabel->setText(QString("Motion Inspector — %1").arg(name));
  m_summaryLabel->setText(QString("%1 motion step(s) of %2 total").arg(motionCount).arg(totalCount));
  return true;
}
