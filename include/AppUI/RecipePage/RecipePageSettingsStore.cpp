// RecipePageSettingsStore.cpp
#include "RecipePageSettingsStore.h"

#include <QSettings>

namespace RecipePageSettingsStore {

namespace {
constexpr const char* kOrganisation    = "Fabrinet";
constexpr const char* kApplication     = "BobcatEngine";
constexpr const char* kLastRecipeKey   = "RecipePage/lastRecipe";
}  // namespace

void SaveLastRecipePath(const QString& path)
{
    QSettings settings(kOrganisation, kApplication);
    settings.setValue(kLastRecipeKey, path);
}

QString LoadLastRecipePath()
{
    QSettings settings(kOrganisation, kApplication);
    return settings.value(kLastRecipeKey, "").toString();
}

}  // namespace RecipePageSettingsStore
