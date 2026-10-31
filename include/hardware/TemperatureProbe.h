#pragma once

#include <OneWire.h>
#include <cstdint>
#include "sensors/ProbeState.h"

namespace aquamax {

struct TemperatureSample {
    ProbeState state = ProbeState::disconnected;
    bool hasValue = false;
    float valueC = 0.0f;
    uint32_t lastSeenMs = 0;
};

class TemperatureProbe {
public:
    explicit TemperatureProbe(uint8_t pin);
    TemperatureSample update(uint32_t nowMs);

private:
    bool findProbe();
    bool beginConversion();
    bool readConversion(float& value);
    OneWire bus_;
    uint8_t address_[8] = {};
    bool found_ = false;
    bool converting_ = false;
    uint32_t startedMs_ = 0;
    uint32_t lastScanMs_ = 0;
    bool scanned_ = false;
    ProbeStateTracker state_;
    TemperatureSample sample_;
};

} // namespace aquamax