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

    // Access to child widgets for external use
    MultiCameraView* cameraView() const { return m_cameraView; }
    LightingControlWidget* lightingWidget() const { return m_lightingWidget; }

private:
    void setupUI();

    ApplicationContext* m_context;

    QTabWidget* m_tabWidget;
    MultiCameraView* m_cameraView;
    LightingControlWidget* m_lightingWidget;
};
