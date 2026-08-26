// CameraLightingPage.h - Combined Camera and Lighting page with tabs
#pragma once

#include <QWidget>
#include <QTabWidget>

struct ApplicationContext;
class MultiCameraView;
class LightingControlWidget;

/**
 * @brief Combined Camera and Lighting page
 *
 * This widget wraps the camera view and lighting controls in a tab widget,
 * allowing users to switch between camera functions and lighting control.
 */
class CameraLightingPage : public QWidget
{
    Q_OBJECT

public:
    explicit CameraLightingPage(ApplicationContext* context, QWidget* parent = nullptr);
    ~CameraLightingPage() = default;

    // Access to child widgets for external use.
    MultiCameraView* cameraView() const { return m_cameraView; }
    // Null until the Lighting tab has been selected at least once - the widget
    // is built lazily. Call ensureLightingWidget() first if you need it.
    LightingControlWidget* lightingWidget() const { return m_lightingWidget; }

    // Builds the Lighting tab's contents if they do not exist yet.
    void ensureLightingWidget();

private:
    void setupUI();

    ApplicationContext* m_context;

    QTabWidget* m_tabWidget;
    MultiCameraView* m_cameraView;
    QWidget* m_lightingTab = nullptr;        // shell, always built
    LightingControlWidget* m_lightingWidget; // contents, built on first show
};
