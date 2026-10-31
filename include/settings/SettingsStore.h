#pragma once

#include "settings/Settings.h"

namespace aquamax {

enum class SettingsLoad { loaded, defaults, error };

class SettingsStore {
public:
    SettingsLoad load(Settings& settings);
    bool save(const Settings& settings);
};

} // namespace aquamax