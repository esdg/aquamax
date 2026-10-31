#pragma once

#include <cstdint>
#include "sensors/ProbeState.h"

namespace aquamax {

enum class AlertState : uint8_t { normal, low, high, probeFault };
enum class AlertEvent : uint8_t { none, low, high, probeFault, recovered };

struct AlertConfig {
    bool thresholdsEnabled = false;
    bool probeFaultEnabled = true;
    float minimum = 20.0f;
    float maximum = 30.0f;
    float hysteresis = 0.5f;
    uint32_t debounceMs = 10000;
    uint32_t repeatMs = 900000;
};

struct AlertResult {
    AlertState state = AlertState::normal;
    AlertEvent event = AlertEvent::none;
};

bool validAlertConfig(const AlertConfig& config);

class AlertEngine {
public:
    AlertResult update(uint32_t nowMs, ProbeState probe, bool hasValue, float value, const AlertConfig& config);
    void reset();

private:
    AlertState state_ = AlertState::normal;
    AlertState pending_ = AlertState::normal;
    uint32_t pendingSince_ = 0;
    uint32_t lastNoticeMs_ = 0;
    bool pendingActive_ = false;
};

} // namespace aquamax