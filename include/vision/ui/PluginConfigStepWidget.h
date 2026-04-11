// PluginConfigStepWidget.h
// Plugin parameter configuration + image view for vision wizard
#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QPointer>
#include <QTimer>

struct ApplicationContext;
struct VisionWizardState;
class VisionImageWidget;

class PluginConfigStepWidget : public QWidget {
    Q_OBJECT

public:
    explicit PluginConfigStepWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void setState(VisionWizardState* state);
    void embedImageWidget(VisionImageWidget* imgWidget);
    void removeImageWidget();

    // Plugin config widget
    void setPluginConfigWidget(QWidget* configWidget);
    void clearPluginConfigWidget();

signals:
    void applyParamsClicked();
    void liveExecuteRequested();

private:
    void setupUI();
    void wireLiveExecution(QWidget* configWidget);

    ApplicationContext* m_context;
    VisionWizardState*  m_state = nullptr;

    // Plugin config (left side)
    QScrollArea*        m_pluginScrollArea;
    QVBoxLayout*        m_pluginParamsLayout;
    QPointer<QWidget>   m_currentPluginConfig;
    QPushButton*        m_applyParamsBtn;

    // Image widget placeholder (right side)
    QWidget*     m_imagePlaceholder;
    QVBoxLayout* m_imagePlaceholderLayout;

    // Live-execute debounce: any spinbox/slider value change in the plugin
    // config widget restarts this timer; on timeout we emit liveExecuteRequested.
    QTimer*      m_liveExecuteTimer = nullptr;
};
