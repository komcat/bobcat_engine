// ProgramSettingsStore.h
// Pure QSettings I/O for the Run Program page's "last program" + "recent
// programs" persistence. Extracted from RunProgramPage so the page class
// can stay focused on UI orchestration.
//
// Behaviour is identical to the previous in-line code; the QSettings keys
// (`RunProgramPage/lastProgram`, `RunProgramPage/recentPrograms`) and
// organisation/application names (`Fabrinet` / `BobcatEngine`) are
// unchanged.
#pragma once

#include <QString>
#include <QStringList>

namespace ProgramSettingsStore {

// Save the most recently opened program path. Used as the auto-load
// target on the next product activation.
void SaveLastProgramPath(const QString& path);

// Returns the saved path or "" if none. The caller is responsible for
// validating the path against the current product and checking that
// the file still exists on disk.
QString LoadLastProgramPath();

// Prepend `path` to the recent list. If it already appears in the list,
// the duplicate entry is removed first so the most-recent rules. The
// list is truncated to `maxCount` entries.
void PrependRecent(const QString& path, int maxCount = 10);

// Returns the recent list as stored. The UI loader filters out missing
// files when populating its widget.
QStringList LoadRecent();

}  // namespace ProgramSettingsStore
