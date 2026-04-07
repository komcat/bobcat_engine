// ExecuteTuneStepWidget.cpp
// Step 4: Execute, tune, and view results for vision wizard
#include "ExecuteTuneStepWidget.h"
#include "VisionImageWidget.h"
#include <QApplication>
#include <iostream>

ExecuteTuneStepWidget::ExecuteTuneStepWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_state(nullptr)
    , m_executeBtn(nullptr)
    , m_stopBtn(nullptr)
    , m_reliabilityBtn(nullptr)
    , m_statusLabel(nullptr)
    , m_progressBar(nullptr)
    , m_imagePlaceholder(nullptr)
    , m_imagePlaceholderLayout(nullptr)
    , m_resultsDisplay(nullptr)
{
    setupUI();
}

void ExecuteTuneStepWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // ---- Top control bar ----
    QHBoxLayout* controlBar = new QHBoxLayout();
    controlBar->setSpacing(6);

    m_executeBtn = new QPushButton("Execute Vision Profile", this);
    m_executeBtn->setToolTip("Execute the selected vision profile");
    m_executeBtn->setStyleSheet(
        "QPushButton {"
        "   background-color: #2E7D32;"
        "   color: white;"
        "   font-weight: bold;"
        "   font-size: 14px;"
        "   padding: 10px;"
        "   border-radius: 5px;"
        "}"
        "QPushButton:hover { background-color: #388E3C; }"
        "QPushButton:pressed { background-color: #1B5E20; }"
    );
    connect(m_executeBtn, &QPushButton::clicked, this, &ExecuteTuneStepWidget::executeClicked);

    m_stopBtn = new QPushButton("Stop", this);
    m_stopBtn->setToolTip("Stop the running vision execution");
    m_stopBtn->setEnabled(false);
    m_stopBtn->setStyleSheet(
        "QPushButton {"
        "   background-color: #C62828;"
        "   color: white;"
        "   font-weight: bold;"
        "   font-size: 12px;"
        "   padding: 10px;"
        "   border-radius: 5px;"
        "}"
        "QPushButton:hover { background-color: #E53935; }"
        "QPushButton:pressed { background-color: #B71C1C; }"
        "QPushButton:disabled { background-color: #555; color: #888; }"
    );
    connect(m_stopBtn, &QPushButton::clicked, this, &ExecuteTuneStepWidget::stopClicked);

    m_reliabilityBtn = new QPushButton("Test Reliability", this);
    m_reliabilityBtn->setToolTip("Run the plugin N times and collect statistics");
    m_reliabilityBtn->setStyleSheet(
        "QPushButton {"
        "   background-color: #1565C0;"
        "   color: white;"
        "   font-weight: bold;"
        "   font-size: 12px;"
        "   padding: 10px;"
        "   border-radius: 5px;"
        "}"
        "QPushButton:hover { background-color: #1976D2; }"
        "QPushButton:pressed { background-color: #0D47A1; }"
    );
    connect(m_reliabilityBtn, &QPushButton::clicked, this, &ExecuteTuneStepWidget::reliabilityTestClicked);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #AAAAAA; font-size: 12px;");

    m_progressBar = new QProgressBar(this);
    m_progressBar->setMinimumWidth(150);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);

    controlBar->addWidget(m_executeBtn);
    controlBar->addWidget(m_stopBtn);
    controlBar->addWidget(m_reliabilityBtn);
    controlBar->addWidget(m_statusLabel, 1);   // stretch to fill remaining space
    controlBar->addWidget(m_progressBar);

    // ---- Content area (horizontal layout) ----
    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // Left: control + results panel (fixed 350px width)
    QWidget* leftPanel = new QWidget(this);
    leftPanel->setFixedWidth(350);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(4, 0, 4, 0);
    leftLayout->setSpacing(6);

    // Control buttons inside left panel
    leftLayout->addLayout(controlBar);

    QLabel* resultsLabel = new QLabel("Results:", leftPanel);
    resultsLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    leftLayout->addWidget(resultsLabel);

    m_resultsDisplay = new QTextEdit(leftPanel);
    m_resultsDisplay->setReadOnly(true);
    m_resultsDisplay->setPlaceholderText("Plugin execution results will appear here...");
    m_resultsDisplay->setStyleSheet(
        "QTextEdit { font-family: 'Consolas', 'Courier New', monospace; font-size: 11px; }"
    );
    leftLayout->addWidget(m_resultsDisplay, 1);

    contentLayout->addWidget(leftPanel);

    // Right: image placeholder (flex)
    m_imagePlaceholder = new QWidget(this);
    m_imagePlaceholderLayout = new QVBoxLayout(m_imagePlaceholder);
    m_imagePlaceholderLayout->setContentsMargins(0, 0, 0, 0);
    m_imagePlaceholderLayout->setSpacing(0);

    contentLayout->addWidget(m_imagePlaceholder, 1);

    mainLayout->addLayout(contentLayout, 1);
}

void ExecuteTuneStepWidget::setState(VisionWizardState* state)
{
    m_state = state;
}

void ExecuteTuneStepWidget::embedImageWidget(VisionImageWidget* imgWidget)
{
    if (!imgWidget) {
        std::cerr << "[ExecuteTuneStepWidget] embedImageWidget: null widget ignored" << std::endl;
        return;
    }

    // Reparent and add to the placeholder layout
    imgWidget->setParent(m_imagePlaceholder);
    m_imagePlaceholderLayout->addWidget(imgWidget, 1);
    imgWidget->show();
}

void ExecuteTuneStepWidget::removeImageWidget()
{
    // Detach all widgets currently inside the placeholder layout
    while (m_imagePlaceholderLayout->count() > 0) {
        QLayoutItem* item = m_imagePlaceholderLayout->takeAt(0);
        if (item) {
            QWidget* w = item->widget();
            if (w) {
                w->setParent(nullptr);
                w->hide();
            }
            delete item;
        }
    }
}

void ExecuteTuneStepWidget::displayResult(const QString& resultText)
{
    if (m_resultsDisplay) {
        m_resultsDisplay->setPlainText(resultText);
    }
}

void ExecuteTuneStepWidget::clearResults()
{
    if (m_resultsDisplay) {
        m_resultsDisplay->clear();
    }
}

void ExecuteTuneStepWidget::setProcessingState(bool isProcessing)
{
    if (isProcessing) {
        m_executeBtn->setEnabled(false);
        m_reliabilityBtn->setEnabled(false);
        m_stopBtn->setEnabled(true);
        m_progressBar->setVisible(true);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        std::cout << "[ExecuteTuneStepWidget] Processing started" << std::endl;
    } else {
        m_executeBtn->setEnabled(true);
        m_reliabilityBtn->setEnabled(true);
        m_stopBtn->setEnabled(false);
        m_progressBar->setVisible(false);
        QApplication::restoreOverrideCursor();
        std::cout << "[ExecuteTuneStepWidget] Processing stopped" << std::endl;
    }
}

void ExecuteTuneStepWidget::setProgress(int percent, const QString& message)
{
    if (m_progressBar) {
        m_progressBar->setValue(percent);
    }
    if (m_statusLabel) {
        m_statusLabel->setText(message);
    }
}
