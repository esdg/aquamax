#pragma once

#include <cstdint>

namespace aquamax {

struct Settings {
    uint32_t tdsIntervalMs = 60000;
    uint32_t tdsWarmupMs = 1000;
    uint32_t tdsSampleMs = 250;
    uint32_t phRecoveryMs = 1000;
    float defaultTemperatureC = 25.0f;
    bool cableDetectEnabled = false;
};

bool validSettings(const Settings& settings);

} // namespace aquamax