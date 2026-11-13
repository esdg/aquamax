#pragma once

#include <cstdint>

namespace aquamax {

enum class StartupScreen { splash, setup, gauges };

class StartupModel {
public:
    static constexpr uint32_t animationCycleMs = 2000;

    void begin(uint32_t nowMs, bool hasSavedWifi);
    StartupScreen update(uint32_t nowMs, bool wifiConnected, bool wifiTimedOut,
                         bool setupAvailable = false, bool setupRequested = false);

private:
    uint32_t startedMs_ = 0;
    bool hasSavedWifi_ = false;
    bool finished_ = false;
};

} // namespace aquamax