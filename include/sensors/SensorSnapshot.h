#pragma once

#include <cstdint>
#include "sensors/ProbeState.h"
#include "sensors/SignalFilter.h"

namespace aquamax {

struct TemperatureReading {
    ProbeState state = ProbeState::disconnected;
    bool hasValue = false;
    float valueC = 0.0f;
    uint32_t lastSeenMs = 0;
    float uncalibratedC = 0.0f;
    bool stable = false;
};

struct SensorReading {
    ProbeState state = ProbeState::disabled;
    bool hasValue = false;
    float value = 0.0f;
    uint32_t lastSeenMs = 0;
    bool calibrated = false;
};

struct SensorSnapshot {
    TemperatureReading temperature;
    SensorReading ph;
    SensorReading tds;
};

class SensorSnapshotBuilder {
public:
    SensorSnapshot updateTemperature(const TemperatureReading& reading, float offsetC = 0.0f);

private:
    SignalFilter temperatureFilter_;
    SensorSnapshot snapshot_;
    uint32_t lastProcessedMs_ = 0;
    bool hasProcessed_ = false;
    float filteredTemperatureC_ = 0.0f;
    float recentTemperatures_[8] = {};
    uint8_t recentCount_ = 0;
    uint8_t recentIndex_ = 0;
};

} // namespace aquamax