#pragma once

#include "alerts/AlertEngine.h"

namespace aquamax {

enum class AlertSettingsLoad { defaults, loaded, error };

class AlertSettingsStore {
public:
    AlertSettingsLoad load(AlertConfig& config);
    bool save(const AlertConfig& config);
};

} // namespace aquamax