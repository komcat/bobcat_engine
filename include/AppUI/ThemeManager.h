// ThemeManager.h
#pragma once

#include <QObject>
#include "Theme.h"

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    static ThemeManager& instance();

    Theme::ThemeId currentTheme() const;
    void setTheme(Theme::ThemeId id);

signals:
    void themeChanged();

private:
    ThemeManager();
    Theme::ThemeId m_current = Theme::ThemeId::Default;
};
