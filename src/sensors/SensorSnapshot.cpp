#include "sensors/SensorSnapshot.h"
#include <cmath>

namespace aquamax {

SensorSnapshot SensorSnapshotBuilder::updateTemperature(const TemperatureReading& reading, float offsetC) {
    snapshot_.temperature.state = reading.state;
    snapshot_.temperature.lastSeenMs = reading.lastSeenMs;
    if (reading.state != ProbeState::connected || !reading.hasValue || !std::isfinite(reading.valueC) || !std::isfinite(offsetC)) {
        temperatureFilter_.reset();
        hasProcessed_ = false;
        recentCount_ = 0;
        recentIndex_ = 0;
        snapshot_.temperature.hasValue = false;
        snapshot_.temperature.valueC = 0.0f;
        snapshot_.temperature.uncalibratedC = 0.0f;
        snapshot_.temperature.stable = false;
        return snapshot_;
    }
    if (!hasProcessed_ || reading.lastSeenMs != lastProcessedMs_) {
        lastProcessedMs_ = reading.lastSeenMs;
        hasProcessed_ = true;
        recentTemperatures_[recentIndex_] = reading.valueC;
        recentIndex_ = (recentIndex_ + 1) % 8;
        if (recentCount_ < 8) ++recentCount_;
        snapshot_.temperature.hasValue = temperatureFilter_.add(reading.valueC, filteredTemperatureC_);
    }
    float minimum = recentTemperatures_[0];
    float maximum = minimum;
    for (uint8_t index = 1; index < recentCount_; ++index) {
        if (recentTemperatures_[index] < minimum) minimum = recentTemperatures_[index];
        if (recentTemperatures_[index] > maximum) maximum = recentTemperatures_[index];
    }
    snapshot_.temperature.stable = snapshot_.temperature.hasValue && recentCount_ == 8 && maximum - minimum <= 0.3f;
    snapshot_.temperature.uncalibratedC = snapshot_.temperature.hasValue ? filteredTemperatureC_ : 0.0f;
    snapshot_.temperature.valueC = snapshot_.temperature.hasValue ? filteredTemperatureC_ + offsetC : 0.0f;
    return snapshot_;
}

} // namespace aquamax