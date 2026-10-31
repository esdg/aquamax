#pragma once

#include <cstdint>

namespace aquamax {

struct MeasurementSchedule {
    uint32_t intervalMs = 60000;
    uint32_t warmupMs = 1000;
    uint32_t sampleMs = 250;
    uint32_t phRecoveryMs = 1000;
};

struct MeasurementWindow {
    bool tdsPower = false;
    bool sampleTds = false;
    bool samplePh = false;
};

class MeasurementScheduler {
public:
    explicit MeasurementScheduler(MeasurementSchedule schedule);
    bool valid() const;
    MeasurementWindow update(uint32_t nowMs, bool enabled);

private:
    enum class Phase : uint8_t { quiet, warming, sampling, recovery };
    MeasurementSchedule schedule_;
    Phase phase_ = Phase::quiet;
    uint32_t phaseStartMs_ = 0;
    uint32_t lastStartMs_ = 0;
    bool initialized_ = false;
};

} // namespace aquamax