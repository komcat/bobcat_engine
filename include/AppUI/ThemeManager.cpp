// ThemeManager.cpp
#include "ThemeManager.h"
#include "DarkTheme.h"
#include <QApplication>
#include <iostream>

ThemeManager::ThemeManager()
    : QObject(nullptr)
    , m_current(Theme::ThemeId::Default)
{
}

ThemeManager& ThemeManager::instance()
{
    static ThemeManager s_instance;
    return s_instance;
}

Theme::ThemeId ThemeManager::currentTheme() const
{
    return m_current;
}

void ThemeManager::setTheme(Theme::ThemeId id)
{
    if (id == m_current) return;

    m_current = id;
    Theme::applyPalette(id);

    // Re-apply the global QSS — DarkTheme::getStyleSheet() reads from
    // Theme::Color::* which applyPalette just reassigned, so the returned
    // QSS now carries the new palette's colors.
    if (qApp) {
        qApp->setStyleSheet(DarkTheme::getStyleSheet());
    }

    std::cout << "[ThemeManager] Theme changed to: "
              << (id == Theme::ThemeId::CC29 ? "CC-29" : "Default") << std::endl;

    emit themeChanged();
}
