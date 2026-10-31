#include "sensors/PhChannel.h"

namespace aquamax {

PhReading PhChannel::update(float adcVolts, const PhVoltageLimits& limits,
                            const PhCalibration& calibration, bool enabled) {
    PhReading reading;
    if (!enabled) {
        reading.state = state_.update(ProbeState::disabled);
        return reading;
    }
    reading.voltageStatus = classifyPhVoltage(adcVolts, limits, reading.boardVolts);
    if (reading.voltageStatus != PhVoltageStatus::valid) {
        reading.state = state_.update(ProbeState::out_of_range);
        return reading;
    }
    reading.hasBoardVolts = true;
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