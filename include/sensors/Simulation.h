#pragma once

#include <cstdint>
#include "sensors/ProbeState.h"

namespace aquamax {

struct SimulatedReading {
    ProbeState state = ProbeState::disconnected;
    bool hasValue = false;
    float value = 0.0f;
};

struct SimulatedSnapshot {
    SimulatedReading temperature;
    SimulatedReading ph;
    SimulatedReading tds;
    bool defaultCompensation = false;
};

class Simulation {
public:
    void setPresent(bool temperature, bool ph, bool tds);
    SimulatedSnapshot sample(uint32_t elapsedMs);

private:
    bool temperaturePresent_ = true;
    bool phPresent_ = true;
    bool tdsPresent_ = true;
    ProbeStateTracker temperatureState_;
    ProbeStateTracker phState_;
    ProbeStateTracker tdsState_;
};

} // namespace aquamax