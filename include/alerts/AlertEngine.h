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

enum class ProbeWarning : uint8_t { normal, signalFault, outOfRange, unstable };

class ProbeWarningMonitor {
public:
    bool update(uint32_t nowMs, ProbeState probe, bool paused, uint32_t debounceMs);
    ProbeWarning state() const { return state_; }

private:
    ProbeWarning state_ = ProbeWarning::normal;
    ProbeWarning pending_ = ProbeWarning::normal;
    uint32_t pendingSince_ = 0;
    uint32_t lastUpdateMs_ = 0;
    bool pendingActive_ = false;
    bool initialized_ = false;
    bool paused_ = false;
};

} // namespace aquamax