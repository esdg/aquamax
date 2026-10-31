#include "sensors/MeasurementScheduler.h"
#include <cstdint>

namespace aquamax {

MeasurementScheduler::MeasurementScheduler(MeasurementSchedule schedule) : schedule_(schedule) {}

bool MeasurementScheduler::valid() const {
    const uint64_t window = static_cast<uint64_t>(schedule_.warmupMs) + schedule_.sampleMs + schedule_.phRecoveryMs;
    return schedule_.intervalMs > 0 && schedule_.intervalMs <= INT32_MAX &&
           schedule_.sampleMs > 0 && window < schedule_.intervalMs;
}

MeasurementWindow MeasurementScheduler::update(uint32_t nowMs, bool enabled) {
    if (!initialized_) {
        initialized_ = true;
        lastStartMs_ = nowMs;
    }
    if (!enabled || !valid()) {
        phase_ = Phase::quiet;
        lastStartMs_ = nowMs;
        return {};
    }
    if (phase_ == Phase::quiet && nowMs - lastStartMs_ >= schedule_.intervalMs) {
        phase_ = Phase::warming;
        phaseStartMs_ = nowMs;
        lastStartMs_ = nowMs;
    }
    if (phase_ == Phase::warming && nowMs - phaseStartMs_ >= schedule_.warmupMs) {
        phase_ = Phase::sampling;
        phaseStartMs_ = nowMs;
    }
    if (phase_ == Phase::sampling && nowMs - phaseStartMs_ >= schedule_.sampleMs) {
        phase_ = Phase::recovery;
        phaseStartMs_ = nowMs;
    }
    if (phase_ == Phase::recovery && nowMs - phaseStartMs_ >= schedule_.phRecoveryMs) {
        phase_ = Phase::quiet;
    }
    return {phase_ == Phase::warming || phase_ == Phase::sampling,
            phase_ == Phase::sampling, phase_ == Phase::quiet};
}

} // namespace aquamax