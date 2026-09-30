#pragma once

#include "preferences/usersettings.h"

class QMainWindow;

namespace mixxx::ohos {
void installMigrationActions(QMainWindow* window, const UserSettingsPointer& settings);
}
