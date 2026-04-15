// ProcessSummary.h
// Pure helper that produces a one-line, human-readable summary of a recipe
// process step's most important parameters, plus a category-color lookup.
//
// Used by RecipeTreeItemFactory to populate the "Details" column and the
// category color stripe of the Recipe Steps tree.  No Qt widget dependencies
// (only QString) so it can be unit-tested in isolation.
#pragma once

#include <QString>
#include <string>

struct ProcessInstance;

/**
 * @brief Build a short summary string of the most important parameters of a
 *        recipe step, suitable for display in a ~180px column.
 *
 * Falls back to "key1=val1 . key2=val2" for unrecognized process types.
 * Returns an empty string for block-end / loop-end markers (the row name
 * already explains those).
 *
 * @param step The process instance to summarize (must not be null).
 * @return Summary text, eliding internally to ~40 characters.
 */
QString summarizeProcess(const ProcessInstance* step);

/**
 * @brief Look up the brand color for a process category, used to paint the
 *        3px-wide stripe at the leading edge of each step row.
 *
 * @param category Category string from RecipeManager::getProcessCategory()
 *                 (e.g. "Motion Control", "I/O & Pneumatics", "Vision").
 * @return Hex color string ("#RRGGBB"), or "transparent" for unknown
 *         categories and Comment.
 */
QString categoryColor(const std::string& category);
