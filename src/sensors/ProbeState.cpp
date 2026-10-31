#include "sensors/ProbeState.h"

namespace aquamax {

ProbeStateTracker::ProbeStateTracker(uint8_t goodSamples, uint8_t badSamples)
    : goodSamples_(goodSamples == 0 ? 1 : goodSamples),
      badSamples_(badSamples == 0 ? 1 : badSamples) {}

ProbeState ProbeStateTracker::update(ProbeState observed) {
    if (observed == ProbeState::disabled) {
        state_ = observed;
        candidate_ = observed;
        consecutive_ = 0;
        return state_;
    }
    if (state_ == ProbeState::disabled) {
        state_ = ProbeState::disconnected;
    }
    if (observed == state_) {
        candidate_ = observed;
        consecutive_ = 0;
        return state_;
    }
    if (candidate_ != observed) {
        candidate_ = observed;
        consecutive_ = 0;
    }
    if (consecutive_ < UINT8_MAX) {
        ++consecutive_;
    }
    const uint8_t threshold = observed == ProbeState::connected ? goodSamples_ : badSamples_;
    if (consecutive_ >= threshold) {
        state_ = observed;
        consecutive_ = 0;
    }
    return state_;
}

} // namespace aquamax