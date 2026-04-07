// ExecuteTuneStepWidget.h
// Step 4: Execute, tune, and view results for vision wizard
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>

struct ApplicationContext;
struct VisionWizardState;
class VisionImageWidget;

class ExecuteTuneStepWidget : public QWidget {
    Q_OBJECT

public:
    explicit ExecuteTuneStepWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void setState(VisionWizardState* state);
    void embedImageWidget(VisionImageWidget* imgWidget);
    void removeImageWidget();

    // Results display
    void displayResult(const QString& resultText);
    void clearResults();

    // Processing state
    void setProcessingState(bool isProcessing);
    void setProgress(int percent, const QString& message);

signals:
    void executeClicked();
    void stopClicked();
    void reliabilityTestClicked();

private:
    void setupUI();

    ApplicationContext* m_context;
    VisionWizardState*  m_state = nullptr;

    // Control bar
    QPushButton*  m_executeBtn;
    QPushButton*  m_stopBtn;
    QPushButton*  m_reliabilityBtn;
    QLabel*       m_statusLabel;
    QProgressBar* m_progressBar;

    // Image widget placeholder (right side)
    QWidget*     m_imagePlaceholder;
    QVBoxLayout* m_imagePlaceholderLayout;

    // Results panel (left side, fixed width)
    QTextEdit* m_resultsDisplay;
};
