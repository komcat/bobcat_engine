// ProgramSettingsStore.cpp
#include "ProgramSettingsStore.h"

#include <QSettings>

namespace ProgramSettingsStore {

namespace {
constexpr const char* kOrganisation     = "Fabrinet";
constexpr const char* kApplication      = "BobcatEngine";
constexpr const char* kLastProgramKey   = "RunProgramPage/lastProgram";
constexpr const char* kRecentProgramKey = "RunProgramPage/recentPrograms";
}  // namespace

void SaveLastProgramPath(const QString& path)
{
    QSettings settings(kOrganisation, kApplication);
    settings.setValue(kLastProgramKey, path);
}

QString LoadLastProgramPath()
{
    QSettings settings(kOrganisation, kApplication);
    return settings.value(kLastProgramKey, "").toString();
}

void PrependRecent(const QString& path, int maxCount)
{
    QSettings settings(kOrganisation, kApplication);
    QStringList recent = settings.value(kRecentProgramKey).toStringList();

    // Remove any existing copy so the prepended path becomes the unique most-recent entry.
    recent.removeAll(path);
    recent.prepend(path);

    if (maxCount > 0 && recent.size() > maxCount) {
        recent = recent.mid(0, maxCount);
    }
    settings.setValue(kRecentProgramKey, recent);
}

QStringList LoadRecent()
{
    QSettings settings(kOrganisation, kApplication);
    return settings.value(kRecentProgramKey).toStringList();
}

}  // namespace ProgramSettingsStore
