// ExecutionStatusWidget.cpp
#include "ExecutionStatusWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

ExecutionStatusWidget::ExecutionStatusWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

ExecutionStatusWidget::~ExecutionStatusWidget() = default;

namespace {

// Build one cell of the command tracker (PREVIOUS / CURRENT / NEXT / AFTER).
QWidget* buildTrackerCell(const QString& headerText, QLabel*& outLabel)
{
    auto* cell = new QWidget();
    auto* cellLayout = new QVBoxLayout(cell);
    cellLayout->setContentsMargins(8, 4, 8, 4);
    cellLayout->setSpacing(2);

    auto* header = new QLabel(headerText);
    header->setStyleSheet("font-size: 10px; color: #888888; font-weight: bold;");
    header->setAlignment(Qt::AlignCenter);
    cellLayout->addWidget(header);

    outLabel = new QLabel("-");
    outLabel->setStyleSheet(
        "QLabel { "
        "  font-size: 11px; "
        "  color: #cccccc; "
        "  padding: 4px; "
        "}");
    outLabel->setAlignment(Qt::AlignCenter);
    outLabel->setWordWrap(true);
    outLabel->setMinimumHeight(40);
    cellLayout->addWidget(outLabel);

    cell->setStyleSheet(
        "QWidget { "
        "  background-color: #2d2d2d; "
        "  border: 1px solid #3f3f46; "
        "  border-radius: 4px; "
        "}");
    return cell;
}

}  // namespace

void ExecutionStatusWidget::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // ---- Header: status badge + stacked program info ----
    auto* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(15);

    m_programStatusLabel = new QLabel("Idle");
    m_programStatusLabel->setFixedSize(250, 150);
    m_programStatusLabel->setAlignment(Qt::AlignCenter);
    headerRow->addWidget(m_programStatusLabel, 0, Qt::AlignTop);

    auto* headerTextLayout = new QVBoxLayout();
    headerTextLayout->setContentsMargins(0, 0, 0, 0);
    headerTextLayout->setSpacing(4);

    m_executionTitleLabel = new QLabel("Program Execution");
    m_executionTitleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #ffffff;");
    headerTextLayout->addWidget(m_executionTitleLabel);

    m_executionStatusLabel = new QLabel("Ready to execute");
    m_executionStatusLabel->setStyleSheet("font-size: 14px; color: #cccccc;");
    headerTextLayout->addWidget(m_executionStatusLabel);

    m_currentRecipeLabel = new QLabel("Current Recipe: None");
    m_currentRecipeLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #0e639c;");
    headerTextLayout->addWidget(m_currentRecipeLabel);

    headerTextLayout->addStretch();
    headerRow->addLayout(headerTextLayout, 1);
    layout->addLayout(headerRow);

    layout->addSpacing(10);

    // ---- Command tracker row (4 cells, hidden by default) ----
    m_commandTrackerRow = new QWidget();
    auto* trackerLayout = new QHBoxLayout(m_commandTrackerRow);
    trackerLayout->setContentsMargins(0, 0, 0, 0);
    trackerLayout->setSpacing(4);

    QWidget* pastCell    = buildTrackerCell("PREVIOUS", m_cmdPastLabel);
    QWidget* currentCell = buildTrackerCell("CURRENT",  m_cmdCurrentLabel);
    QWidget* next1Cell   = buildTrackerCell("NEXT",     m_cmdNext1Label);
    QWidget* next2Cell   = buildTrackerCell("AFTER",    m_cmdNext2Label);

    // Highlight the current cell distinctly.
    currentCell->setStyleSheet(
        "QWidget { "
        "  background-color: #0e4f7c; "
        "  border: 1px solid #0078d4; "
        "  border-radius: 4px; "
        "}");
    m_cmdCurrentLabel->setStyleSheet(
        "QLabel { "
        "  font-size: 11px; "
        "  color: #ffffff; "
        "  font-weight: bold; "
        "  padding: 4px; "
        "}");

    trackerLayout->addWidget(pastCell, 1);
    trackerLayout->addWidget(currentCell, 1);
    trackerLayout->addWidget(next1Cell, 1);
    trackerLayout->addWidget(next2Cell, 1);

    layout->addWidget(m_commandTrackerRow);
    // Hidden from the UI by request; underlying labels still receive
    // updates and can be re-enabled by removing this hide() call.
    m_commandTrackerRow->hide();

    // ---- Action detail line ----
    m_actionDetailLabel = new QLabel("-");
    m_actionDetailLabel->setStyleSheet(
        "QLabel { "
        "  font-size: 12px; "
        "  color: #cccccc; "
        "  background-color: #2d2d2d; "
        "  border: 1px solid #3f3f46; "
        "  border-radius: 4px; "
        "  padding: 6px 10px; "
        "}");
    m_actionDetailLabel->setWordWrap(false);
    m_actionDetailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_actionDetailLabel->setToolTip("Current sequence action");
    layout->addWidget(m_actionDetailLabel);

    // ---- Progress bar ----
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFixedHeight(18);
    m_progressBar->setStyleSheet(
        "QProgressBar { "
        "  border: 1px solid #555555; "
        "  border-radius: 3px; "
        "  background-color: #2d2d30; "
        "  text-align: center; "
        "  color: #ffffff; "
        "  font-weight: bold; "
        "} "
        "QProgressBar::chunk { "
        "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "      stop:0 #58d68d, stop:1 #27ae60); "
        "  border-radius: 2px; "
        "}");
    layout->addWidget(m_progressBar);
    layout->addSpacing(4);
}
