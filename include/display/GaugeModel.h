#pragma once

#include <cstdint>
#include "sensors/ProbeState.h"

namespace aquamax {

struct GaugeRange {
    float minimum;
    float maximum;
};

struct GaugeFrame {
    bool hasValue = false;
    bool showCursor = false;
    bool alert = false;
    float value = 0.0f;
    float cursorPosition = 0.0f;
    ProbeState state = ProbeState::disconnected;
};

class GaugeModel {
public:
    explicit GaugeModel(GaugeRange range, float easingSeconds = 0.5f);
    bool setRange(GaugeRange range);
    GaugeFrame update(ProbeState state, bool hasValue, float value, bool alert, uint32_t nowMs);

private:
    GaugeRange range_;
    float easingSeconds_;
    float cursor_ = 0.0f;
    uint32_t lastUpdateMs_ = 0;
    bool rangeValid_ = false;
    bool cursorValid_ = false;
};

} // namespace aquamax