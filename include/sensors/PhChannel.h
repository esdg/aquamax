#pragma once

#include "calibration/PhCalibration.h"
#include "sensors/ProbeState.h"

namespace aquamax {

struct PhReading {
    ProbeState state = ProbeState::disconnected;
    PhVoltageStatus voltageStatus = PhVoltageStatus::valid;
    bool hasBoardVolts = false;
    float boardVolts = 0.0f;
    bool stable = false;
    bool hasValue = false;
    float ph = 0.0f;
    bool calibrationRequired = true;
};

class PhChannel {
public:
    PhReading update(float adcVolts, const PhVoltageLimits& limits,
                     const PhCalibration& calibration, bool enabled);

private:
    ProbeStateTracker state_;
    float recentVoltages_[8] = {};
    uint8_t recentCount_ = 0;
    uint8_t recentIndex_ = 0;
};

} // namespace aquamax