// ContextViewerWindow.cpp
#include "ContextViewerWindow.h"
#include "SequenceContext.h"
#include <QHeaderView>
#include <QHBoxLayout>
#include <QClipboard>
#include <QApplication>
#include <iostream>

ContextViewerWindow::ContextViewerWindow(QWidget* parent)
  : QWidget(parent, Qt::Window | Qt::WindowStaysOnTopHint)
  , m_table(nullptr)
  , m_statusLabel(nullptr)
  , m_refreshButton(nullptr)
  , m_clearButton(nullptr)
  , m_copyButton(nullptr)
  , m_itemCount(0)
{
  setupUI();
  applyStyles();

  setWindowTitle("Context Viewer");
  resize(600, 400);

  std::cout << "[ContextViewerWindow] Window created" << std::endl;
}

void ContextViewerWindow::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(10, 10, 10, 10);
  mainLayout->setSpacing(8);

  // Title label
  QLabel* titleLabel = new QLabel("Recipe Execution Context");
  titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #d4d4d4;");
  mainLayout->addWidget(titleLabel);

  // Status label
  m_statusLabel = new QLabel("No context data");
  m_statusLabel->setStyleSheet("color: #888888; font-style: italic;");
  mainLayout->addWidget(m_statusLabel);

  // Table
  m_table = new QTableWidget();
  m_table->setColumnCount(3);
  m_table->setHorizontalHeaderLabels({"#", "Variable", "Value"});
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setAlternatingRowColors(true);

  // Column widths
  m_table->setColumnWidth(0, 50);  // Item #
  m_table->setColumnWidth(1, 250); // Variable
  m_table->horizontalHeader()->setStretchLastSection(true);  // Value stretches

  m_table->verticalHeader()->setVisible(false);
  m_table->setSortingEnabled(true);

  mainLayout->addWidget(m_table);

  // Button row
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(10);

  m_refreshButton = new QPushButton("Refresh");
  m_refreshButton->setToolTip("Request context refresh from recipe");
  connect(m_refreshButton, &QPushButton::clicked, this, &ContextViewerWindow::onRefreshClicked);
  buttonLayout->addWidget(m_refreshButton);

  m_copyButton = new QPushButton("Copy");
  m_copyButton->setToolTip("Copy all values to clipboard");
  connect(m_copyButton, &QPushButton::clicked, this, &ContextViewerWindow::onCopyClicked);
  buttonLayout->addWidget(m_copyButton);

  m_clearButton = new QPushButton("Clear");
  m_clearButton->setToolTip("Clear the display");
  connect(m_clearButton, &QPushButton::clicked, this, &ContextViewerWindow::onClearClicked);
  buttonLayout->addWidget(m_clearButton);

  buttonLayout->addStretch();

  mainLayout->addLayout(buttonLayout);
}

void ContextViewerWindow::applyStyles()
{
  setStyleSheet(
    "QWidget { "
    "  background-color: #1e1e1e; "
    "  color: #d4d4d4; "
    "} "
    "QTableWidget { "
    "  background-color: #2d2d30; "
    "  border: 1px solid #3f3f46; "
    "  gridline-color: #3f3f46; "
    "} "
    "QTableWidget::item { "
    "  padding: 4px; "
    "} "
    "QTableWidget::item:alternate { "
    "  background-color: #252526; "
    "} "
    "QHeaderView::section { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  padding: 6px; "
    "  border: 1px solid #3f3f46; "
    "  font-weight: bold; "
    "} "
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 4px; "
    "  padding: 6px 12px; "
    "  min-width: 80px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4c4c4c; "
    "  border-color: #0e639c; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2d2d30; "
    "}"
  );
}

void ContextViewerWindow::updateFromContext(SequenceContext* context)
{
  if (!context) {
    clearContext();
    return;
  }

  auto summary = context->GetSummary();

  m_table->setRowCount(0);
  m_itemCount = 0;

  if (summary.capturedValues.empty()) {
    m_statusLabel->setText("Context is empty (no captured values)");
    return;
  }

  m_table->setRowCount(static_cast<int>(summary.capturedValues.size()));

  int row = 0;
  for (const auto& [key, value] : summary.capturedValues) {
    // Item number
    QTableWidgetItem* numItem = new QTableWidgetItem(QString::number(row + 1));
    numItem->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, 0, numItem);

    // Variable key
    QTableWidgetItem* keyItem = new QTableWidgetItem(QString::fromStdString(key));
    m_table->setItem(row, 1, keyItem);

    // Value
    QString valueStr;
    switch (value.type) {
      case SequenceContext::ValueType::NUMERIC:
        valueStr = QString::number(value.numericValue, 'f', 6);
        break;
      case SequenceContext::ValueType::STRING:
        valueStr = QString::fromStdString(value.stringValue);
        break;
      case SequenceContext::ValueType::BOOLEAN:
        valueStr = value.booleanValue ? "true" : "false";
        break;
      case SequenceContext::ValueType::IMAGE_FRAME:
        valueStr = "[Image Frame]";
        if (!value.tempFilePath.empty()) {
          valueStr += QString(" (%1)").arg(QString::fromStdString(value.tempFilePath));
        }
        break;
      default:
        valueStr = "(none)";
        break;
    }

    QTableWidgetItem* valueItem = new QTableWidgetItem(valueStr);
    m_table->setItem(row, 2, valueItem);

    row++;
  }

  m_itemCount = row;
  m_statusLabel->setText(QString("Showing %1 context variables").arg(m_itemCount));

  std::cout << "[ContextViewerWindow] Updated with " << m_itemCount << " values" << std::endl;
}

void ContextViewerWindow::clearContext()
{
  m_table->setRowCount(0);
  m_itemCount = 0;
  m_statusLabel->setText("No context data");

  std::cout << "[ContextViewerWindow] Context cleared" << std::endl;
}

int ContextViewerWindow::getItemCount() const
{
  return m_itemCount;
}

void ContextViewerWindow::onRefreshClicked()
{
  std::cout << "[ContextViewerWindow] Refresh requested" << std::endl;
  emit refreshRequested();
}

void ContextViewerWindow::onClearClicked()
{
  clearContext();
}

void ContextViewerWindow::onCopyClicked()
{
  if (m_table->rowCount() == 0) {
    return;
  }

  QString clipboardText;
  clipboardText += "# Recipe Execution Context\n";
  clipboardText += QString("# %1 variables\n\n").arg(m_itemCount);

  for (int row = 0; row < m_table->rowCount(); ++row) {
    QString key = m_table->item(row, 1)->text();
    QString value = m_table->item(row, 2)->text();
    clipboardText += QString("%1 = %2\n").arg(key).arg(value);
  }

  QApplication::clipboard()->setText(clipboardText);
  m_statusLabel->setText(QString("Copied %1 variables to clipboard").arg(m_itemCount));

  std::cout << "[ContextViewerWindow] Copied " << m_itemCount << " values to clipboard" << std::endl;
}
