// RecipePageSettingsStore.h
// Pure QSettings I/O for the Recipe page's "last recipe" persistence.
// Extracted from RecipePage so the page class can stay focused on UI
// orchestration. Mirrors the ProgramSettingsStore pattern used by
// RunProgramPage.
//
// Behaviour is identical to the previous in-line code; the QSettings key
// (`RecipePage/lastRecipe`) and organisation/application names
// (`Fabrinet` / `BobcatEngine`) are unchanged.
#pragma once

#include <QString>

namespace RecipePageSettingsStore {

// Save the most recently opened recipe path. Used as the auto-load
// target on the next product activation.
void SaveLastRecipePath(const QString& path);

// Returns the saved path or "" if none. The caller is responsible for
// validating the path against the current product and checking that
// the file still exists on disk.
QString LoadLastRecipePath();

}  // namespace RecipePageSettingsStore
