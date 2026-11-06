#include "sensors/PhChannel.h"

namespace aquamax {

PhReading PhChannel::update(float adcVolts, const PhVoltageLimits& limits,
                            const PhCalibration& calibration, bool enabled) {
    PhReading reading;
    if (!enabled) {
        recentCount_ = 0;
        recentIndex_ = 0;
        reading.state = state_.update(ProbeState::disabled);
        return reading;
    }
    reading.voltageStatus = classifyPhVoltage(adcVolts, limits, reading.boardVolts);
    if (reading.voltageStatus != PhVoltageStatus::valid) {
        recentCount_ = 0;
        recentIndex_ = 0;
        reading.state = state_.update(ProbeState::signal_fault);
        return reading;
    }
    reading.hasBoardVolts = true;
    recentVoltages_[recentIndex_] = reading.boardVolts;
    recentIndex_ = (recentIndex_ + 1) % 8;
    if (recentCount_ < 8) ++recentCount_;
    float minimum = recentVoltages_[0];
    float maximum = minimum;
    for (uint8_t index = 1; index < recentCount_; ++index) {
        if (recentVoltages_[index] < minimum) minimum = recentVoltages_[index];
        if (recentVoltages_[index] > maximum) maximum = recentVoltages_[index];
    }
    reading.stable = recentCount_ == 8 && maximum - minimum <= 0.02f;
    float health = 0.0f;
    reading.calibrationRequired = !phSlopeHealth(calibration, health);
    float ph = 0.0f;
    if (!reading.calibrationRequired && !phFromVoltage(adcVolts, limits, calibration, ph)) {
        reading.state = state_.update(ProbeState::out_of_range);
        return reading;
    }
    reading.state = state_.update(ProbeState::connected);
    if (!reading.calibrationRequired && reading.state == ProbeState::connected) {
        reading.hasValue = true;
        reading.ph = ph;
    }
    return reading;
}

} // namespace aquamax