#pragma once

#include <cstdint>

namespace aquamax {

enum class ProbeState : uint8_t {
    connected,
    disconnected,
    unstable,
    out_of_range,
    disabled,
};

class ProbeStateTracker {
public:
    explicit ProbeStateTracker(uint8_t goodSamples = 3, uint8_t badSamples = 3);

    ProbeState update(ProbeState observed);
    ProbeState state() const { return state_; }

private:
    ProbeState state_ = ProbeState::disconnected;
    ProbeState candidate_ = ProbeState::disconnected;
    uint8_t consecutive_ = 0;
    uint8_t goodSamples_;
    uint8_t badSamples_;
};

} // namespace aquamax