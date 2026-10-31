#include "alerts/AlertEngine.h"
#include <cmath>

namespace aquamax {
namespace {
AlertEvent eventFor(AlertState state) {
    switch (state) {
        case AlertState::low: return AlertEvent::low;
        case AlertState::high: return AlertEvent::high;
        case AlertState::probeFault: return AlertEvent::probeFault;
        case AlertState::normal: return AlertEvent::recovered;
    }
    return AlertEvent::none;
}
}

bool validAlertConfig(const AlertConfig& config) {
    return std::isfinite(config.minimum) && std::isfinite(config.maximum) && std::isfinite(config.hysteresis) &&
           config.minimum >= -55.0f && config.maximum <= 125.0f && config.minimum < config.maximum &&
           config.hysteresis >= 0.0f && config.hysteresis * 2.0f < config.maximum - config.minimum &&
           config.debounceMs <= 3600000 && config.repeatMs >= 60000 && config.repeatMs <= 86400000;
}

void AlertEngine::reset() {
    state_ = AlertState::normal;
    pending_ = AlertState::normal;
    pendingActive_ = false;
    pendingSince_ = 0;
    lastNoticeMs_ = 0;
}

AlertResult AlertEngine::update(uint32_t nowMs, ProbeState probe, bool hasValue, float value, const AlertConfig& config) {
    if (!validAlertConfig(config) || probe == ProbeState::disabled) { reset(); return {}; }
    const bool validReading = probe == ProbeState::connected && hasValue && std::isfinite(value);
    const bool faulty = probe == ProbeState::disconnected || probe == ProbeState::unstable || probe == ProbeState::out_of_range;
    if (!validReading && (state_ == AlertState::low || state_ == AlertState::high)) state_ = AlertState::normal;
    AlertState desired = AlertState::normal;
    if (faulty && config.probeFaultEnabled) desired = AlertState::probeFault;
    else if (!validReading && state_ == AlertState::probeFault && config.probeFaultEnabled) desired = AlertState::probeFault;
    else if (validReading && config.thresholdsEnabled) {
        if (state_ == AlertState::low && value < config.minimum + config.hysteresis) desired = AlertState::low;
        else if (state_ == AlertState::high && value > config.maximum - config.hysteresis) desired = AlertState::high;
        else if (value < config.minimum) desired = AlertState::low;
        else if (value > config.maximum) desired = AlertState::high;
    }
    if (desired != state_) {
        if (!pendingActive_ || pending_ != desired) { pending_ = desired; pendingSince_ = nowMs; pendingActive_ = true; }
        if (nowMs - pendingSince_ >= config.debounceMs) {
            state_ = desired;
            pendingActive_ = false;
            lastNoticeMs_ = nowMs;
            return {state_, eventFor(state_)};
        }
    } else {
        pendingActive_ = false;
        if (state_ != AlertState::normal && nowMs - lastNoticeMs_ >= config.repeatMs) {
            lastNoticeMs_ = nowMs;
            return {state_, eventFor(state_)};
        }
    }
    return {state_, AlertEvent::none};
}

} // namespace aquamax