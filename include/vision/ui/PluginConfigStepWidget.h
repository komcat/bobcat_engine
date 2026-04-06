// PluginConfigStepWidget.h
// Plugin parameter configuration + image view for vision wizard
#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QPointer>

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

private:
    void setupUI();

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
};
